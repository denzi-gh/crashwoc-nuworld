/* src/gameplay: the profile's switches and the retail values they change. */
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "game/creature.h"
#include "game/gametypes.h"
#include "gameplay/air.h"
#include "gameplay/airindicator.h"
#include "gameplay/edges.h"
#include "gameplay/gameplay.h"
#include "gameplay/ground.h"
#include "gameplay/hazards.h"
#include "gameplay/line2d.h"
#include "gameplay/monkeybars.h"
#include "gameplay/submarine.h"
#include "nu3d/nugscn.h"
#include "nu_port.h"
#include "numath/nutrig.h"
#include "numath/nuvec.h"
#include "nups2/ps2ctl.h"
#include "port/hz.h"

extern struct MoveInfo CrashMoveInfo;
extern struct MoveInfo CocoMoveInfo;
extern struct MoveInfo SubmarineMoveInfo;
extern f32 D_005F28F8[];
extern struct nuvec_s v001;
extern s32 vtog_blend;
extern s32 plr_terrain_ok;
extern s32 VEHICLECONTROL;
extern struct ldata_s *LDATA;
extern struct gtimer_s GameTimer;
extern struct game_s Game;
extern f32 GRAVITY;
extern f32 in_speed;
extern f32 in_f_friction;
extern f32 in_s_friction;
extern NwTerI *TerI;
extern struct rail_s Rail[];
extern struct obj_s *pObj[64];
extern s32 GAMEOBJECTCOUNT;
void MoveLoopXZ(struct obj_s *obj, u16 *hdg);

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

static bool dangle(float speed)
{
    return CrashMoveInfo.DANGLESPEED == speed && CocoMoveInfo.DANGLESPEED == speed;
}

/* At boot, then between frames as the debug menu asks; back to retail's at the end. */
static void monkey_bars(int hz, float retail)
{
    nw_hz_set(hz);
    CrashMoveInfo.DANGLESPEED = CocoMoveInfo.DANGLESPEED = retail;
    nw_profile_apply(&(NwProfile){ 0 });
    CHECK(dangle(retail));
    nw_profile_apply(&(NwProfile){ .movement = NW_MOVEMENT_NHANCED });
    CHECK(dangle(retail * 3.5f));
    nw_profile_request(&(NwProfile){ .movement = { .monkey_bar_speed = 150 } });
    CHECK(dangle(retail * 3.5f));
    nw_gameplay_swap();
    CHECK(dangle(retail * 1.5f));
    /* A value someone else wrote stays. */
    CocoMoveInfo.DANGLESPEED = 1.0f;
    nw_profile_request(&(NwProfile){ .movement = { .monkey_bar_speed = 200 } });
    nw_gameplay_swap();
    CHECK(CrashMoveInfo.DANGLESPEED == retail * 2.0f && CocoMoveInfo.DANGLESPEED == 1.0f);
    CocoMoveInfo.DANGLESPEED = retail * 2.0f;
    nw_profile_request(&(NwProfile){ .movement = NW_MOVEMENT_RETAIL });
    nw_gameplay_swap();
    CHECK(dangle(retail));
}

static struct creature_s plr;
static struct CharacterModel model;
static struct animlist walk = { .speed = 1.0f };

/* hngwalk's speed after AnimateCRASH picked action with Crash moved by retail's bar speed times moved. */
static float hang_walk(int action, float moved)
{
    plr.obj.anim.newaction = action;
    plr.obj.xz_distance = 0.015599999f * moved;
    nw_monkey_bars_animate(&plr);
    return walk.speed;
}

static void monkey_bar_anim(void)
{
    nw_hz_set(50);
    model.animlist[0x4D] = &walk;
    plr.obj.model = &model;
    plr.obj.dangle = 1;
    CHECK(hang_walk(0x4D, 2.0f) == 1.0f);
    nw_profile_apply(&(NwProfile){ .movement = NW_MOVEMENT_NHANCED });
    /* Faster with the speed, but less than it grows. */
    float twice = hang_walk(0x4D, 2.0f);
    CHECK(twice > 1.0f && twice < 2.0f && hang_walk(0x4D, 3.0f) > twice);
    CHECK(hang_walk(0x4D, 0.5f) == 1.0f); /* not slower than retail's, also pushing against the bar's end */
    CHECK(hang_walk(0x4D, 10.0f) == hang_walk(0x4D, 20.0f)); /* the setting's speed at most */
    /* Let go: walks on while faster than retail's bar speed. */
    CHECK(hang_walk(0x20, 2.0f) == twice && plr.obj.anim.newaction == 0x4D);
    CHECK(hang_walk(0x20, 0.5f) == 1.0f && plr.obj.anim.newaction == 0x20);
    plr.obj.dangle = 2;
    CHECK(hang_walk(0x36, 2.0f) == 1.0f && plr.obj.anim.newaction == 0x36);
    plr.obj.dangle = 1;
    /* A speed someone else wrote stays. */
    walk.speed = 9.0f;
    CHECK(hang_walk(0x4D, 2.0f) == 9.0f);
    walk.speed = 1.0f;
    CHECK(hang_walk(0x4D, 2.0f) == twice);
    nw_profile_apply(&(NwProfile){ 0 });
    CHECK(hang_walk(0x4D, 2.0f) == 1.0f);
}

static struct creature_s jumper;
static struct ldata_s level_data;

/* The ring's floor and alpha with Crash's feet at feet over a floor at 2. */
static bool ring_at(float feet, float *y, float *alpha)
{
    jumper.obj.pos.y = feet;
    return nw_air_indicator_at(&jumper, y, alpha);
}

static void air_indicator(void)
{
    float y = 0.0f, alpha = 0.0f;
    LDATA = &level_data;
    plr_terrain_ok = 1;
    jumper.obj.SCALE = 1.0f;
    jumper.obj.vehicle = -1;
    jumper.obj.layer_type = -1;
    jumper.obj.shadow = 2.0f;
    CHECK(ring_at(4.0f, &y, &alpha) && y == 2.0f && alpha == 1.0f);
    CHECK(ring_at(2.3f, &y, &alpha) && alpha > 0.0f && alpha < 1.0f);
    CHECK(!ring_at(2.05f, &y, &alpha)); /* stepping off */
    jumper.obj.ground = 2;
    CHECK(!ring_at(4.0f, &y, &alpha));
    jumper.obj.ground = 0;
    jumper.obj.dangle = 1;
    CHECK(!ring_at(4.0f, &y, &alpha));
    jumper.obj.dangle = 0;
    /* No floor: retail's miss, then GetSurfaceInfo's SAFEY; a crate or platform under him is one. */
    jumper.obj.shadow = 2000000.0f;
    CHECK(!ring_at(4.0f, &y, &alpha));
    jumper.obj.shadow = 2.0f;
    plr_terrain_ok = 0;
    CHECK(!ring_at(4.0f, &y, &alpha));
    jumper.obj.got_shadow = 1;
    CHECK(ring_at(4.0f, &y, &alpha) && y == 2.0f);
    jumper.obj.got_shadow = 0;
    plr_terrain_ok = 1;
    /* Water over the floor. */
    jumper.obj.layer_type = 1;
    jumper.obj.layer_shadow = 3.0f;
    CHECK(ring_at(4.0f, &y, &alpha) && y == 3.0f);
    jumper.obj.layer_type = -1;
    VEHICLECONTROL = 1;
    jumper.obj.vehicle = 0x20;
    CHECK(!ring_at(4.0f, &y, &alpha));
    VEHICLECONTROL = 0;
    jumper.obj.vehicle = -1;

    /* The switch, at boot and between frames; off draws nothing. */
    nw_profile_apply(&(NwProfile){ 0 });
    CHECK(!nw_profile.air_indicator);
    nw_air_indicator_draw(&jumper);
    nw_profile_apply(&(NwProfile){ .air_indicator = true });
    CHECK(nw_profile.air_indicator);
    nw_profile_request(&(NwProfile){ .air_indicator = false });
    CHECK(nw_profile.air_indicator);
    nw_gameplay_swap();
    CHECK(!nw_profile.air_indicator);
}

static bool near(float a, float b, float within)
{
    return fabsf(a - b) <= within * fabsf(b);
}

/* Retail with one of Crash's switches on, its values at N. Hanced's. */
static NwProfile only(size_t offset)
{
    NwProfile p = { .movement = NW_MOVEMENT_RETAIL };
    *(bool *)((char *)&p.movement.crash + offset) = true;
    return p;
}

#define ONLY(f) only(offsetof(NwCrashMovement, f))

/* Retail's Crash data and globals at hz, then p. */
static void crash_at(int hz, const NwProfile *p)
{
    nw_hz_set(hz);
    nw_profile_apply(&(NwProfile){ 0 });
    CrashMoveInfo.JUMPFRAMES0 = NW_HZ(5, 6);
    CrashMoveInfo.JUMPFRAMES1 = NW_HZ(10, 12);
    CrashMoveInfo.RUNSPEED = NW_HZ(0.072f, 0.060000002f);
    CrashMoveInfo.SPRINTSPEED = NW_HZ(0.096f, 0.080000006f);
    CrashMoveInfo.SLIDESPEED = NW_HZ(0.096f, 0.080000006f);
    GRAVITY = NW_HZ(-0.0072000003f, -0.005f);
    VEHICLECONTROL = 0;
    nw_profile_apply(p);
    memset(&Character[0], 0, sizeof(Character[0]));
    Character[0].obj.vehicle = -1;
}

/* pull: the largest change of vertical speed in a frame after take-off, units a second squared. */
typedef struct Arc {
    float time, apex, reach, early, pull;
} Arc;

static struct nupad_s pad;

/* MoveCRASH's jump steps. */
static void retail_jump(struct creature_s *c, bool held)
{
    if (c->jump && c->jump_frame < c->jump_frames) {
        c->obj.mom.y = CrashMoveInfo.JUMPHEIGHT / CrashMoveInfo.JUMPFRAMES2;
    }
    if (!c->obj.ground) {
        c->obj.mom.y += GRAVITY;
    }
    if (c->jump && c->jump_frame < c->jump_frames) {
        c->jump_frame++;
    }
    if (c->jump == 1 && held && c->jump_frames < CrashMoveInfo.JUMPFRAMES2) {
        c->jump_frames++;
    } else if (c->jump == 1 && !held) {
        c->jump = 3;
    }
}

/* A take-off at run speed on flat ground, the button held for hold frames. */
static Arc jump(int hz, const NwProfile *p, int hold)
{
    crash_at(hz, p);
    struct creature_s *c = &Character[0];
    float run = CrashMoveInfo.RUNSPEED, friction = NW_HZ(0.0072f, 0.005f), dt = 1.0f / hz;
    c->obj.mom.z = run;
    GameTimer.frame++;
    pad.paddata = pad.paddata_db = 0x40;
    c->obj.ground = 1;
    nw_air_before(c, &pad);
    c->jump = 1;
    c->jump_type = 1;
    c->jump_frames = CrashMoveInfo.JUMPFRAMES0;
    c->jump_frame = 0;
    c->obj.ground = 0;
    nw_air_move(c, &pad);
    Arc arc = { 0 };
    float was = c->obj.mom.y;
    for (int tick = 1; tick < 500; tick++) {
        GameTimer.frame++;
        pad.paddata = tick <= hold ? 0x40 : 0;
        pad.paddata_db = 0;
        in_speed = run;
        in_f_friction = in_s_friction = friction;
        MoveLoopXZ(&c->obj, &c->obj.thdg);
        c->obj.pos.y += c->obj.mom.y;
        c->obj.pos.z += c->obj.mom.z;
        arc.apex = fmaxf(arc.apex, c->obj.pos.y);
        if (tick == hz / 10) {
            arc.early = c->obj.pos.y;
        }
        c->obj.ground = c->obj.pos.y < 0.0f;
        if (c->obj.ground) {
            c->obj.pos.y = c->obj.mom.y = 0.0f;
            c->jump = 0;
        }
        nw_air_before(c, &pad);
        retail_jump(c, tick <= hold);
        nw_air_move(c, &pad);
        if (c->obj.ground) {
            arc.time = tick / (float)hz;
            arc.reach = c->obj.pos.z;
            break;
        }
        if (tick > 1) {
            arc.pull = fmaxf(arc.pull, fabsf(c->obj.mom.y - was) / (dt * dt));
        }
        was = c->obj.mom.y;
    }
    return arc;
}

static void jump_fall(void)
{
    NwProfile retail = { 0 }, on = ONLY(jump_fall);
    for (int hold = 1; hold <= 100; hold += 99) {
        Arc pal = jump(50, &retail, hold), arc = jump(50, &on, hold);
        CHECK(near(arc.apex, pal.apex, 0.05f) && near(arc.reach, pal.reach, 0.05f));
        CHECK(fabsf(arc.time - pal.time) <= 0.05f);
        CHECK(arc.early > pal.early && arc.pull <= 61.0f);
    }
    /* Holds of 0.1 s and to the ground at both rates. */
    for (int hold = 5; hold <= 100; hold += 95) {
        Arc arc = jump(50, &on, hold), ntsc = jump(60, &on, hold * 6 / 5);
        CHECK(near(ntsc.apex, arc.apex, 0.03f) && near(ntsc.reach, arc.reach, 0.05f));
        CHECK(fabsf(ntsc.time - arc.time) <= 0.03f);
    }
    CHECK(jump(50, &on, 5).apex > jump(50, &on, 1).apex && jump(50, &on, 100).apex > jump(50, &on, 5).apex);
    CHECK(jump(60, &retail, 100).apex > jump(50, &retail, 100).apex * 1.1f);

    /* The sliders keep the height asked for and at least retail's reach. */
    Arc pal = jump(50, &retail, 100);
    for (int i = 0; i < 8; i++) {
        NwProfile p = on;
        p.movement.crash.jump_float = i & 1 ? 200 : 0;
        p.movement.crash.jump_weight = i & 2 ? 200 : 100;
        p.movement.crash.jump_height = i & 4 ? 130 : 100;
        Arc arc = jump(50, &p, 100), ntsc = jump(60, &p, 120);
        float height = pal.apex * p.movement.crash.jump_height / 100.0f;
        CHECK(near(arc.apex, height, 0.03f) && arc.reach >= pal.reach * 0.995f && arc.pull <= 61.0f);
        CHECK(near(ntsc.apex, arc.apex, 0.03f) && fabsf(ntsc.time - arc.time) <= 0.03f);
    }

    crash_at(60, &on);
    CHECK(CrashMoveInfo.JUMPFRAMES2 == 18 && CrashMoveInfo.STARJUMPFRAMES == 24);
    crash_at(50, &on);
    CHECK(CrashMoveInfo.JUMPFRAMES2 == 15 && CrashMoveInfo.STARJUMPFRAMES == 20);
    crash_at(60, &retail);
    CHECK(CrashMoveInfo.JUMPFRAMES2 == 15 && CrashMoveInfo.STARJUMPFRAMES == 20);

    /* Switched off mid-air: retail's gravity from the next frame. */
    struct creature_s *c = &Character[0];
    crash_at(50, &on);
    c->jump = 1;
    c->jump_type = 1;
    GameTimer.frame++;
    nw_air_before(c, &pad);
    nw_air_move(c, &pad);
    CHECK(near(c->obj.mom.y, 7.0f * 0.02f, 0.02f));
    nw_profile_request(&(NwProfile){ 0 });
    nw_gameplay_swap();
    GameTimer.frame++;
    c->jump = 3;
    c->jump_frame = c->jump_frames = 5;
    nw_air_before(c, &pad);
    retail_jump(c, false);
    float fall = c->obj.mom.y;
    nw_air_move(c, &pad);
    CHECK(c->obj.mom.y == fall);
}

/* MoveCRASH's case 3. */
static void retail_double(struct creature_s *c)
{
    if (c->jump == 3 && (Game.powerbits & 2) && (pad.paddata & 0x40) && c->obj.mom.y > -0.099999994f) {
        c->jump = 4;
        c->jump_frame = 0;
        c->jump_frames = CrashMoveInfo.JUMPFRAMES0;
        c->obj.mom.x = c->obj.mom.z = 0.0f;
    }
    c->obj.mom.y += GRAVITY;
}

/* Falling at fall after jump (0: off a ledge) with the button pressed; jump after the frame. */
static int air_jump_at(float fall, signed char jump, u8 allow_jump)
{
    struct creature_s *c = &Character[0];
    c->jump = jump;
    c->jump_frame = c->jump_frames = 5;
    c->allow_jump = allow_jump;
    c->obj.mom.y = fall;
    pad.paddata = pad.paddata_db = 0x40;
    GameTimer.frame++;
    nw_air_before(c, &pad);
    retail_double(c);
    nw_air_move(c, &pad);
    return c->jump;
}

static void air_jump(void)
{
    NwProfile retail = { 0 }, on = ONLY(air_jump), p;
    u8 powers = Game.powerbits;
    crash_at(50, &on);
    Game.powerbits = 2;
    CHECK(air_jump_at(-0.05f, 3, 0) == 4 && air_jump_at(-0.3f, 3, 0) == 4);
    CHECK(air_jump_at(-0.3f, 0, 0) == 4 && air_jump_at(-0.05f, 6, 0) == 4);
    CHECK(air_jump_at(-0.05f, 0, 5) == 0 && air_jump_at(-0.3f, 2, 0) == 2);
    Character[0].slam = 1;
    CHECK(air_jump_at(-0.3f, 3, 0) == 3 && near(Character[0].obj.mom.y, -0.3f + GRAVITY, 1e-6f));
    Character[0].slam = 0;
    Game.powerbits = 0;
    CHECK(air_jump_at(-0.3f, 3, 0) == 3 && near(Character[0].obj.mom.y, -0.3f + GRAVITY, 1e-6f));
    p = on;
    p.movement.crash.air_jump_free = true;
    crash_at(50, &p);
    CHECK(air_jump_at(-0.3f, 3, 0) == 4 && Game.powerbits == 0);
    Game.powerbits = 2;
    p = on;
    p.movement.crash.air_jump_ledge = false;
    p.movement.crash.air_jump_fall = 8;
    crash_at(50, &p);
    CHECK(air_jump_at(-0.05f, 0, 0) == 0);
    CHECK(air_jump_at(-0.15f, 3, 0) == 4 && air_jump_at(-0.2f, 3, 0) == 3);
    p = on;
    p.movement.crash.jump_fall = true;
    crash_at(50, &p);
    CHECK(air_jump_at(-0.3f, 0, 0) == 4 && near(Character[0].obj.mom.y, 7.0f * 0.02f, 0.02f));
    crash_at(50, &retail);
    CHECK(air_jump_at(-0.05f, 3, 0) == 4 && air_jump_at(-0.3f, 3, 0) == 3 && air_jump_at(-0.05f, 0, 0) == 0);
    Game.powerbits = powers;
}

/* One MoveLoopXZ with MovePlayer's target and air friction; the globals come back. */
static void loop(float side, float forward, float target)
{
    struct creature_s *c = &Character[0];
    float friction = NW_HZ(0.0072f, 0.005f);
    c->obj.mom.x = side;
    c->obj.mom.z = forward;
    c->obj.pad_speed = target > 0.0f ? 0.5f : 0.0f;
    in_speed = target;
    in_f_friction = in_s_friction = friction;
    GameTimer.frame++;
    MoveLoopXZ(&c->obj, &c->obj.thdg);
    CHECK(in_speed == target && in_f_friction == friction && in_s_friction == friction);
}

static float loop_forward(float forward, float target)
{
    loop(0.0f, forward, target);
    return Character[0].obj.mom.z;
}

static float loop_side(float side)
{
    loop(side, 0.0f, 0.0f);
    return Character[0].obj.mom.x;
}

static void jump_distance(void)
{
    NwProfile retail = { 0 }, on = ONLY(jump_distance);
    float walk = 0.03f, run = 0.072f, step = 0.0072f;
    crash_at(50, &on);
    Character[0].jump = 3;
    CHECK(near(loop_forward(0.0f, walk), 2.0f * step, 1e-5f));
    CHECK(near(loop_forward(0.06f, walk), run, 1e-5f));
    CHECK(near(loop_forward(0.05f, 0.0f), 0.05f - step, 1e-5f));
    Character[0].slam = 1;
    CHECK(near(loop_forward(0.0f, walk), step, 1e-5f));
    Character[0].slam = 0;
    Character[0].obj.ground = 1;
    CHECK(near(loop_forward(0.0f, walk), step, 1e-5f));
    on.movement.crash.air_accel = 400;
    crash_at(50, &on);
    Character[0].jump = 3;
    CHECK(near(loop_forward(0.0f, walk), 4.0f * step, 1e-5f));
    crash_at(50, &retail);
    Character[0].jump = 3;
    CHECK(near(loop_forward(0.0f, walk), step, 1e-5f) && near(loop_forward(0.06f, walk), 0.06f - step, 1e-5f));
}

static void air_momentum(void)
{
    NwProfile retail = { 0 }, on = ONLY(air_momentum);
    float step = 0.0072f;
    crash_at(50, &on);
    Character[0].jump = 3;
    CHECK(near(loop_forward(0.05f, 0.0f), 0.05f - step * 0.25f, 1e-5f));
    CHECK(near(loop_forward(0.0f, 0.072f), step, 1e-5f));
    Character[0].jump = 0;
    CHECK(near(loop_forward(0.05f, 0.0f), 0.05f - step * 0.25f, 1e-5f));
    Character[0].slam = 2;
    CHECK(near(loop_forward(0.05f, 0.0f), 0.05f - step, 1e-5f));
    Character[0].slam = 0;
    Character[0].obj.ground = 1;
    CHECK(near(loop_forward(0.05f, 0.0f), 0.05f - step, 1e-5f));
    on.movement.crash.air_drift = 100;
    crash_at(50, &on);
    CHECK(loop_forward(0.05f, 0.0f) == 0.05f);
    crash_at(50, &retail);
    CHECK(near(loop_forward(0.05f, 0.0f), 0.05f - step, 1e-5f));
}

/* MoveCRASH's hook, then the next frame's MoveLoopXZ. */
static float dash_frame(float forward)
{
    nw_air_move(&Character[0], &pad);
    return loop_forward(forward, 0.072f);
}

static void dash_jump(void)
{
    NwProfile retail = { 0 }, on = ONLY(dash_jump);
    float step = 0.0072f;
    struct creature_s *c = &Character[0];
    for (int sprint = 0; sprint <= 2; sprint++) {
        if (sprint == 2) {
            on.movement.crash.dash_kept = 50;
        }
        crash_at(50, &on);
        c->obj.ground = 1;
        nw_air_move(c, &pad);
        GameTimer.frame++;
        c->obj.ground = 0;
        c->jump = 1;
        c->sprint = sprint != 0;
        float after = sprint == 0 ? 0.08f - step : sprint == 1 ? 0.08f + step : 0.084f;
        CHECK(near(dash_frame(0.08f), after, 1e-5f));
        c->jump = 3;
        c->jump_frame = 5;
        c->sprint = 0;
        CHECK(near(dash_frame(0.08f), after, 1e-5f));
        c->obj.ground = 1;
        CHECK(near(dash_frame(0.08f), 0.08f - step, 1e-5f));
    }
    crash_at(50, &retail);
    nw_air_move(c, &pad);
    GameTimer.frame++;
    c->jump = 1;
    c->sprint = 1;
    CHECK(near(dash_frame(0.08f), 0.08f - step, 1e-5f));
}

static void run_speed(void)
{
    NwProfile retail = { 0 }, on = ONLY(run_speed);
    crash_at(60, &on);
    CHECK(CrashMoveInfo.RUNSPEED == 0.060000002f * 1.2f && CrashMoveInfo.SPRINTSPEED == 0.080000006f * 1.2f);
    crash_at(50, &on);
    CHECK(CrashMoveInfo.RUNSPEED == 0.072f * 1.2f && CrashMoveInfo.SPRINTSPEED == 0.096f * 1.2f);
    float run = CrashMoveInfo.RUNSPEED, step = 0.0072f;
    Character[0].obj.ground = 1;
    CHECK(near(loop_forward(0.0f, run), step * 1.5f, 1e-5f));
    CHECK(near(loop_forward(0.05f, 0.0f), 0.05f - step, 1e-5f));
    Character[0].obj.surface_type = 1; /* ice */
    CHECK(near(loop_forward(0.0f, run), step, 1e-5f));
    Character[0].obj.surface_type = 0;
    Character[0].obj.ground = 0;
    CHECK(near(loop_forward(0.0f, run), step, 1e-5f));

    static struct CharacterModel crash;
    static struct animlist run_anim = { .speed = 1.3f }, walk_anim = { .speed = 1.2f };
    crash.animlist[0x3A] = &run_anim;
    crash.animlist[0x74] = &walk_anim;
    Character[0].obj.model = &crash;
    nw_ground_animate(&Character[0]);
    CHECK(run_anim.speed == 1.3f * 1.2f && walk_anim.speed == 1.2f);
    on.movement.crash.run_top = 150;
    on.movement.crash.run_accel = 300;
    crash_at(50, &on);
    CHECK(CrashMoveInfo.RUNSPEED == 0.072f * 1.5f);
    Character[0].obj.ground = 1;
    CHECK(near(loop_forward(0.0f, CrashMoveInfo.RUNSPEED), step * 3.0f, 1e-5f));
    Character[0].obj.model = &crash;
    nw_ground_animate(&Character[0]);
    CHECK(run_anim.speed == 1.3f * 1.5f);
    crash_at(50, &retail);
    CHECK(CrashMoveInfo.RUNSPEED == 0.072f && CrashMoveInfo.SPRINTSPEED == 0.096f);
    Character[0].obj.model = &crash;
    nw_ground_animate(&Character[0]);
    CHECK(run_anim.speed == 1.3f);
}

static void grip(void)
{
    NwProfile retail = { 0 }, on = ONLY(grip);
    float step = 0.0072f;
    crash_at(50, &on);
    Character[0].obj.ground = 1;
    CHECK(near(loop_side(0.05f), 0.05f - 3.0f * step, 1e-5f));
    CHECK(near(loop_forward(0.05f, 0.0f), 0.05f - 2.0f * step, 1e-5f));
    CHECK(near(loop_forward(-0.05f, 0.072f), -0.05f + 2.0f * step, 1e-5f));
    CHECK(near(loop_forward(0.0f, 0.072f), step, 1e-5f));
    Character[0].obj.surface_type = 1;
    CHECK(near(loop_side(0.05f), 0.05f - step, 1e-5f));
    Character[0].obj.surface_type = 0;
    Character[0].obj.ground = 0;
    CHECK(near(loop_side(0.05f), 0.05f - step, 1e-5f));
    on.movement.crash.grip_turn = 500;
    on.movement.crash.grip_brake = 400;
    crash_at(50, &on);
    Character[0].obj.ground = 1;
    CHECK(near(loop_side(0.05f), 0.05f - 5.0f * step, 1e-5f));
    CHECK(near(loop_forward(0.05f, 0.0f), 0.05f - 4.0f * step, 1e-5f));
    crash_at(50, &retail);
    Character[0].obj.ground = 1;
    CHECK(near(loop_side(0.05f), 0.05f - step, 1e-5f) && near(loop_forward(0.05f, 0.0f), 0.05f - step, 1e-5f));
}

static void long_spin(void)
{
    NwProfile retail = { 0 }, on = ONLY(long_spin);
    crash_at(50, &on);
    CHECK(CrashMoveInfo.SPINFRAMES == 20);
    crash_at(60, &on);
    CHECK(CrashMoveInfo.SPINFRAMES == 24);
    on.movement.crash.spin_ms = 600;
    crash_at(60, &on);
    CHECK(CrashMoveInfo.SPINFRAMES == 36);
    crash_at(60, &retail);
    CHECK(CrashMoveInfo.SPINFRAMES == 16);
    CrashMoveInfo.SPINFRAMES = 30;
    nw_profile_apply(&on);
    CHECK(CrashMoveInfo.SPINFRAMES == 30);
    CrashMoveInfo.SPINFRAMES = 16;
}

/* MoveCRASH's slide timer, crouch held or let go; his speed after the frame. */
static float slide_frame(bool crouch)
{
    struct creature_s *c = &Character[0];
    pad.paddata = crouch ? 0x20 : 0;
    nw_ground_before(c, &pad);
    if (c->slide && --c->slide == 0) {
        c->crawl = crouch;
        c->obj.mom.x = c->obj.mom.z = 0.0f;
    }
    nw_ground_after(c);
    return c->obj.mom.z;
}

static void slide(void)
{
    NwProfile retail = { 0 }, on = ONLY(slide);
    struct creature_s *c = &Character[0];
    crash_at(60, &on);
    CHECK(CrashMoveInfo.SLIDEFRAMES == 24 && CrashMoveInfo.SLIDESPEED == 0.080000006f * 1.15f);
    crash_at(50, &on);
    CHECK(CrashMoveInfo.SLIDEFRAMES == 20 && CrashMoveInfo.SLIDESPEED == 0.096f * 1.15f);
    c->slide = 20;
    c->obj.mom.z = 0.11f;
    CHECK(slide_frame(true) == 0.11f && slide_frame(false) == 0.11f && c->slide == 18); /* too soon to stand */
    c->slide = 12;
    CHECK(slide_frame(false) == 0.11f && c->slide == 0 && !c->crawl); /* stood up early, still running */
    c->slide = 1;
    CHECK(slide_frame(true) == 0.0f && c->crawl); /* held to the end: crawls */
    on.movement.crash.slide_keep = false;
    on.movement.crash.slide_cancel = false;
    crash_at(50, &on);
    c->slide = 12;
    c->obj.mom.z = 0.11f;
    CHECK(slide_frame(false) == 0.11f && c->slide == 11);
    c->slide = 1;
    CHECK(slide_frame(false) == 0.0f);
    crash_at(50, &retail);
    CHECK(CrashMoveInfo.SLIDEFRAMES == 25 && CrashMoveInfo.SLIDESPEED == 0.096f);
    c->slide = 1;
    c->obj.mom.z = 0.11f;
    CHECK(slide_frame(false) == 0.0f);
}

static NwTerI contact;

/* A contact at normal y (world), under his centre when below; true when it became a floor. */
static bool held(short type, float y, bool below, const unsigned char *flags)
{
    memset(&contact, 0, sizeof(contact));
    contact.hittype = type;
    contact.yscale = 2.0f;
    contact.uhitnorm = (struct nuvec_s){ sqrtf(1.0f - y * y), below ? y : -y, 0.0f };
    contact.hitnorm = contact.uhitnorm;
    nw_edges_impact(flags);
    return contact.uhitnorm.y == 1.0f && contact.hitnorm.y == 1.0f && contact.uhitnorm.x == 0.0f;
}

static void edge_grip(void)
{
    NwProfile retail = { 0 }, on = ONLY(edge_grip);
    NwTerI *was = TerI;
    unsigned char *own = Character[0].obj.gndflags.chrs, other[2] = { 0 };
    TerI = &contact;
    crash_at(50, &on);
    CHECK(!held(2, 0.5f, true, own)); /* no drop under him: a step or a ridge */
    Character[0].obj.pos.y = 1.0f;
    CHECK(held(2, 0.5f, true, own) && held(3, 0.5f, true, own) && !held(0x13, 0.5f, true, own));
    CHECK(!held(1, 0.5f, true, own) && !held(2, 0.3f, true, own) && !held(2, 0.5f, false, own));
    CHECK(!held(2, 0.5f, true, other));
    CHECK(!held(2, 0.8f, true, own) && contact.uhitnorm.y == 0.8f);

    /* Standing still on a held rim: MovePlayer's release of the ground is undone; after a face it is not. */
    for (int rim = 1; rim >= 0; rim--) {
        held(rim ? 2 : 1, 0.5f, true, own);
        own[0] = own[1] = 1;
        GameTimer.frame++;
        nw_edges_scanned(own);
        Character[0].obj.ground = 1;
        own[0] = own[1] = 0;
        GameTimer.frame++;
        nw_edges_scan(own);
        CHECK(own[1] == rim);
    }
    on.movement.crash.edge_reach = 90;
    crash_at(50, &on);
    Character[0].obj.shadow = 2000000.0f; /* no floor at all */
    CHECK(held(2, 0.3f, true, own));
    on.movement.crash.edge_reach = 45;
    crash_at(50, &on);
    Character[0].obj.pos.y = 1.0f;
    CHECK(!held(2, 0.6f, true, own));
    crash_at(50, &retail);
    Character[0].obj.pos.y = 1.0f;
    CHECK(!held(2, 0.5f, true, own));
    TerI = was;
}

/* A side-on rail along z: left spline at x 0, right at x 2. */
static struct nuvec_s rail_left[2] = { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 10.0f } };
static struct nuvec_s rail_right[2] = { { 2.0f, 0.0f, 0.0f }, { 2.0f, 0.0f, 10.0f } };
static struct nugspline_s spline_left = { 2, sizeof(struct nuvec_s), "left", (char *)rail_left };
static struct nugspline_s spline_right = { 2, sizeof(struct nuvec_s), "right", (char *)rail_right };

/* His momentum across the rail after the hold, at x in camera mode. */
static float line_frame(float x, u8 mode, float mom_x)
{
    struct creature_s *c = &Character[0];
    c->obj.pos = (struct nuvec_s){ x, 0.0f, 5.0f };
    c->obj.mom = (struct nuvec_s){ mom_x, 0.0f, 0.05f };
    c->obj.RPos = (RailPosition){ .iRAIL = 0, .iALONG = 0, .fALONG = 0.5f, .fACROSS = x / 2.0f, .mode = mode };
    GameTimer.frame++;
    nw_line2d_hold(c);
    CHECK(near(c->obj.mom.z, 0.05f, 1e-5f));
    return c->obj.mom.x;
}

static void line_2d(void)
{
    NwProfile retail = { 0 }, on = ONLY(line_2d);
    Rail[0] = (struct rail_s){ .pLEFT = &spline_left, .pRIGHT = &spline_right, .edges = 2 };
    crash_at(50, &on);
    CHECK(fabsf(line_frame(1.0f, 4, 0.03f)) < 1e-6f);
    CHECK(near(line_frame(1.04f, 8, 0.03f), -0.04f * 0.25f, 1e-4f));
    CHECK(near(line_frame(1.6f, 4, 0.0f), -0.02f, 1e-5f));
    CHECK(line_frame(1.6f, 1, 0.03f) == 0.03f);
    CHECK(fabsf(line_frame(1.6f, 4, 0.03f)) < 1e-6f);
    /* The stick straight into depth walks there, and the line moves with him. */
    Character[0].obj.pad_speed = 0.5f;
    Character[0].obj.thdg = 0x4000;
    CHECK(line_frame(1.7f, 4, 0.03f) == 0.03f);
    Character[0].obj.thdg = 0x1000;
    CHECK(fabsf(line_frame(1.75f, 4, 0.03f)) < 1e-6f);
    Character[0].obj.pad_speed = 0.0f;
    CHECK(fabsf(line_frame(1.75f, 4, 0.03f)) < 1e-6f);
    on.movement.crash.line_pull = 0;
    crash_at(50, &on);
    CHECK(fabsf(line_frame(1.0f, 4, 0.0f)) < 1e-6f && fabsf(line_frame(1.04f, 4, 0.03f)) < 1e-6f);
    crash_at(50, &retail);
    CHECK(line_frame(1.6f, 4, 0.03f) == 0.03f);
    Rail[0] = (struct rail_s){ 0 };
}

static void hazard_hitboxes(void)
{
    NwProfile retail = { 0 }, on = ONLY(hazard_hitboxes);
    static struct obj_s enemy, harmless;
    struct obj_s *crash = &Character[0].obj;
    enemy = (struct obj_s){ .attack = 1, .vulnerable = 2, .RADIUS = 0.5f, .min = { -1.0f, 0.0f, -1.0f },
                            .max = { 1.0f, 1.0f, 1.0f } };
    harmless = (struct obj_s){ .RADIUS = 0.5f };
    pObj[0] = &enemy;
    pObj[1] = &harmless;
    GAMEOBJECTCOUNT = 2;
    crash_at(50, &on);
    nw_hazards_shrink(crash);
    CHECK(enemy.RADIUS == 0.5f * 0.8f && enemy.max.x == 0.8f && enemy.min.z == -0.8f && enemy.max.y == 1.0f);
    CHECK(harmless.RADIUS == 0.5f);
    nw_hazards_restore();
    CHECK(enemy.RADIUS == 0.5f && enemy.max.x == 1.0f && enemy.min.z == -1.0f);
    crash->attack = 2; /* spinning */
    nw_hazards_shrink(crash);
    CHECK(enemy.RADIUS == 0.5f);
    nw_hazards_restore();
    crash->attack = 0;
    nw_hazards_shrink(&harmless);
    CHECK(enemy.RADIUS == 0.5f);
    nw_hazards_restore();
    CHECK(nw_hazards_particle(0.225f) == 0.225f * 0.5f);
    enemy.min.x = 0.0f; /* off its origin: shrinks round its own middle */
    enemy.max.x = 2.0f;
    nw_hazards_shrink(crash);
    CHECK(near(enemy.min.x, 0.2f, 1e-5f) && near(enemy.max.x, 1.8f, 1e-5f));
    nw_hazards_restore();
    CHECK(enemy.min.x == 0.0f && enemy.max.x == 2.0f);
    on.movement.crash.hazard_enemy = 50;
    on.movement.crash.hazard_fire = 25;
    crash_at(50, &on);
    nw_hazards_shrink(crash);
    CHECK(enemy.RADIUS == 0.25f && nw_hazards_particle(0.2f) == 0.05f);
    nw_hazards_restore();
    crash_at(50, &retail);
    nw_hazards_shrink(crash);
    CHECK(enemy.RADIUS == 0.5f && nw_hazards_particle(0.225f) == 0.225f);
    nw_hazards_restore();
    pObj[0] = pObj[1] = NULL;
    GAMEOBJECTCOUNT = 0;
}

/* Retail's submarine data at hz with Crash in the sub, then p. */
static void sub_at(int hz, const NwProfile *p)
{
    nw_hz_set(hz);
    nw_profile_apply(&(NwProfile){ 0 });
    SubmarineMoveInfo.WALKSPEED = NW_HZ(0.024999999f, 0.020833334f);
    SubmarineMoveInfo.RUNSPEED = NW_HZ(0.06f, 0.050000004f);
    nw_profile_apply(p);
    memset(&Character[0], 0, sizeof(Character[0]));
    VEHICLECONTROL = 1;
    vtog_blend = 0;
    Character[0].obj.vehicle = 0x20;
}

/* Retail with one of the submarine's switches on, its values at N. Hanced's. */
static NwProfile sub_only(size_t offset)
{
    NwProfile p = { .movement = NW_MOVEMENT_RETAIL };
    *(bool *)((char *)&p.movement.submarine + offset) = true;
    return p;
}

#define SUB_ONLY(f) sub_only(offsetof(NwSubMovement, f))

/* The stick at full tilt: dx across, angle as MovePlayer gives it (0 up, 0x4000 right). */
static void sub_stick(float dx, u16 angle)
{
    Character[0].obj.pad_speed = SubmarineMoveInfo.RUNSPEED;
    Character[0].obj.pad_dx = dx;
    Character[0].obj.pad_angle = angle;
}

/* A frame of MovePlayer's heading, with a turn of retail's the hook throws away. */
static void sub_frame(void)
{
    GameTimer.frame++;
    nw_sub_move(&Character[0]);
    Character[0].obj.hdg += 0x1234;
    nw_sub_turn(&Character[0]);
}

static int sub_turn_to(u16 hdg)
{
    int frames = 0;
    while (Character[0].obj.hdg != hdg && frames < 100) {
        sub_frame();
        frames++;
    }
    return frames;
}

static void sub_turning(void)
{
    NwProfile on = SUB_ONLY(turning), retail = { .movement = NW_MOVEMENT_RETAIL };
    struct obj_s *sub = &Character[0].obj;

    /* A diagonal turns it right round, through the screen; 0x369 a frame at 200 %. */
    sub_at(50, &on);
    sub_stick(-0.7f, 0xA000);
    int pal = sub_turn_to(0x8000);
    CHECK(pal == 19 && sub->thdg == 0x8000);
    sub_at(60, &on);
    sub_stick(-0.7f, 0xA000);
    int ntsc = sub_turn_to(0x8000);
    CHECK(fabsf(pal / 50.0f - ntsc / 60.0f) <= 1.0f / 50.0f);
    /* Back mid-turn, and a turn finishes with the stick let go. */
    sub_at(50, &on);
    sub_stick(-0.7f, 0xA000);
    for (int i = 0; i < 5; i++) {
        sub_frame();
    }
    CHECK(sub->hdg == 5 * 0x6D2);
    sub_stick(0.7f, 0x6000);
    sub_frame();
    CHECK(sub->hdg == 4 * 0x6D2 && sub->thdg == 0);
    sub_stick(-0.7f, 0xA000);
    sub_frame();
    sub->pad_speed = 0.0f;
    CHECK(sub_turn_to(0x8000) == 14);
    /* Up and down keep it facing, also the screen. */
    sub_stick(0.0f, 0);
    sub_frame();
    CHECK(sub->hdg == 0x8000);
    GameTimer.frame += 10;
    sub->hdg = 0x4000;
    sub_frame();
    CHECK(sub->hdg == 0x4000);
    /* Turn speed. */
    on.movement.submarine.turn_speed = 400;
    sub_at(50, &on);
    sub_stick(-0.7f, 0xA000);
    sub_frame();
    CHECK(sub->hdg == 4 * 0x369);
    /* Getting in, on foot, the switch off: retail's turn. */
    vtog_blend = 1;
    sub_frame();
    CHECK(sub->hdg == 4 * 0x369 + 0x1234);
    vtog_blend = 0;
    VEHICLECONTROL = 0;
    sub_frame();
    CHECK(sub->hdg == 4 * 0x369 + 2 * 0x1234);
    sub_at(50, &retail);
    sub_stick(-0.7f, 0xA000);
    sub_frame();
    CHECK(sub->hdg == 0x1234);
}

/* Frames from level until the nose is 3/4 of the way up. */
static int sub_climb(void)
{
    struct obj_s *sub = &Character[0].obj;
    int frames = 0;
    sub->xrot = 0;
    sub_stick(0.0f, 0);
    while ((s16)sub->xrot < 0x3000 && frames < 1000) {
        nw_sub_pitch_before(sub);
        sub->xrot += 0x10;
        nw_sub_pitch(sub, 2);
        frames++;
    }
    return frames;
}

static void sub_pitch(void)
{
    NwProfile on = SUB_ONLY(turning), retail = { .movement = NW_MOVEMENT_RETAIL };
    struct obj_s *sub = &Character[0].obj;

    /* At 100 % retail's PAL seek, a 32nd of the way; 200 % as fast in seconds at both rates. */
    on.movement.submarine.pitch_speed = 100;
    sub_at(50, &on);
    sub_stick(0.0f, 0);
    nw_sub_pitch_before(sub);
    nw_sub_pitch(sub, 2);
    CHECK(sub->xrot == 0x200);
    on.movement.submarine.pitch_speed = 200;
    sub_at(50, &on);
    int pal = sub_climb();
    sub_at(60, &on);
    int ntsc = sub_climb();
    CHECK(pal >= 21 && pal <= 23 && fabsf(pal / 50.0f - ntsc / 60.0f) <= 1.0f / 50.0f);
    /* Let go, it levels out. */
    sub->pad_speed = 0.0f;
    nw_sub_pitch_before(sub);
    nw_sub_pitch(sub, 2);
    CHECK((s16)sub->xrot < 0x3000);
    /* Not the sub's branch, the switch off: retail's. */
    sub_at(50, &on);
    sub_stick(0.0f, 0);
    nw_sub_pitch_before(sub);
    sub->xrot = 0x10;
    nw_sub_pitch(sub, 1);
    CHECK(sub->xrot == 0x10);
    sub->flags = 0x10000;
    nw_sub_pitch(sub, 2);
    CHECK(sub->xrot == 0x10);
    sub_at(50, &retail);
    sub_stick(0.0f, 0);
    nw_sub_pitch_before(sub);
    sub->xrot = 0x10;
    nw_sub_pitch(sub, 2);
    CHECK(sub->xrot == 0x10);
}

/* Frames to the stick's speed from rest (stop: to rest from it), MovePlayer's step then the hook's. */
static int sub_frames(bool stop)
{
    struct obj_s *sub = &Character[0].obj;
    float step = NW_HZ(0.00144f, 0.001f);
    int frames = 0;
    sub_stick(1.0f, 0x4000);
    float top = sub->pad_speed;
    sub->xrot = 0;
    sub->mom = (struct nuvec_s){ 0.0f, 0.0f, stop ? top : 0.0f };
    if (stop) {
        sub->pad_speed = 0.0f;
    }
    while ((stop ? sub->mom.z > 0.0f : sub->mom.z < top) && frames < 1000) {
        sub->mom.z = stop ? fmaxf(sub->mom.z - step, 0.0f) : fminf(sub->mom.z + step, top);
        nw_sub_accel(&sub->mom);
        frames++;
    }
    return frames;
}

static void sub_speed(void)
{
    NwProfile on = SUB_ONLY(speed), retail = { .movement = NW_MOVEMENT_RETAIL };
    struct obj_s *sub = &Character[0].obj;

    /* Retail: 0.06 a frame, 0.00144 a frame more each frame. */
    sub_at(50, &retail);
    CHECK(SubmarineMoveInfo.RUNSPEED == 0.06f && sub_frames(false) == 42 && sub_frames(true) == 42);
    /* 125 % top speed, to it and to rest twice as fast, as fast in seconds at 60 Hz. */
    sub_at(50, &on);
    CHECK(near(SubmarineMoveInfo.RUNSPEED, 0.075f, 1e-6f) && near(SubmarineMoveInfo.WALKSPEED, 0.03125f, 1e-6f));
    int pal = sub_frames(false);
    CHECK(pal == 27 && sub_frames(true) == 27);
    sub_at(60, &on);
    CHECK(near(SubmarineMoveInfo.RUNSPEED, 0.0625f, 1e-6f));
    CHECK(fabsf(pal / 50.0f - sub_frames(false) / 60.0f) <= 1.0f / 50.0f);
    /* Climbing: the same on y. */
    sub_at(50, &on);
    sub_stick(0.0f, 0);
    sub->xrot = 0x4000;
    struct nuvec_s want;
    NuVecRotateX(&want, &v001, -sub->xrot);
    nw_sub_accel(&sub->mom);
    CHECK(near(fabsf(sub->mom.y), 0.00144f, 1e-4f) && (sub->mom.y > 0.0f) == (want.y > 0.0f) && sub->mom.z == 0.0f);
    /* Only the sub's own speed, not dead, between frames as the menu changes it. */
    struct nuvec_s other = { 0.0f, 0.0f, 0.0f };
    nw_sub_accel(&other);
    CHECK(other.y == 0.0f && other.z == 0.0f);
    sub_stick(1.0f, 0x4000);
    sub->xrot = 0;
    sub->mom = (struct nuvec_s){ 0.0f, 0.0f, 0.0f };
    sub->dead = 1;
    nw_sub_accel(&sub->mom);
    CHECK(sub->mom.z == 0.0f);
    on.movement.submarine.speed_top = 200;
    nw_profile_request(&on);
    nw_gameplay_swap();
    CHECK(near(SubmarineMoveInfo.RUNSPEED, 0.12f, 1e-6f));
    /* Retail puts its values back, but leaves one something else wrote. */
    nw_profile_request(&retail);
    nw_gameplay_swap();
    CHECK(SubmarineMoveInfo.RUNSPEED == 0.06f && SubmarineMoveInfo.WALKSPEED == 0.024999999f);
    SubmarineMoveInfo.RUNSPEED = 0.5f;
    nw_profile_apply(&on);
    CHECK(SubmarineMoveInfo.RUNSPEED == 0.5f && SubmarineMoveInfo.WALKSPEED != 0.024999999f);
    sub_at(50, &retail);
}

static void sub_hitbox(void)
{
    NwProfile on = SUB_ONLY(hitbox), retail = { .movement = NW_MOVEMENT_RETAIL };
    struct obj_s *sub = &Character[0].obj;

    /* 75 % round its middle, once however deep the calls nest, back exactly. */
    struct CharacterData *d = &CData[0x20];
    float min = d->min.y, max = d->max.y;
    sub_at(50, &on);
    sub->RADIUS = 0.7f;
    sub->SCALE = 1.0f;
    sub->pos.y = 1.6f;
    sub->bot = -0.6f;
    sub->top = 0.5f;
    sub->objbot = 1.0f;
    sub->objtop = 2.1f;
    nw_sub_shrink();
    CHECK(near(sub->RADIUS, 0.525f, 1e-6f) && near(sub->objbot, 1.1375f, 1e-6f) && near(sub->objtop, 1.9625f, 1e-6f));
    CHECK(near(sub->bot, -0.4625f, 1e-6f) && near(sub->top, 0.3625f, 1e-6f));
    CHECK(near(d->min.y, min + (max - min) * 0.125f, 1e-6f) && near(d->max.y, max - (max - min) * 0.125f, 1e-6f));
    nw_sub_shrink();
    nw_sub_restore();
    CHECK(near(sub->RADIUS, 0.525f, 1e-6f));
    nw_sub_restore();
    CHECK(sub->RADIUS == 0.7f && near(sub->objbot, 1.0f, 1e-6f) && near(sub->objtop, 2.1f, 1e-6f));
    CHECK(sub->bot == -0.6f && sub->top == 0.5f && d->min.y == min && d->max.y == max);
    CHECK(near(nw_sub_particle(0.2f), 0.15f, 1e-6f));
    /* Its height taken afresh in between (GetTopBot, NewTopBot after a move) comes back at the new place. */
    nw_sub_shrink();
    sub->pos.y = 2.0f;
    sub->bot = d->min.y;
    sub->top = d->max.y;
    sub->objbot = sub->pos.y + sub->bot;
    sub->objtop = sub->pos.y + sub->top;
    CHECK(near(sub->objtop - sub->objbot, (max - min) * 0.75f, 1e-5f));
    nw_sub_restore();
    CHECK(near(sub->objbot, 2.0f + min, 1e-5f) && near(sub->objtop, 2.0f + max, 1e-5f) && d->min.y == min);
    /* On foot and the switch off: retail's. */
    VEHICLECONTROL = 0;
    nw_sub_shrink();
    CHECK(sub->RADIUS == 0.7f && nw_sub_particle(0.2f) == 0.2f);
    nw_sub_restore();
    sub_at(50, &retail);
    sub->RADIUS = 0.7f;
    nw_sub_shrink();
    CHECK(sub->RADIUS == 0.7f && nw_sub_particle(0.2f) == 0.2f);
    nw_sub_restore();
}

static void sub_weapons(void)
{
    NwProfile on = SUB_ONLY(weapons), retail = { .movement = NW_MOVEMENT_RETAIL };
    struct creature_s *plr = &Character[0];
    struct nuvec_s torpedo = { 0.0f, 0.0f, 0.16f }, bomb, v;

    /* Reloads by the rate, the same in seconds at 60 Hz; one already running stays. */
    on.movement.submarine.torpedo_rate = 200;
    on.movement.submarine.bomb_rate = 400;
    sub_at(50, &on);
    nw_sub_fire_before(plr);
    plr->fire = 0x19;
    plr->tap = 0x32;
    nw_sub_fire_after(plr);
    CHECK(plr->fire == 13 && plr->tap == 13);
    plr->fire = 5;
    nw_sub_fire_before(plr);
    nw_sub_fire_after(plr);
    CHECK(plr->fire == 5);
    sub_at(60, &on);
    nw_sub_fire_before(plr);
    plr->fire = 0x1E;
    nw_sub_fire_after(plr);
    CHECK(plr->fire == 15);
    /* Torpedo and bomb speed, only while the sub fires its own. */
    on.movement.submarine.torpedo_speed = 200;
    on.movement.submarine.bomb_speed = 200;
    sub_at(50, &on);
    plr->obj.mom.y = 0.01f;
    bomb = (struct nuvec_s){ 0.0f, 0.01f - 0.02f, 0.0f };
    nw_sub_fire_before(plr);
    CHECK(nw_sub_launch(4, &torpedo, &plr->obj, &v) == &v && near(v.z, 0.32f, 1e-6f) && torpedo.z == 0.16f);
    CHECK(nw_sub_launch(10, &bomb, &plr->obj, &v) == &v && near(v.y, -0.03f, 1e-5f));
    CHECK(nw_sub_launch(4, &torpedo, &Character[1].obj, &v) == &torpedo);
    CHECK(nw_sub_launch(2, &torpedo, &plr->obj, &v) == &torpedo);
    nw_sub_fire_after(plr);
    CHECK(nw_sub_launch(4, &torpedo, &plr->obj, &v) == &torpedo);
    /* The bomb sinks toward SPRINTSPEED, which is terminal_fall_speed. */
    CHECK((void *)D_005F28F8 == (void *)&SubmarineMoveInfo.SPRINTSPEED);
    SubmarineMoveInfo.SPRINTSPEED = 0.08f;
    sub_at(50, &(NwProfile){ 0 });
    SubmarineMoveInfo.SPRINTSPEED = 0.08f;
    nw_profile_apply(&on);
    CHECK(near(SubmarineMoveInfo.SPRINTSPEED, 0.16f, 1e-6f));
    nw_profile_apply(&retail);
    CHECK(SubmarineMoveInfo.SPRINTSPEED == 0.08f);
    /* On foot and the switch off: retail's. */
    sub_at(50, &on);
    VEHICLECONTROL = 0;
    nw_sub_fire_before(plr);
    plr->fire = 0x19;
    nw_sub_fire_after(plr);
    CHECK(plr->fire == 0x19 && nw_sub_launch(4, &torpedo, &plr->obj, &v) == &torpedo);
    sub_at(50, &retail);
    nw_sub_fire_before(plr);
    plr->fire = 0x19;
    nw_sub_fire_after(plr);
    CHECK(plr->fire == 0x19);
}

int main(void)
{
    NuTrigInit();
    monkey_bars(50, 0.015599999f);
    monkey_bars(60, 0.013f);
    monkey_bar_anim();
    air_indicator();
    jump_fall();
    jump_distance();
    air_momentum();
    dash_jump();
    run_speed();
    grip();
    long_spin();
    slide();
    edge_grip();
    line_2d();
    hazard_hitboxes();
    air_jump();
    sub_turning();
    sub_pitch();
    sub_speed();
    sub_hitbox();
    sub_weapons();

    NwMovement m = NW_MOVEMENT_RETAIL;
    CHECK(nw_movement_profile(&(NwMovement){ 0 }) == NW_MOVEMENT_PROFILE_RETAIL);
    CHECK(nw_movement_profile(&m) == NW_MOVEMENT_PROFILE_RETAIL);
    m.crash.run_top = 150;
    CHECK(nw_movement_profile(&m) == NW_MOVEMENT_PROFILE_RETAIL);
    m = (NwMovement)NW_MOVEMENT_NHANCED;
    CHECK(nw_movement_profile(&m) == NW_MOVEMENT_PROFILE_NHANCED);
    m.crash.run_top = 150;
    CHECK(nw_movement_profile(&m) == NW_MOVEMENT_PROFILE_CUSTOM);
    CHECK(nw_movement_profile(&(NwMovement){ .monkey_bar_speed = 200 }) == NW_MOVEMENT_PROFILE_CUSTOM);
    CHECK(nw_movement_profile(&(NwMovement){ .monkey_bar_speed = 350 }) == NW_MOVEMENT_PROFILE_CUSTOM);
    CHECK(nw_movement_profile(&(NwMovement){ .crash = { .jump_fall = true } }) == NW_MOVEMENT_PROFILE_CUSTOM);
    CHECK(nw_movement_profile(&(NwMovement){ .submarine = { .turning = true } }) == NW_MOVEMENT_PROFILE_CUSTOM);
    m = (NwMovement)NW_MOVEMENT_RETAIL;
    m.submarine.speed_top = 200;
    CHECK(nw_movement_profile(&m) == NW_MOVEMENT_PROFILE_RETAIL);
    m = (NwMovement)NW_MOVEMENT_NHANCED;
    m.submarine.hitbox_size = 50;
    CHECK(nw_movement_profile(&m) == NW_MOVEMENT_PROFILE_CUSTOM);

    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    return 0;
}
