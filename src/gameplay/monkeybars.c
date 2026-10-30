#include "gameplay/monkeybars.h"

#include "game/creature.h"
#include "gameplay/gameplay.h"
#include "nu_port.h"

#define NW_MONKEY_BAR_SPEED 3.5f // <- the speed multiplier for fast_monkey_bars

extern struct MoveInfo CrashMoveInfo;
extern struct MoveInfo CocoMoveInfo;

/* DANGLESPEED is MovePlayer's speed on the bars.*/
void nw_monkey_bars_apply(void)
{
    if (!nw_profile.fast_monkey_bars) {
        return;
    }
    NW_DEVIATION(fast_monkey_bars);
    struct MoveInfo *infos[] = { &CrashMoveInfo, &CocoMoveInfo };
    for (int i = 0; i < 2; i++) {
        if (infos[i]->DANGLESPEED == NW_HZ(0.015599999f, 0.013f)) {
            infos[i]->DANGLESPEED *= NW_MONKEY_BAR_SPEED;
        }
    }
}
