/* src/gameplay: the profile's switches and the retail values they change. */
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/creature.h"
#include "game/gametypes.h"
#include "game/vehicle.h"
#include "game/vehicle.h"
#include "gameplay/air.h"
#include "gameplay/airindicator.h"
#include "gameplay/bosshealth.h"
#include "gameplay/edges.h"
#include "gameplay/firefly.h"
#include "gameplay/gameplay.h"
#include "gameplay/ground.h"
#include "gameplay/hazards.h"
#include "gameplay/line2d.h"
#include "gameplay/mech.h"
#include "gameplay/monkeybars.h"
#include "gameplay/plane.h"
#include "gameplay/submarine.h"
#include "nu3d/nugscn.h"
#include "nu_port.h"
#include "numath/numtx.h"
#include "numath/nutrig.h"
#include "numath/nuvec.h"
#include "nups2/ps2ctl.h"
#include "port/hz.h"

extern struct MoveInfo CrashMoveInfo;
extern struct MoveInfo CocoMoveInfo;
extern struct MoveInfo MechMoveInfo;
extern struct cammtx_s GameCam[];
extern struct nuvec_s plr_target_firepos;
extern f32 vtog_time;
extern f32 vtog_duration;
extern s32 fadeval;
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
extern s32 Level;
extern s32 vtog_blend;
extern RailPosition *best_cRPos;
extern f32 SMOKEYSPEED;
extern f32 SMOKEYBOOSTSPEED;
extern f32 OFFROADERSEEK;
extern f32 OFFROADERSPEEDTIME;
extern struct MoveInfo OffRoaderMoveInfo;
void MoveLoopXZ(struct obj_s *obj, u16 *hdg);
u16 SeekRot(u16 a, u16 b, s32 shift);
u16 SeekRot__retail(u16 a, u16 b, s32 shift);

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

static void boss_health(void)
{
    /* Rumble's and Fire's 3, Crunch Time's 4; Air Crunch's 100 has no phases. */
    CHECK(nw_boss_health_lines(1) == 0);
    CHECK(nw_boss_health_lines(3) == 2);
    CHECK(nw_boss_health_lines(4) == 3);
    CHECK(nw_boss_health_lines(100) == 0);
    char text[8];
    nw_boss_health_percent(100, text);
    CHECK(strcmp(text, "100%") == 0);
    nw_boss_health_percent(73, text);
    CHECK(strcmp(text, " 73%") == 0);
    nw_boss_health_percent(5, text);
    CHECK(strcmp(text, "  5%") == 0);
    nw_boss_health_percent(-3, text);
    CHECK(strcmp(text, "  0%") == 0);
    /* Crunch Time: 4 - its attack phase, which is 4 - Cortex's hits. */
    CHECK(nw_boss_health_crunch_time_hp(0) == 4);
    CHECK(nw_boss_health_crunch_time_hp(3) == 1);
    CHECK(nw_boss_health_crunch_time_hp(4) == 0);
    CHECK(nw_boss_health_crunch_time_hp(9) == 0);
    CHECK(nw_boss_health_crunch_time_hp((unsigned)-1) == 4);

    /* Off draws nothing; on without the panel's bar loaded neither. */
    nw_profile_apply(&(NwProfile){ 0 });
    nw_boss_health_bar(0.65f, -0.85f, 1.0f, -0.1f, 0.1f, 2, 3);
    nw_profile_apply(&(NwProfile){ .boss_health = true });
    nw_boss_health_bar(0.65f, -0.85f, 1.0f, -0.1f, 0.1f, 2, 3);
    nw_profile_request(&(NwProfile){ .boss_health = false });
    CHECK(nw_profile.boss_health);
    nw_gameplay_swap();
    CHECK(!nw_profile.boss_health);
}

static bool near(float a, float b, float within)
{
    return fabsf(a - b) <= within * fabsf(b);
}

/* Retail with one switch on, its values at N. Hanced's. */
static NwProfile only(size_t offset)
{
    NwProfile p = { .movement = NW_MOVEMENT_RETAIL };
    *(bool *)((char *)&p.movement + offset) = true;
    return p;
}

#define ONLY(f) only(offsetof(NwMovement, crash.f))
#define COCO_ONLY(f) only(offsetof(NwMovement, coco.f))
#define MECH_ONLY(f) only(offsetof(NwMovement, mech.f))
#define SUB_ONLY(f) only(offsetof(NwMovement, submarine.f))
#define JEEP_ONLY(f) only(offsetof(NwMovement, jeep.f))
#define ATLAS_ONLY(f) only(offsetof(NwMovement, atlasphere.f))
#define FIREFLY_ONLY(f) only(offsetof(NwMovement, firefly.f))
#define PLANE_ONLY(f) only(offsetof(NwMovement, plane.f))
#define SPACE_ONLY(f) only(offsetof(NwMovement, spacefighter.f))

/* Retail's Crash and Coco data and globals at hz, then p. */
static void crash_at(int hz, const NwProfile *p)
{
    nw_hz_set(hz);
    nw_profile_apply(&(NwProfile){ 0 });
    CrashMoveInfo.JUMPFRAMES0 = NW_HZ(5, 6);
    CrashMoveInfo.JUMPFRAMES1 = NW_HZ(10, 12);
    CrashMoveInfo.RUNSPEED = NW_HZ(0.072f, 0.060000002f);
    CrashMoveInfo.SPRINTSPEED = NW_HZ(0.096f, 0.080000006f);
    CrashMoveInfo.SLIDESPEED = NW_HZ(0.096f, 0.080000006f);
    CocoMoveInfo.JUMPFRAMES0 = NW_HZ(5, 6);
    CocoMoveInfo.JUMPFRAMES1 = NW_HZ(10, 12);
    CocoMoveInfo.RUNSPEED = NW_HZ(0.06f, 0.050000004f);
    CocoMoveInfo.SPRINTSPEED = NW_HZ(0.089999996f, 0.075f);
    GRAVITY = NW_HZ(-0.0072000003f, -0.005f);
    VEHICLECONTROL = 0;
    nw_profile_apply(p);
    memset(&Character[0], 0, sizeof(Character[0]));
    Character[0].obj.vehicle = -1;
}

static void coco_at(int hz, const NwProfile *p)
{
    crash_at(hz, p);
    Character[0].obj.character = 1;
}

/* pull: the largest change of vertical speed in a frame after take-off, units a second squared. */
typedef struct Arc {
    float time, apex, reach, early, pull;
} Arc;

static struct nupad_s pad;
static short jump_character; /* who jump() takes off, Crash or Coco */
static signed char jump_type = 1;

static struct MoveInfo *jump_info(void)
{
    return jump_character ? &CocoMoveInfo : &CrashMoveInfo;
}

/* MoveCRASH's and MoveCOCO's jump steps. */
static void retail_jump(struct creature_s *c, bool held)
{
    if (c->jump && c->jump_frame < c->jump_frames) {
        c->obj.mom.y = jump_info()->JUMPHEIGHT / jump_info()->JUMPFRAMES2;
    }
    if (!c->obj.ground) {
        c->obj.mom.y += GRAVITY;
    }
    if (c->jump && c->jump_frame < c->jump_frames) {
        c->jump_frame++;
    }
    if (c->jump == 1 && held && c->jump_frames < jump_info()->JUMPFRAMES2) {
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
    c->obj.character = jump_character;
    float run = jump_info()->RUNSPEED, friction = NW_HZ(0.0072f, 0.005f), dt = 1.0f / hz;
    c->obj.mom.z = run;
    GameTimer.frame++;
    pad.paddata = pad.paddata_db = 0x40;
    c->obj.ground = 1;
    nw_air_before(c, &pad);
    c->jump = 1;
    c->jump_type = jump_type;
    c->jump_frames = jump_info()->JUMPFRAMES0;
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

    /* Each level's LoadCharacterModels sets the length from his animation: 23 frames at 50 Hz, 28 at 60. */
    crash_at(50, &on);
    CrashMoveInfo.SLIDEFRAMES = 23;
    nw_ground_loaded();
    CHECK(CrashMoveInfo.SLIDEFRAMES == 20);
    nw_profile_request(&retail);
    nw_gameplay_swap();
    CHECK(CrashMoveInfo.SLIDEFRAMES == 23);
    nw_hz_set(60);
    CrashMoveInfo.SLIDEFRAMES = 28;
    nw_ground_loaded();
    CHECK(CrashMoveInfo.SLIDEFRAMES == 28);
    nw_profile_apply(&on);
    CHECK(CrashMoveInfo.SLIDEFRAMES == 24);
    CrashMoveInfo.SLIDEFRAMES = 28;
    nw_ground_loaded();
    CHECK(CrashMoveInfo.SLIDEFRAMES == 24);
    nw_profile_apply(&retail);
    CHECK(CrashMoveInfo.SLIDEFRAMES == 28);
    CrashMoveInfo.SLIDEFRAMES = 25;
    nw_ground_loaded();
    CHECK(CrashMoveInfo.SLIDEFRAMES == 25);
}

static void coco_jump(void)
{
    NwProfile retail = { 0 }, on = COCO_ONLY(jump_fall), his = ONLY(jump_fall);
    jump_character = 1;
    for (jump_type = 1; jump_type <= 2; jump_type++) {
        Arc pal = jump(50, &retail, 100), arc = jump(50, &on, 100), ntsc = jump(60, &on, 120);
        CHECK(near(arc.apex, pal.apex, 0.05f) && near(arc.reach, pal.reach, 0.05f));
        CHECK(fabsf(arc.time - pal.time) <= 0.05f && arc.early > pal.early && arc.pull <= 61.0f);
        CHECK(near(ntsc.apex, arc.apex, 0.03f) && fabsf(ntsc.time - arc.time) <= 0.03f);
        Arc crash = jump(50, &his, 100);
        CHECK(crash.apex == pal.apex && crash.reach == pal.reach && crash.time == pal.time);
    }
    jump_type = 1;
    on.movement.coco.jump_height = 130;
    CHECK(near(jump(50, &on, 100).apex, jump(50, &retail, 100).apex * 1.3f, 0.03f));
    on.movement.coco.jump_height = 100;
    Arc coco = jump(50, &on, 100);
    jump_character = 0;
    Arc crash = jump(50, &his, 100), pal = jump(50, &retail, 100), off = jump(50, &on, 100);
    CHECK(coco.apex == crash.apex && coco.time == crash.time);
    CHECK(off.apex == pal.apex && off.reach == pal.reach && off.time == pal.time);
    crash_at(60, &on);
    CHECK(CocoMoveInfo.JUMPFRAMES2 == 18 && CrashMoveInfo.JUMPFRAMES2 == 15);
    crash_at(60, &retail);
    CHECK(CocoMoveInfo.JUMPFRAMES2 == 15);

    /* Her run speed in the air, her dash in a dash jump. */
    float step = 0.0072f;
    on = COCO_ONLY(jump_distance);
    coco_at(50, &on);
    Character[0].jump = 3;
    CHECK(near(loop_forward(0.05f, 0.024f), 0.06f, 1e-5f));
    on = COCO_ONLY(air_momentum);
    coco_at(50, &on);
    Character[0].jump = 3;
    CHECK(near(loop_forward(0.05f, 0.0f), 0.05f - step * 0.25f, 1e-5f));
    his = ONLY(air_momentum);
    coco_at(50, &his);
    Character[0].jump = 3;
    CHECK(near(loop_forward(0.05f, 0.0f), 0.05f - step, 1e-5f));
    on = COCO_ONLY(dash_jump);
    coco_at(50, &on);
    struct creature_s *c = &Character[0];
    c->obj.ground = 1;
    nw_air_move(c, &pad);
    GameTimer.frame++;
    c->obj.ground = 0;
    c->jump = 1;
    c->sprint = 1;
    CHECK(near(dash_frame(0.08f), 0.08f + step, 1e-5f));
    c->jump = 3;
    c->sprint = 0;
    CHECK(near(dash_frame(0.088f), 0.089999996f, 1e-5f));
}

static void coco_run(void)
{
    NwProfile retail = { 0 }, on = COCO_ONLY(run_speed), nhanced = { .movement = NW_MOVEMENT_NHANCED };
    float step = 0.0072f;
    coco_at(60, &on);
    CHECK(CocoMoveInfo.RUNSPEED == 0.060000002f * 1.2f && CocoMoveInfo.SPRINTSPEED == 0.080000006f * 1.2f);
    CHECK(CrashMoveInfo.RUNSPEED == 0.060000002f);
    coco_at(50, &on);
    CHECK(CocoMoveInfo.RUNSPEED == 0.072f * 1.2f && CocoMoveInfo.SPRINTSPEED == 0.096f * 1.2f);
    Character[0].obj.ground = 1;
    CHECK(near(loop_forward(0.0f, CocoMoveInfo.RUNSPEED), step * 1.5f, 1e-5f));

    static struct CharacterModel coco;
    static struct animlist run_anim = { .speed = 1.2f }, dash_anim = { .speed = 1.2f };
    coco.animlist[0x3A] = &run_anim;
    coco.animlist[0x48] = &dash_anim;
    Character[0].obj.model = &coco;
    nw_ground_animate(&Character[0]);
    CHECK(near(run_anim.speed, 1.2f * 1.44f, 1e-5f) && near(dash_anim.speed, 1.2f * 1.28f, 1e-5f));
    nw_profile_apply(&retail);
    CHECK(CocoMoveInfo.RUNSPEED == 0.06f && CocoMoveInfo.SPRINTSPEED == 0.089999996f);
    nw_ground_animate(&Character[0]);
    CHECK(run_anim.speed == 1.2f && dash_anim.speed == 1.2f);
    CocoMoveInfo.RUNSPEED = 0.5f;
    nw_profile_apply(&on);
    CHECK(CocoMoveInfo.RUNSPEED == 0.5f && CocoMoveInfo.SPRINTSPEED == 0.096f * 1.2f);
    CocoMoveInfo.RUNSPEED = 0.06f;
    coco_at(50, &nhanced);
    CHECK(CocoMoveInfo.RUNSPEED == CrashMoveInfo.RUNSPEED && CocoMoveInfo.SPRINTSPEED == CrashMoveInfo.SPRINTSPEED);

    on = COCO_ONLY(grip);
    coco_at(50, &on);
    Character[0].obj.ground = 1;
    CHECK(near(loop_side(0.05f), 0.05f - 3.0f * step, 1e-5f));
    CHECK(near(loop_forward(0.05f, 0.0f), 0.05f - 2.0f * step, 1e-5f));
    /* Crash's switches leave her retail. */
    NwProfile his = ONLY(run_speed);
    his.movement.crash.grip = true;
    coco_at(50, &his);
    Character[0].obj.ground = 1;
    CHECK(CocoMoveInfo.RUNSPEED == 0.06f);
    CHECK(near(loop_forward(0.0f, 0.06f), step, 1e-5f) && near(loop_side(0.05f), 0.05f - step, 1e-5f));
    crash_at(50, &retail);
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

/* crash_at, then Crash in the mech. */
static void mech_at(int hz, const NwProfile *p)
{
    crash_at(hz, p);
    VEHICLECONTROL = 1;
    vtog_time = vtog_duration = 1.0f;
    MechMoveInfo.WALKSPEED = 0.018f;
    GameTimer.frame = 1000;
    fadeval = 0;
    GameCam[0].yrot = 0;
    Character[0].obj.vehicle = 0x44;
    Character[0].obj.RADIUS = 0.35f;
    Character[0].obj.attack = 0x400;
    pad = (struct nupad_s){ 0 };
}

/* The take-off's frame sees a walk, so MoveMECH takes the running jump; the frame after the real stick again. */
static void mech_stand_jump(void)
{
    NwProfile retail = { 0 }, on = MECH_ONLY(stand_jump);
    struct creature_s *c = &Character[0];
    mech_at(50, &on);
    c->obj.ground = 1;
    pad.paddata = 0x40;
    nw_mech_before(c, &pad);
    CHECK(c->obj.pad_speed == MechMoveInfo.WALKSPEED);
    nw_mech_after(c, &pad);
    CHECK(c->obj.pad_speed == 0.0f);
    c->obj.pad_speed = MechMoveInfo.RUNSPEED; /* running already */
    nw_mech_before(c, &pad);
    CHECK(c->obj.pad_speed == MechMoveInfo.RUNSPEED);
    nw_mech_after(c, &pad);
    c->obj.pad_speed = 0.0f;
    c->target = 1; /* aiming: no jump */
    nw_mech_before(c, &pad);
    CHECK(c->obj.pad_speed == 0.0f);
    c->target = 0;
    GameTimer.frame = 10; /* no stick at the level's start */
    nw_mech_before(c, &pad);
    CHECK(c->obj.pad_speed == 0.0f);
    mech_at(50, &retail);
    c->obj.ground = 1;
    pad.paddata = 0x40;
    nw_mech_before(c, &pad);
    CHECK(c->obj.pad_speed == 0.0f);
    /* Not in the mech, nothing. */
    mech_at(50, &on);
    VEHICLECONTROL = 0;
    c->obj.ground = 1;
    pad.paddata = 0x40;
    nw_mech_before(c, &pad);
    CHECK(c->obj.pad_speed == 0.0f);
}

static void mech_shot(void)
{
    NwProfile retail = { 0 }, on = MECH_ONLY(shot_stop);
    struct creature_s *c = &Character[0];
    /* A tap: released before tap ran out still fires. */
    mech_at(50, &on);
    c->target = 2;
    c->tap = 7;
    nw_mech_before(c, &pad);
    CHECK(c->tap == 0);
    mech_at(50, &retail);
    c->target = 2;
    c->tap = 7;
    nw_mech_before(c, &pad);
    CHECK(c->tap == 7);
    on.movement.mech.shot_tap = false;
    mech_at(50, &on);
    c->target = 2;
    c->tap = 7;
    nw_mech_before(c, &pad);
    CHECK(c->tap == 7);

    /* The stop after it ends shot_stop_ms on, once he steers. */
    for (int hz = 50; hz <= 60; hz += 10) {
        mech_at(hz, &(NwProfile){ .movement = NW_MOVEMENT_NHANCED });
        int frames = 150 * hz / 1000;
        c->target = 2;
        nw_mech_before(c, &pad);
        c->fire = 30;
        c->tap = 35;
        nw_mech_after(c, &pad);
        c->target = 0;
        for (int frame = 1; frame <= frames; frame++) {
            GameTimer.frame++;
            c->obj.pad_speed = frame == frames ? MechMoveInfo.RUNSPEED : 0.0f;
            nw_mech_before(c, &pad);
            nw_mech_after(c, &pad);
            CHECK(frame == frames ? c->fire == 0 && c->tap == 0 : c->fire == 30);
        }
    }
    mech_at(50, &retail);
    c->target = 2;
    nw_mech_before(c, &pad);
    c->fire = 30;
    c->tap = 35;
    nw_mech_after(c, &pad);
    c->target = 0;
    GameTimer.frame += 100;
    c->obj.pad_speed = MechMoveInfo.RUNSPEED;
    nw_mech_after(c, &pad);
    CHECK(c->fire == 30 && c->tap == 35);
}

static void mech_aim_walk(void)
{
    NwProfile retail = { 0 }, on = MECH_ONLY(aim_walk);
    static struct nupad_s copy;
    struct creature_s *c = &Character[0];
    float friction = 0.0072f;
    mech_at(50, &on);
    pad.rdata.l_alg_x = 255;
    pad.rdata.l_alg_y = 127;
    pad.rdata.r_alg_x = 0;
    pad.rdata.r_alg_y = 200;
    CHECK(nw_mech_pad(c, &pad, &copy) == &pad); /* not aiming */
    c->target = 1;
    CHECK(nw_mech_pad(c, &pad, &copy) == &copy && copy.rdata.l_alg_x == 0 && copy.rdata.l_alg_y == 200);
    CHECK(pad.rdata.l_alg_x == 255 && pad.rdata.r_alg_x == 0);
    /* Retail's aim stops him; the left stick walks him to the right, still facing ahead. */
    in_speed = 0.0f;
    in_f_friction = in_s_friction = friction;
    MoveLoopXZ(&c->obj, &c->obj.thdg);
    CHECK(near(c->obj.mom.x, friction, 1e-4f) && fabsf(c->obj.mom.z) < 1e-4f && c->obj.hdg == 0);
    CHECK(in_speed == 0.0f && in_f_friction == friction);
    for (int i = 0; i < 20; i++) {
        MoveLoopXZ(&c->obj, &c->obj.thdg);
    }
    CHECK(near(c->obj.mom.x, MechMoveInfo.WALKSPEED * 1.5f, 1e-4f));
    /* The camera turned a quarter: the same stick goes the camera's right. */
    mech_at(50, &on);
    c->target = 1;
    GameCam[0].yrot = 0x4000;
    pad.rdata.l_alg_x = 255;
    pad.rdata.l_alg_y = 127;
    nw_mech_pad(c, &pad, &copy);
    in_speed = 0.0f;
    in_f_friction = in_s_friction = friction;
    MoveLoopXZ(&c->obj, &c->obj.thdg);
    CHECK(fabsf(c->obj.mom.x) < 1e-4f && near(fabsf(c->obj.mom.z), friction, 1e-4f));
    /* In the dead zone he stands. */
    mech_at(50, &on);
    c->target = 1;
    pad.rdata.l_alg_x = 150;
    pad.rdata.l_alg_y = 127;
    nw_mech_pad(c, &pad, &copy);
    in_speed = 0.0f;
    MoveLoopXZ(&c->obj, &c->obj.thdg);
    CHECK(c->obj.mom.x == 0.0f && c->obj.mom.z == 0.0f);
    mech_at(50, &retail);
    c->target = 1;
    CHECK(nw_mech_pad(c, &pad, &copy) == &pad);
}

/* plr_target_dir as MovePlayer builds it. */
static struct nuvec_s aim_dir(const struct creature_s *c)
{
    static struct nuvec_s ahead = { 0.0f, 0.0f, 1.0f };
    struct nuvec_s d;
    NuVecRotateX(&d, &ahead, -(u16)c->obj.target_xrot & 0xFFFF);
    NuVecRotateY(&d, &d, c->obj.hdg + (u16)c->obj.target_yrot);
    return d;
}

static void mech_aim(void)
{
    NwProfile retail = { 0 }, fast = MECH_ONLY(aim_fast), assist = MECH_ONLY(aim_assist);
    struct creature_s *c = &Character[0];
    mech_at(50, &fast);
    c->target = 1;
    c->obj.pad_speed = MechMoveInfo.RUNSPEED;
    c->obj.pad_dx = 1.0f;
    nw_mech_aim(c);
    CHECK(abs(c->obj.target_yrot - (int)(10923 * 1.5f / 32.0f)) <= 1 && c->obj.target_xrot == 0);
    c->fire = 1; /* the shot's animation holds it */
    c->obj.target_yrot = 0;
    nw_mech_aim(c);
    CHECK(c->obj.target_yrot == 0);
    mech_at(50, &retail);
    c->target = 1;
    c->obj.pad_speed = MechMoveInfo.RUNSPEED;
    c->obj.pad_dx = 1.0f;
    nw_mech_aim(c);
    CHECK(c->obj.target_yrot == 0);

    /* Assist: it settles on the creature, up and to the side, as plr_target_dir points. */
    static struct obj_s enemy;
    enemy = (struct obj_s){ .pos = { 1.0f, 2.5f, 10.0f }, .vulnerable = 0x100, .SCALE = 1.0f };
    pObj[0] = &enemy;
    GAMEOBJECTCOUNT = 1;
    plr_target_firepos = (struct nuvec_s){ 0.0f, 1.0f, 0.0f };
    struct nuvec_s to = { 1.0f, 1.5f, 10.0f };
    float len = sqrtf(to.x * to.x + to.y * to.y + to.z * to.z);
    for (int h = 0; h < 2; h++) {
        mech_at(50, &assist);
        nw_mech_aim(c); /* not aiming: the next frame has no muzzle yet */
        c->target = 1;
        c->obj.hdg = h ? 0x1000 : 0;
        c->obj.target_yrot = h ? -0x1000 : 0;
        nw_mech_aim(c);
        CHECK(c->obj.target_xrot == 0 && c->obj.target_yrot == (h ? -0x1000 : 0));
        for (int i = 0; i < 300; i++) {
            nw_mech_aim(c);
        }
        struct nuvec_s d = aim_dir(c);
        CHECK((d.x * to.x + d.y * to.y + d.z * to.z) / len > 0.999f);
        CHECK(c->obj.target_xrot > 0);
    }
    /* Outside the cone, out of reach, not shootable, or dead: no pull. */
    enemy.pos = (struct nuvec_s){ 10.0f, 1.0f, 0.0f };
    mech_at(50, &assist);
    c->target = 1;
    nw_mech_aim(c);
    nw_mech_aim(c);
    CHECK(c->obj.target_yrot == 0);
    enemy.pos = (struct nuvec_s){ 0.0f, 1.0f, 25.0f };
    nw_mech_aim(c);
    CHECK(c->obj.target_yrot == 0 && c->obj.target_xrot == 0);
    enemy.pos = (struct nuvec_s){ 2.0f, 1.0f, 10.0f };
    enemy.vulnerable = 0x20;
    nw_mech_aim(c);
    CHECK(c->obj.target_yrot == 0);
    enemy.vulnerable = 0x100;
    enemy.dead = 1;
    nw_mech_aim(c);
    CHECK(c->obj.target_yrot == 0);
    enemy.dead = 0;
    nw_mech_aim(c);
    CHECK(c->obj.target_yrot > 0);
    /* It stays inside retail's reach. */
    enemy.pos = (struct nuvec_s){ 9.397f, 1.0f, 3.42f }; /* 70 degrees */
    c->obj.target_yrot = 0x2800;
    for (int i = 0; i < 300; i++) {
        nw_mech_aim(c);
    }
    CHECK(c->obj.target_yrot <= 0x2AAB && c->obj.target_yrot > 0x2A00);
    pObj[0] = NULL;
    GAMEOBJECTCOUNT = 0;
}

static void mech_hitbox(void)
{
    NwProfile retail = { 0 }, on = MECH_ONLY(hitbox), crash = ONLY(hazard_hitboxes);
    static struct obj_s enemy;
    struct obj_s *mech = &Character[0].obj;
    enemy = (struct obj_s){ .attack = 1, .vulnerable = 2, .RADIUS = 0.5f, .SCALE = 1.0f,
                            .min = { -1.0f, 0.0f, -1.0f }, .max = { 1.0f, 1.0f, 1.0f } };
    pObj[0] = &enemy;
    GAMEOBJECTCOUNT = 1;
    float cut = 0.35f * 0.3f;
    mech_at(50, &on);
    nw_hazards_shrink(mech);
    CHECK(near(enemy.RADIUS, 0.5f - cut, 1e-5f) && near(enemy.max.x, 1.0f - cut, 1e-5f));
    CHECK(near(enemy.min.z, -1.0f + cut, 1e-5f) && enemy.max.y == 1.0f && mech->RADIUS == 0.35f);
    nw_hazards_restore();
    CHECK(enemy.RADIUS == 0.5f && enemy.max.x == 1.0f && enemy.min.z == -1.0f);
    enemy.vulnerable = 0x400; /* the mech's own attack breaks it */
    nw_hazards_shrink(mech);
    CHECK(enemy.RADIUS == 0.5f);
    nw_hazards_restore();
    enemy.vulnerable = 2;
    CHECK(near(nw_hazards_chase(mech), 0.35f, 1e-6f) && near(mech->RADIUS, 0.35f * 0.7f, 1e-5f));
    mech->RADIUS = 0.35f;
    CHECK(near(nw_hazards_radius(mech), 0.35f * 0.7f, 1e-5f));
    /* Crash's switch is not the mech's, and the other way round. */
    mech_at(50, &crash);
    nw_hazards_shrink(mech);
    CHECK(enemy.RADIUS == 0.5f && nw_hazards_chase(mech) == 0.35f && mech->RADIUS == 0.35f);
    nw_hazards_restore();
    crash_at(50, &on);
    nw_hazards_shrink(&Character[0].obj);
    CHECK(enemy.RADIUS == 0.5f);
    nw_hazards_restore();
    mech_at(50, &retail);
    nw_hazards_shrink(mech);
    CHECK(enemy.RADIUS == 0.5f && nw_hazards_chase(mech) == 0.35f && nw_hazards_radius(mech) == 0.35f);
    nw_hazards_restore();
    pObj[0] = NULL;
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

static RailPosition jeep_rail;

/* Retail's off-roader data at hz, then p; Crash driving it on a rail at 0x4000. */
static void jeep_at(int hz, const NwProfile *p, int level)
{
    nw_hz_set(hz);
    nw_profile_apply(&(NwProfile){ 0 });
    SMOKEYSPEED = 8.0f;
    SMOKEYBOOSTSPEED = 12.0f;
    OFFROADERSEEK = 0.02f;
    OFFROADERSPEEDTIME = 2.0f;
    OffRoaderMoveInfo.RUNSPEED = NW_HZ(0.12f, 0.10000001f);
    OffRoaderMoveInfo.SPRINTSPEED = NW_HZ(0.17999999f, 0.15f);
    nw_profile_apply(p);
    memset(&Character[0], 0, sizeof(Character[0]));
    Character[0].obj.vehicle = 0x99;
    VEHICLECONTROL = 1;
    Level = level;
    vtog_blend = 0;
    jeep_rail = (RailPosition){ .angle = 0x4000, .mode = 1 };
    best_cRPos = &jeep_rail;
}

/* MovePlayer's turn toward the rail plus off. */
static u16 jeep_turn(u16 hdg, s32 off, s32 shift)
{
    Character[0].obj.hdg = hdg;
    Character[0].obj.thdg = (u16)(jeep_rail.angle + off);
    return SeekRot(hdg, Character[0].obj.thdg, shift);
}

/* The turn and its target as retail's. */
static bool jeep_turn_retail(u16 hdg, s32 off, s32 shift)
{
    u16 target = (u16)(jeep_rail.angle + off);
    return jeep_turn(hdg, off, shift) == SeekRot__retail(hdg, target, shift) && Character[0].obj.thdg == target;
}

static void jeep_steer(void)
{
    NwProfile retail = { 0 }, on = JEEP_ONLY(steering);
    static const s32 offs[] = { 0x2AAB, -0x2AAB, 0x1000, -0x1000, 0x123, -1, 0 };
    static const u16 hdgs[] = { 0x4000, 0x3000, 0x5555, 0xFFF0, 0x0010 };
    jeep_at(50, &retail, 3);
    CHECK(jeep_turn(0x4000, 0x2000, 5) == 0x4100 && Character[0].obj.thdg == 0x6000);
    for (size_t h = 0; h < sizeof(hdgs) / sizeof(hdgs[0]); h++) {
        for (size_t o = 0; o < sizeof(offs) / sizeof(offs[0]); o++) {
            jeep_at(50, &retail, 3);
            CHECK(jeep_turn_retail(hdgs[h], offs[o], 5));
            jeep_at(60, &retail, 8);
            CHECK(jeep_turn_retail(hdgs[h], offs[o], 3));
        }
    }
    /* 100% at 50 Hz is retail */
    on.movement.jeep.steer_response = on.movement.jeep.steer_angle = 100;
    for (size_t h = 0; h < sizeof(hdgs) / sizeof(hdgs[0]); h++) {
        for (size_t o = 0; o < sizeof(offs) / sizeof(offs[0]); o++) {
            jeep_at(50, &on, 3);
            CHECK(jeep_turn_retail(hdgs[h], offs[o], 5));
            jeep_at(50, &on, 8);
            CHECK(jeep_turn_retail(hdgs[h], offs[o], 3));
        }
    }
    /* 200% */
    on = JEEP_ONLY(steering);
    jeep_at(50, &on, 8);
    CHECK(abs((s16)(jeep_turn(0x4000, 0x1000, 3) - (0x4000 + 960))) <= 1); /* 0x1000 * (1 - (7/8)^2) */
    jeep_at(50, &on, 3);
    CHECK(abs((s16)(jeep_turn(0x4000, 0x2000, 5) - (0x4000 + 504))) <= 1); /* 0x2000 * (1 - (31/32)^2) */
    CHECK(abs((s16)(jeep_turn(0x4000, -0x2000, 5) - (0x4000 - 504))) <= 1);
    /* 60 Hz */
    on.movement.jeep.steer_response = 100;
    jeep_at(60, &on, 8);
    u16 hdg = 0x4000 + 0x1000;
    for (int i = 0; i < 60; i++) {
        hdg = jeep_turn(hdg, 0, 3);
    }
    float left = (float)(hdg - 0x4000) / 0x1000;
    CHECK(fabsf(left - powf(7.0f / 8.0f, 50.0f)) < 0.01f);
    /* angle */
    on.movement.jeep.steer_angle = 50;
    jeep_at(50, &on, 8);
    jeep_turn(0x4000, 0x1000, 3);
    CHECK(Character[0].obj.thdg == 0x4800);
    jeep_turn(0x4000, -0x1000, 3);
    CHECK(Character[0].obj.thdg == 0x3800);
    on.movement.jeep.steer_angle = 150;
    jeep_at(50, &on, 3);
    jeep_turn(0x4000, 0x2AAB, 5);
    CHECK(Character[0].obj.thdg == 0x4000 + 0x3FFF);
    jeep_turn(0x4000, 0, 5);
    CHECK(Character[0].obj.thdg == 0x4000);
    /* other SeekRots */
    jeep_at(50, &on, 8);
    CHECK(jeep_turn_retail(0x4000, 0x1000, 2) && jeep_turn_retail(0x4000, 0x1000, 5));
    Character[0].obj.hdg = 0x4000;
    Character[0].obj.thdg = 0x5000;
    CHECK(SeekRot(0x3000, 0x5000, 3) == SeekRot__retail(0x3000, 0x5000, 3) && Character[0].obj.thdg == 0x5000);
    CHECK(SeekRot(0x4000, 0x4800, 3) == SeekRot__retail(0x4000, 0x4800, 3) && Character[0].obj.thdg == 0x5000);
    vtog_blend = 1;
    CHECK(jeep_turn_retail(0x4000, 0x1000, 3));
    vtog_blend = 0;
    best_cRPos = NULL;
    CHECK(jeep_turn_retail(0x4000, 0x1000, 3));
    best_cRPos = &jeep_rail;
    VEHICLECONTROL = 0;
    CHECK(jeep_turn_retail(0x4000, 0x1000, 3));
    VEHICLECONTROL = 1;
    Character[0].obj.vehicle = -1;
    CHECK(jeep_turn_retail(0x4000, 0x1000, 3));
    best_cRPos = NULL;
    Level = 35;
}

static void jeep_speed(void)
{
    NwProfile retail = { 0 }, on = JEEP_ONLY(speed);
    jeep_at(50, &on, 3);
    CHECK(SMOKEYSPEED == 8.0f * 1.5f && SMOKEYBOOSTSPEED == 12.0f * 1.5f);
    CHECK(OFFROADERSPEEDTIME == 2.0f * (1.1f / 1.5f));
    CHECK(near(SMOKEYSPEED * OFFROADERSPEEDTIME, 8.0f * 2.0f * 1.1f, 1e-6f));
    CHECK(near(SMOKEYBOOSTSPEED * OFFROADERSPEEDTIME, 12.0f * 2.0f * 1.1f, 1e-6f));
    CHECK(OffRoaderMoveInfo.RUNSPEED == 0.12f * 1.1f && OffRoaderMoveInfo.SPRINTSPEED == 0.17999999f * 1.1f);
    jeep_at(60, &on, 8);
    CHECK(OffRoaderMoveInfo.RUNSPEED == 0.10000001f * 1.1f && OffRoaderMoveInfo.SPRINTSPEED == 0.15f * 1.1f);
    CHECK(SMOKEYSPEED == 8.0f * 1.5f && OFFROADERSPEEDTIME == 2.0f * (1.1f / 1.5f));
    on.movement.jeep.speed_top = 150;
    on.movement.jeep.speed_accel = 100;
    nw_profile_apply(&on);
    CHECK(SMOKEYSPEED == 8.0f && OFFROADERSPEEDTIME == 2.0f * 1.5f && OffRoaderMoveInfo.RUNSPEED == 0.10000001f * 1.5f);
    jeep_at(50, &retail, 3);
    CHECK(SMOKEYSPEED == 8.0f && SMOKEYBOOSTSPEED == 12.0f && OFFROADERSPEEDTIME == 2.0f);
    CHECK(OffRoaderMoveInfo.RUNSPEED == 0.12f && OffRoaderMoveInfo.SPRINTSPEED == 0.17999999f);
    /* A value someone else wrote stays. */
    SMOKEYSPEED = 5.0f;
    OffRoaderMoveInfo.RUNSPEED = 0.2f;
    nw_profile_apply(&on);
    CHECK(SMOKEYSPEED == 5.0f && OffRoaderMoveInfo.RUNSPEED == 0.2f && OFFROADERSPEEDTIME == 2.0f * 1.5f);
    nw_profile_apply(&retail);
    CHECK(SMOKEYSPEED == 5.0f && OffRoaderMoveInfo.RUNSPEED == 0.2f && OFFROADERSPEEDTIME == 2.0f);
    jeep_at(50, &retail, 35);
}

static void jeep_grip(void)
{
    NwProfile retail = { 0 }, on = JEEP_ONLY(grip);
    jeep_at(50, &on, 3);
    CHECK(OFFROADERSEEK == 0.02f * 3.0f && SMOKEYSPEED == 8.0f && OffRoaderMoveInfo.RUNSPEED == 0.12f);
    on.movement.jeep.grip_speed = 1000;
    nw_profile_apply(&on);
    CHECK(OFFROADERSEEK == 0.02f * 10.0f);
    nw_profile_apply(&retail);
    CHECK(OFFROADERSEEK == 0.02f);
    OFFROADERSEEK = 0.05f;
    nw_profile_apply(&on);
    CHECK(OFFROADERSEEK == 0.05f);
    jeep_at(50, &retail, 35);
}

extern struct NEWBUGGY PlayerAtlas, EarthBoss;
extern s32 Level;
void ProcessMovementAtlas(struct NEWBUGGY *Atlas, f32 DeltaTime);

static s32 roll_ground = 2; /* the ball's onground as the step sees it, after ProcessAtlas's shift */

/* b after one step under p, rolling at vx along +x, the stick at ang degrees (90 is +x) by mag. */
static struct NEWBUGGY roll(struct NEWBUGGY *b, const NwProfile *p, float vx, float ang, float mag, float dt)
{
    nw_profile_apply(p);
    memset(b, 0, sizeof(*b));
    b->type = b == &PlayerAtlas ? 1 : 2;
    b->ball_radius = 0.65f;
    b->onground = roll_ground;
    b->lastnormal = (struct nuvec_s){ 0.0f, 1.0f, 0.0f };
    b->ball_vel = (struct nuvec_s){ vx, 0.0f, 0.0f };
    b->inputang = ang;
    b->inputmag = mag;
    ProcessMovementAtlas(b, dt);
    return *b;
}

/* Whether that step under p comes out as retail's. */
static bool as_retail(struct NEWBUGGY *b, const NwProfile *p, float vx, float ang, float mag, float dt)
{
    struct NEWBUGGY r = roll(b, &(NwProfile){ 0 }, vx, ang, mag, dt);
    struct NEWBUGGY t = roll(b, p, vx, ang, mag, dt);
    return memcmp(&r, &t, sizeof(r)) == 0;
}

/* The tests have no terrain: NewShadowMask finds no floor anywhere, a pit wherever the guard looks. */
static void atlasphere(void)
{
    NwProfile retail = { 0 }, nhanced = { .movement = NW_MOVEMENT_NHANCED };
    NwProfile steer = ATLAS_ONLY(steer), brake = ATLAS_ONLY(brake), guard = ATLAS_ONLY(edge_guard);
    for (int hz = 50; hz <= 60; hz += 10) {
        float dt = hz == 50 ? 0.02f : 0.016666668f;
        struct NEWBUGGY r, b;

        /* Steering across a roll along +x: about twice the turn, at retail's speed. */
        r = roll(&PlayerAtlas, &retail, 5.0f, 0.0f, 1.0f, dt);
        CHECK(r.ball_vel.z > 0.1f);
        b = roll(&PlayerAtlas, &steer, 5.0f, 0.0f, 1.0f, dt);
        CHECK(near(hypotf(b.ball_vel.x, b.ball_vel.z), hypotf(r.ball_vel.x, r.ball_vel.z), 1e-5f));
        CHECK(b.ball_vel.z > 1.95f * r.ball_vel.z && b.ball_vel.z < 2.0f * r.ball_vel.z);
        CHECK(near(b.ball_target.z, r.ball_target.z + 0.5f * dt * (b.ball_vel.z - r.ball_vel.z), 1e-4f));
        CHECK(b.ball_vel.y == r.ball_vel.y && b.ball_target.y == r.ball_target.y);
        steer.movement.atlasphere.steer_turn = 400;
        CHECK(roll(&PlayerAtlas, &steer, 5.0f, 0.0f, 1.0f, dt).ball_vel.z > 3.8f * r.ball_vel.z);
        steer.movement.atlasphere.steer_turn = 200;
        /* With the roll it stays, let go too. */
        r = roll(&PlayerAtlas, &retail, 5.0f, 90.0f, 1.0f, dt);
        CHECK(near(roll(&PlayerAtlas, &steer, 5.0f, 90.0f, 1.0f, dt).ball_vel.x, r.ball_vel.x, 1e-6f));
        CHECK(as_retail(&PlayerAtlas, &steer, 5.0f, 0.0f, 0.0f, dt));

        /* Pulling back against the roll: that push twice, the rest retail's. */
        r = roll(&PlayerAtlas, &retail, 5.0f, 270.0f, 1.0f, dt);
        CHECK(r.ball_force.x < -7.0f && r.ball_vel.x > 0.0f);
        b = roll(&PlayerAtlas, &brake, 5.0f, 270.0f, 1.0f, dt);
        CHECK(near(b.ball_vel.x, r.ball_vel.x + r.ball_force.x * dt, 1e-5f));
        CHECK(near(b.ball_target.x, r.ball_target.x + 0.5f * dt * dt * r.ball_force.x, 1e-6f));
        CHECK(b.ball_vel.y == r.ball_vel.y && b.ball_target.y == r.ball_target.y);
        brake.movement.atlasphere.brake_force = 400;
        b = roll(&PlayerAtlas, &brake, 5.0f, 270.0f, 1.0f, dt);
        CHECK(near(b.ball_vel.x, r.ball_vel.x + 3.0f * r.ball_force.x * dt, 1e-5f));
        /* It stops the roll, never turns it back. */
        b = roll(&PlayerAtlas, &brake, 0.2f, 270.0f, 1.0f, dt);
        CHECK(fabsf(b.ball_vel.x) < 1e-6f);
        brake.movement.atlasphere.brake_force = 200;
        /* Steering across the roll, pushing with it and letting go stay retail's. */
        CHECK(as_retail(&PlayerAtlas, &brake, 5.0f, 0.0f, 1.0f, dt));
        CHECK(as_retail(&PlayerAtlas, &brake, 5.0f, 90.0f, 1.0f, dt));
        CHECK(as_retail(&PlayerAtlas, &brake, 5.0f, 0.0f, 0.0f, dt));
        /* Let-go braking: that share of the stick's push, which is more in the earth boss's arena. */
        brake.movement.atlasphere.coast_brake = 50;
        r = roll(&PlayerAtlas, &retail, 5.0f, 0.0f, 0.0f, dt);
        b = roll(&PlayerAtlas, &brake, 5.0f, 0.0f, 0.0f, dt);
        CHECK(near(b.ball_vel.x, r.ball_vel.x - 0.5f * 7.9863551f * dt, 1e-5f) && b.ball_vel.z == r.ball_vel.z);
        s32 level = Level;
        Level = 0x15;
        r = roll(&PlayerAtlas, &retail, 5.0f, 0.0f, 0.0f, dt);
        b = roll(&PlayerAtlas, &brake, 5.0f, 0.0f, 0.0f, dt);
        CHECK(near(b.ball_vel.x, r.ball_vel.x - 0.5f * 9.998477f * dt, 1e-5f));
        Level = level;
        brake.movement.atlasphere.coast_brake = 0;

        /* Edge guard, let go: it slows by its speed over the look-ahead... */
        r = roll(&PlayerAtlas, &retail, 3.0f, 0.0f, 0.0f, dt);
        b = roll(&PlayerAtlas, &guard, 3.0f, 0.0f, 0.0f, dt);
        CHECK(near(b.ball_vel.x, r.ball_vel.x * (1.0f - dt / 0.3f), 1e-5f));
        CHECK(near(b.ball_target.x, r.ball_target.x - 0.5f * dt * dt * r.ball_vel.x / 0.3f, 1e-6f));
        /* ...by its braking at most... */
        guard.movement.atlasphere.guard_brake = 100;
        b = roll(&PlayerAtlas, &guard, 3.0f, 0.0f, 0.0f, dt);
        CHECK(near(b.ball_vel.x, r.ball_vel.x - 7.9863551f * dt, 1e-5f));
        guard.movement.atlasphere.guard_brake = 300;
        /* ...and at the edge it backs off. */
        b = roll(&PlayerAtlas, &guard, 0.2f, 0.0f, 0.0f, dt);
        CHECK(near(b.ball_vel.x, -0.05f, 1e-4f));
        /* Pushed at the pit it rolls in, unless Push to roll in is off; a light push does not count. */
        CHECK(as_retail(&PlayerAtlas, &guard, 3.0f, 90.0f, 1.0f, dt));
        r = roll(&PlayerAtlas, &retail, 3.0f, 90.0f, 1.0f, dt);
        guard.movement.atlasphere.guard_push = false;
        CHECK(roll(&PlayerAtlas, &guard, 3.0f, 90.0f, 1.0f, dt).ball_vel.x < r.ball_vel.x);
        guard.movement.atlasphere.guard_push = true;
        r = roll(&PlayerAtlas, &retail, 3.0f, 90.0f, 0.4f, dt);
        CHECK(roll(&PlayerAtlas, &guard, 3.0f, 90.0f, 0.4f, dt).ball_vel.x < r.ball_vel.x);
        /* Faster than Up to speed it rolls on. */
        CHECK(as_retail(&PlayerAtlas, &guard, 8.0f, 0.0f, 0.0f, dt));

        /* Every switch on: the earth boss's ball, and the player's in the air, stay retail's. */
        CHECK(as_retail(&EarthBoss, &nhanced, 3.0f, 270.0f, 1.0f, dt));
        CHECK(!as_retail(&PlayerAtlas, &nhanced, 3.0f, 270.0f, 1.0f, dt));
        roll_ground = 0;
        CHECK(as_retail(&PlayerAtlas, &nhanced, 3.0f, 270.0f, 1.0f, dt));
        CHECK(as_retail(&PlayerAtlas, &nhanced, 3.0f, 0.0f, 0.0f, dt));
        roll_ground = 4; /* touched the step before last */
        CHECK(!as_retail(&PlayerAtlas, &nhanced, 3.0f, 270.0f, 1.0f, dt));
        roll_ground = 2;
    }
    nw_profile_apply(&retail);
    memset(&PlayerAtlas, 0, sizeof(PlayerAtlas));
    memset(&EarthBoss, 0, sizeof(EarthBoss));
}

extern struct GLIDERSTRUCT PlayerGlider;
extern struct battleship_s BattleShipList[6];
extern struct gunboat_s GunBoatList[4];
extern struct zoffa_s EnemyZoffa[4];
extern struct gliderbomb_s GliderBombs[10];
extern struct creature_s *player;
extern f32 Level_TargetTime;
extern f32 TorpedoTime;
extern f32 FlyingLevelCompleteTimer;
extern f32 Level_GliderFloor;
extern f32 Level_GliderCeiling;
extern f32 Level_GliderCurrentCeiling;
extern f32 Level_GliderRadius;
extern f32 Level_GliderCentreX;
extern f32 Level_GliderCentreZ;
extern s32 FlyingLevelExtro;
extern s32 FireFlyIntroOn;
extern s32 WBIntroOn;
void ControlGlider(struct GLIDERSTRUCT *glider, struct nupad_s *pad);
void ControlGlider__retail(struct GLIDERSTRUCT *glider, struct nupad_s *pad);
void GliderFire(struct GLIDERSTRUCT *glider, struct nupad_s *pad);

/* The firefly in level, its arena's values, a battleship 30 ahead of the camera at hz, then p. */
static void firefly_at(int hz, const NwProfile *p, s32 level)
{
    nw_hz_set(hz);
    nw_profile_apply(p);
    Level = level;
    memset(&Character[0], 0, sizeof(Character[0]));
    Character[0].obj.vehicle = 0x8B;
    player = &Character[0];
    memset(&PlayerGlider, 0, sizeof(PlayerGlider));
    PlayerGlider.Cre = &Character[0];
    PlayerGlider.Position = (struct nuvec_s){ 40.0f, 15.0f, 50.0f };
    PlayerGlider.AngleY = 30.0f;
    Level_GliderFloor = 3.0f;
    Level_GliderCeiling = Level_GliderCurrentCeiling = 25.0f;
    Level_GliderRadius = 90.0f;
    Level_GliderCentreX = 40.32f;
    Level_GliderCentreZ = 52.09f;
    Level_TargetTime = level == 0x12 ? 1.3f : 0.7f;
    FlyingLevelCompleteTimer = 1.0f;
    FlyingLevelExtro = FireFlyIntroOn = WBIntroOn = 0;
    memset(BattleShipList, 0, sizeof(BattleShipList));
    memset(GunBoatList, 0, sizeof(GunBoatList));
    memset(EnemyZoffa, 0, sizeof(EnemyZoffa));
    memset(GliderBombs, 0, sizeof(GliderBombs));
    BattleShipList[0].active = 1;
    BattleShipList[0].hitpoints = 3;
    BattleShipList[0].pos = (struct nuvec_s){ 0.0f, 0.0f, 30.0f };
    memset(&GameCam[0], 0, sizeof(GameCam[0]));
    NuMtxSetIdentity(&GameCam[0].m);
    pad = (struct nupad_s){ 0 };
    pad.rdata.l_alg_x = pad.rdata.l_alg_y = 127;
}

/* One ControlGlider from bank tiltz and pitch tiltx, the stick at (lx, ly); retail's if retail. */
static struct GLIDERSTRUCT firefly_steer_once(int hz, const NwProfile *p, s32 level, u8 lx, u8 ly, float tiltz,
                                              float tiltx, bool retail)
{
    firefly_at(hz, p, level);
    PlayerGlider.TiltZ = tiltz;
    PlayerGlider.TiltX = tiltx;
    pad.rdata.l_alg_x = lx;
    pad.rdata.l_alg_y = ly;
    if (retail) {
        ControlGlider__retail(&PlayerGlider, &pad);
    } else {
        ControlGlider(&PlayerGlider, &pad);
    }
    return PlayerGlider;
}

static bool firefly_steer_retail(int hz, const NwProfile *p, s32 level, u8 lx, u8 ly, float tiltz, float tiltx)
{
    struct GLIDERSTRUCT r = firefly_steer_once(hz, p, level, lx, ly, tiltz, tiltx, true);
    struct GLIDERSTRUCT t = firefly_steer_once(hz, p, level, lx, ly, tiltz, tiltx, false);
    return memcmp(&r, &t, sizeof(r)) == 0;
}

static void firefly_steer(void)
{
    NwProfile retail = { 0 }, nhanced = { .movement = NW_MOVEMENT_NHANCED }, on = FIREFLY_ONLY(steering);
    NwProfile plain = on;
    plain.movement.firefly.steer_response = plain.movement.firefly.steer_turn = 100;
    plain.movement.firefly.pitch_response = 100;
    for (int hz = 50; hz <= 60; hz += 10) {
        float dt = hz == 50 ? 0.02f : 0.016666668f;
        /* Retail, and the switch at 100 % everywhere, are retail's. */
        CHECK(firefly_steer_retail(hz, &retail, 0x12, 255, 0, 0.0f, 0.0f));
        CHECK(firefly_steer_retail(hz, &plain, 0x12, 255, 0, 0.0f, 0.0f));
        CHECK(firefly_steer_retail(hz, &plain, 0x12, 127, 127, 40.0f, -20.0f));
        /* Banking in and climbing at twice the speed, turning 25 % more. */
        struct GLIDERSTRUCT r = firefly_steer_once(hz, &retail, 0x12, 255, 0, 0.0f, 0.0f, true);
        struct GLIDERSTRUCT b = firefly_steer_once(hz, &on, 0x12, 255, 0, 0.0f, 0.0f, false);
        CHECK(fabsf(r.DestTiltZ) > 30.0f && fabsf(r.DestTiltX) > 20.0f);
        CHECK(b.DestTiltZ == r.DestTiltZ && b.DestTiltX == r.DestTiltX);
        CHECK(near(r.TiltZ, r.DestTiltZ * (1.0f - powf(2.0f, -dt / 0.1f)), 1e-4f));
        CHECK(near(b.TiltZ, r.DestTiltZ * (1.0f - powf(2.0f, -dt / 0.05f)), 1e-4f));
        CHECK(near(b.TiltX, r.DestTiltX * (1.0f - powf(2.0f, -dt / 0.1f)), 1e-4f));
        CHECK(near(b.AngleY - 30.0f, b.TiltZ * 2.0f * dt * 0.25f, 1e-3f));
        /* Levelling out at twice the speed too. */
        r = firefly_steer_once(hz, &retail, 0x12, 127, 127, 40.0f, 0.0f, true);
        b = firefly_steer_once(hz, &on, 0x12, 127, 127, 40.0f, 0.0f, false);
        CHECK(r.DestTiltZ == 0.0f && near(40.0f - r.TiltZ, 40.0f * (1.0f - powf(2.0f, -dt / 0.05f)), 1e-4f));
        CHECK(near(40.0f - b.TiltZ, 40.0f * (1.0f - powf(2.0f, -dt / 0.025f)), 1e-4f));
        /* The other gliders, and the firefly on autopilot, stay retail's. */
        CHECK(firefly_steer_retail(hz, &on, 0xD, 255, 0, 0.0f, 0.0f));
        CHECK(firefly_steer_retail(hz, &on, 0x1A, 255, 0, 0.0f, 0.0f));
        CHECK(firefly_steer_retail(hz, &on, 0x24, 255, 0, 0.0f, 0.0f));
        firefly_at(hz, &nhanced, 0x12);
        PlayerGlider.AutoPilot = 1;
        struct GLIDERSTRUCT a = PlayerGlider;
        ControlGlider__retail(&a, &pad);
        ControlGlider(&PlayerGlider, &pad);
        CHECK(memcmp(&a, &PlayerGlider, sizeof(a)) == 0);
    }
    nw_profile_apply(&retail);
}

/* GliderFire with X held or not. */
static void firefly_fire(bool held)
{
    pad.paddata = held ? 0x40 : 0;
    GliderFire(&PlayerGlider, &pad);
}

static bool firefly_bomb(void)
{
    for (int i = 0; i < 10; i++) {
        if (GliderBombs[i].life) {
            return true;
        }
    }
    return false;
}

static void firefly_lock_on(void)
{
    NwProfile retail = { 0 }, nhanced = { .movement = NW_MOVEMENT_NHANCED }, on = FIREFLY_ONLY(lock_on);
    struct nuvec_s at;
    float progress;
    for (int hz = 50; hz <= 60; hz += 10) {
        /* Retail: X locks in 1.3 s, letting go before cancels it. */
        firefly_at(hz, &retail, 0x12);
        firefly_fire(true);
        CHECK(PlayerGlider.TargetOn && PlayerGlider.TargetTimer == 1.3f);
        firefly_fire(false);
        CHECK(!PlayerGlider.TargetOn);
        CHECK(!nw_firefly_marker());

        /* The marker: what X locks. */
        firefly_at(hz, &on, 0x12);
        CHECK(nw_firefly_lock(&at, &progress) == NW_FIREFLY_TARGET && at.z == 30.0f);
        /* In 1 s, and let go early it fires once it is locked. */
        firefly_fire(true);
        CHECK(PlayerGlider.TargetOn && PlayerGlider.TargetTimer == 1.0f && Level_TargetTime == 1.0f);
        firefly_fire(false);
        CHECK(PlayerGlider.TargetOn && nw_firefly_lock(&at, &progress) == NW_FIREFLY_LOCKING);
        CHECK(progress > 0.0f && progress < 0.1f);
        int frames = 1;
        while (PlayerGlider.TargetOn && frames < 2 * hz) {
            if (PlayerGlider.TargetTimer == 0.0f) {
                CHECK(nw_firefly_lock(&at, &progress) == NW_FIREFLY_LOCKED && progress == 1.0f);
            }
            firefly_fire(false);
            frames++;
        }
        CHECK(abs(frames - hz) <= 2);
        CHECK(firefly_bomb() && PlayerGlider.FireTimer == TorpedoTime);
        CHECK(nw_firefly_lock(&at, &progress) == NW_FIREFLY_RELOAD && progress < 0.1f);
        /* Quick tap off: letting go early drops it, and the marker shows it lost. */
        on.movement.firefly.lock_tap = false;
        firefly_at(hz, &on, 0x12);
        firefly_fire(true);
        firefly_fire(false);
        CHECK(!PlayerGlider.TargetOn && nw_firefly_lock(&at, &progress) == NW_FIREFLY_LOST && progress == 1.0f);
        CHECK(at.z == 30.0f);
        on.movement.firefly.lock_tap = true;
        /* Turned off, the lock time is retail's again. */
        nw_profile_apply(&retail);
        firefly_fire(false);
        CHECK(Level_TargetTime == 1.3f);

        /* The unused airship level shares the lock-on and stays retail's. */
        firefly_at(hz, &nhanced, 0x24);
        PlayerGlider.TargetOn = 1;
        PlayerGlider.TargetTimer = 0.5f;
        PlayerGlider.MovingTargetPoint = &BattleShipList[0].pos;
        firefly_fire(false);
        CHECK(Level_TargetTime == 0.7f);
        CHECK(!PlayerGlider.TargetOn && !nw_firefly_marker());
    }
    nw_profile_apply(&retail);
    memset(&PlayerGlider, 0, sizeof(PlayerGlider));
    memset(BattleShipList, 0, sizeof(BattleShipList));
    memset(GliderBombs, 0, sizeof(GliderBombs));
}

extern s32 FlyingLevelExtro;
extern s32 WBIntroOn;
extern f32 FlyingLevelCompleteTimer;
extern f32 Level_GliderFloor, Level_GliderCeiling, Level_GliderCurrentCeiling, Level_GliderRadius;
extern f32 Level_GliderCentreX, Level_GliderCentreZ;
extern struct nuvec_s GliderTargetPos;
struct gliderbullet_s {
    struct numtx_s mtx;
    struct nuvec_s vel;
    s16 life;
    s16 lastlife;
};
extern struct gliderbullet_s GliderBullet[64];
extern struct gliderbullet_s *FreeGliderBulletList[64];
extern struct gliderbullet_s *UsedGliderBulletList[];
extern s32 FreeGliderBulletNum, UsedGliderBulletNum;
extern struct zoffa_s EnemyZoffa[4];
void ControlGlider(struct GLIDERSTRUCT *glider, struct nupad_s *pad);
void GliderFire(struct GLIDERSTRUCT *glider, struct nupad_s *pad);

static struct nupad_s glide_pad;

/* The plane after one ControlGlider under p in level, from g, the stick at (x, y), the d-pad bits in buttons. */
static struct GLIDERSTRUCT glide(const NwProfile *p, int level, const struct GLIDERSTRUCT *g, u8 x, u8 y, u32 buttons)
{
    nw_profile_apply(p);
    Level = level;
    FlyingLevelExtro = WBIntroOn = 0;
    Level_GliderFloor = 16.0f;
    Level_GliderCeiling = Level_GliderCurrentCeiling = 66.0f;
    Level_GliderRadius = 177.5f;
    Level_GliderCentreX = Level_GliderCentreZ = 0.0f;
    PlayerGlider = *g;
    memset(&glide_pad, 0, sizeof(glide_pad));
    glide_pad.rdata.l_alg_x = x;
    glide_pad.rdata.l_alg_y = y;
    glide_pad.paddata = buttons;
    ControlGlider(&PlayerGlider, &glide_pad);
    return PlayerGlider;
}

static bool glide_retail(const NwProfile *p, int level, const struct GLIDERSTRUCT *g, u8 x, u8 y, u32 buttons)
{
    struct GLIDERSTRUCT r = glide(&(NwProfile){ 0 }, level, g, x, y, buttons);
    struct GLIDERSTRUCT t = glide(p, level, g, x, y, buttons);
    return memcmp(&r, &t, sizeof(r)) == 0;
}

/* The share of the way an ease at half-life h goes in a frame. */
static float glide_ease(float h, float dt)
{
    return 1.0f - 1.0f / powf(2.0f, dt / h);
}

static void plane_steering(void)
{
    static const u8 sticks[][2] = { { 0x80, 0x80 }, { 0x80, 0x00 }, { 0xFF, 0x80 }, { 0xA0, 0x60 }, { 0x93, 0x80 },
                                    { 0xC0, 0x20 }, { 0x7F, 0x9F }, { 0x00, 0xFF }, { 0xFF, 0x10 } };
    static const int levels[] = { 0xD, 0x18, 0x24 };
    NwProfile retail = { 0 }, nhanced = { .movement = NW_MOVEMENT_NHANCED }, steer = PLANE_ONLY(steering);
    NwProfile own = { .movement = NW_MOVEMENT_RETAIL };
    own.movement.plane = nhanced.movement.plane;
    steer.movement.plane.deadzone = 25;
    steer.movement.plane.pitch_response = steer.movement.plane.bank_response = 100;
    struct GLIDERSTRUCT states[5] = { 0 };
    for (int i = 0; i < 5; i++) {
        states[i].Position = (struct nuvec_s){ 5.0f, 40.0f, -20.0f };
    }
    states[1].TiltX = 20.0f;
    states[1].TiltZ = -30.0f;
    states[2].BarrelRoll = 1;
    states[2].TiltZ = -10.0f;
    states[3].OverideTiltZ = 0.5f;
    states[3].DestTiltZ = 45.0f;
    states[4].AutoPilot = 1;
    for (int hz = 50; hz <= 60; hz += 10) {
        nw_hz_set(hz);
        float dt = hz == 50 ? 0.02f : 0.016666668f;
        /* At the PS2's values it is retail's; on the other gliders' levels all its switches on too, on autopilot
           everything. */
        for (size_t s = 0; s < sizeof(sticks) / sizeof(sticks[0]); s++) {
            u8 x = sticks[s][0], y = sticks[s][1];
            for (int g = 0; g < 5; g++) {
                for (size_t l = 0; l < sizeof(levels) / sizeof(levels[0]); l++) {
                    CHECK(glide_retail(&steer, levels[l], &states[g], x, y, 0));
                }
                CHECK(glide_retail(&own, 0x12, &states[g], x, y, 0));
                CHECK(glide_retail(&own, 0x1A, &states[g], x, y, 0));
            }
            CHECK(glide_retail(&nhanced, 0xD, &states[4], x, y, 0));
        }
        CHECK(glide_retail(&steer, 0xD, &states[0], 0x80, 0x80, 0x1000));

        /* Full forward in Tornado Alley: retail's 0.5 s nose-down at 2.5 times the pace. */
        struct GLIDERSTRUCT r = glide(&retail, 0xD, &states[0], 0x7F, 0x00, 0);
        struct GLIDERSTRUCT n = glide(&nhanced, 0xD, &states[0], 0x7F, 0x00, 0);
        CHECK(near(r.DestTiltX, -40.0f, 1e-4f) && n.DestTiltX == r.DestTiltX);
        CHECK(near(r.TiltX, r.DestTiltX * glide_ease(0.5f, dt), 1e-4f));
        CHECK(near(n.TiltX, n.DestTiltX * glide_ease(0.2f, dt), 1e-4f));
        /* Let go: back to level at 1 s, now 0.4 s, the bank at 0.05 s, now a third sooner. */
        n = glide(&nhanced, 0xD, &states[1], 0x80, 0x80, 0);
        CHECK(n.DestTiltX == 0.0f && near(n.TiltX, 20.0f - 20.0f * glide_ease(0.4f, dt), 1e-4f));
        CHECK(n.DestTiltZ == 0.0f && near(n.TiltZ, -30.0f + 30.0f * glide_ease(0.05f / 1.5f, dt), 1e-4f));
        /* Atmospheric Pressure's 0.2 s at 0.08 s, its bank's 0.1 s at 1.5 times. */
        n = glide(&nhanced, 0x18, &states[0], 0xFF, 0x00, 0);
        CHECK(n.DestTiltX < -20.0f && near(n.TiltX, n.DestTiltX * glide_ease(0.08f, dt), 1e-4f));
        CHECK(n.DestTiltZ > 40.0f && near(n.TiltZ, n.DestTiltZ * glide_ease(0.1f / 1.5f, dt), 1e-4f));
        r = glide(&retail, 0x18, &states[0], 0xFF, 0x00, 0);
        CHECK(near(r.TiltZ, r.DestTiltZ * glide_ease(0.1f, dt), 1e-4f));
        /* Under an override and mid roll the bank eases as retail's. */
        n = glide(&nhanced, 0xD, &states[3], 0xFF, 0x80, 0);
        r = glide(&retail, 0xD, &states[3], 0xFF, 0x80, 0);
        CHECK(n.TiltZ == r.TiltZ);
        CHECK(glide_retail(&nhanced, 0xD, &states[2], 0x80, 0x80, 0));

        /* A sixth of the way out: nothing on the PS2, a turn at a 10 % deadzone. */
        CHECK(glide(&retail, 0xD, &states[0], 0x7F + 20, 0x7F, 0).InputX == 0.0f);
        n = glide(&nhanced, 0xD, &states[0], 0x7F + 20, 0x7F, 0);
        CHECK(n.InputX > 0.05f && n.InputX < 0.1f && n.DestTiltZ > 0.0f);
        CHECK(glide(&nhanced, 0xD, &states[0], 0x7F + 12, 0x7F, 0).InputX == 0.0f);
        CHECK(near(glide(&nhanced, 0xD, &states[0], 0x7F + 126, 0x7F, 0).InputX, 1.0f, 1e-4f));

        /* push_dive off: forward and the d-pad's up climb. */
        NwProfile climb = nhanced;
        climb.movement.plane.push_dive = false;
        CHECK(near(glide(&climb, 0xD, &states[0], 0x7F, 0x00, 0).DestTiltX, 40.0f, 1e-4f));
        CHECK(glide(&climb, 0xD, &states[0], 0x7F, 0xFF, 0).DestTiltX < -39.0f);
        CHECK(near(glide(&climb, 0x18, &states[0], 0x80, 0x80, 0x1000).DestTiltX, 40.0f, 1e-4f));
        CHECK(near(glide(&nhanced, 0x18, &states[0], 0x80, 0x80, 0x1000).DestTiltX, -40.0f, 1e-4f));
    }
    nw_profile_apply(&retail);
    memset(&PlayerGlider, 0, sizeof(PlayerGlider));
    Level = 0;
}

/* The shot GliderFire adds in Tornado Alley under p, flying level along -z; NULL for none. */
static struct gliderbullet_s *plane_shot(const NwProfile *p)
{
    nw_profile_apply(p);
    Level = 0xD;
    FlyingLevelExtro = WBIntroOn = 0;
    FlyingLevelCompleteTimer = 1.5f;
    GameCam[0].pos = (struct nuvec_s){ 0.0f, 0.0f, 0.0f };
    memset(&PlayerGlider, 0, sizeof(PlayerGlider));
    PlayerGlider.Cre = &Character[0];
    for (int i = 0; i < 64; i++) {
        FreeGliderBulletList[i] = &GliderBullet[i];
    }
    FreeGliderBulletNum = 64;
    UsedGliderBulletNum = 0;
    memset(&glide_pad, 0, sizeof(glide_pad));
    glide_pad.paddata = 0x40;
    GliderFire(&PlayerGlider, &glide_pad);
    return UsedGliderBulletNum == 1 ? UsedGliderBulletList[0] : NULL;
}

static float DotProduct(const struct nuvec_s *a, const struct nuvec_s *b)
{
    return a->x * b->x + a->y * b->y + a->z * b->z;
}

/* The cosine between the shot's flight and the way to `to`. */
static float plane_off(const struct gliderbullet_s *b, struct nuvec_s to)
{
    struct nuvec_s d = { to.x - b->mtx._30, to.y - b->mtx._31, to.z - b->mtx._32 }, v = b->vel;
    return DotProduct(&d, &v) / (NuVecMag(&d) * NuVecMag(&v));
}

/* The cosine between the tracer's z row and its flight. */
static float plane_tracer(const struct gliderbullet_s *b)
{
    struct nuvec_s z = { b->mtx._20, b->mtx._21, b->mtx._22 }, v = b->vel;
    return DotProduct(&z, &v) / (NuVecMag(&z) * NuVecMag(&v));
}

/* How near the shot passes a UFO flying from pos at vel. */
static float plane_miss(const struct gliderbullet_s *b, float dt, struct nuvec_s pos, struct nuvec_s vel)
{
    struct nuvec_s rel = { b->mtx._30 - pos.x, b->mtx._31 - pos.y, b->mtx._32 - pos.z };
    struct nuvec_s dv = { b->vel.x / dt - vel.x, b->vel.y / dt - vel.y, b->vel.z / dt - vel.z };
    float t = -DotProduct(&rel, &dv) / DotProduct(&dv, &dv);
    struct nuvec_s at = { rel.x + dv.x * t, rel.y + dv.y * t, rel.z + dv.z * t };
    return NuVecMag(&at);
}

static void plane_aim(void)
{
    NwProfile retail = { 0 }, aim = PLANE_ONLY(aim);
    aim.movement.plane.aim_assist = 0;
    memset(EnemyZoffa, 0, sizeof(EnemyZoffa));
    for (int hz = 50; hz <= 60; hz += 10) {
        nw_hz_set(hz);
        float dt = hz == 50 ? 0.02f : 0.016666668f;
        /* Retail's shot drops 6 in 50, the reticle 8.5. */
        struct gliderbullet_s *b = plane_shot(&retail);
        CHECK(b && near(b->vel.y, -6.0f * dt, 1e-4f) && near(b->vel.z, -50.0f * dt, 1e-4f));
        CHECK(b && plane_off(b, GliderTargetPos) < 0.9995f);
        float speed = b ? NuVecMag(&b->vel) : 0.0f, tracer = b ? plane_tracer(b) : 0.0f;
        b = plane_shot(&aim);
        CHECK(b && plane_off(b, GliderTargetPos) > 0.99999f && near(NuVecMag(&b->vel), speed, 1e-5f));
        CHECK(b && near(plane_tracer(b), tracer, 1e-4f));
        /* The assist leads a UFO 1.5 degrees off the reticle; at 5.7 degrees or out of reach it is left alone. */
        aim.movement.plane.aim_assist = 3;
        EnemyZoffa[0].active = 1;
        EnemyZoffa[0].pos = (struct nuvec_s){ -4.5f, -10.06f, -59.15f };
        EnemyZoffa[0].vel = (struct nuvec_s){ 5.0f, 0.0f, 0.0f };
        b = plane_shot(&aim);
        CHECK(b && plane_miss(b, dt, EnemyZoffa[0].pos, EnemyZoffa[0].vel) < 0.05f);
        CHECK(b && plane_off(b, GliderTargetPos) < 0.9999f);
        EnemyZoffa[0].pos = (struct nuvec_s){ 6.0f, -10.06f, -59.15f };
        EnemyZoffa[0].vel = (struct nuvec_s){ 0.0f, 0.0f, 0.0f };
        b = plane_shot(&aim);
        CHECK(b && plane_off(b, GliderTargetPos) > 0.99999f);
        EnemyZoffa[0].pos = (struct nuvec_s){ 0.0f, -20.0f, -150.0f };
        b = plane_shot(&aim);
        CHECK(b && plane_off(b, GliderTargetPos) > 0.99999f);
        memset(EnemyZoffa, 0, sizeof(EnemyZoffa));
        aim.movement.plane.aim_assist = 0;
        /* Only in Tornado Alley. */
        nw_profile_apply(&aim);
        Level = 0x18;
        CHECK(!nw_plane_fire(&PlayerGlider, &glide_pad));
    }
    nw_profile_apply(&retail);
    memset(&PlayerGlider, 0, sizeof(PlayerGlider));
    UsedGliderBulletNum = 0;
    Level = 0;
}

extern struct GLIDERSTRUCT PlayerGlider;
extern f32 CRASHTEROIDSSeekTiltXTimeZero;
extern f32 CRASHTEROIDSSeekTiltXTimeNonZero;
extern f32 SeekTiltZZero;
extern f32 SeekTiltZNonZero;
extern f32 Level_GliderSpeed;
extern f32 Level_GliderFloor;
extern f32 Level_GliderCeiling;
extern f32 Level_GliderCurrentCeiling;
extern f32 Level_GliderRadius;
extern f32 FlyingLevelCompleteTimer;
extern s32 FlyingLevelExtro;
extern struct nuvec_s GliderTargetPos;
extern struct nuvec_s SpaceFirePosA;
extern struct nuvec_s SpaceFirePosB;
extern struct zoffa_s EnemyZoffa[4];
extern struct satellite_s SatelliteList[9];
extern unsigned char AsteroidList[];
void ControlGlider(struct GLIDERSTRUCT *glider, struct nupad_s *pad);
void ProcessGliderMovement(struct GLIDERSTRUCT *glider, f32 dt);
void GliderFire(struct GLIDERSTRUCT *glider, struct nupad_s *pad);
s32 CollideGliderBullets(struct nuvec_s *pos, f32 radius, s32 enemy, f32 yscale, s32 debris, s32 reflect);

/* vehicle.c's struct gliderbullet_s; vel is a frame's. */
typedef struct gliderbullet_s Bolt;
extern s32 FreeGliderBulletNum;
extern s32 UsedGliderBulletNum;

#define SPACE_LEVEL 0x1A

/* Crashteroids' glider at hz under p, as SpaceArenaReset leaves it, the stick centred. */
static void space_at(int hz, const NwProfile *p)
{
    nw_hz_set(hz);
    nw_profile_apply(p);
    Level = SPACE_LEVEL;
    CRASHTEROIDSSeekTiltXTimeZero = 1.0f;
    CRASHTEROIDSSeekTiltXTimeNonZero = 0.5f;
    SeekTiltZZero = 0.05f;
    SeekTiltZNonZero = 0.1f;
    Level_GliderSpeed = 8.0f;
    Level_GliderFloor = -30.0f;
    Level_GliderCeiling = Level_GliderCurrentCeiling = 35.0f;
    Level_GliderRadius = 180.0f;
    FlyingLevelExtro = 0;
    FlyingLevelCompleteTimer = 1.5f;
    SpaceFirePosA = (struct nuvec_s){ 0.75f, -0.1f, 0.0f };
    SpaceFirePosB = (struct nuvec_s){ -0.75f, -0.1f, 0.0f };
    memset(&PlayerGlider, 0, sizeof(PlayerGlider));
    PlayerGlider.Cre = &Character[0];
    PlayerGlider.HitPoints = 100;
    player = &Character[0];
    memset(EnemyZoffa, 0, sizeof(EnemyZoffa));
    memset(SatelliteList, 0, sizeof(SatelliteList));
    memset(AsteroidList, 0, 100 * 0x4C);
    for (int i = 0; i < 64; i++) {
        FreeGliderBulletList[i] = &GliderBullet[i];
    }
    FreeGliderBulletNum = 64;
    UsedGliderBulletNum = 0;
    memset(&pad, 0, sizeof(pad));
    pad.rdata.l_alg_x = pad.rdata.l_alg_y = 127;
}

/* The glider after frames of ControlGlider with the stick at (x, y). */
static struct GLIDERSTRUCT space_steer(int hz, const NwProfile *p, u8 x, u8 y, int frames)
{
    space_at(hz, p);
    pad.rdata.l_alg_x = x;
    pad.rdata.l_alg_y = y;
    for (int i = 0; i < frames; i++) {
        ControlGlider(&PlayerGlider, &pad);
    }
    return PlayerGlider;
}

/* Whether a second of steering and flying in level, the autopilot on or off, comes out as retail's. */
static bool space_as_retail(int hz, const NwProfile *p, int level, s32 autopilot)
{
    struct GLIDERSTRUCT r;
    for (int run = 0; run < 2; run++) {
        space_at(hz, run ? p : &(NwProfile){ 0 });
        Level = level;
        PlayerGlider.AutoPilot = autopilot;
        PlayerGlider.Speed = -8.0f;
        pad.rdata.l_alg_x = 200;
        pad.rdata.l_alg_y = 40;
        for (int i = 0; i < hz; i++) {
            ControlGlider(&PlayerGlider, &pad);
            ProcessGliderMovement(&PlayerGlider, NW_HZ(0.02f, 0.016666668f));
        }
        if (!run) {
            r = PlayerGlider;
        }
    }
    return memcmp(&r, &PlayerGlider, sizeof(r)) == 0;
}

static void space_steering(void)
{
    NwProfile retail = { 0 }, on = SPACE_ONLY(steering), nhanced = { .movement = NW_MOVEMENT_NHANCED };
    NwProfile own = { .movement = NW_MOVEMENT_RETAIL };
    own.movement.spacefighter = nhanced.movement.spacefighter;
    s32 level = Level;
    for (int hz = 50; hz <= 60; hz += 10) {
        float dt = hz == 50 ? 0.02f : 0.016666668f;
        /* Climb and dive: half way in the PS2's 0.5 s, in 0.2 s at 250 %. */
        CHECK(near(space_steer(hz, &retail, 127, 0, hz / 2).TiltX, -20.0f, 1e-3f));
        CHECK(near(space_steer(hz, &on, 127, 0, hz / 5).TiltX, -20.0f, 1e-3f));
        /* Banking: the PS2's 0.1 s half-life, 1.5 times as fast. */
        struct GLIDERSTRUCT r = space_steer(hz, &retail, 255, 127, 3), b = space_steer(hz, &on, 255, 127, 3);
        CHECK(near(fabsf(r.TiltZ), 60.0f * (1.0f - exp2f(-3.0f * dt / 0.1f)), 1e-2f));
        CHECK(near(fabsf(b.TiltZ), 60.0f * (1.0f - exp2f(-3.0f * dt * 1.5f / 0.1f)), 1e-2f));
        CHECK(CRASHTEROIDSSeekTiltXTimeNonZero == 0.5f && SeekTiltZNonZero == 0.1f);
        /* A light push (20 of 127): the PS2's deadzone drops it, 10 % takes it; a full push stays full. */
        CHECK(space_steer(hz, &retail, 127, 107, 1).InputZ == 0.0f);
        r = space_steer(hz, &on, 127, 107, 1);
        CHECK(r.InputZ > 0.05f && r.InputZ < 0.1f && pad.rdata.l_alg_y == 107);
        CHECK(space_steer(hz, &on, 127, 0, 1).InputZ == 1.0f);
        on.movement.spacefighter.deadzone = 25;
        CHECK(space_steer(hz, &on, 127, 107, 1).InputZ == 0.0f);
        on.movement.spacefighter.deadzone = 10;
        /* Turning at a bank of 30: 60 degrees a second, 75 at 125 %. */
        space_at(hz, &retail);
        PlayerGlider.TiltZ = 30.0f;
        ProcessGliderMovement(&PlayerGlider, dt);
        CHECK(near(PlayerGlider.AngleY, 60.0f * dt, 1e-4f));
        space_at(hz, &on);
        PlayerGlider.TiltZ = 30.0f;
        ProcessGliderMovement(&PlayerGlider, dt);
        CHECK(near(PlayerGlider.AngleY, 75.0f * dt, 1e-4f));
        /* Every switch on: the autopilot stays retail's, and with its own on the other gliders' levels. */
        CHECK(!space_as_retail(hz, &nhanced, SPACE_LEVEL, 0));
        CHECK(space_as_retail(hz, &nhanced, SPACE_LEVEL, 1));
        CHECK(space_as_retail(hz, &own, 0xD, 0));
        CHECK(space_as_retail(hz, &own, 0x12, 0));
    }
    nw_profile_apply(&retail);
    Level = level;
}

static void space_roll(void)
{
    NwProfile retail = { 0 }, on = SPACE_ONLY(roll);
    s32 level = Level;
    for (int hz = 50; hz <= 60; hz += 10) {
        float r = 0.0f;
        for (int run = 0; run < 3; run++) {
            on.movement.spacefighter.roll_kept = run == 2 ? 0 : 100;
            space_at(hz, run ? &on : &retail);
            PlayerGlider.Speed = -8.0f;
            PlayerGlider.BarrelRoll = 3;
            for (int i = 0; i < hz; i++) {
                ProcessGliderMovement(&PlayerGlider, NW_HZ(0.02f, 0.016666668f));
            }
            CHECK(Level_GliderSpeed == 8.0f);
            if (run == 0) {
                /* A second of a roll: half way to an eighth of the speed. */
                r = PlayerGlider.Speed;
                CHECK(near(r, -4.5f, 1e-3f));
            }
            CHECK(run == 1 ? PlayerGlider.Speed == -8.0f : PlayerGlider.Speed == r);
        }
    }
    nw_profile_apply(&retail);
    Level = level;
}

/* GliderFire from a fixed pose under p, firing or not; a still UFO at ufo, when given. */
static void space_fire(int hz, const NwProfile *p, bool fire, const struct nuvec_s *ufo)
{
    space_at(hz, p);
    if (ufo) {
        EnemyZoffa[0].active = 1;
        EnemyZoffa[0].pos = *ufo;
    }
    PlayerGlider.Position = (struct nuvec_s){ 1.0f, 2.0f, 3.0f };
    PlayerGlider.TiltX = 10.0f;
    PlayerGlider.TiltZ = 5.0f;
    PlayerGlider.AngleY = 30.0f;
    pad.paddata = fire ? 0x40 : 0;
    GliderFire(&PlayerGlider, &pad);
}

static struct nuvec_s bolt_pos(const Bolt *b)
{
    return (struct nuvec_s){ b->mtx._30, b->mtx._31, b->mtx._32 };
}

/* How far p is off the line through o along the unit d, and how far along it. */
static float off_line(struct nuvec_s p, struct nuvec_s o, struct nuvec_s d, float *along)
{
    struct nuvec_s r = { p.x - o.x, p.y - o.y, p.z - o.z };
    *along = r.x * d.x + r.y * d.y + r.z * d.z;
    r = (struct nuvec_s){ r.x - d.x * *along, r.y - d.y * *along, r.z - d.z * *along };
    return sqrtf(r.x * r.x + r.y * r.y + r.z * r.z);
}

static float degrees(struct nuvec_s a, struct nuvec_s b)
{
    float c = (a.x * b.x + a.y * b.y + a.z * b.z) / (NuVecMag(&a) * NuVecMag(&b));
    return acosf(fminf(c, 1.0f)) * 180.0f / 3.14159265f;
}

static void space_aim(void)
{
    NwProfile retail = { 0 }, on = SPACE_ONLY(aim);
    s32 level = Level;
    for (int hz = 50; hz <= 60; hz += 10) {
        space_fire(hz, &retail, true, NULL);
        CHECK(UsedGliderBulletNum == 2);
        Bolt a = *UsedGliderBulletList[0], b = *UsedGliderBulletList[1];
        struct nuvec_s old = GliderTargetPos;
        struct nuvec_s mid = { (a.mtx._30 + b.mtx._30) * 0.5f, (a.mtx._31 + b.mtx._31) * 0.5f,
                               (a.mtx._32 + b.mtx._32) * 0.5f };
        struct nuvec_s dir = { a.vel.x + b.vel.x, a.vel.y + b.vel.y, a.vel.z + b.vel.z };
        NuVecNorm(&dir, &dir);
        float along;
        /* The PS2's reticle is off the shots' middle line... */
        CHECK(off_line(old, mid, dir, &along) > 2.0f);
        /* ...this one on it, 60 ahead; the shots stay the PS2's. */
        space_fire(hz, &on, true, NULL);
        CHECK(UsedGliderBulletNum == 2 && memcmp(&a, UsedGliderBulletList[0], sizeof(a)) == 0 &&
              memcmp(&b, UsedGliderBulletList[1], sizeof(b)) == 0);
        CHECK(off_line(GliderTargetPos, mid, dir, &along) < 0.05f && near(along, 61.0f, 0.01f));
        struct nuvec_s aimed = GliderTargetPos;
        space_fire(hz, &on, false, NULL);
        CHECK(UsedGliderBulletNum == 0 && memcmp(&aimed, &GliderTargetPos, sizeof(aimed)) == 0);
        /* The other gliders' reticles stay retail's. */
        space_at(hz, &retail);
        Level = 0xD;
        PlayerGlider.FireTimer = 1.0f;
        GliderFire(&PlayerGlider, &pad);
        old = GliderTargetPos;
        space_at(hz, &on);
        Level = 0xD;
        PlayerGlider.FireTimer = 1.0f;
        GliderFire(&PlayerGlider, &pad);
        CHECK(memcmp(&old, &GliderTargetPos, sizeof(old)) == 0);
    }
    nw_profile_apply(&retail);
    Level = level;
}

/* One of Coco's shots at (x, 0, z), its last step of 2.4 from +z. */
static Bolt *space_bolt(float x, float z, s16 life)
{
    Bolt *b = &GliderBullet[0];
    memset(b, 0, sizeof(*b));
    b->mtx._00 = b->mtx._11 = b->mtx._22 = b->mtx._33 = 1.0f;
    b->mtx._30 = x;
    b->mtx._32 = z;
    b->vel.z = -2.4f;
    b->life = life;
    b->lastlife = 38;
    UsedGliderBulletList[0] = b;
    UsedGliderBulletNum = 1;
    FreeGliderBulletNum = 0;
    return b;
}

static void space_sweep(void)
{
    NwProfile retail = { 0 }, on = SPACE_ONLY(aim);
    struct nuvec_s centre = { 0.0f, 0.0f, 0.0f };
    s32 level = Level;
    for (int hz = 50; hz <= 60; hz += 10) {
        /* Through the middle of a UFO between two frames: the PS2's misses it. */
        space_at(hz, &retail);
        space_bolt(0.0f, -1.3f, 30);
        CHECK(CollideGliderBullets(&centre, 1.0f, 0, 1.0f, 0, 0) == 0 && UsedGliderBulletNum == 1);
        space_at(hz, &on);
        space_bolt(0.0f, -1.3f, 30);
        CHECK(CollideGliderBullets(&centre, 1.0f, 0, 1.0f, 0, 0) == 1 && UsedGliderBulletNum == 0);
        /* The step passes 0.9 under it: 1.35 with y scaled by 1.5. */
        space_at(hz, &on);
        Bolt *b = space_bolt(0.0f, -1.3f, 30);
        b->mtx._31 = -0.9f;
        CHECK(CollideGliderBullets(&centre, 1.0f, 0, 1.5f, 0, 0) == 0);
        CHECK(CollideGliderBullets(&centre, 1.0f, 0, 1.0f, 0, 0) == 1);
        /* Past it, or before its first step: missed, and left where it is. */
        space_at(hz, &on);
        b = space_bolt(1.5f, -1.3f, 30);
        CHECK(CollideGliderBullets(&centre, 1.0f, 0, 1.0f, 0, 0) == 0 && UsedGliderBulletNum == 1);
        CHECK(b->mtx._30 == 1.5f && b->mtx._31 == 0.0f && b->mtx._32 == -1.3f);
        space_at(hz, &on);
        space_bolt(0.0f, -1.3f, NW_HZ(37, 45));
        CHECK(CollideGliderBullets(&centre, 1.0f, 0, 1.0f, 0, 0) == 0);
        /* A shield's reflection, the flag off and the other levels test as the PS2. */
        space_at(hz, &on);
        space_bolt(0.0f, -1.3f, 30);
        CHECK(CollideGliderBullets(&centre, 1.0f, 0, 1.0f, 0, 1) == 0);
        on.movement.spacefighter.aim_sweep = false;
        space_at(hz, &on);
        space_bolt(0.0f, -1.3f, 30);
        CHECK(CollideGliderBullets(&centre, 1.0f, 0, 1.0f, 0, 0) == 0);
        on.movement.spacefighter.aim_sweep = true;
        space_at(hz, &on);
        Level = 0xD;
        space_bolt(0.0f, -1.3f, 30);
        CHECK(CollideGliderBullets(&centre, 1.0f, 0, 1.0f, 0, 0) == 0);
    }
    nw_profile_apply(&retail);
    Level = level;
}

static void space_assist(void)
{
    NwProfile retail = { 0 }, on = SPACE_ONLY(aim_assist);
    s32 level = Level;
    for (int hz = 50; hz <= 60; hz += 10) {
        space_fire(hz, &retail, true, NULL);
        Bolt a = *UsedGliderBulletList[0];
        struct nuvec_s from = bolt_pos(&a), dir = a.vel, up = { 0.0f, 1.0f, 0.0f }, side;
        NuVecNorm(&dir, &dir);
        NuVecCross(&side, &dir, &up);
        NuVecNorm(&side, &side);
        for (int off = 3; off <= 10; off += 7) {
            float r = off * 3.14159265f / 180.0f;
            struct nuvec_s ufo = { from.x + 40.0f * (dir.x * cosf(r) + side.x * sinf(r)),
                                   from.y + 40.0f * (dir.y * cosf(r) + side.y * sinf(r)),
                                   from.z + 40.0f * (dir.z * cosf(r) + side.z * sinf(r)) };
            space_fire(hz, &on, true, &ufo);
            Bolt c = *UsedGliderBulletList[0];
            struct nuvec_s to = { ufo.x - from.x, ufo.y - from.y, ufo.z - from.z };
            if (off == 3) {
                /* 3 degrees off: three quarters of the way to it, the bolt turned with it. */
                CHECK(near(degrees(c.vel, to), 0.75f, 0.1f) && near(NuVecMag(&c.vel), NuVecMag(&a.vel), 1e-4f));
                struct nuvec_s fa = { a.mtx._20, a.mtx._21, a.mtx._22 }, fc = { c.mtx._20, c.mtx._21, c.mtx._22 };
                CHECK(near(degrees(fa, fc), degrees(a.vel, c.vel), 0.05f));
                on.movement.spacefighter.assist_pull = 0;
                space_fire(hz, &on, true, &ufo);
                CHECK(memcmp(&a, UsedGliderBulletList[0], sizeof(a)) == 0);
                on.movement.spacefighter.assist_pull = 75;
            } else {
                /* Outside the cone: the PS2's. */
                CHECK(memcmp(&a, &c, sizeof(a)) == 0);
            }
        }
    }
    nw_profile_apply(&retail);
    Level = level;
}

int main(void)
{
    NuTrigInit();
    monkey_bars(50, 0.015599999f);
    monkey_bars(60, 0.013f);
    monkey_bar_anim();
    air_indicator();
    boss_health();
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
    coco_jump();
    coco_run();
    mech_stand_jump();
    mech_shot();
    mech_aim_walk();
    mech_aim();
    mech_hitbox();
    sub_turning();
    sub_pitch();
    sub_speed();
    sub_hitbox();
    sub_weapons();
    jeep_steer();
    jeep_speed();
    jeep_grip();
    atlasphere();
    firefly_steer();
    firefly_lock_on();
    plane_steering();
    plane_aim();
    space_steering();
    space_roll();
    space_aim();
    space_sweep();
    space_assist();

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
    m = (NwMovement)NW_MOVEMENT_RETAIL;
    m.coco.run_top = 150;
    CHECK(nw_movement_profile(&m) == NW_MOVEMENT_PROFILE_RETAIL);
    m.coco.grip = true;
    CHECK(nw_movement_profile(&m) == NW_MOVEMENT_PROFILE_CUSTOM);
    m = (NwMovement)NW_MOVEMENT_NHANCED;
    m.coco.run_top = 130;
    CHECK(nw_movement_profile(&m) == NW_MOVEMENT_PROFILE_CUSTOM);
    m = (NwMovement)NW_MOVEMENT_RETAIL;
    m.mech.aim_speed = 400;
    CHECK(nw_movement_profile(&m) == NW_MOVEMENT_PROFILE_RETAIL);
    m.mech.aim_fast = true;
    CHECK(nw_movement_profile(&m) == NW_MOVEMENT_PROFILE_CUSTOM);
    m = (NwMovement)NW_MOVEMENT_NHANCED;
    m.mech.hitbox_size = 90;
    CHECK(nw_movement_profile(&m) == NW_MOVEMENT_PROFILE_CUSTOM);
    CHECK(nw_movement_profile(&(NwMovement){ .submarine = { .turning = true } }) == NW_MOVEMENT_PROFILE_CUSTOM);
    m = (NwMovement)NW_MOVEMENT_RETAIL;
    m.submarine.speed_top = 200;
    CHECK(nw_movement_profile(&m) == NW_MOVEMENT_PROFILE_RETAIL);
    m = (NwMovement)NW_MOVEMENT_NHANCED;
    m.submarine.hitbox_size = 50;
    CHECK(nw_movement_profile(&m) == NW_MOVEMENT_PROFILE_CUSTOM);
    m = (NwMovement)NW_MOVEMENT_RETAIL;
    m.jeep.steer_response = 400;
    CHECK(nw_movement_profile(&m) == NW_MOVEMENT_PROFILE_RETAIL);
    m = (NwMovement)NW_MOVEMENT_NHANCED;
    m.jeep.grip_speed = 500;
    CHECK(nw_movement_profile(&m) == NW_MOVEMENT_PROFILE_CUSTOM);
    m = (NwMovement)NW_MOVEMENT_NHANCED;
    m.jeep.steering = false;
    CHECK(nw_movement_profile(&m) == NW_MOVEMENT_PROFILE_CUSTOM);
    CHECK(nw_movement_profile(&(NwMovement){ .jeep = { .speed = true } }) == NW_MOVEMENT_PROFILE_CUSTOM);
    m = (NwMovement)NW_MOVEMENT_RETAIL;
    m.atlasphere.guard_speed = 13;
    CHECK(nw_movement_profile(&m) == NW_MOVEMENT_PROFILE_RETAIL);
    m.atlasphere.edge_guard = true;
    CHECK(nw_movement_profile(&m) == NW_MOVEMENT_PROFILE_CUSTOM);
    m = (NwMovement)NW_MOVEMENT_NHANCED;
    m.atlasphere.coast_brake = 25;
    CHECK(nw_movement_profile(&m) == NW_MOVEMENT_PROFILE_CUSTOM);
    m.atlasphere.coast_brake = 0;
    m.atlasphere.guard_push = false;
    CHECK(nw_movement_profile(&m) == NW_MOVEMENT_PROFILE_CUSTOM);
    CHECK(nw_movement_profile(&(NwMovement){ .atlasphere = { .brake = true } }) == NW_MOVEMENT_PROFILE_CUSTOM);
    m = (NwMovement)NW_MOVEMENT_RETAIL;
    m.firefly.lock_ms = 1300;
    CHECK(nw_movement_profile(&m) == NW_MOVEMENT_PROFILE_RETAIL);
    m.firefly.steering = true;
    CHECK(nw_movement_profile(&m) == NW_MOVEMENT_PROFILE_CUSTOM);
    m = (NwMovement)NW_MOVEMENT_NHANCED;
    m.firefly.lock_tap = false;
    CHECK(nw_movement_profile(&m) == NW_MOVEMENT_PROFILE_CUSTOM);
    CHECK(nw_movement_profile(&(NwMovement){ .firefly = { .lock_on = true } }) == NW_MOVEMENT_PROFILE_CUSTOM);
    m = (NwMovement)NW_MOVEMENT_RETAIL;
    m.plane.pitch_response = 400;
    CHECK(nw_movement_profile(&m) == NW_MOVEMENT_PROFILE_RETAIL);
    m.plane.aim = true;
    CHECK(nw_movement_profile(&m) == NW_MOVEMENT_PROFILE_CUSTOM);
    m = (NwMovement)NW_MOVEMENT_NHANCED;
    m.plane.push_dive = false;
    CHECK(nw_movement_profile(&m) == NW_MOVEMENT_PROFILE_CUSTOM);
    m = (NwMovement)NW_MOVEMENT_RETAIL;
    m.spacefighter.aim_range = 88;
    CHECK(nw_movement_profile(&m) == NW_MOVEMENT_PROFILE_RETAIL);
    m.spacefighter.roll = true;
    CHECK(nw_movement_profile(&m) == NW_MOVEMENT_PROFILE_CUSTOM);
    m = (NwMovement)NW_MOVEMENT_NHANCED;
    m.spacefighter.assist_pull = 50;
    CHECK(nw_movement_profile(&m) == NW_MOVEMENT_PROFILE_CUSTOM);
    m.spacefighter.assist_pull = 75;
    m.spacefighter.aim_sweep = false;
    CHECK(nw_movement_profile(&m) == NW_MOVEMENT_PROFILE_CUSTOM);
    CHECK(nw_movement_profile(&(NwMovement){ .spacefighter = { .aim = true } }) == NW_MOVEMENT_PROFILE_CUSTOM);

    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    return 0;
}
