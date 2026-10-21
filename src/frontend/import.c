#include "frontend/import.h"

#include <SDL3/SDL.h>

#include "vfs/extract.h"

struct NwImport {
    SDL_Thread *thread;
    SDL_Mutex *lock;
    char *image;
    char *root;
    const NwManifest *m;
    SDL_AtomicInt state;
    SDL_AtomicInt cancel;
    uint64_t done; /* under lock */
    uint64_t total;
    char error[512];
};

static bool nw_import_ext(const char *name, const char *ext)
{
    size_t n = SDL_strlen(name), e = SDL_strlen(ext);
    return n > e && SDL_strcasecmp(name + n - e, ext) == 0;
}

bool nw_import_is_image(const char *name)
{
    return nw_import_ext(name, ".iso") || nw_import_ext(name, ".bin") || nw_import_ext(name, ".cue");
}

static SDL_EnumerationResult SDLCALL nw_import_collect(void *user, const char *dir, const char *name)
{
    NwImageList *list = user;
    SDL_PathInfo info;
    char *path = NULL;
    if (name[0] == '.' || !nw_import_is_image(name)) {
        return SDL_ENUM_CONTINUE;
    }
    size_t n = SDL_strlen(dir);
    const char *sep = n > 0 && (dir[n - 1] == '/' || dir[n - 1] == '\\') ? "" : "/";
    bool file = SDL_asprintf(&path, "%s%s%s", dir, sep, name) >= 0 && SDL_GetPathInfo(path, &info) &&
                info.type == SDL_PATHTYPE_FILE;
    SDL_free(path);
    if (!file) {
        return SDL_ENUM_CONTINUE;
    }
    char **names = SDL_realloc(list->names, (list->count + 1) * sizeof(*names));
    if (!names) {
        return SDL_ENUM_FAILURE;
    }
    list->names = names;
    list->names[list->count] = SDL_strdup(name);
    return list->names[list->count++] ? SDL_ENUM_CONTINUE : SDL_ENUM_FAILURE;
}

static int SDLCALL nw_import_cmp(const void *a, const void *b)
{
    return SDL_strcasecmp(*(char *const *)a, *(char *const *)b);
}

/* A .bin that a .cue of the same name in the list points at. */
static bool nw_import_has_cue(const NwImageList *list, const char *bin)
{
    size_t stem = SDL_strlen(bin) - 4;
    for (size_t i = 0; i < list->count; i++) {
        const char *name = list->names[i];
        if (SDL_strlen(name) == stem + 4 && SDL_strncasecmp(name, bin, stem) == 0 && nw_import_ext(name, ".cue")) {
            return true;
        }
    }
    return false;
}

bool nw_import_scan(const char *dir, NwImageList *list)
{
    SDL_zerop(list);
    if (!SDL_GetPathInfo(dir, NULL)) {
        return true;
    }
    if (!SDL_EnumerateDirectory(dir, nw_import_collect, list)) {
        nw_import_list_free(list);
        return false;
    }
    size_t kept = 0;
    for (size_t i = 0; i < list->count; i++) {
        if (nw_import_ext(list->names[i], ".bin") && nw_import_has_cue(list, list->names[i])) {
            list->names[i][0] = '\0'; /* dropped below; the cue lookup still sees the other names */
        }
    }
    for (size_t i = 0; i < list->count; i++) {
        if (list->names[i][0]) {
            list->names[kept++] = list->names[i];
        } else {
            SDL_free(list->names[i]);
        }
    }
    list->count = kept;
    SDL_qsort(list->names, list->count, sizeof(*list->names), nw_import_cmp);
    return true;
}

void nw_import_list_free(NwImageList *list)
{
    for (size_t i = 0; i < list->count; i++) {
        SDL_free(list->names[i]);
    }
    SDL_free(list->names);
    SDL_zerop(list);
}

static bool nw_import_progress(void *user, uint64_t done, uint64_t total)
{
    NwImport *job = user;
    SDL_LockMutex(job->lock);
    job->done = done;
    job->total = total;
    SDL_UnlockMutex(job->lock);
    return !SDL_GetAtomicInt(&job->cancel);
}

static int SDLCALL nw_import_run(void *user)
{
    NwImport *job = user;
    NwDisc disc;
    NwIso iso;

    bool ok = nw_disc_open(&disc, job->image);
    if (ok) {
        ok = nw_iso_index(&iso, &disc);
        if (ok) {
            ok = nw_extract_disc(&disc, &iso, job->m, job->root, nw_import_progress, job);
            nw_iso_free(&iso);
        }
        nw_disc_close(&disc);
    }
    if (!ok) {
        SDL_strlcpy(job->error, SDL_GetError(), sizeof(job->error));
    }
    SDL_SetAtomicInt(&job->state, ok                              ? NW_IMPORT_DONE
                                  : SDL_GetAtomicInt(&job->cancel) ? NW_IMPORT_CANCELLED
                                                                   : NW_IMPORT_FAILED);
    return 0;
}

NwImport *nw_import_start(const char *image, const char *root, const NwManifest *m)
{
    NwImport *job = SDL_calloc(1, sizeof(*job));
    if (!job) {
        return NULL;
    }
    job->m = m;
    job->image = SDL_strdup(image);
    job->root = SDL_strdup(root);
    job->lock = SDL_CreateMutex();
    SDL_SetAtomicInt(&job->state, NW_IMPORT_RUNNING);
    if (job->image && job->root && job->lock) {
        job->thread = SDL_CreateThread(nw_import_run, "nw_import", job);
    }
    if (!job->thread) {
        nw_import_free(job);
        return NULL;
    }
    return job;
}

NwImportState nw_import_poll(NwImport *job, uint64_t *done, uint64_t *total)
{
    SDL_LockMutex(job->lock);
    *done = job->done;
    *total = job->total;
    SDL_UnlockMutex(job->lock);
    return (NwImportState)SDL_GetAtomicInt(&job->state);
}

const char *nw_import_error(const NwImport *job)
{
    return job->error;
}

void nw_import_cancel(NwImport *job)
{
    SDL_SetAtomicInt(&job->cancel, 1);
}

void nw_import_free(NwImport *job)
{
    if (!job) {
        return;
    }
    if (job->thread) {
        nw_import_cancel(job);
        SDL_WaitThread(job->thread, NULL);
    }
    SDL_DestroyMutex(job->lock);
    SDL_free(job->root);
    SDL_free(job->image);
    SDL_free(job);
}
