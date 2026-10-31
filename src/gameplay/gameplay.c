#include "gameplay/gameplay.h"

#include <stdio.h>

#include <SDL3/SDL.h>

#include "gameplay/air.h"
#include "gameplay/ground.h"
#include "gameplay/monkeybars.h"
#include "gameplay/title.h"
#include "port/game.h"

NwProfile nw_profile;

static SDL_SpinLock nw_profile_lock;
static bool nw_profile_asked;
static NwProfile nw_profile_ask;

NwMovementProfile nw_movement_profile(const NwMovement *m)
{
    static const NwMovement nhanced = NW_MOVEMENT_NHANCED;
    const NwCrashMovement *c = &m->crash, *n = &nhanced.crash;
#define NW_CRASH_SET(f, ...) || c->f
#define NW_CRASH_SAME(f, ...) &&c->f == n->f
    bool retail = m->monkey_bar_speed <= 100 && !(false NW_CRASH_MOVEMENT(NW_CRASH_SET, NW_CRASH_NONE, NW_CRASH_NONE));
    bool enhanced = m->monkey_bar_speed == nhanced.monkey_bar_speed &&
                    (true NW_CRASH_MOVEMENT(NW_CRASH_SAME, NW_CRASH_SAME, NW_CRASH_SAME));
#undef NW_CRASH_SET
#undef NW_CRASH_SAME
    return retail ? NW_MOVEMENT_PROFILE_RETAIL : enhanced ? NW_MOVEMENT_PROFILE_NHANCED : NW_MOVEMENT_PROFILE_CUSTOM;
}

void nw_profile_apply(const NwProfile *p)
{
    nw_profile = *p;
    nw_monkey_bars_apply();
    nw_air_apply();
    nw_ground_apply();
}

void nw_profile_request(const NwProfile *p)
{
    SDL_LockSpinlock(&nw_profile_lock);
    nw_profile_ask = *p;
    nw_profile_asked = true;
    SDL_UnlockSpinlock(&nw_profile_lock);
}

static void nw_profile_poll(void)
{
    SDL_LockSpinlock(&nw_profile_lock);
    bool asked = nw_profile_asked;
    NwProfile p = nw_profile_ask;
    nw_profile_asked = false;
    SDL_UnlockSpinlock(&nw_profile_lock);
    if (asked) {
        nw_profile.movement = p.movement;
        nw_profile.air_indicator = p.air_indicator;
        nw_monkey_bars_apply();
        nw_air_apply();
        nw_ground_apply();
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
    nw_profile_poll();
    nw_title_poll();
}
