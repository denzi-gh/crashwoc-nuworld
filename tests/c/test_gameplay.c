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

static void monkey_bars(int hz, float retail, float fast)
{
    nw_hz_set(hz);
    CrashMoveInfo.DANGLESPEED = CocoMoveInfo.DANGLESPEED = retail;
    nw_profile_apply(&(NwProfile){ 0 });
    CHECK(CrashMoveInfo.DANGLESPEED == retail && CocoMoveInfo.DANGLESPEED == retail);
    nw_profile_apply(&(NwProfile){ .fast_monkey_bars = true });
    CHECK(CrashMoveInfo.DANGLESPEED == fast && CocoMoveInfo.DANGLESPEED == fast);
    /* Not retail's any more: a second apply keeps it. */
    nw_profile_apply(&(NwProfile){ .fast_monkey_bars = true });
    CHECK(CrashMoveInfo.DANGLESPEED == fast && CocoMoveInfo.DANGLESPEED == fast);
}

int main(void)
{
    monkey_bars(50, 0.015599999f, 0.031199998f);
    monkey_bars(60, 0.013f, 0.026f);

    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    return 0;
}
