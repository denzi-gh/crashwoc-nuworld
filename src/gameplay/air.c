#include "gameplay/air.h"

#include "game/creature.h"
#include "game/gametypes.h"
#include "gameplay/crash.h"
#include "gameplay/gameplay.h"
#include "nu_port.h"
#include "nups2/ps2ctl.h"

/* Declared by no header. */
extern struct MoveInfo CrashMoveInfo;
extern struct MoveInfo CocoMoveInfo;
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

typedef struct NwAirShape {
    float launch, hang_v, ramp_v, fall;
    float reach; /* air speed scale that keeps retail's jump distance */
} NwAirShape;

static NwAirShape nw_air_shapes[2]; /* Crash's, Coco's */

static bool nw_air_arc, nw_air_cut, nw_air_dash;
static u32 nw_air_frame; /* GameTimer's frame at the last MoveCRASH */
static short nw_air_jump2 = 15, nw_air_star = 20, nw_air_coco_jump2 = 15;

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

static float nw_air_gravity(const NwAirShape *s, float v, bool cut)
{
    if (v > s->hang_v) {
        float up = cut ? NW_AIR_CUT : NW_AIR_RISE;
        return NW_AIR_HANG + (up - NW_AIR_HANG) * nw_air_ramp((v - s->hang_v) / s->ramp_v);
    }
    if (v > -s->hang_v) {
        return NW_AIR_HANG;
    }
    return NW_AIR_HANG + (s->fall - NW_AIR_HANG) * nw_air_ramp((-v - s->hang_v) / s->ramp_v);
}

/* A held jump on flat ground, frame by frame as the game flies it. */
static void nw_air_fly(const NwAirShape *s, float launch, float *apex, float *time)
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
        vy -= nw_air_gravity(s, vy / dt, false) * dt * dt;
    }
}

static void nw_air_shape_set(NwAirShape *s, int jump_float, int jump_weight, int jump_height)
{
    float f = jump_float / 100.0f, apex, time, lo = 1.0f, hi = 20.0f;
    s->hang_v = NW_AIR_HANG_V * f;
    s->ramp_v = NW_AIR_RAMP_V * (f < 1.0f ? f : 1.0f) + 0.01f;
    s->fall = NW_AIR_FALL * jump_weight / 100.0f;
    float height = NW_AIR_RETAIL_HEIGHT * jump_height / 100.0f;
    for (int i = 0; i < 40; i++) {
        float mid = (lo + hi) * 0.5f;
        nw_air_fly(s, mid, &apex, &time);
        *(apex < height ? &lo : &hi) = mid;
    }
    s->launch = hi;
    nw_air_fly(s, hi, &apex, &time);
    s->reach = time < NW_AIR_RETAIL_TIME ? NW_AIR_RETAIL_TIME / time : 1.0f;
}

static const NwAirShape *nw_air_shape(const NwCrashMovement *c)
{
    return &nw_air_shapes[c != &nw_profile.movement.crash];
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
        nw_air_shape_set(&nw_air_shapes[0], c->jump_float, c->jump_weight, c->jump_height);
    }
    const NwCocoMovement *coco = &nw_profile.movement.coco;
    short coco_jump2 = 15;
    if (coco->jump_fall) {
        NW_DEVIATION(movement.coco.jump_fall);
        coco_jump2 = NW_HZ(15, 18);
        nw_air_shape_set(&nw_air_shapes[1], coco->jump_float, coco->jump_weight, coco->jump_height);
    }
    if (CrashMoveInfo.JUMPFRAMES2 == nw_air_jump2) {
        CrashMoveInfo.JUMPFRAMES2 = jump2;
    }
    if (CrashMoveInfo.STARJUMPFRAMES == nw_air_star) {
        CrashMoveInfo.STARJUMPFRAMES = star;
    }
    if (CocoMoveInfo.JUMPFRAMES2 == nw_air_coco_jump2) {
        CocoMoveInfo.JUMPFRAMES2 = coco_jump2;
    }
    nw_air_jump2 = jump2;
    nw_air_star = star;
    nw_air_coco_jump2 = coco_jump2;
}

static bool nw_air_continued(void)
{
    return GameTimer.frame - nw_air_frame <= 1;
}

static struct MoveInfo *nw_air_info(const struct creature_s *plr)
{
    return plr->obj.character == 1 ? &CocoMoveInfo : &CrashMoveInfo;
}

/* Belly flop and the Death Tornado's hover keep retail's physics. */
static bool nw_air_flying(const struct creature_s *plr)
{
    bool hover = (ExtraMoves != 0 || (Game.powerbits & 4) != 0) && plr->spin >= 3 &&
                 plr->spin_frame < plr->spin_frames - nw_air_info(plr)->SPINRESETFRAMES;
    return (nw_crash_on_foot(plr) || nw_coco_on_foot(plr)) && !plr->obj.ground && !plr->obj.dangle &&
           !plr->obj.transporting && !plr->freeze && !(plr->slam != 0 && plr->slam < 3) && !hover;
}

void nw_air_loop(const struct creature_s *plr, float forward)
{
    const NwCrashMovement *c = nw_foot(plr);
    const struct MoveInfo *info = nw_air_info(plr);
    bool flying = nw_air_flying(plr);
    bool steered = flying && plr_rebound == 0 && plr->obj.pad_speed > 0.0f && in_speed > 0.0f;
    if (c->dash_jump && steered && nw_air_dash && nw_air_continued()) {
        NW_FOOT_DEVIATION(c, dash_jump);
        float run = info->RUNSPEED, dash = run + (info->SPRINTSPEED - run) * c->dash_kept / 100.0f;
        in_speed = in_speed < dash ? dash : in_speed;
    }
    bool jumping = steered && plr->jump != 0;
    if (c->jump_distance && jumping) {
        NW_FOOT_DEVIATION(c, jump_distance);
        in_speed = in_speed < info->RUNSPEED ? info->RUNSPEED : in_speed;
    }
    if (c->jump_fall && nw_air_arc && nw_air_continued() && in_speed > 0.0f) {
        NW_FOOT_DEVIATION(c, jump_fall);
        in_speed *= nw_air_shape(c)->reach;
    }
    if (c->jump_distance && jumping && forward < in_speed) {
        in_f_friction *= c->air_accel / 100.0f;
    }
    if (c->air_momentum && flying && plr->obj.pad_speed == 0.0f) {
        NW_FOOT_DEVIATION(c, air_momentum);
        in_f_friction *= 1.0f - c->air_drift / 100.0f;
        in_s_friction *= 1.0f - c->air_drift / 100.0f;
    }
}

/* air_jump: retail's double jump (case 3 in MoveCRASH) wants a released first jump, a slow fall and the power;
   fake what it lacks, undo it after if it still refused. */
void nw_air_before(struct creature_s *plr, const struct nupad_s *pad)
{
    const NwCrashMovement *c = nw_foot(plr);
    bool power = ExtraMoves != 0 || (Game.powerbits & 2) != 0;
    nw_air_fall = plr->obj.mom.y;
    nw_air_jump = plr->jump;
    nw_air_faked_fall = nw_air_faked_jump = nw_air_faked_power = false;
    /* Falling on the arc: end retail's pin so MoveCRASH's attack and belly flop checks see him fall too. */
    if (c->jump_fall && nw_air_arc && nw_air_continued() && plr->obj.mom.y <= 0.0f &&
        plr->jump_frame < plr->jump_frames) {
        NW_FOOT_DEVIATION(c, jump_fall);
        plr->jump_frame = plr->jump_frames;
    }
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
    const NwCrashMovement *c = nw_foot(plr);
    const NwAirShape *s = nw_air_shape(c);
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
    /* Coco's standing jump is type 2. */
    bool type = plr->jump_type == 1 || (plr->jump_type == 2 && plr->obj.character == 1);
    bool launch = (plr->jump == 1 && plr->jump_frame == 0 && type) || doubled;
    if (c->jump_fall && launch && nw_air_flying(plr)) {
        NW_FOOT_DEVIATION(c, jump_fall);
        nw_air_arc = true;
        nw_air_cut = false;
        plr->obj.mom.x *= s->reach;
        plr->obj.mom.z *= s->reach;
        plr->obj.mom.y = s->launch * dt;
    } else if (nw_air_arc) {
        nw_air_arc = c->jump_fall && nw_air_continued() && nw_air_flying(plr) && !plr->obj.dead && !plr->obj.boing &&
                     plr->jump != 0 && plr->jump_type != 3;
        if (nw_air_arc) {
            /* Over retail's pin and gravity, from the speed the frame's collisions left. */
            NW_FOOT_DEVIATION(c, jump_fall);
            float v = nw_air_fall / dt;
            nw_air_cut = nw_air_cut || (v > 0.0f && !(pad->paddata & 0x40));
            plr->obj.mom.y = nw_air_fall - nw_air_gravity(s, v, nw_air_cut) * dt * dt;
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
