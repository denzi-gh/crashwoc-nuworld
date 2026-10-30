#include "gameplay/air.h"

#include "game/creature.h"
#include "game/gametypes.h"
#include "gameplay/gameplay.h"
#include "nu_port.h"

/* Declared by no header. */
extern struct MoveInfo CrashMoveInfo;
extern struct gtimer_s GameTimer;
extern struct game_s Game;
extern s32 ExtraMoves;
extern s32 plr_rebound;
extern f32 GRAVITY;
extern f32 in_speed;
extern f32 in_f_friction;
extern f32 in_s_friction;

/* jump_fall: after the pinned rise a jump plays this much faster up to the apex and after it. Speeds scale with it,
   gravity with its square, so height and reach stay retail's. */
#define NW_AIR_RISE 1.25f
#define NW_AIR_FALL 1.5f
/* jump_distance: in a jump any stick tilt is a full run, reached this much faster. */
#define NW_AIR_ACCEL 2.0f
/* air_momentum: with the stick let go Crash slows down this much as fast as retail's. */
#define NW_AIR_DRIFT 0.25f

static float nw_air_warp = 1.0f; /* what Crash's mom is scaled by */
static u32 nw_air_frame;         /* GameTimer's frame at the last MoveCRASH */
static short nw_air_jump2 = 15, nw_air_star = 20; /* the jump data set last, retail's at both rates */

void nw_air_apply(void)
{
    /* PAL's rise at 60 Hz too: retail's NTSC data keeps the pin's 50 Hz tick counts. */
    bool on = nw_profile.movement.crash.jump_fall;
    short jump2 = 15, star = 20;
    if (on) {
        NW_DEVIATION(movement.crash.jump_fall);
        jump2 = NW_HZ(15, 18);
        star = NW_HZ(20, 24);
    }
    if (CrashMoveInfo.JUMPFRAMES2 == nw_air_jump2) {
        CrashMoveInfo.JUMPFRAMES2 = jump2;
    }
    if (CrashMoveInfo.STARJUMPFRAMES == nw_air_star) {
        CrashMoveInfo.STARJUMPFRAMES = star;
    }
    nw_air_jump2 = jump2;
    nw_air_star = star;
}

/* The warp Crash's mom holds: none after a frame without MoveCRASH (dead, vehicle, water, level start). */
static float nw_air_current(void)
{
    return GameTimer.frame - nw_air_frame <= 1 ? nw_air_warp : 1.0f;
}

/* In the air under his own steam: not on the bars, frozen or carried, and not in a belly flop or the Death Tornado,
   which keep retail's own physics. */
static bool nw_air_flying(const struct creature_s *plr)
{
    bool tornado = (ExtraMoves != 0 || (Game.powerbits & 4) != 0) && plr->spin != 0 &&
                   plr->spin_frame < plr->spin_frames - CrashMoveInfo.SPINRESETFRAMES;
    return !plr->obj.ground && !plr->obj.dangle && !plr->obj.transporting && !plr->freeze &&
           !(plr->slam != 0 && plr->slam < 3) && !tornado;
}

/* After the pinned rise, or a walk off a ledge once its jump window is over. */
static float nw_air_want(const struct creature_s *plr)
{
    if (!nw_profile.movement.crash.jump_fall || !nw_air_flying(plr) || plr->obj.dead ||
        (plr->jump ? plr->jump_frame < plr->jump_frames : plr->allow_jump != 0)) {
        return 1.0f;
    }
    return plr->obj.mom.y > 0.0f ? NW_AIR_RISE : NW_AIR_FALL;
}

void nw_air_loop(const struct creature_s *plr, float forward)
{
    const NwCrashMovement *sw = &nw_profile.movement.crash;
    float s = nw_air_current();
    bool flying = nw_air_flying(plr);
    if (sw->jump_distance && flying && plr->jump != 0 && plr_rebound == 0 && plr->obj.pad_speed > 0.0f &&
        in_speed > 0.0f) {
        NW_DEVIATION(movement.crash.jump_distance);
        if (in_speed < CrashMoveInfo.RUNSPEED) {
            in_speed = CrashMoveInfo.RUNSPEED;
        }
        if (forward < in_speed * s) {
            in_f_friction *= NW_AIR_ACCEL;
        }
    }
    if (sw->air_momentum && flying && plr->obj.pad_speed == 0.0f) {
        NW_DEVIATION(movement.crash.air_momentum);
        in_f_friction *= NW_AIR_DRIFT;
        in_s_friction *= NW_AIR_DRIFT;
    }
    if (s != 1.0f && sw->jump_fall) {
        NW_DEVIATION(movement.crash.jump_fall);
        in_speed *= s;
        in_f_friction *= s * s;
        in_s_friction *= s * s;
    }
}

void nw_air_move(struct creature_s *plr)
{
    float was = nw_air_current(), now = nw_air_want(plr), k = now / was;
    if (now != was) {
        plr->obj.mom.x *= k;
        plr->obj.mom.z *= k;
    }
    /* Landing, a pin, a belly flop or the bars set the vertical speed themselves; retail just added one GRAVITY. */
    if (now != 1.0f) {
        NW_DEVIATION(movement.crash.jump_fall);
        plr->obj.mom.y = (plr->obj.mom.y - GRAVITY) * k + now * now * GRAVITY;
    } else if (was != 1.0f && !nw_profile.movement.crash.jump_fall) {
        plr->obj.mom.y = (plr->obj.mom.y - GRAVITY) * k + GRAVITY;
    }
    nw_air_warp = now;
    nw_air_frame = GameTimer.frame;
}
