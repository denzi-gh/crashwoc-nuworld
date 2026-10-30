#include "gameplay/gameplay.h"

#include <stdio.h>

#include <SDL3/SDL.h>

#include "gameplay/monkeybars.h"
#include "gameplay/title.h"
#include "port/game.h"

NwProfile nw_profile;

static SDL_SpinLock nw_movement_lock;
static bool nw_movement_asked;
static NwMovement nw_movement_ask;

NwMovementProfile nw_movement_profile(const NwMovement *m)
{
    static const NwMovement nhanced = NW_MOVEMENT_NHANCED;
    bool retail = m->monkey_bar_speed <= 100;
    bool enhanced = m->monkey_bar_speed == nhanced.monkey_bar_speed;
    return retail ? NW_MOVEMENT_PROFILE_RETAIL : enhanced ? NW_MOVEMENT_PROFILE_NHANCED : NW_MOVEMENT_PROFILE_CUSTOM;
}

void nw_profile_apply(const NwProfile *p)
{
    nw_profile = *p;
    nw_monkey_bars_apply();
}

void nw_movement_request(const NwMovement *m)
{
    SDL_LockSpinlock(&nw_movement_lock);
    nw_movement_ask = *m;
    nw_movement_asked = true;
    SDL_UnlockSpinlock(&nw_movement_lock);
}

static void nw_movement_poll(void)
{
    SDL_LockSpinlock(&nw_movement_lock);
    bool asked = nw_movement_asked;
    NwMovement m = nw_movement_ask;
    nw_movement_asked = false;
    SDL_UnlockSpinlock(&nw_movement_lock);
    if (asked) {
        nw_profile.movement = m;
        nw_monkey_bars_apply();
    }
}

void nw_deviation_off(const char *sw)
{
    char message[128];
    snprintf(message, sizeof message, "gameplay: %s ran with its switch off", sw);
    nw_game_fatal(message);
}

void nw_gameplay_swap(void)
{
    nw_movement_poll();
    nw_title_poll();
}
