#include "port/presence.h"

#include <SDL3/SDL.h>

#include "game/gametypes.h"
#include "gameplay/minigame.h"

/* Declared by no header. */
extern struct ldata_s LData[44];
extern struct game_s Game;
extern struct cursor_s Cursor;
extern char *LevelName[44][6], *HubName[6][6], *tWARPROOM[6];
extern s32 Level, Hub, GameMode, cutmovie, Paused, Demo, TimeTrial, Bonus, mg_wumpatot;
extern volatile s32 bgload_complete;

static SDL_SpinLock nw_presence_lock;
static NwPresenceSnap nw_presence_snap;

/* A game text as printable ASCII. */
static void nw_presence_copy(char *out, size_t size, const char *text)
{
    size_t n = 0;
    for (; text && *text && n + 1 < size; text++) {
        if ((unsigned char)*text >= 0x20 && (unsigned char)*text < 0x7F) {
            out[n++] = *text;
        }
    }
    out[n] = '\0';
}

void nw_presence_swap(void)
{
    NwPresenceSnap s;
    SDL_zero(s);
    s.valid = true;
    s.level = Level;
    s.hub = Hub;
    s.game_mode = GameMode;
    s.cutmovie = cutmovie;
    s.menu = Cursor.menu;
    s.bonus = Bonus;
    s.paused = Paused != 0;
    s.loading = bgload_complete == 0;
    s.demo = Demo != 0;
    s.time_trial = TimeTrial != 0;
    s.minigame = nw_minigame_playing();
    s.minigame_score = mg_wumpatot;
    s.minigame_best = nw_minigame_best();
    s.vehicle = -1;
    if (Level >= 0 && Level < (s32)SDL_arraysize(LData)) {
        s.level_flags = LData[Level].flags;
        s.character = LData[Level].character;
        s.vehicle = LData[Level].vehicle;
        nw_presence_copy(s.level_name, sizeof(s.level_name), LevelName[Level][0]);
    }
    if (Level >= 0 && Level < (s32)SDL_arraysize(Game.level)) {
        s.collected = Game.level[Level].flags;
    }
    if (Hub >= 0 && Hub < (s32)SDL_arraysize(HubName)) {
        nw_presence_copy(s.room_name, sizeof(s.room_name), HubName[Hub][0]);
    }
    nw_presence_copy(s.warp_room, sizeof(s.warp_room), tWARPROOM[0]);
    s.percent = Game.percent;
    s.crystals = Game.crystals;
    s.gems = Game.gems;
    s.relics = Game.relics;
    SDL_LockSpinlock(&nw_presence_lock);
    nw_presence_snap = s;
    SDL_UnlockSpinlock(&nw_presence_lock);
}

void nw_presence_poll(NwPresenceSnap *out)
{
    SDL_LockSpinlock(&nw_presence_lock);
    *out = nw_presence_snap;
    SDL_UnlockSpinlock(&nw_presence_lock);
}
