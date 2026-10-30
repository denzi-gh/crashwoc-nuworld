/* src/gameplay: the profile's switches and the retail values they change. */
#include <stdio.h>

#include "game/creature.h"
#include "gameplay/gameplay.h"
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

int main(void)
{
    monkey_bars(50, 0.015599999f);
    monkey_bars(60, 0.013f);

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
