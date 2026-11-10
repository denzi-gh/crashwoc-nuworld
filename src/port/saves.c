/* NuWorld's saves in the game (port/saves.h). */
#include "port/saves.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <SDL3/SDL.h>

#include "app.h"
#include "nw_convert.h"
#include "port/names.h"
#include "port/autosave.h"
#include "save/saveworker.h"

/* Retail's result screens, in logic ticks: GAME SAVED / GAME DELETED (NTSC's shorter, port/values/hz.toml's
   ProcMenu 0x32), GAME LOADED. */
#define NW_SAVES_SAVED_FRAMES (nw_hz == 60 ? 0x28 : 0x32)
#define NW_SAVES_LOADED_FRAMES 0x28

extern int nw_hz; /* port/hz.c */

/* The record layout the codec writes. */
#define NW_SAVES_AT(type, field, at) _Static_assert(offsetof(struct type, field) == (at), #type "." #field)
#define NW_SAVES_COUNT(type, field) (sizeof(((struct type *)0)->field) / sizeof(((struct type *)0)->field[0]))
_Static_assert(sizeof(struct game_s) == NW_SAVE_GAME_SIZE, "game_s");
NW_SAVES_AT(game_s, name, NW_SAVE_NAME);
_Static_assert(sizeof(((struct game_s *)0)->name) == NW_SAVE_NAME_SIZE, "game_s.name");
NW_SAVES_AT(game_s, vibration, NW_SAVE_VIBRATION);
NW_SAVES_AT(game_s, surround, NW_SAVE_SURROUND);
NW_SAVES_AT(game_s, sfx_volume, NW_SAVE_SFX_VOLUME);
NW_SAVES_AT(game_s, music_volume, NW_SAVE_MUSIC_VOLUME);
NW_SAVES_AT(game_s, screen_x, NW_SAVE_SCREEN_X);
NW_SAVES_AT(game_s, screen_y, NW_SAVE_SCREEN_Y);
NW_SAVES_AT(game_s, language, NW_SAVE_LANGUAGE);
NW_SAVES_AT(game_s, hub, NW_SAVE_HUB);
_Static_assert(NW_SAVES_COUNT(game_s, hub) == NW_SAVE_HUBS, "game_s.hub");
NW_SAVES_AT(game_s, level, NW_SAVE_LEVEL);
_Static_assert(NW_SAVES_COUNT(game_s, level) == NW_SAVE_LEVELS, "game_s.level");
NW_SAVES_AT(game_s, lives, NW_SAVE_LIVES);
NW_SAVES_AT(game_s, wumpas, NW_SAVE_WUMPAS);
NW_SAVES_AT(game_s, mask, NW_SAVE_MASK);
NW_SAVES_AT(game_s, percent, NW_SAVE_PERCENT);
NW_SAVES_AT(game_s, crystals, NW_SAVE_CRYSTALS);
NW_SAVES_AT(game_s, relics, NW_SAVE_RELICS);
NW_SAVES_AT(game_s, crate_gems, NW_SAVE_CRATE_GEMS);
NW_SAVES_AT(game_s, bonus_gems, NW_SAVE_BONUS_GEMS);
NW_SAVES_AT(game_s, gems, NW_SAVE_GEMS);
NW_SAVES_AT(game_s, gembits, NW_SAVE_GEMBITS);
NW_SAVES_AT(game_s, powerbits, NW_SAVE_POWERBITS);
NW_SAVES_AT(game_s, empty, NW_SAVE_EMPTY);
NW_SAVES_AT(game_s, cutbits, NW_SAVE_CUTBITS);
_Static_assert(sizeof(struct hub_s) == NW_SAVE_HUB_SIZE, "hub_s");
NW_SAVES_AT(hub_s, flags, NW_SAVE_HUB_FLAGS);
NW_SAVES_AT(hub_s, crystals, NW_SAVE_HUB_CRYSTALS);
NW_SAVES_AT(hub_s, pad1, NW_SAVE_HUB_PAD);
_Static_assert(sizeof(struct level_s) == NW_SAVE_LEVEL_SIZE, "level_s");
NW_SAVES_AT(level_s, flags, NW_SAVE_LEVEL_FLAGS);
NW_SAVES_AT(level_s, pad1, NW_SAVE_LEVEL_PAD);
NW_SAVES_AT(level_s, time, NW_SAVE_LEVEL_TIME);
_Static_assert(NW_SAVES_COUNT(level_s, time) == NW_SAVE_TIMES, "level_s.time");
_Static_assert(sizeof(struct time_s) == NW_SAVE_TIME_SIZE, "time_s");
NW_SAVES_AT(time_s, name, NW_SAVE_TIME_NAME);
_Static_assert(sizeof(((struct time_s *)0)->name) == NW_SAVE_TIME_NAME_SIZE, "time_s.name");
NW_SAVES_AT(time_s, itime, NW_SAVE_TIME_ITIME);

/* The game's (a root of the data image). */
extern struct game_s SaveSlot[2][2];
extern struct game_s *game; /* game/game's: the slot a save menu picked */
extern struct ldata_s LData[];
_Static_assert(sizeof(SaveSlot) == NW_SAVES_PAGE * sizeof(struct game_s), "SaveSlot is a page");

static struct {
    char *dir; /* NULL: no saves folder */
    NwSaveRegistry reg;
    bool reg_ready;
    bool converted; /* the saves from elsewhere converted, or tried */
    int page;
    NwSaveTime saved[NW_SAVES_PAGE]; /* when SaveSlot's games were saved */
    NwSaveMeta meta[NW_SAVES_PAGE];
    bool recovered[NW_SAVES_PAGE];
    bool automatic;
    bool has_autos, pending;
    NwSaveTask task;
    struct {
        const char *dir;
        const NwSaveRegistry *reg;
        int number, page, index;
        bool automatic, deleting, ok;
        struct game_s game;
        NwSaveNames names;
        NwSaveMeta meta;
        NwSaveTime saved;
        int latest, last;
        bool has_autos;
        char error[512];
    } job;
    int latest, last;                /* nw_save_scan's */
    bool ok;                         /* the last write or delete */
    int wait;                        /* the result screen's frames left */
} nw_saves;

static struct game_s *nw_saves_slot(int i)
{
    return &SaveSlot[i / 2][i % 2];
}

void nw_saves_init(const char *dir)
{
    nw_saves_finish();
    nw_autosave_init(dir);
    SDL_free(nw_saves.dir);
    nw_saves.dir = dir ? SDL_strdup(dir) : NULL;
    nw_saves.converted = false;
    nw_saves.page = 0;
    nw_saves.automatic = false;
    nw_saves.has_autos = dir && nw_save_scan_auto(dir, NULL) >= 0;
    SDL_zeroa(nw_saves.saved);
    SDL_zeroa(nw_saves.meta);
    SDL_zeroa(nw_saves.recovered);
    nw_saves.latest = nw_saves.last = -1;
    nw_saves.ok = true;
    nw_saves.wait = 0;
    for (int i = 0; i < NW_SAVES_PAGE; i++) {
        nw_saves_slot(i)->empty = 1;
        SDL_zerop(nw_names_slot(i));
    }
    if (!dir) nw_save_worker_shutdown();
}

/* Level keys from LData's paths, made on first use (by whichever thread comes first). */
const NwSaveRegistry *nw_saves_levels(void)
{
    static SDL_InitState once;
    if (SDL_ShouldInit(&once)) {
        const char *paths[NW_SAVE_LEVELS];
        for (int i = 0; i < NW_SAVE_LEVELS; i++) {
            paths[i] = LData[i].path;
        }
        nw_saves.reg_ready = nw_save_registry_init(&nw_saves.reg, paths, NW_SAVE_LEVELS, NW_SAVE_HUBS);
        SDL_SetInitialized(&once, nw_saves.reg_ready);
    }
    return nw_saves.reg_ready ? &nw_saves.reg : NULL;
}

/* The registry, with dir's saves from elsewhere converted first; NULL without a folder. */
static const NwSaveRegistry *nw_saves_ready(const char *dir)
{
    const NwSaveRegistry *reg = dir ? nw_saves_levels() : NULL;
    bool ours = dir == nw_saves.dir;
    if (reg && !(ours && nw_saves.converted)) {
        int made = nw_save_convert(dir, reg);
        if (made > 0) {
            fprintf(stderr, NW_APP_NAME ": saves: converted %d games\n", made);
        } else if (made < 0) {
            fprintf(stderr, NW_APP_NAME ": saves: cannot convert: %s\n", SDL_GetError());
        }
        nw_saves.converted = nw_saves.converted || ours;
    }
    return reg;
}

int nw_saves_import(const char *dir, const char *file)
{
    const NwSaveRegistry *reg = nw_saves_ready(dir);
    return reg ? nw_save_import(dir, file, reg) : -1;
}

bool nw_saves_export(const char *dir, const char *card, const int *picks, const void *icon, size_t icon_len)
{
    const NwSaveRegistry *reg = nw_saves_ready(dir);
    return reg && nw_save_export(dir, reg, card, picks, icon, icon_len);
}

static void nw_saves_scan(void)
{
    nw_saves.last = -1;
    nw_saves.latest = nw_saves.dir ? (nw_saves.automatic ? nw_save_scan_auto(nw_saves.dir, &nw_saves.last)
                                                       : nw_save_scan(nw_saves.dir, &nw_saves.last)) : -1;
    nw_saves.has_autos = nw_saves.dir && nw_save_scan_auto(nw_saves.dir, NULL) >= 0;
    if (nw_saves.automatic) {
        if (nw_saves.latest >= 0) nw_saves.latest -= NW_SAVE_GAMES;
        if (nw_saves.last >= 0) nw_saves.last -= NW_SAVE_GAMES;
    }
}

void nw_saves_autos(bool automatic)
{
    nw_saves.automatic = automatic;
    nw_saves_show(0);
}

bool nw_saves_automatic(void) { return nw_saves.automatic; }
bool nw_saves_has_autos(void) { return nw_saves.has_autos; }
void nw_saves_auto_changed(void) { nw_saves.has_autos = true; }
bool nw_saves_recovered(int i) { return i >= 0 && i < NW_SAVES_PAGE && nw_saves.recovered[i]; }

static int nw_saves_number(int i)
{
    return (nw_saves.automatic ? NW_SAVE_GAMES : 0) + nw_saves.page * NW_SAVES_PAGE + i;
}

int nw_saves_page(void)
{
    return nw_saves.page;
}

int nw_saves_pages(void)
{
    if (nw_saves.last < 0) {
        return 1;
    }
    int pages = nw_saves.last / NW_SAVES_PAGE + (nw_saves.automatic ? 1 : 2);
    return SDL_min(pages, NW_SAVES_PAGES);
}

int nw_saves_latest(void)
{
    return nw_saves.latest;
}

const NwSaveTime *nw_saves_time(int i)
{
    static const NwSaveTime none;
    return i >= 0 && i < NW_SAVES_PAGE ? &nw_saves.saved[i] : &none;
}

/* A save that does not load shows as an empty slot. */
void nw_saves_show(int page)
{
    const NwSaveRegistry *reg = nw_saves_ready(nw_saves.dir);
    nw_saves.page = SDL_clamp(page, 0, NW_SAVES_PAGES - 1);
    for (int i = 0; i < NW_SAVES_PAGE; i++) {
        struct game_s *slot = nw_saves_slot(i);
        int n = nw_saves_number(i);
        NwSaveResult r = reg ? nw_save_load_meta(nw_saves.dir, n, 0, reg, (uint8_t *)slot, nw_names_slot(i),
                                                &nw_saves.saved[i], &nw_saves.meta[i])
                             : NW_SAVE_NONE;
        nw_saves.recovered[i] = r == NW_SAVE_BACKUP;
        if (r == NW_SAVE_BACKUP) {
            fprintf(stderr, NW_APP_NAME ": saves: save %d: loaded the backup: %s\n", n + 1, SDL_GetError());
        } else if (r == NW_SAVE_FAILED) {
            fprintf(stderr, NW_APP_NAME ": saves: save %d: cannot load: %s\n", n + 1, SDL_GetError());
        }
        if (r == NW_SAVE_NONE || r == NW_SAVE_FAILED) {
            slot->empty = 1;
            SDL_zero(nw_saves.saved[i]);
            SDL_zerop(nw_names_slot(i));
            SDL_zero(nw_saves.meta[i]);
        }
    }
    nw_saves_scan();
}

/* slot's place on the page, -1 when it is not SaveSlot's. */
static int nw_saves_index(const struct game_s *slot)
{
    uintptr_t at = (uintptr_t)slot - (uintptr_t)&SaveSlot[0][0];
    return at < sizeof(SaveSlot) && at % sizeof(struct game_s) == 0 ? (int)(at / sizeof(struct game_s)) : -1;
}

bool nw_saves_previous(struct game_s *slot, int generation)
{
    int i = nw_saves_index(slot);
    const NwSaveRegistry *reg = nw_saves_ready(nw_saves.dir);
    if (i < 0 || !reg) return false;
    NwSaveResult r = nw_save_load_meta(nw_saves.dir, nw_saves_number(i), generation, reg, (uint8_t *)slot,
                                      nw_names_slot(i), &nw_saves.saved[i], &nw_saves.meta[i]);
    if (r != NW_SAVE_OK && r != NW_SAVE_BACKUP) return false;
    nw_saves.recovered[i] = true;
    return true;
}

static void nw_saves_worker(void *unused)
{
    (void)unused;
    if (nw_saves.job.deleting) {
        nw_saves.job.ok = nw_save_remove(nw_saves.job.dir, nw_saves.job.number) != NW_SAVE_FAILED;
    } else {
        nw_saves.job.ok = nw_save_store_meta(nw_saves.job.dir, nw_saves.job.number, nw_saves.job.reg,
                                             (const uint8_t *)&nw_saves.job.game, &nw_saves.job.names,
                                             &nw_saves.job.saved, &nw_saves.job.meta);
    }
    if (!nw_saves.job.ok) SDL_strlcpy(nw_saves.job.error, SDL_GetError(), sizeof(nw_saves.job.error));
    nw_saves.job.latest = nw_saves.job.automatic ? nw_save_scan_auto(nw_saves.job.dir, &nw_saves.job.last)
                                               : nw_save_scan(nw_saves.job.dir, &nw_saves.job.last);
    if (!nw_saves.job.deleting && nw_saves.job.ok) nw_saves.job.latest = nw_saves.job.number;
    nw_saves.job.has_autos = nw_save_scan_auto(nw_saves.job.dir, NULL) >= 0;
}

static void nw_saves_poll(void)
{
    if (!nw_saves.pending || !nw_save_worker_done(&nw_saves.task)) return;
    nw_saves.pending = false;
    nw_saves.ok = nw_saves.job.ok;
    nw_saves.has_autos = nw_saves.job.has_autos;
    if (nw_saves.automatic == nw_saves.job.automatic) {
        int offset = nw_saves.automatic ? NW_SAVE_GAMES : 0;
        nw_saves.latest = nw_saves.job.latest >= 0 ? nw_saves.job.latest - offset : -1;
        nw_saves.last = nw_saves.job.last >= 0 ? nw_saves.job.last - offset : -1;
    }
    if (!nw_saves.ok) {
        fprintf(stderr, NW_APP_NAME ": saves: %s\n", nw_saves.job.error);
        return;
    }
    if (nw_saves.page == nw_saves.job.page && nw_saves.automatic == nw_saves.job.automatic) {
        int i = nw_saves.job.index;
        nw_saves.recovered[i] = false;
        if (nw_saves.job.deleting) {
            nw_saves_slot(i)->empty = 1;
            SDL_zero(nw_saves.saved[i]);
            SDL_zero(nw_saves.meta[i]);
            SDL_zerop(nw_names_slot(i));
        } else {
            nw_saves.saved[i] = nw_saves.job.saved;
            nw_saves.meta[i] = nw_saves.job.meta;
        }
    }
}

bool nw_saves_pending(void) { nw_saves_poll(); return nw_saves.pending; }

void nw_saves_finish(void)
{
    if (nw_saves.pending) {
        nw_save_worker_wait(&nw_saves.task);
        nw_saves_poll();
    }
}

static void nw_saves_submit(struct game_s *slot, bool deleting)
{
    nw_saves_poll();
    if (nw_saves.pending) return;
    const NwSaveRegistry *reg = nw_saves.dir ? nw_saves_levels() : NULL;
    int i = nw_saves_index(slot);
    nw_saves.wait = NW_SAVES_SAVED_FRAMES;
    nw_saves.ok = false;
    if (!reg || i < 0 || (!deleting && nw_saves.automatic)) return;
    nw_saves.job.dir = nw_saves.dir;
    nw_saves.job.reg = reg;
    nw_saves.job.number = nw_saves_number(i);
    nw_saves.job.page = nw_saves.page;
    nw_saves.job.index = i;
    nw_saves.job.automatic = nw_saves.automatic;
    nw_saves.job.deleting = deleting;
    nw_saves.job.game = *slot;
    if (!deleting) nw_names_save(i);
    nw_saves.job.names = *nw_names_slot(i);
    nw_saves.job.meta = nw_autosave_active() ? *nw_autosave_meta() : (NwSaveMeta){ 0 };
    SDL_Time now = 0;
    SDL_GetCurrentTime(&now);
    nw_save_time_from(now, &nw_saves.job.saved);
    nw_saves.pending = nw_save_worker_submit(&nw_saves.task, nw_saves_worker, NULL);
    if (nw_saves.pending && deleting && nw_saves.automatic && nw_autosave_active() &&
        SDL_strcmp(nw_saves.meta[i].campaign, nw_autosave_meta()->campaign) == 0) {
        (void)nw_autosave_set(false);
    }
}

void nw_saves_write(struct game_s *slot) { nw_saves_submit(slot, false); }
void nw_saves_delete(struct game_s *slot) { nw_saves_submit(slot, true); }

void nw_saves_loaded(void)
{
    int i = nw_saves_index(game);
    nw_names_load(i); /* retail copied the slot to the game in play: its longer names too */
    if (i >= 0) {
        nw_autosave_loaded(&nw_saves.meta[i], game, nw_names_slot(i), nw_saves.recovered[i]);
    }
    nw_saves.wait = NW_SAVES_LOADED_FRAMES;
}

int nw_saves_shown(void)
{
    if (nw_saves_pending() || nw_autosave_loading()) return 0;
    if (nw_saves.wait > 0) {
        nw_saves.wait--;
    }
    return nw_saves.wait == 0;
}

int nw_saves_ok(void)
{
    nw_saves_poll();
    return nw_saves.ok;
}

int nw_saves_any(void)
{
    return nw_saves.latest >= 0;
}
