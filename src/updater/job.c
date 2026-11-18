#include "updater/job.h"

#include <stdio.h>

#include <SDL3/SDL.h>

#include "platform/paths.h"
#include "platform/update.h"
#include "updater/apply.h"
#include "updater/archive.h"
#include "updater/pubkey.h"

#define NW_UPDATER_TIMEOUT_MS 30000
#define NW_UPDATER_SUMS "SHA256SUMS"
#define NW_UPDATER_SIGNATURE "SHA256SUMS.sig"
#define NW_UPDATER_NEW "new"

struct NwUpdater {
    NwUpdaterConfig config;
    char *install, *from, *to;
    SDL_Thread *thread;
    SDL_Mutex *lock;
    SDL_AtomicInt state, cancel;
    uint64_t done, total;
    char error[512];
};

static bool nw_updater_github(void *user, const char *name, const char *path, uint64_t max, NwHttpProgress progress,
                              void *progress_user)
{
    const NwUpdaterConfig *c = user;
    char url[256], agent[64];
    SDL_snprintf(url, sizeof(url), "https://github.com/" NW_UPDATE_REPO "/releases/download/v%s/%s",
                 c->to + (*c->to == 'v'), name);
    SDL_snprintf(agent, sizeof(agent), "NuWorldUpdater/%s", c->from);
    return nw_http_download(url, agent, NW_UPDATER_TIMEOUT_MS, path, max, progress, progress_user);
}

static bool nw_updater_progress(void *user, uint64_t done, uint64_t total)
{
    NwUpdater *u = user;
    SDL_LockMutex(u->lock);
    u->done = done;
    u->total = total;
    SDL_UnlockMutex(u->lock);
    return !SDL_GetAtomicInt(&u->cancel);
}

static bool nw_updater_fetch(NwUpdater *u, const char *stage, const char *name, uint64_t max, bool shown, char **path)
{
    const NwUpdaterConfig *c = &u->config;
    if (SDL_GetAtomicInt(&u->cancel)) {
        return SDL_SetError("cancelled");
    }
    if (SDL_asprintf(path, "%s%s", stage, name) < 0) {
        *path = NULL;
        return false;
    }
    if (!c->fetch(c->fetch_user ? c->fetch_user : (void *)c, name, *path, max, shown ? nw_updater_progress : NULL, u)) {
        char *why = SDL_strdup(SDL_GetError());
        SDL_SetError("%s: %s", name, why ? why : "the download failed");
        SDL_free(why);
        return false;
    }
    return true;
}

static bool nw_updater_install(NwUpdater *u, const char *stage)
{
    const NwUpdaterConfig *c = &u->config;
    const char *to = c->to + (*c->to == 'v');
    char name[96], want[NW_SHA256_HEX], got[NW_SHA256_HEX];
    char *sums_path = NULL, *signature_path = NULL, *archive_path = NULL, *fresh = NULL, *program = NULL;
    char *replaced = NULL, *journal = NULL, *journal_path = NULL;
    size_t sums_size = 0, signature_size = 0, archive_size = 0;
    void *sums = NULL, *signature = NULL, *archive = NULL;
    SDL_snprintf(name, sizeof(name), "CrashWOC-NuWorld-v%s-" NW_UPDATER_ARCHIVE, to);

    SDL_SetAtomicInt(&u->state, NW_UPDATER_DOWNLOADING);
    bool ok = nw_updater_fetch(u, stage, NW_UPDATER_SUMS, 1 << 16, false, &sums_path) &&
              nw_updater_fetch(u, stage, NW_UPDATER_SIGNATURE, 1 << 10, false, &signature_path) &&
              (sums = SDL_LoadFile(sums_path, &sums_size)) != NULL &&
              (signature = SDL_LoadFile(signature_path, &signature_size)) != NULL &&
              nw_updater_signed(c->keys, c->key_count, sums, sums_size, signature, signature_size) &&
              nw_updater_listed(sums, name, want) &&
              nw_updater_fetch(u, stage, name, (uint64_t)128 << 20, true, &archive_path) &&
              nw_updater_file_hash(archive_path, got);
    if (ok && SDL_strcmp(want, got) != 0) {
        ok = SDL_SetError("the download does not match SHA256SUMS");
    }
    if (ok && SDL_GetAtomicInt(&u->cancel)) {
        ok = SDL_SetError("cancelled");
    }
    if (ok) {
        SDL_SetAtomicInt(&u->state, NW_UPDATER_INSTALLING);
        ok = SDL_asprintf(&fresh, "%s" NW_UPDATER_NEW "/", stage) >= 0 &&
             SDL_asprintf(&program, "%s" NW_UPDATER_PROGRAM, fresh) >= 0 && SDL_CreateDirectory(fresh) &&
             (archive = SDL_LoadFile(archive_path, &archive_size)) != NULL &&
             nw_archive_unpack(archive, archive_size, fresh);
    }
    if (ok && !SDL_GetPathInfo(program, NULL)) {
        ok = SDL_SetError("the update holds no " NW_UPDATER_PROGRAM);
    }
    ok = ok && nw_updater_swap(fresh, c->install, &replaced);
    if (ok && SDL_asprintf(&journal, "from %s\n%s", c->from, replaced) >= 0 &&
        SDL_asprintf(&journal_path, "%s" NW_UPDATE_JOURNAL, stage) >= 0) {
        SDL_SaveFile(journal_path, journal, SDL_strlen(journal));
    }
    if (ok) {
        fresh[SDL_strlen(fresh) - 1] = '\0';
        nw_paths_remove_tree(fresh);
        SDL_RemovePath(archive_path);
        SDL_RemovePath(sums_path);
        SDL_RemovePath(signature_path);
    }
    SDL_free(sums);
    SDL_free(signature);
    SDL_free(archive);
    SDL_free(sums_path);
    SDL_free(signature_path);
    SDL_free(archive_path);
    SDL_free(fresh);
    SDL_free(program);
    SDL_free(replaced);
    SDL_free(journal);
    SDL_free(journal_path);
    return ok;
}

static bool nw_updater_work(NwUpdater *u)
{
    const NwUpdaterConfig *c = &u->config;
    int order = 0;
    char *program = NULL, *stage_dir = NULL, *stage = NULL;
    if (!nw_update_compare(c->to, c->from, &order) || order <= 0) {
        return SDL_SetError("%s is not newer than %s", c->to, c->from);
    }
    if (c->wait) {
        while (fgetc(stdin) != EOF) {
        }
    }
    if (SDL_GetAtomicInt(&u->cancel)) {
        return SDL_SetError("cancelled");
    }
    bool ok = SDL_asprintf(&program, "%s" NW_UPDATER_PROGRAM, c->install) >= 0 &&
              SDL_asprintf(&stage_dir, "%s" NW_UPDATE_STAGE, c->install) >= 0 &&
              SDL_asprintf(&stage, "%s/", stage_dir) >= 0;
    if (ok && !SDL_GetPathInfo(program, NULL)) {
        ok = SDL_SetError("there is no " NW_UPDATER_PROGRAM " in %s", c->install);
    }
    if (ok && !(nw_paths_remove_tree(stage_dir) && SDL_CreateDirectory(stage_dir))) {
        char *why = SDL_strdup(SDL_GetError());
        ok = SDL_SetError("cannot write to %s: %s", c->install, why ? why : "");
        SDL_free(why);
    } else if (ok) {
        ok = nw_updater_install(u, stage);
        if (!ok) {
            char *why = SDL_strdup(SDL_GetError());
            nw_paths_remove_tree(stage_dir);
            SDL_SetError("%s", why ? why : "the update failed");
            SDL_free(why);
        }
    }
    SDL_free(program);
    SDL_free(stage_dir);
    SDL_free(stage);
    return ok;
}

static void nw_updater_finish(NwUpdater *u, bool ok)
{
    if (!ok) {
        SDL_strlcpy(u->error, SDL_GetError(), sizeof(u->error));
    }
    SDL_SetAtomicInt(&u->state, ok ? NW_UPDATER_DONE
                                   : SDL_GetAtomicInt(&u->cancel) ? NW_UPDATER_CANCELLED : NW_UPDATER_FAILED);
}

static bool nw_updater_init(NwUpdater *u, const NwUpdaterConfig *config)
{
    SDL_zerop(u);
    u->config = *config;
    u->install = SDL_strdup(config->install);
    u->from = SDL_strdup(config->from);
    u->to = SDL_strdup(config->to);
    u->lock = SDL_CreateMutex();
    u->config.install = u->install;
    u->config.from = u->from;
    u->config.to = u->to;
    if (!u->config.fetch) {
        u->config.fetch = nw_updater_github;
    }
    if (!u->config.keys) {
        u->config.keys = nw_updater_keys;
        u->config.key_count = NW_UPDATER_KEYS;
    }
    return u->install && u->from && u->to && u->lock;
}

static void nw_updater_clear(NwUpdater *u)
{
    SDL_DestroyMutex(u->lock);
    SDL_free(u->install);
    SDL_free(u->from);
    SDL_free(u->to);
}

bool nw_updater_apply(const NwUpdaterConfig *config)
{
    NwUpdater u;
    bool ok = nw_updater_init(&u, config) && nw_updater_work(&u);
    nw_updater_finish(&u, ok);
    nw_updater_clear(&u);
    return ok || SDL_SetError("%s", u.error);
}

static int SDLCALL nw_updater_thread(void *user)
{
    NwUpdater *u = user;
    nw_updater_finish(u, nw_updater_work(u));
    return 0;
}

NwUpdater *nw_updater_start(const NwUpdaterConfig *config)
{
    NwUpdater *u = SDL_malloc(sizeof(*u));
    if (u && nw_updater_init(u, config)) {
        u->thread = SDL_CreateThread(nw_updater_thread, "updater", u);
    }
    if (!u || !u->thread) {
        if (u) {
            nw_updater_clear(u);
        }
        SDL_free(u);
        return NULL;
    }
    return u;
}

NwUpdaterState nw_updater_poll(NwUpdater *u, uint64_t *done, uint64_t *total)
{
    SDL_LockMutex(u->lock);
    *done = u->done;
    *total = u->total;
    SDL_UnlockMutex(u->lock);
    return (NwUpdaterState)SDL_GetAtomicInt(&u->state);
}

const char *nw_updater_error(const NwUpdater *u)
{
    return u->error;
}

void nw_updater_cancel(NwUpdater *u)
{
    SDL_SetAtomicInt(&u->cancel, 1);
}

void nw_updater_free(NwUpdater *u)
{
    if (!u) {
        return;
    }
    SDL_WaitThread(u->thread, NULL);
    nw_updater_clear(u);
    SDL_free(u);
}
