/* src/gameplay: the profile's switches and the retail values they change. */
#include <stdio.h>

#include "game/creature.h"
#include "gameplay/gameplay.h"
#include "gameplay/monkeybars.h"
#include "port/hz.h"

extern struct MoveInfo CrashMoveInfo;
extern struct MoveInfo CocoMoveInfo;

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
    nw_movement_request(&(NwMovement){ .monkey_bar_speed = 150 });
    CHECK(dangle(retail * 3.5f));
    nw_gameplay_swap();
    CHECK(dangle(retail * 1.5f));
    /* A value someone else wrote stays. */
    CocoMoveInfo.DANGLESPEED = 1.0f;
    nw_movement_request(&(NwMovement){ .monkey_bar_speed = 200 });
    nw_gameplay_swap();
    CHECK(CrashMoveInfo.DANGLESPEED == retail * 2.0f && CocoMoveInfo.DANGLESPEED == 1.0f);
    CocoMoveInfo.DANGLESPEED = retail * 2.0f;
    nw_movement_request(&(NwMovement)NW_MOVEMENT_RETAIL);
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

int main(void)
{
    monkey_bars(50, 0.015599999f);
    monkey_bars(60, 0.013f);
    monkey_bar_anim();

    CHECK(nw_movement_profile(&(NwMovement){ 0 }) == NW_MOVEMENT_PROFILE_RETAIL);
    CHECK(nw_movement_profile(&(NwMovement)NW_MOVEMENT_RETAIL) == NW_MOVEMENT_PROFILE_RETAIL);
    CHECK(nw_movement_profile(&(NwMovement)NW_MOVEMENT_NHANCED) == NW_MOVEMENT_PROFILE_NHANCED);
    CHECK(nw_movement_profile(&(NwMovement){ .monkey_bar_speed = 200 }) == NW_MOVEMENT_PROFILE_CUSTOM);

    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    return 0;
}
