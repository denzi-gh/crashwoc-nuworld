#include "gameplay/cheats.h"

#include <SDL3/SDL.h>

#include "game/gametypes.h"

extern struct game_s Game;
extern struct ldata_s LData[44];
extern s32 Level, Demo, InvincibilityCHEAT, last_level;
extern u16 plr_items, new_lev_flags;

s32 HubFromLevel(s32 level);
void CalculateGamePercentage(struct game_s *game);

#define NW_CHEATS_HUB 0x25
#define NW_CHEATS_ROOMS 6
#define NW_CHEATS_BOSS_ROOMS 5

typedef struct NwCheatsAsk {
    bool asked;
    int invincible;
    uint8_t power_mask, power_value;
    uint16_t mask[NW_CHEATS_LEVELS], value[NW_CHEATS_LEVELS];
} NwCheatsAsk;

static const NwCheatsAsk nw_cheats_none = { .invincible = -1 };

static struct {
    SDL_SpinLock lock;
    NwCheatsAsk ask;
    NwCheatsView view;
    uint16_t items[NW_CHEATS_LEVELS];
    bool built;
    bool used;
} nw_cheats = { .ask = { .invincible = -1 } };

static const struct {
    int level;
    uint16_t gem;
} nw_cheats_gems[] = {
    { 1, NW_CHEAT_RED_GEM },    { 17, NW_CHEAT_GREEN_GEM }, { 7, NW_CHEAT_BLUE_GEM },   { 19, NW_CHEAT_YELLOW_GEM },
    { 10, NW_CHEAT_PURPLE_GEM }, { 4, NW_CHEAT_BONUS_GEM },  { 5, NW_CHEAT_BONUS_GEM },  { 9, NW_CHEAT_BONUS_GEM },
    { 12, NW_CHEAT_BONUS_GEM },  { 14, NW_CHEAT_BONUS_GEM }, { 20, NW_CHEAT_BONUS_GEM }, { 27, NW_CHEAT_BONUS_GEM },
    { 28, NW_CHEAT_BONUS_GEM },  { 29, NW_CHEAT_BONUS_GEM }, { 32, NW_CHEAT_BONUS_GEM }, { 33, NW_CHEAT_BONUS_GEM },
};

static const struct {
    uint16_t owned;
    uint16_t held;
} nw_cheats_held[] = {
    { NW_CHEAT_CRYSTAL, 0x1 },    { NW_CHEAT_CRATE_GEM, 0x2 },   { NW_CHEAT_BONUS_GEM, 0x4 },
    { NW_CHEAT_RED_GEM, 0x8 },    { NW_CHEAT_BLUE_GEM, 0x10 },   { NW_CHEAT_GREEN_GEM, 0x20 },
    { NW_CHEAT_YELLOW_GEM, 0x40 }, { NW_CHEAT_PURPLE_GEM, 0x80 },
};

void nw_cheats_view(NwCheatsView *out)
{
    SDL_LockSpinlock(&nw_cheats.lock);
    *out = nw_cheats.view;
    SDL_UnlockSpinlock(&nw_cheats.lock);
}

void nw_cheats_invincible(bool on)
{
    SDL_LockSpinlock(&nw_cheats.lock);
    nw_cheats.ask.invincible = on;
    nw_cheats.ask.asked = true;
    SDL_UnlockSpinlock(&nw_cheats.lock);
}

void nw_cheats_powers(uint8_t mask, uint8_t value)
{
    SDL_LockSpinlock(&nw_cheats.lock);
    NwCheatsAsk *a = &nw_cheats.ask;
    a->power_value = (uint8_t)((a->power_value & ~mask) | (value & mask));
    a->power_mask |= mask;
    a->asked = true;
    SDL_UnlockSpinlock(&nw_cheats.lock);
}

void nw_cheats_level(int level, uint16_t mask, uint16_t value)
{
    if (level < 0 || level >= NW_CHEATS_LEVELS) {
        return;
    }
    SDL_LockSpinlock(&nw_cheats.lock);
    NwCheatsAsk *a = &nw_cheats.ask;
    a->value[level] = (uint16_t)((a->value[level] & ~mask) | (value & mask));
    a->mask[level] |= mask;
    a->asked = true;
    SDL_UnlockSpinlock(&nw_cheats.lock);
}

static void nw_cheats_build(void)
{
    for (int l = 0; l < NW_CHEATS_LEVELS; l++) {
        int room = HubFromLevel(l);
        uint16_t items = 0;
        if (room >= 0 && (LData[l].flags & 2)) {
            items = NW_CHEAT_BOSS;
        } else if (room >= 0) {
            items = NW_CHEAT_RELIC | NW_CHEAT_CRATE_GEM | (room < NW_CHEATS_BOSS_ROOMS ? NW_CHEAT_CRYSTAL : 0);
        }
        nw_cheats.items[l] = items;
    }
    for (size_t i = 0; i < SDL_arraysize(nw_cheats_gems); i++) {
        if (nw_cheats.items[nw_cheats_gems[i].level] & NW_CHEAT_CRATE_GEM) {
            nw_cheats.items[nw_cheats_gems[i].level] |= nw_cheats_gems[i].gem;
        }
    }
    nw_cheats.built = true;
}

static int nw_cheats_pending(void)
{
    int level = Level == NW_CHEATS_HUB ? last_level : Level;
    return level >= 0 && level < NW_CHEATS_LEVELS ? level : -1;
}

static void nw_cheats_rooms(const bool *rooms, int pending)
{
    if (pending >= 0 && (new_lev_flags & NW_CHEAT_CRYSTAL) && !(Game.level[pending].flags & NW_CHEAT_CRYSTAL)) {
        int room = HubFromLevel(pending);
        if (room >= 0 && room < NW_CHEATS_ROOMS) {
            Game.hub[room].crystals++;
        }
    }
    for (int r = 0; r < NW_CHEATS_BOSS_ROOMS; r++) {
        if (!rooms[r] || (Game.hub[r].flags & 4)) {
            continue;
        }
        if (Game.hub[r].crystals >= 5) {
            Game.hub[r].flags |= 2;
        } else {
            Game.hub[r].flags &= (u8)~2;
        }
    }
}

static void nw_cheats_apply(const NwCheatsAsk *a)
{
    if (a->invincible >= 0) {
        InvincibilityCHEAT = a->invincible;
    }
    Game.powerbits = (u8)((Game.powerbits & ~a->power_mask) | (a->power_value & a->power_mask));
    int pending = nw_cheats_pending();
    bool rooms[NW_CHEATS_ROOMS] = { false }, changed = false;
    for (int l = 0; l < NW_CHEATS_LEVELS; l++) {
        uint16_t mask = a->mask[l] & nw_cheats.items[l];
        if (!mask) {
            continue;
        }
        uint16_t was = Game.level[l].flags | (l == pending ? new_lev_flags : 0);
        uint16_t now = (uint16_t)((was & ~mask) | (a->value[l] & mask));
        uint16_t lost = was & ~now, won = now & ~was;
        Game.level[l].flags = (u16)((Game.level[l].flags & ~mask) | (now & mask));
        if (l == pending) {
            new_lev_flags &= (u16)~lost;
        }
        int room = HubFromLevel(l);
        if (room >= 0 && room < NW_CHEATS_ROOMS) {
            if (won & NW_CHEAT_BOSS) {
                Game.hub[room].flags |= 7;
                if (room + 1 < NW_CHEATS_ROOMS) {
                    Game.hub[room + 1].flags |= 1;
                }
            }
            if (lost & NW_CHEAT_BOSS) {
                Game.hub[room].flags &= (u8)~4;
            }
            rooms[room] = true;
        }
        changed = true;
    }
    if (changed) {
        CalculateGamePercentage(&Game);
        nw_cheats_rooms(rooms, pending);
        nw_cheats.used = true;
    }
}

static void nw_cheats_held_items(void)
{
    if (!nw_cheats.used || Level < 0 || Level >= NW_CHEATS_LEVELS) {
        return;
    }
    for (size_t i = 0; i < SDL_arraysize(nw_cheats_held); i++) {
        if (Game.level[Level].flags & nw_cheats_held[i].owned) {
            plr_items &= (u16)~nw_cheats_held[i].held;
        }
    }
}

void nw_cheats_swap(void)
{
    if (!nw_cheats.built) {
        nw_cheats_build();
    }
    SDL_LockSpinlock(&nw_cheats.lock);
    NwCheatsAsk a = nw_cheats.ask;
    nw_cheats.ask = nw_cheats_none;
    SDL_UnlockSpinlock(&nw_cheats.lock);
    if (a.asked && !Demo) {
        nw_cheats_apply(&a);
    }
    nw_cheats_held_items();
    NwCheatsView v = { .valid = !Demo,
                       .invincible = InvincibilityCHEAT != 0,
                       .powers = (uint8_t)(Game.powerbits & ((1u << NW_CHEATS_POWERS) - 1)),
                       .level = Level >= 0 && Level < NW_CHEATS_LEVELS ? Level : -1,
                       .percent = Game.percent,
                       .crystals = Game.crystals,
                       .gems = Game.gems,
                       .relics = Game.relics };
    int pending = nw_cheats_pending();
    for (int l = 0; l < NW_CHEATS_LEVELS; l++) {
        v.items[l] = nw_cheats.items[l];
        v.flags[l] = (uint16_t)((Game.level[l].flags | (l == pending ? new_lev_flags : 0)) & nw_cheats.items[l]);
    }
    SDL_LockSpinlock(&nw_cheats.lock);
    nw_cheats.view = v;
    SDL_UnlockSpinlock(&nw_cheats.lock);
}
