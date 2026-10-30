/* src/gameplay: the profile's switches and the retail values they change. */
#include <math.h>
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
#include "nu3d/nugscn.h"
#include "nu_port.h"
#include "numath/nutrig.h"
#include "port/hz.h"

extern struct MoveInfo CrashMoveInfo;
extern struct MoveInfo CocoMoveInfo;
extern s32 plr_terrain_ok;
extern s32 VEHICLECONTROL;
extern struct ldata_s *LDATA;
extern struct gtimer_s GameTimer;
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

/* Retail's Crash data and globals at hz, then p applied. */
static void crash_at(int hz, const NwProfile *p)
{
    nw_hz_set(hz);
    nw_profile_apply(&(NwProfile){ 0 });
    CrashMoveInfo.JUMPFRAMES0 = NW_HZ(5, 6);
    CrashMoveInfo.JUMPFRAMES1 = NW_HZ(10, 12);
    CrashMoveInfo.RUNSPEED = NW_HZ(0.072f, 0.060000002f);
    CrashMoveInfo.SPRINTSPEED = NW_HZ(0.096f, 0.080000006f);
    GRAVITY = NW_HZ(-0.0072000003f, -0.005f);
    VEHICLECONTROL = 0;
    nw_profile_apply(p);
    memset(&Character[0], 0, sizeof(Character[0]));
    Character[0].obj.vehicle = -1;
}

/* A jump's seconds in the air, apex and reach. */
typedef struct Arc {
    float time, apex, reach;
} Arc;

/* Crash jumps at run speed on flat ground with the stick held and the button for hold ticks: MovePlayer's and
   MoveCRASH's jump steps cut down, the hooks called where the game calls them. */
static Arc jump(int hz, const NwProfile *p, int hold)
{
    crash_at(hz, p);
    struct creature_s *c = &Character[0];
    float run = CrashMoveInfo.RUNSPEED, friction = NW_HZ(0.0072f, 0.005f);
    c->obj.mom.z = run;
    c->jump = 1;
    c->jump_type = 1;
    c->jump_frames = CrashMoveInfo.JUMPFRAMES0;
    Arc arc = { 0 };
    for (int tick = 1; tick < 500; tick++) {
        GameTimer.frame++;
        in_speed = run;
        in_f_friction = in_s_friction = friction;
        MoveLoopXZ(&c->obj, &c->obj.thdg);
        c->obj.pos.y += c->obj.mom.y;
        c->obj.pos.z += c->obj.mom.z;
        arc.apex = fmaxf(arc.apex, c->obj.pos.y);
        c->obj.ground = c->obj.pos.y < 0.0f;
        if (c->obj.ground) {
            c->obj.pos.y = c->obj.mom.y = 0.0f;
            c->jump = 0;
        }
        if (c->jump && c->jump_frame < c->jump_frames) {
            c->obj.mom.y = CrashMoveInfo.JUMPHEIGHT / CrashMoveInfo.JUMPFRAMES2;
        }
        if (!c->obj.ground) {
            c->obj.mom.y += GRAVITY;
        }
        if (c->jump && c->jump_frame < c->jump_frames) {
            c->jump_frame++;
        }
        if (c->jump == 1 && tick <= hold && c->jump_frames < CrashMoveInfo.JUMPFRAMES2) {
            c->jump_frames++;
        } else if (c->jump == 1) {
            c->jump = 3;
        }
        nw_air_move(c);
        if (c->obj.ground) {
            arc.time = tick / (float)hz;
            arc.reach = c->obj.pos.z;
            break;
        }
    }
    return arc;
}

/* Same height and reach in less time, PAL's jump at 60 Hz; the switch let go of mid-air. */
static void jump_fall(void)
{
    NwProfile retail = { 0 }, on = { .movement = { .crash = { .jump_fall = true } } };
    for (int hold = 1; hold <= 100; hold += 99) {
        Arc pal = jump(50, &retail, hold), fast = jump(50, &on, hold), ntsc = jump(60, &on, hold);
        CHECK(near(fast.apex, pal.apex, 0.03f) && near(fast.reach, pal.reach, 0.05f));
        CHECK(fast.time < pal.time * 0.9f);
        CHECK(near(ntsc.apex, fast.apex, 0.03f) && near(ntsc.reach, fast.reach, 0.05f));
        CHECK(fabsf(ntsc.time - fast.time) <= 0.03f);
    }
    CHECK(jump(60, &retail, 100).apex > jump(50, &retail, 100).apex * 1.1f); /* retail's NTSC jump is higher */

    crash_at(60, &on);
    CHECK(CrashMoveInfo.JUMPFRAMES2 == 18 && CrashMoveInfo.STARJUMPFRAMES == 24);
    crash_at(50, &on);
    CHECK(CrashMoveInfo.JUMPFRAMES2 == 15 && CrashMoveInfo.STARJUMPFRAMES == 20);
    crash_at(60, &retail);
    CHECK(CrashMoveInfo.JUMPFRAMES2 == 15 && CrashMoveInfo.STARJUMPFRAMES == 20);

    /* Falling: warped, then back to retail's speeds at the next MoveCRASH once the switch is off. */
    crash_at(50, &on);
    struct creature_s *c = &Character[0];
    c->jump = 3;
    c->allow_jump = 0;
    c->obj.mom.y = -0.05f + GRAVITY; /* MoveCRASH's gravity before the hook */
    c->obj.mom.z = 0.072f;
    GameTimer.frame++;
    nw_air_move(c);
    CHECK(near(c->obj.mom.z, 0.072f * 1.5f, 1e-5f) && near(c->obj.mom.y, -0.05f * 1.5f + 2.25f * GRAVITY, 1e-5f));
    float fall = c->obj.mom.y;
    nw_profile_request(&(NwProfile){ 0 });
    nw_gameplay_swap();
    GameTimer.frame++;
    in_speed = c->obj.mom.z;
    in_f_friction = in_s_friction = 0.0f;
    MoveLoopXZ(&c->obj, &c->obj.thdg);
    CHECK(near(in_speed, c->obj.mom.z, 1e-6f));
    c->obj.mom.y += GRAVITY;
    nw_air_move(c);
    CHECK(near(c->obj.mom.z, 0.072f, 1e-5f) && near(c->obj.mom.y, fall / 1.5f + GRAVITY, 1e-5f));
}

/* One MoveLoopXZ on Crash moving side across and forward along the stick, tilted to MovePlayer's target in_speed,
   with its air friction; the globals come back as they were. */
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

/* Mid-air a half tilt runs, and gets there twice as fast; on the ground it walks. */
static void jump_distance(void)
{
    NwProfile retail = { 0 }, on = { .movement = { .crash = { .jump_distance = true } } };
    float walk = 0.03f, run = 0.072f, step = 0.0072f;
    crash_at(50, &on);
    Character[0].jump = 3;
    CHECK(near(loop_forward(0.0f, walk), 2.0f * step, 1e-5f));
    CHECK(near(loop_forward(0.06f, walk), run, 1e-5f));
    CHECK(near(loop_forward(0.05f, 0.0f), 0.05f - step, 1e-5f)); /* let go: retail */
    Character[0].slam = 1;
    CHECK(near(loop_forward(0.0f, walk), step, 1e-5f));
    Character[0].slam = 0;
    Character[0].obj.ground = 1;
    CHECK(near(loop_forward(0.0f, walk), step, 1e-5f));
    crash_at(50, &retail);
    Character[0].jump = 3;
    CHECK(near(loop_forward(0.0f, walk), step, 1e-5f) && near(loop_forward(0.06f, walk), 0.06f - step, 1e-5f));
}

/* Let go mid-air, Crash slows down a quarter as fast; on the ground, in a belly flop and off, as retail. */
static void air_momentum(void)
{
    NwProfile retail = { 0 }, on = { .movement = { .crash = { .air_momentum = true } } };
    float step = 0.0072f;
    crash_at(50, &on);
    Character[0].jump = 3;
    CHECK(near(loop_forward(0.05f, 0.0f), 0.05f - step * 0.25f, 1e-5f));
    CHECK(near(loop_forward(0.0f, 0.072f), step, 1e-5f)); /* stick held: retail */
    Character[0].jump = 0;
    CHECK(near(loop_forward(0.05f, 0.0f), 0.05f - step * 0.25f, 1e-5f)); /* off a ledge */
    Character[0].slam = 2;
    CHECK(near(loop_forward(0.05f, 0.0f), 0.05f - step, 1e-5f));
    Character[0].slam = 0;
    Character[0].obj.ground = 1;
    CHECK(near(loop_forward(0.05f, 0.0f), 0.05f - step, 1e-5f));
    crash_at(50, &retail);
    CHECK(near(loop_forward(0.05f, 0.0f), 0.05f - step, 1e-5f));
}

/* Forward speed after MoveCRASH's hook ran on Crash's state and then the next frame's MoveLoopXZ: a jump out of a dash
   keeps heading for the dash's speed after retail's coyote window dropped it to a run. */
static float dash_frame(float forward)
{
    nw_air_move(&Character[0]);
    return loop_forward(forward, 0.072f);
}

static void dash_jump(void)
{
    NwProfile retail = { 0 }, on = { .movement = { .crash = { .dash_jump = true } } };
    float step = 0.0072f;
    struct creature_s *c = &Character[0];
    for (int sprint = 0; sprint <= 1; sprint++) {
        crash_at(50, &on);
        c->obj.ground = 1;
        nw_air_move(c);
        GameTimer.frame++;
        c->obj.ground = 0;
        c->jump = 1;
        c->sprint = sprint;
        float after = sprint ? 0.08f + step : 0.08f - step;
        CHECK(near(dash_frame(0.08f), after, 1e-5f));
        c->jump = 3;
        c->jump_frame = 5;
        c->sprint = 0;
        CHECK(near(dash_frame(0.08f), after, 1e-5f));
        c->obj.ground = 1;
        CHECK(near(dash_frame(0.08f), 0.08f - step, 1e-5f)); /* landed */
    }
    crash_at(50, &retail);
    nw_air_move(c);
    GameTimer.frame++;
    c->jump = 1;
    c->sprint = 1;
    CHECK(near(dash_frame(0.08f), 0.08f - step, 1e-5f));
}

/* Faster run and dash at both rates, quicker to speed on the ground but not on ice or in the air, the run animations
   with them. */
static void run_speed(void)
{
    NwProfile retail = { 0 }, on = { .movement = { .crash = { .run_speed = true } } };
    crash_at(60, &on);
    CHECK(CrashMoveInfo.RUNSPEED == 0.060000002f * 1.2f && CrashMoveInfo.SPRINTSPEED == 0.080000006f * 1.2f);
    crash_at(50, &on);
    CHECK(CrashMoveInfo.RUNSPEED == 0.072f * 1.2f && CrashMoveInfo.SPRINTSPEED == 0.096f * 1.2f);
    float run = CrashMoveInfo.RUNSPEED, step = 0.0072f;
    Character[0].obj.ground = 1;
    CHECK(near(loop_forward(0.0f, run), step * 1.5f, 1e-5f));
    CHECK(near(loop_forward(0.05f, 0.0f), 0.05f - step, 1e-5f)); /* stopping: retail */
    Character[0].obj.surface_type = 1;                          /* ice */
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
    crash_at(50, &retail);
    CHECK(CrashMoveInfo.RUNSPEED == 0.072f && CrashMoveInfo.SPRINTSPEED == 0.096f);
    Character[0].obj.model = &crash;
    nw_ground_animate(&Character[0]);
    CHECK(run_anim.speed == 1.3f);
}

/* Crash's speed across the stick after one MoveLoopXZ from side, stick let go. */
static float loop_side(float side)
{
    loop(side, 0.0f, 0.0f);
    return Character[0].obj.mom.x;
}

/* On the ground a turn's leftover speed and a stop or turn round brake harder; speeding up, ice and the air stay
   retail's. */
static void grip(void)
{
    NwProfile retail = { 0 }, on = { .movement = { .crash = { .grip = true } } };
    float step = 0.0072f;
    crash_at(50, &on);
    Character[0].obj.ground = 1;
    CHECK(near(loop_side(0.05f), 0.05f - 3.0f * step, 1e-5f));
    CHECK(near(loop_forward(0.05f, 0.0f), 0.05f - 2.0f * step, 1e-5f));
    CHECK(near(loop_forward(-0.05f, 0.072f), -0.05f + 2.0f * step, 1e-5f)); /* turning round */
    CHECK(near(loop_forward(0.0f, 0.072f), step, 1e-5f));
    Character[0].obj.surface_type = 1; /* ice: MovePlayer's own friction for it, untouched */
    CHECK(near(loop_side(0.05f), 0.05f - step, 1e-5f));
    Character[0].obj.surface_type = 0;
    Character[0].obj.ground = 0;
    CHECK(near(loop_side(0.05f), 0.05f - step, 1e-5f));
    crash_at(50, &retail);
    Character[0].obj.ground = 1;
    CHECK(near(loop_side(0.05f), 0.05f - step, 1e-5f) && near(loop_forward(0.05f, 0.0f), 0.05f - step, 1e-5f));
}

/* 0.4 s of spin at both rates; a value someone else wrote stays. */
static void long_spin(void)
{
    NwProfile retail = { 0 }, on = { .movement = { .crash = { .long_spin = true } } };
    crash_at(50, &on);
    CHECK(CrashMoveInfo.SPINFRAMES == 20);
    crash_at(60, &on);
    CHECK(CrashMoveInfo.SPINFRAMES == 24);
    crash_at(60, &retail);
    CHECK(CrashMoveInfo.SPINFRAMES == 16);
    CrashMoveInfo.SPINFRAMES = 30;
    nw_profile_apply(&on);
    CHECK(CrashMoveInfo.SPINFRAMES == 30);
    CrashMoveInfo.SPINFRAMES = 16;
}

static NwTerI contact;

/* A contact of hittype type at a normal y up (world space), centre above it when below; true when it was made a
   floor. */
static bool held(short type, float y, bool below, const unsigned char *flags)
{
    memset(&contact, 0, sizeof(contact));
    contact.hittype = type;
    contact.uhitnorm = (struct nuvec_s){ sqrtf(1.0f - y * y), below ? y : -y, 0.0f };
    contact.hitnorm = contact.uhitnorm;
    nw_edges_impact(flags);
    return contact.uhitnorm.y == 1.0f && contact.hitnorm.y == 1.0f && contact.uhitnorm.x == 0.0f;
}

/* Edges and corners under Crash hold him up to three quarters of his radius out, and keep holding while he stands
   still; faces, other movers and retail stay as they were. */
static void edge_grip(void)
{
    NwProfile retail = { 0 }, on = { .movement = { .crash = { .edge_grip = true } } };
    NwTerI *was = TerI;
    unsigned char *own = Character[0].obj.gndflags.chrs, other[2] = { 0 };
    TerI = &contact;
    crash_at(50, &on);
    CHECK(held(2, 0.5f, true, own) && held(3, 0.5f, true, own) && held(0x13, 0.45f, true, own));
    CHECK(!held(1, 0.5f, true, own) && !held(2, 0.3f, true, own) && !held(2, 0.5f, false, own));
    CHECK(!held(2, 0.5f, true, other));
    CHECK(!held(2, 0.8f, true, own) && contact.uhitnorm.y == 0.8f); /* a floor already */

    /* Still on a held rim: MovePlayer's release of the ground is undone; after a face, retail's. */
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
    crash_at(50, &retail);
    CHECK(!held(2, 0.5f, true, own));
    TerI = was;
}

/* A side-on rail along z, 2 wide: left spline at x 0, right at x 2. */
static struct nuvec_s rail_left[2] = { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 10.0f } };
static struct nuvec_s rail_right[2] = { { 2.0f, 0.0f, 0.0f }, { 2.0f, 0.0f, 10.0f } };
static struct nugspline_s spline_left = { 2, sizeof(struct nuvec_s), "left", (char *)rail_left };
static struct nugspline_s spline_right = { 2, sizeof(struct nuvec_s), "right", (char *)rail_right };

/* Crash at x on the rail in camera mode with momentum mom_x across it; his momentum across after the hold. */
static float line_frame(float x, u8 mode, float mom_x)
{
    struct creature_s *c = &Character[0];
    c->obj.pos = (struct nuvec_s){ x, 0.0f, 5.0f };
    c->obj.mom = (struct nuvec_s){ mom_x, 0.0f, 0.05f };
    c->obj.RPos = (RailPosition){ .iRAIL = 0, .iALONG = 0, .fALONG = 0.5f, .fACROSS = x / 2.0f, .mode = mode };
    nw_line2d_hold(c);
    CHECK(near(c->obj.mom.z, 0.05f, 1e-5f));
    return c->obj.mom.x;
}

/* Side-on, the drift across goes and a small pull brings him back to where he came in; elsewhere and off, retail. */
static void line_2d(void)
{
    NwProfile retail = { 0 }, on = { .movement = { .crash = { .line_2d = true } } };
    Rail[0] = (struct rail_s){ .pLEFT = &spline_left, .pRIGHT = &spline_right, .edges = 2 };
    crash_at(50, &on);
    CHECK(fabsf(line_frame(1.0f, 4, 0.03f)) < 1e-6f);
    CHECK(near(line_frame(1.04f, 8, 0.03f), -0.04f * 0.25f, 1e-4f));
    CHECK(near(line_frame(1.6f, 4, 0.0f), -0.02f, 1e-5f));    /* at most 1 a second */
    CHECK(line_frame(1.6f, 1, 0.03f) == 0.03f);                /* camera behind: free */
    CHECK(fabsf(line_frame(1.6f, 4, 0.03f)) < 1e-6f);          /* back side-on: the new line */
    crash_at(50, &retail);
    CHECK(line_frame(1.6f, 4, 0.03f) == 0.03f);
    Rail[0] = (struct rail_s){ 0 };
}

/* A creature that can hurt Crash while he can't hurt it reaches less far, and back after the test; fire and steam
   count half his radius. */
static void hazard_hitboxes(void)
{
    NwProfile retail = { 0 }, on = { .movement = { .crash = { .hazard_hitboxes = true } } };
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
    crash->attack = 2; /* spinning: its full size, so his reach stays */
    nw_hazards_shrink(crash);
    CHECK(enemy.RADIUS == 0.5f);
    nw_hazards_restore();
    crash->attack = 0;
    nw_hazards_shrink(&harmless);
    CHECK(enemy.RADIUS == 0.5f);
    nw_hazards_restore();
    CHECK(nw_hazards_particle(0.225f) == 0.225f * 0.5f);
    crash_at(50, &retail);
    nw_hazards_shrink(crash);
    CHECK(enemy.RADIUS == 0.5f && nw_hazards_particle(0.225f) == 0.225f);
    nw_hazards_restore();
    pObj[0] = pObj[1] = NULL;
    GAMEOBJECTCOUNT = 0;
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
    edge_grip();
    line_2d();
    hazard_hitboxes();

    CHECK(nw_movement_profile(&(NwMovement){ 0 }) == NW_MOVEMENT_PROFILE_RETAIL);
    CHECK(nw_movement_profile(&(NwMovement)NW_MOVEMENT_RETAIL) == NW_MOVEMENT_PROFILE_RETAIL);
    CHECK(nw_movement_profile(&(NwMovement)NW_MOVEMENT_NHANCED) == NW_MOVEMENT_PROFILE_NHANCED);
    CHECK(nw_movement_profile(&(NwMovement){ .monkey_bar_speed = 200 }) == NW_MOVEMENT_PROFILE_CUSTOM);
    CHECK(nw_movement_profile(&(NwMovement){ .monkey_bar_speed = 350 }) == NW_MOVEMENT_PROFILE_CUSTOM);
    CHECK(nw_movement_profile(&(NwMovement){ .crash = { .jump_fall = true } }) == NW_MOVEMENT_PROFILE_CUSTOM);

    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    return 0;
}
