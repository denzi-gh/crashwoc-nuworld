#include "gameplay/monkeybars.h"

#include "game/creature.h"
#include "gameplay/gameplay.h"
#include "nu_port.h"

extern struct MoveInfo CrashMoveInfo;
extern struct MoveInfo CocoMoveInfo;

static int nw_monkey_bars_speed = 100; // <- actual speed modifier

/* Retail's DANGLESPEED at this rate, at speed percent. */
static float nw_monkey_bars_value(int speed)
{
    return NW_HZ(0.015599999f, 0.013f) * ((float)speed / 100.0f);
}

/* DANGLESPEED is MovePlayer's speed on the bars. */
void nw_monkey_bars_apply(void)
{
    int speed = nw_profile.movement.monkey_bar_speed;
    if (speed > 100) {
        NW_DEVIATION(movement.monkey_bar_speed);
    } else {
        speed = 100;
    }
    float was = nw_monkey_bars_value(nw_monkey_bars_speed), now = nw_monkey_bars_value(speed);
    struct MoveInfo *infos[] = { &CrashMoveInfo, &CocoMoveInfo };
    for (int i = 0; i < 2; i++) {
        if (infos[i]->DANGLESPEED == was) {
            infos[i]->DANGLESPEED = now;
        }
    }
    nw_monkey_bars_speed = speed;
}
