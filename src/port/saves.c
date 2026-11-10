/* NuWorld's saves in the game (port/saves.h). */
#include "port/saves.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <SDL3/SDL.h>

#include "app.h"
#include "nw_convert.h"

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
extern struct ldata_s LData[];
_Static_assert(sizeof(SaveSlot) == NW_SAVES_PAGE * sizeof(struct game_s), "SaveSlot is a page");

static struct {
    char *dir; /* NULL: no saves folder */
    NwSaveRegistry reg;
    bool reg_ready;
    bool converted; /* the saves from elsewhere converted, or tried */
    int page;
    NwSaveTime saved[NW_SAVES_PAGE]; /* when SaveSlot's games were saved */
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
    SDL_free(nw_saves.dir);
    nw_saves.dir = dir ? SDL_strdup(dir) : NULL;
    nw_saves.converted = false;
    nw_saves.page = 0;
    SDL_zeroa(nw_saves.saved);
    nw_saves.latest = nw_saves.last = -1;
    nw_saves.ok = true;
    nw_saves.wait = 0;
    for (int i = 0; i < NW_SAVES_PAGE; i++) {
        nw_saves_slot(i)->empty = 1;
    }
}

/* Level keys from LData's paths, made on first use. */
static const NwSaveRegistry *nw_saves_registry(void)
{
    if (!nw_saves.reg_ready) {
        const char *paths[NW_SAVE_LEVELS];
        for (int i = 0; i < NW_SAVE_LEVELS; i++) {
            paths[i] = LData[i].path;
        }
        nw_saves.reg_ready = nw_save_registry_init(&nw_saves.reg, paths, NW_SAVE_LEVELS, NW_SAVE_HUBS);
    }
    return nw_saves.reg_ready ? &nw_saves.reg : NULL;
}

/* The registry, with dir's saves from elsewhere converted first; NULL without a folder. */
static const NwSaveRegistry *nw_saves_ready(const char *dir)
{
    const NwSaveRegistry *reg = dir ? nw_saves_registry() : NULL;
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

static void nw_saves_scan(void)
{
    nw_saves.last = -1;
    nw_saves.latest = nw_saves.dir ? nw_save_scan(nw_saves.dir, &nw_saves.last) : -1;
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
    int pages = nw_saves.last / NW_SAVES_PAGE + 2; /* up to the last save's page, and one more */
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
        int n = nw_saves.page * NW_SAVES_PAGE + i;
        NwSaveResult r = reg ? nw_save_load(nw_saves.dir, n, reg, (uint8_t *)slot, &nw_saves.saved[i]) : NW_SAVE_NONE;
        if (r == NW_SAVE_BACKUP) {
            fprintf(stderr, NW_APP_NAME ": saves: save %d: loaded the backup: %s\n", n + 1, SDL_GetError());
        } else if (r == NW_SAVE_FAILED) {
            fprintf(stderr, NW_APP_NAME ": saves: save %d: cannot load: %s\n", n + 1, SDL_GetError());
        }
        if (r == NW_SAVE_NONE || r == NW_SAVE_FAILED) {
            slot->empty = 1;
            SDL_zero(nw_saves.saved[i]);
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

void nw_saves_write(struct game_s *slot)
{
    const NwSaveRegistry *reg = nw_saves_ready(nw_saves.dir);
    int i = nw_saves_index(slot);
    nw_saves.ok = reg && i >= 0 && nw_save_store(nw_saves.dir, nw_saves.page * NW_SAVES_PAGE + i, reg,
                                                 (const uint8_t *)slot, NULL);
    if (nw_saves.ok) {
        SDL_Time now = 0;
        SDL_GetCurrentTime(&now);
        nw_save_time_from(now, &nw_saves.saved[i]);
    } else {
        fprintf(stderr, NW_APP_NAME ": saves: cannot save: %s\n", reg ? SDL_GetError() : "no saves folder");
    }
    nw_saves_scan();
    if (nw_saves.ok) { /* written now: the last, whatever the file times say */
        nw_saves.latest = nw_saves.page * NW_SAVES_PAGE + i;
    }
    nw_saves.wait = NW_SAVES_SAVED_FRAMES;
}

void nw_saves_delete(struct game_s *slot)
{
    int i = nw_saves_index(slot);
    NwSaveResult r = nw_saves.dir && i >= 0 ? nw_save_remove(nw_saves.dir, nw_saves.page * NW_SAVES_PAGE + i)
                                            : NW_SAVE_FAILED;
    nw_saves.ok = r != NW_SAVE_FAILED;
    if (nw_saves.ok) {
        slot->empty = 1;
        SDL_zero(nw_saves.saved[i]);
    } else {
        fprintf(stderr, NW_APP_NAME ": saves: cannot delete: %s\n", nw_saves.dir ? SDL_GetError() : "no saves folder");
    }
    nw_saves_scan();
    nw_saves.wait = NW_SAVES_SAVED_FRAMES;
}

void nw_saves_loaded(void)
{
    nw_saves.wait = NW_SAVES_LOADED_FRAMES;
}

int nw_saves_shown(void)
{
    if (nw_saves.wait > 0) {
        nw_saves.wait--;
    }
    return nw_saves.wait == 0;
}

int nw_saves_ok(void)
{
    return nw_saves.ok;
}

int nw_saves_any(void)
{
    return nw_saves.latest >= 0;
}
