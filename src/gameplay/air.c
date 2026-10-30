#include "gameplay/air.h"

#include "game/creature.h"
#include "game/gametypes.h"
#include "gameplay/gameplay.h"
#include "nu_port.h"
#include "nups2/ps2ctl.h"

/* Declared by no header. */
extern struct MoveInfo CrashMoveInfo;
extern struct gtimer_s GameTimer;
extern struct game_s Game;
extern s32 ExtraMoves;
extern s32 plr_rebound;
extern f32 in_speed;
extern f32 in_f_friction;
extern f32 in_s_friction;

/* The jump arc at 100 %, in units and seconds. */
#define NW_AIR_RISE 22.0f
#define NW_AIR_CUT 60.0f
#define NW_AIR_HANG 9.0f
#define NW_AIR_HANG_V 1.2f
#define NW_AIR_RAMP_V 1.5f
#define NW_AIR_FALL 27.0f
/* Retail's held jump. */
#define NW_AIR_RETAIL_HEIGHT 1.2632f
#define NW_AIR_RETAIL_TIME 0.88f

static struct {
    float launch, hang_v, ramp_v, fall;
    float reach; /* air speed scale that keeps retail's jump distance */
} nw_air_shape;

static bool nw_air_arc, nw_air_cut, nw_air_dash;
static u32 nw_air_frame; /* GameTimer's frame at the last MoveCRASH */
static short nw_air_jump2 = 15, nw_air_star = 20;

static float nw_air_fall;
static signed char nw_air_jump;
static bool nw_air_faked_fall, nw_air_faked_jump, nw_air_faked_power;

static float nw_air_dt(void)
{
    return NW_HZ(1.0f / 50.0f, 1.0f / 60.0f);
}

static float nw_air_ramp(float x)
{
    return x <= 0.0f ? 0.0f : x >= 1.0f ? 1.0f : x;
}

static float nw_air_gravity(float v, bool cut)
{
    if (v > nw_air_shape.hang_v) {
        float up = cut ? NW_AIR_CUT : NW_AIR_RISE;
        return NW_AIR_HANG + (up - NW_AIR_HANG) * nw_air_ramp((v - nw_air_shape.hang_v) / nw_air_shape.ramp_v);
    }
    if (v > -nw_air_shape.hang_v) {
        return NW_AIR_HANG;
    }
    return NW_AIR_HANG +
           (nw_air_shape.fall - NW_AIR_HANG) * nw_air_ramp((-v - nw_air_shape.hang_v) / nw_air_shape.ramp_v);
}

/* A held jump on flat ground, frame by frame as the game flies it. */
static void nw_air_fly(float launch, float *apex, float *time)
{
    float dt = nw_air_dt(), y = 0.0f, vy = launch * dt;
    *apex = 0.0f;
    for (int frame = 1; frame < 1000; frame++) {
        y += vy;
        *apex = y > *apex ? y : *apex;
        *time = frame * dt;
        if (y < 0.0f) {
            return;
        }
        vy -= nw_air_gravity(vy / dt, false) * dt * dt;
    }
}

static void nw_air_shape_set(const NwCrashMovement *c)
{
    float f = c->jump_float / 100.0f, apex, time, lo = 1.0f, hi = 20.0f;
    nw_air_shape.hang_v = NW_AIR_HANG_V * f;
    nw_air_shape.ramp_v = NW_AIR_RAMP_V * (f < 1.0f ? f : 1.0f) + 0.01f;
    nw_air_shape.fall = NW_AIR_FALL * c->jump_weight / 100.0f;
    float height = NW_AIR_RETAIL_HEIGHT * c->jump_height / 100.0f;
    for (int i = 0; i < 40; i++) {
        float mid = (lo + hi) * 0.5f;
        nw_air_fly(mid, &apex, &time);
        *(apex < height ? &lo : &hi) = mid;
    }
    nw_air_shape.launch = hi;
    nw_air_fly(hi, &apex, &time);
    nw_air_shape.reach = time < NW_AIR_RETAIL_TIME ? NW_AIR_RETAIL_TIME / time : 1.0f;
}

void nw_air_apply(void)
{
    /* NTSC data keeps the pin's 50 Hz frame counts; bounces and the star jump still use the pin. */
    const NwCrashMovement *c = &nw_profile.movement.crash;
    short jump2 = 15, star = 20;
    if (c->jump_fall) {
        NW_DEVIATION(movement.crash.jump_fall);
        jump2 = NW_HZ(15, 18);
        star = NW_HZ(20, 24);
        nw_air_shape_set(c);
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

static bool nw_air_continued(void)
{
    return GameTimer.frame - nw_air_frame <= 1;
}

/* Belly flop and Death Tornado keep retail's physics. */
static bool nw_air_flying(const struct creature_s *plr)
{
    bool tornado = (ExtraMoves != 0 || (Game.powerbits & 4) != 0) && plr->spin != 0 &&
                   plr->spin_frame < plr->spin_frames - CrashMoveInfo.SPINRESETFRAMES;
    return !plr->obj.ground && !plr->obj.dangle && !plr->obj.transporting && !plr->freeze &&
           !(plr->slam != 0 && plr->slam < 3) && !tornado;
}

void nw_air_loop(const struct creature_s *plr, float forward)
{
    const NwCrashMovement *c = &nw_profile.movement.crash;
    bool flying = nw_air_flying(plr);
    bool steered = flying && plr_rebound == 0 && plr->obj.pad_speed > 0.0f && in_speed > 0.0f;
    if (c->dash_jump && steered && nw_air_dash && nw_air_continued()) {
        NW_DEVIATION(movement.crash.dash_jump);
        float run = CrashMoveInfo.RUNSPEED, dash = run + (CrashMoveInfo.SPRINTSPEED - run) * c->dash_kept / 100.0f;
        in_speed = in_speed < dash ? dash : in_speed;
    }
    bool jumping = steered && plr->jump != 0;
    if (c->jump_distance && jumping) {
        NW_DEVIATION(movement.crash.jump_distance);
        in_speed = in_speed < CrashMoveInfo.RUNSPEED ? CrashMoveInfo.RUNSPEED : in_speed;
    }
    if (c->jump_fall && nw_air_arc && nw_air_continued() && in_speed > 0.0f) {
        NW_DEVIATION(movement.crash.jump_fall);
        in_speed *= nw_air_shape.reach;
    }
    if (c->jump_distance && jumping && forward < in_speed) {
        in_f_friction *= c->air_accel / 100.0f;
    }
    if (c->air_momentum && flying && plr->obj.pad_speed == 0.0f) {
        NW_DEVIATION(movement.crash.air_momentum);
        in_f_friction *= 1.0f - c->air_drift / 100.0f;
        in_s_friction *= 1.0f - c->air_drift / 100.0f;
    }
}

/* air_jump: retail's double jump (case 3 in MoveCRASH) wants a released first jump, a slow fall and the power;
   fake what it lacks, undo it after if it still refused. */
void nw_air_before(struct creature_s *plr, const struct nupad_s *pad)
{
    const NwCrashMovement *c = &nw_profile.movement.crash;
    bool power = ExtraMoves != 0 || (Game.powerbits & 2) != 0;
    nw_air_fall = plr->obj.mom.y;
    nw_air_jump = plr->jump;
    nw_air_faked_fall = nw_air_faked_jump = nw_air_faked_power = false;
    if (!c->air_jump || !(power || c->air_jump_free) || !nw_air_flying(plr) || plr->obj.dead) {
        return;
    }
    if (c->air_jump_ledge && (plr->jump == 0 || plr->jump == 6) && plr->allow_jump == 0 &&
        (pad->paddata_db & 0x40)) {
        NW_DEVIATION(movement.crash.air_jump);
        plr->jump = 3;
        plr->jump_frame = plr->jump_frames;
        nw_air_faked_jump = true;
    }
    if (plr->jump != 3 || !(pad->paddata & 0x40)) {
        return;
    }
    float fall = -plr->obj.mom.y / nw_air_dt();
    if (plr->obj.mom.y <= NW_HZ(-0.099999994f, -0.083333336f) && (c->air_jump_fall >= 12 || fall <= c->air_jump_fall)) {
        NW_DEVIATION(movement.crash.air_jump);
        plr->obj.mom.y = 0.0f;
        nw_air_faked_fall = true;
    }
    if (!power) {
        NW_DEVIATION(movement.crash.air_jump);
        Game.powerbits |= 2;
        nw_air_faked_power = true;
    }
}

void nw_air_move(struct creature_s *plr, const struct nupad_s *pad)
{
    const NwCrashMovement *c = &nw_profile.movement.crash;
    bool doubled = plr->jump == 4 && plr->jump_frame == 0;
    if (nw_air_faked_power) {
        Game.powerbits &= ~2;
    }
    if (!doubled && nw_air_faked_fall) {
        plr->obj.mom.y += nw_air_fall;
    }
    if (!doubled && nw_air_faked_jump) {
        plr->jump = nw_air_jump;
    }
    float dt = nw_air_dt();
    bool launch = (plr->jump == 1 && plr->jump_frame == 0 && plr->jump_type == 1) || doubled;
    if (c->jump_fall && launch && nw_air_flying(plr)) {
        NW_DEVIATION(movement.crash.jump_fall);
        nw_air_arc = true;
        nw_air_cut = false;
        plr->obj.mom.y = nw_air_shape.launch * dt;
    } else if (nw_air_arc) {
        nw_air_arc = c->jump_fall && nw_air_continued() && nw_air_flying(plr) && !plr->obj.dead && !plr->obj.boing &&
                     plr->jump != 0 && plr->jump_type != 3;
        if (nw_air_arc) {
            /* Over retail's pin and gravity, from the speed the frame's collisions left. */
            NW_DEVIATION(movement.crash.jump_fall);
            float v = nw_air_fall / dt;
            nw_air_cut = nw_air_cut || (v > 0.0f && !(pad->paddata & 0x40));
            plr->obj.mom.y = nw_air_fall - nw_air_gravity(v, nw_air_cut) * dt * dt;
        }
    }
    /* Only retail's take-off frame ends with jump 1 at frame 0. */
    if (plr->jump == 1 && plr->jump_frame == 0) {
        nw_air_dash = plr->sprint != 0;
    }
    nw_air_dash = nw_air_dash && nw_air_continued() && c->dash_jump && !plr->obj.ground && !plr->obj.dangle &&
                  !plr->obj.dead && !(plr->slam != 0 && plr->slam < 3);
    nw_air_frame = GameTimer.frame;
}
