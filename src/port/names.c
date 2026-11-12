/* NuWorld's longer names beside the game's own (port/names.h). */
#include "port/names.h"

#include <stdint.h>

#include <SDL3/SDL.h>

#include "game/gametypes.h"
#include "game/game.retail.h"
#include "gameplay/gameplay.h"
#include "nu_types.h"

/* Declared by no header. */
extern struct game_s Game;
extern struct game_s SaveSlot[2][2];
extern s32 Level, newleveltime_slot;

_Static_assert(sizeof(((struct game_s *)0)->name) == NW_NAMES_OWN_GAME + 1, "game_s.name");
_Static_assert(sizeof(((struct time_s *)0)->name) == NW_NAMES_OWN_TIME + 1, "time_s.name");
_Static_assert(sizeof(((struct game_s *)0)->level) / sizeof(struct level_s) == NW_SAVE_LEVELS, "game_s.level");

static struct {
    NwSaveNames game;
    NwSaveNames slot[4];
} nw_names;

NwSaveNames *nw_names_game(void)
{
    return &nw_names.game;
}

NwSaveNames *nw_names_slot(int i)
{
    return &nw_names.slot[i & 3];
}

void nw_names_save(int i)
{
    if (i >= 0 && i < 4) {
        nw_names.slot[i] = nw_names.game;
    }
}

void nw_names_load(int i)
{
    if (i >= 0 && i < 4) {
        nw_names.game = nw_names.slot[i];
    }
}

int nw_names_time_max(void)
{
    return nw_profile.long_time_names ? NW_NAMES_TIME : NW_NAMES_OWN_TIME;
}

/* Whether the longer name stands for the game's own one of width letters: own is its start, spaces after it. */
static bool nw_names_stand(const char *longer, const char *own, size_t width)
{
    size_t n = SDL_strlen(longer);
    if (!n) {
        return false;
    }
    for (size_t k = 0; k < width; k++) {
        if (own[k] != (k < n ? longer[k] : ' ')) {
            return false;
        }
    }
    return own[width] == 0;
}

/* Which set g is in: Game's, a SaveSlot's, else NULL. */
static NwSaveNames *nw_names_set(const void *at, const struct game_s **in)
{
    uintptr_t off = (uintptr_t)at - (uintptr_t)&SaveSlot[0][0];
    if (off < sizeof(SaveSlot)) {
        *in = &SaveSlot[0][0] + off / sizeof(struct game_s);
        return &nw_names.slot[off / sizeof(struct game_s)];
    }
    off = (uintptr_t)at - (uintptr_t)&Game;
    if (off < sizeof(Game)) {
        *in = &Game;
        return &nw_names.game;
    }
    return NULL;
}

char *nw_names_of(const struct game_s *g)
{
    const struct game_s *in = NULL;
    NwSaveNames *set = nw_names_set(g, &in);
    if (set && in == g && nw_names_stand(set->game, g->name, NW_NAMES_OWN_GAME)) {
        return set->game;
    }
    return (char *)g->name;
}

char *nw_names_time_text(const void *time)
{
    const struct time_s *t = time;
    const struct game_s *in = NULL;
    NwSaveNames *set = nw_names_set(time, &in);
    if (set) {
        uintptr_t off = (uintptr_t)time - (uintptr_t)in->level;
        size_t l = off / sizeof(struct level_s);
        uintptr_t inner = off % sizeof(struct level_s) - offsetof(struct level_s, time);
        size_t j = inner / sizeof(struct time_s);
        if (off < sizeof(in->level) && inner % sizeof(struct time_s) == 0 && j < NW_SAVE_TIMES &&
            nw_names_stand(set->time[l][j], t->name, NW_NAMES_OWN_TIME)) {
            return set->time[l][j];
        }
    }
    return (char *)t->name;
}

float nw_names_scale(float scale, const char *text, size_t fit)
{
    size_t n = SDL_strlen(text);
    return n > fit ? scale * (float)fit / (float)n : scale;
}

/* Retail's, then the longer names move down with the records it moves; the new one's is being entered. */
void NewLevelTime(s32 t)
{
    NewLevelTime__retail(t);
    int i = newleveltime_slot;
    if (i >= 0 && i < NW_SAVE_TIMES && (u32)Level < NW_SAVE_LEVELS) {
        char (*times)[NW_SAVE_LONG_TIME + 1] = nw_names.game.time[Level];
        for (int j = NW_SAVE_TIMES - 1; j > i; j--) {
            SDL_memcpy(times[j], times[j - 1], sizeof(times[j]));
        }
        times[i][0] = 0;
    }
}

/* Retail's; resetting every record's name (a new game) drops their longer names. */
void DefaultTimeTrialNames(s32 all)
{
    DefaultTimeTrialNames__retail(all);
    if (all) {
        SDL_zeroa(nw_names.game.time);
    }
}
