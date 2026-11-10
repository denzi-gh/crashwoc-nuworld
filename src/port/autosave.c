#include "port/autosave.h"

#include <SDL3/SDL.h>

#include "game/gametypes.h"
#include "port/names.h"
#include "port/saves.h"
#include "save/saveworker.h"

extern struct game_s Game;
extern struct cursor_s Cursor;
extern struct panelcount_s plr_lives, plr_wumpas;
extern s32 Level, Hub, Demo, GameMode, editor_active, cutmovie, new_level, new_mode, new_lev_flags;
void CalculateGamePercentage(struct game_s *game);

typedef struct NwAutoSnapshot {
    struct game_s game;
    NwSaveNames names;
} NwAutoSnapshot;

static struct {
    char *dir;
    bool active, dirty, have_snapshot, rollback;
    int slot;
    NwSaveMeta meta;
    NwAutoSnapshot snapshot, submitted, observed;
    bool have_submitted, have_observed, pending;
    NwSaveTask task, prefs_task;
    bool prefs_pending;
    NwSaveTask lookup_task;
    bool lookup_pending;
    struct {
        const char *dir;
        const NwSaveRegistry *reg;
        NwSaveMeta requested, latest;
        int slot;
    } lookup;
    struct {
        const char *dir;
        const NwSaveRegistry *reg;
        NwSaveMeta meta;
        bool ok;
    } prefs;
    struct {
        const char *dir;
        const NwSaveRegistry *reg;
        int slot;
        NwAutoSnapshot snapshot;
        NwSaveMeta meta;
        bool ok;
        char error[512];
    } job;
    NwAutosaveStatus status;
    bool presented;
    uint64_t notice;
} nw_auto = { .slot = -1 };

static void nw_autosave_notice(NwAutosaveStatus status)
{
    nw_auto.status = status;
    nw_auto.presented = false;
    nw_auto.notice = SDL_GetTicks();
}

static void nw_autosave_id(void)
{
    if (!nw_auto.meta.campaign[0]) {
        SDL_Time now = 0;
        SDL_GetCurrentTime(&now);
        SDL_snprintf(nw_auto.meta.campaign, sizeof(nw_auto.meta.campaign), "%016llx%08x%08x",
                     (unsigned long long)now, (unsigned)SDL_rand_bits(), (unsigned)SDL_rand_bits());
    }
}

/* A campaign keeps its own destination, even with the same player name. Never replace an unreadable slot. */
static int nw_autosave_find(const char *dir, const NwSaveRegistry *reg, const char *campaign, NwSaveMeta *latest)
{
    int free_slot = -1;
    SDL_zerop(latest);
    if (!reg) {
        return -1;
    }
    for (int n = NW_SAVE_GAMES; n < NW_SAVE_TOTAL; n++) {
        struct game_s game;
        NwSaveMeta meta;
        NwSaveResult r = nw_save_load_meta(dir, n, 0, reg, (uint8_t *)&game, NULL, NULL, &meta);
        if ((r == NW_SAVE_OK || r == NW_SAVE_BACKUP) &&
            SDL_strcmp(meta.campaign, campaign) == 0 && meta.campaign[0]) {
            *latest = meta;
            return n;
        }
        if (free_slot < 0 && r == NW_SAVE_NONE && !nw_save_exists(dir, n)) {
            free_slot = n;
        }
    }
    return free_slot;
}

static void nw_autosave_worker(void *unused)
{
    (void)unused;
    if (nw_auto.job.slot < 0) {
        NwSaveMeta latest;
        nw_auto.job.slot = nw_autosave_find(nw_auto.job.dir, nw_auto.job.reg, nw_auto.job.meta.campaign, &latest);
    }
    nw_auto.job.ok = nw_auto.job.slot >= 0 && nw_save_store_meta(nw_auto.job.dir, nw_auto.job.slot, nw_auto.job.reg,
                                        (const uint8_t *)&nw_auto.job.snapshot.game, &nw_auto.job.snapshot.names,
                                        NULL, &nw_auto.job.meta);
    if (!nw_auto.job.ok) {
        SDL_strlcpy(nw_auto.job.error, SDL_GetError(), sizeof(nw_auto.job.error));
    }
}

static void nw_autosave_collect(void)
{
    nw_auto.pending = false;
    if (nw_auto.job.ok) {
        if (SDL_strcmp(nw_auto.meta.campaign, nw_auto.job.meta.campaign) == 0) nw_auto.slot = nw_auto.job.slot;
        nw_saves_auto_changed();
        nw_autosave_notice(NW_AUTOSAVE_COMPLETE);
    } else {
        SDL_Log("autosave: %s", nw_auto.job.error);
        nw_auto.have_submitted = false;
        nw_autosave_notice(NW_AUTOSAVE_FAILED);
    }
}

static void nw_autosave_start(void)
{
    if (!nw_auto.dirty || !nw_auto.have_snapshot || !nw_auto.meta.autosave || nw_auto.rollback || nw_auto.pending ||
        nw_auto.lookup_pending) {
        return;
    }
    nw_auto.dirty = false;
    const NwSaveRegistry *reg = nw_auto.dir ? nw_saves_levels() : NULL;
    if (!reg) {
        nw_autosave_notice(NW_AUTOSAVE_FAILED);
        return;
    }
    SDL_Time now = 0;
    SDL_GetCurrentTime(&now);
    nw_auto.meta.revision = SDL_max(nw_auto.meta.revision + 1, now);
    nw_auto.job.dir = nw_auto.dir;
    nw_auto.job.reg = reg;
    nw_auto.job.slot = nw_auto.slot;
    nw_auto.job.snapshot = nw_auto.snapshot;
    nw_auto.job.meta = nw_auto.meta;
    nw_auto.job.ok = false;
    nw_autosave_notice(NW_AUTOSAVE_WRITING);
    nw_auto.pending = nw_save_worker_submit(&nw_auto.task, nw_autosave_worker, NULL);
    if (!nw_auto.pending) {
        nw_autosave_notice(NW_AUTOSAVE_FAILED);
        return;
    }
    nw_auto.submitted = nw_auto.snapshot;
    nw_auto.have_submitted = true;
}

static void nw_autosave_lookup_worker(void *unused)
{
    (void)unused;
    nw_auto.lookup.slot = nw_autosave_find(nw_auto.lookup.dir, nw_auto.lookup.reg,
                                          nw_auto.lookup.requested.campaign, &nw_auto.lookup.latest);
}

static void nw_autosave_lookup_collect(void)
{
    if (!nw_auto.lookup_pending || !nw_save_worker_done(&nw_auto.lookup_task)) return;
    nw_auto.lookup_pending = false;
    if (SDL_strcmp(nw_auto.lookup.requested.campaign, nw_auto.meta.campaign) != 0) return;
    nw_auto.slot = nw_auto.lookup.slot;
    if (nw_auto.lookup.latest.campaign[0]) {
        nw_auto.rollback = nw_auto.lookup.latest.revision > nw_auto.meta.revision;
        nw_auto.meta.autosave = nw_auto.meta.autosave && nw_auto.lookup.latest.autosave;
    }
}

bool nw_autosave_loading(void)
{
    nw_autosave_lookup_collect();
    return nw_auto.lookup_pending;
}

void nw_autosave_finish(void)
{
    if (nw_auto.pending) {
        nw_save_worker_wait(&nw_auto.task);
        nw_autosave_collect();
    }
    if (nw_auto.lookup_pending) {
        nw_save_worker_wait(&nw_auto.lookup_task);
        nw_autosave_lookup_collect();
    }
    nw_autosave_start();
    if (nw_auto.pending) {
        nw_save_worker_wait(&nw_auto.task);
        nw_autosave_collect();
    }
    if (nw_auto.prefs_pending) {
        nw_save_worker_wait(&nw_auto.prefs_task);
        nw_auto.prefs_pending = false;
    }
}

void nw_autosave_init(const char *dir)
{
    nw_autosave_finish();
    SDL_free(nw_auto.dir);
    SDL_zero(nw_auto);
    nw_auto.dir = dir ? SDL_strdup(dir) : NULL;
    nw_auto.slot = -1;
    if (dir && !nw_save_worker_init()) nw_autosave_notice(NW_AUTOSAVE_FAILED);
}

void nw_autosave_new(bool enabled)
{
    nw_auto.active = true;
    nw_auto.have_snapshot = nw_auto.have_submitted = nw_auto.dirty = nw_auto.rollback = false;
    nw_auto.have_observed = false;
    nw_auto.slot = -1;
    SDL_zero(nw_auto.meta);
    nw_autosave_id();
    nw_auto.meta.autosave = enabled;
    nw_autosave_checkpoint(false);
}

void nw_autosave_loaded(const NwSaveMeta *meta, const struct game_s *game, const NwSaveNames *names, bool recovered)
{
    nw_auto.active = true;
    nw_auto.meta = *meta;
    nw_autosave_id();
    nw_auto.snapshot.game = *game;
    nw_auto.snapshot.names = *names;
    nw_auto.submitted = nw_auto.snapshot;
    nw_auto.have_snapshot = nw_auto.have_submitted = true;
    nw_auto.have_observed = false;
    nw_auto.dirty = nw_auto.rollback = false;
    nw_auto.slot = -1;
    nw_auto.lookup.dir = nw_auto.dir;
    nw_auto.lookup.reg = nw_auto.dir ? nw_saves_levels() : NULL;
    nw_auto.lookup.requested = nw_auto.meta;
    nw_auto.lookup_pending = nw_save_worker_submit(&nw_auto.lookup_task, nw_autosave_lookup_worker, NULL);
    if (!nw_auto.lookup_pending) nw_autosave_notice(NW_AUTOSAVE_FAILED);
    if (recovered) {
        nw_autosave_recovered();
    }
}

void nw_autosave_checkpoint(bool rewards)
{
    if (!nw_auto.active || Demo || editor_active || GameMode == 1 || Cursor.menu == 15 || Cursor.menu == 2) {
        return;
    }
    NwAutoSnapshot snapshot = { .game = Game, .names = *nw_names_game() };
    snapshot.game.empty = 0;
    if (cutmovie == -1 && Level >= 0 && Level <= 0x25) {
        snapshot.game.lives = (uint8_t)SDL_clamp(plr_lives.count, 0, 99);
        snapshot.game.wumpas = (uint8_t)SDL_clamp(plr_wumpas.count, 0, 99);
    }
    if (rewards && Level >= 0 && Level < NW_SAVE_LEVELS) {
        snapshot.game.level[Level].flags |= (uint16_t)new_lev_flags;
    }
    if (nw_auto.have_observed && SDL_memcmp(&snapshot.game, &nw_auto.observed.game, sizeof(snapshot.game)) == 0 &&
        SDL_memcmp(&snapshot.names, &nw_auto.observed.names, sizeof(snapshot.names)) == 0 &&
        (nw_auto.status != NW_AUTOSAVE_FAILED || SDL_GetTicks() - nw_auto.notice < 10000)) return;
    nw_auto.observed = snapshot;
    nw_auto.have_observed = true;
    CalculateGamePercentage(&snapshot.game);
    nw_auto.snapshot = snapshot;
    nw_auto.have_snapshot = true;
    if (nw_auto.meta.autosave && !nw_auto.rollback &&
        (!nw_auto.have_submitted || SDL_memcmp(&snapshot.game, &nw_auto.submitted.game, sizeof(snapshot.game)) != 0 ||
         SDL_memcmp(&snapshot.names, &nw_auto.submitted.names, sizeof(snapshot.names)) != 0)) {
        /* Back off after an error instead of retrying every frame on a full/unwritable disk. */
        if (nw_auto.status != NW_AUTOSAVE_FAILED || SDL_GetTicks() - nw_auto.notice >= 10000) {
            nw_auto.dirty = true;
            nw_autosave_start();
        }
    }
}

void nw_autosave_poll(void)
{
    nw_autosave_lookup_collect();
    if (nw_auto.pending && nw_save_worker_done(&nw_auto.task) && SDL_GetTicks() - nw_auto.notice >= 700) {
        nw_autosave_collect();
        nw_autosave_start();
    }
    if (nw_auto.prefs_pending && nw_save_worker_done(&nw_auto.prefs_task)) {
        nw_auto.prefs_pending = false;
        if (!nw_auto.prefs.ok) nw_autosave_notice(NW_AUTOSAVE_FAILED);
    }
    if (nw_auto.active && (Level == 0x23 || (cutmovie == 0 && Cursor.menu == 0))) {
        nw_autosave_leave();
    } else if (nw_auto.active && nw_auto.meta.autosave && Level == 0x25 && cutmovie == -1 && new_level == -1 && new_mode == -1 &&
               new_lev_flags == 0) {
        nw_autosave_checkpoint(false);
    }
}

void nw_autosave_leave(void)
{
    nw_autosave_start();
    nw_auto.active = false;
}

const NwSaveMeta *nw_autosave_meta(void) { return &nw_auto.meta; }
bool nw_autosave_enabled(void) { return nw_auto.active && nw_auto.meta.autosave; }
bool nw_autosave_active(void) { return nw_auto.active; }
bool nw_autosave_rollback(void) { return nw_auto.active && nw_auto.meta.autosave && nw_auto.rollback; }

static void nw_autosave_preferences(void *unused)
{
    (void)unused;
    NwAutoSnapshot latest;
    NwSaveMeta meta, found;
    int slot = nw_autosave_find(nw_auto.prefs.dir, nw_auto.prefs.reg, nw_auto.prefs.meta.campaign, &found);
    nw_auto.prefs.ok = true;
    if (slot >= 0 && found.campaign[0]) {
        NwSaveResult r = nw_save_load_meta(nw_auto.prefs.dir, slot, 0, nw_auto.prefs.reg,
                                          (uint8_t *)&latest.game, &latest.names, NULL, &meta);
        nw_auto.prefs.ok = r == NW_SAVE_OK || r == NW_SAVE_BACKUP;
        if (nw_auto.prefs.ok) {
            meta.autosave = false;
            nw_auto.prefs.ok = nw_save_store_meta(nw_auto.prefs.dir, slot, nw_auto.prefs.reg,
                                                  (const uint8_t *)&latest.game, &latest.names, NULL, &meta);
        }
    }
}

bool nw_autosave_set(bool enabled)
{
    if (!nw_auto.active) {
        return true;
    }
    if (enabled && nw_auto.rollback) {
        return false;
    }
    if (!enabled && nw_auto.dir && !nw_auto.prefs_pending) {
        nw_auto.prefs.dir = nw_auto.dir;
        nw_auto.prefs.reg = nw_saves_levels();
        nw_auto.prefs.meta = nw_auto.meta;
        nw_auto.prefs_pending = nw_save_worker_submit(&nw_auto.prefs_task, nw_autosave_preferences, NULL);
        if (!nw_auto.prefs_pending) nw_autosave_notice(NW_AUTOSAVE_FAILED);
    }
    nw_auto.meta.autosave = enabled;
    nw_auto.dirty = enabled;
    if (enabled) {
        nw_auto.have_observed = false;
        nw_autosave_checkpoint(false);
        nw_autosave_start();
    }
    return true;
}

void nw_autosave_accept_rollback(bool enabled)
{
    if (enabled) nw_auto.rollback = false;
    nw_auto.meta.autosave = enabled;
    nw_auto.have_submitted = false;
    nw_auto.dirty = enabled;
    nw_autosave_start();
}

NwAutosaveStatus nw_autosave_status(void)
{
    uint64_t elapsed = SDL_GetTicks() - nw_auto.notice;
    if (nw_auto.presented && nw_auto.status == NW_AUTOSAVE_COMPLETE && elapsed >= 2500) {
        return NW_AUTOSAVE_IDLE;
    }
    if (nw_auto.presented && (nw_auto.status == NW_AUTOSAVE_FAILED || nw_auto.status == NW_AUTOSAVE_RECOVERED) && elapsed >= 6000) {
        return NW_AUTOSAVE_IDLE;
    }
    return nw_auto.status;
}

bool nw_autosave_notice_visible(uint64_t now)
{
    return nw_auto.status != NW_AUTOSAVE_WRITING || ((now - nw_auto.notice) / 350) % 2 == 0;
}

void nw_autosave_notice_presented(void)
{
    if (!nw_auto.presented) {
        nw_auto.presented = true;
        /* A result hidden by loading/cutscenes still gets its full display time on the first visible frame. */
        if (nw_auto.status != NW_AUTOSAVE_WRITING) nw_auto.notice = SDL_GetTicks();
    }
}

void nw_autosave_recovered(void) { nw_autosave_notice(NW_AUTOSAVE_RECOVERED); }
