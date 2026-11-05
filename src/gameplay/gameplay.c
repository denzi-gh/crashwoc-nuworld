#include "gameplay/gameplay.h"

#include <stdio.h>

#include <SDL3/SDL.h>

#include "gameplay/air.h"
#include "gameplay/ground.h"
#include "gameplay/jeep.h"
#include "gameplay/monkeybars.h"
#include "gameplay/submarine.h"
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
    const NwCocoMovement *cc = &m->coco, *cn = &nhanced.coco;
    const NwMechMovement *mc = &m->mech, *mn = &nhanced.mech;
    const NwSubMovement *sc = &m->submarine, *sn = &nhanced.submarine;
    const NwJeepMovement *jc = &m->jeep, *jn = &nhanced.jeep;
    const NwAtlasMovement *ac = &m->atlasphere, *an = &nhanced.atlasphere;
    const NwFireflyMovement *fc = &m->firefly, *fn = &nhanced.firefly;
    const NwPlaneMovement *pc = &m->plane, *pn = &nhanced.plane;
    const NwSpaceMovement *sfc = &m->spacefighter, *sfn = &nhanced.spacefighter;
#define NW_CRASH_SET(f, ...) || c->f
#define NW_CRASH_SAME(f, ...) &&c->f == n->f
#define NW_COCO_SET(f, ...) || cc->f
#define NW_COCO_SAME(f, ...) &&cc->f == cn->f
#define NW_MECH_SET(f, ...) || mc->f
#define NW_MECH_SAME(f, ...) &&mc->f == mn->f
#define NW_SUB_SET(f, ...) || sc->f
#define NW_SUB_SAME(f, ...) &&sc->f == sn->f
#define NW_JEEP_SET(f, ...) || jc->f
#define NW_JEEP_SAME(f, ...) &&jc->f == jn->f
#define NW_ATLAS_SET(f, ...) || ac->f
#define NW_ATLAS_SAME(f, ...) &&ac->f == an->f
#define NW_FIREFLY_SET(f, ...) || fc->f
#define NW_FIREFLY_SAME(f, ...) &&fc->f == fn->f
#define NW_PLANE_SET(f, ...) || pc->f
#define NW_PLANE_SAME(f, ...) &&pc->f == pn->f
#define NW_SPACE_SET(f, ...) || sfc->f
#define NW_SPACE_SAME(f, ...) &&sfc->f == sfn->f
    bool retail = m->monkey_bar_speed <= 100 && !(false NW_CRASH_MOVEMENT(NW_CRASH_SET, NW_CRASH_NONE, NW_CRASH_NONE)
                                                      NW_COCO_MOVEMENT(NW_COCO_SET, NW_CRASH_NONE, NW_CRASH_NONE)
                                                      NW_MECH_MOVEMENT(NW_MECH_SET, NW_CRASH_NONE, NW_CRASH_NONE)
                                                      NW_SUB_MOVEMENT(NW_SUB_SET, NW_CRASH_NONE, NW_CRASH_NONE)
                                                      NW_JEEP_MOVEMENT(NW_JEEP_SET, NW_CRASH_NONE, NW_CRASH_NONE)
                                                      NW_ATLAS_MOVEMENT(NW_ATLAS_SET, NW_CRASH_NONE, NW_CRASH_NONE)
                                                      NW_FIREFLY_MOVEMENT(NW_FIREFLY_SET, NW_CRASH_NONE, NW_CRASH_NONE)
                                                      NW_PLANE_MOVEMENT(NW_PLANE_SET, NW_CRASH_NONE, NW_CRASH_NONE)
                                                      NW_SPACE_MOVEMENT(NW_SPACE_SET, NW_CRASH_NONE, NW_CRASH_NONE));
    bool enhanced = m->monkey_bar_speed == nhanced.monkey_bar_speed &&
                    (true NW_CRASH_MOVEMENT(NW_CRASH_SAME, NW_CRASH_SAME, NW_CRASH_SAME)
                         NW_COCO_MOVEMENT(NW_COCO_SAME, NW_COCO_SAME, NW_COCO_SAME)
                         NW_MECH_MOVEMENT(NW_MECH_SAME, NW_MECH_SAME, NW_MECH_SAME)
                         NW_SUB_MOVEMENT(NW_SUB_SAME, NW_SUB_SAME, NW_SUB_SAME)
                         NW_JEEP_MOVEMENT(NW_JEEP_SAME, NW_JEEP_SAME, NW_JEEP_SAME)
                         NW_ATLAS_MOVEMENT(NW_ATLAS_SAME, NW_ATLAS_SAME, NW_ATLAS_SAME)
                         NW_FIREFLY_MOVEMENT(NW_FIREFLY_SAME, NW_FIREFLY_SAME, NW_FIREFLY_SAME)
                         NW_PLANE_MOVEMENT(NW_PLANE_SAME, NW_PLANE_SAME, NW_PLANE_SAME)
                         NW_SPACE_MOVEMENT(NW_SPACE_SAME, NW_SPACE_SAME, NW_SPACE_SAME));
#undef NW_CRASH_SET
#undef NW_CRASH_SAME
#undef NW_COCO_SET
#undef NW_COCO_SAME
#undef NW_MECH_SET
#undef NW_MECH_SAME
#undef NW_SUB_SET
#undef NW_SUB_SAME
#undef NW_JEEP_SET
#undef NW_JEEP_SAME
#undef NW_ATLAS_SET
#undef NW_ATLAS_SAME
#undef NW_FIREFLY_SET
#undef NW_FIREFLY_SAME
#undef NW_PLANE_SET
#undef NW_PLANE_SAME
#undef NW_SPACE_SET
#undef NW_SPACE_SAME
    return retail ? NW_MOVEMENT_PROFILE_RETAIL : enhanced ? NW_MOVEMENT_PROFILE_NHANCED : NW_MOVEMENT_PROFILE_CUSTOM;
}

void nw_profile_apply(const NwProfile *p)
{
    nw_profile = *p;
    nw_monkey_bars_apply();
    nw_air_apply();
    nw_ground_apply();
    nw_sub_apply();
    nw_jeep_apply();
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
        nw_profile.boss_health = p.boss_health;
        nw_profile.camera_stability = p.camera_stability;
        nw_profile.hitboxes = p.hitboxes;
        nw_monkey_bars_apply();
        nw_air_apply();
        nw_ground_apply();
        nw_sub_apply();
        nw_jeep_apply();
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
