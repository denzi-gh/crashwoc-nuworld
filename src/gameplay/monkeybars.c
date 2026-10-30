#include "gameplay/monkeybars.h"

#include <math.h>
#include <stddef.h>

#include "game/creature.h"
#include "gameplay/gameplay.h"
#include "nu_port.h"

#define NW_HNGABOUT 0x20 /* hanging still */
#define NW_HNGWALK 0x4D  /* hand over hand */

extern struct MoveInfo CrashMoveInfo;
extern struct MoveInfo CocoMoveInfo;

static int nw_monkey_bars_speed = 100; // <- actual speed modifier
static float nw_monkey_bars_rate = 1.0f; /* what hngwalk's speed holds */

/* Retail's DANGLESPEED at this rate, at speed percent. */
static float nw_monkey_bars_value(int speed)
{
    return NW_HZ(0.015599999f, 0.013f) * ((float)speed / 100.0f);
}

/* nw_profile's speed, 100 when it is retail's. */
static int nw_monkey_bars_target(void)
{
    int speed = nw_profile.movement.monkey_bar_speed;
    if (speed <= 100) {
        return 100;
    }
    NW_DEVIATION(movement.monkey_bar_speed);
    return speed;
}

/* DANGLESPEED is MovePlayer's speed on the bars. */
void nw_monkey_bars_apply(void)
{
    int speed = nw_monkey_bars_target();
    float was = nw_monkey_bars_value(nw_monkey_bars_speed), now = nw_monkey_bars_value(speed);
    struct MoveInfo *infos[] = { &CrashMoveInfo, &CocoMoveInfo };
    for (int i = 0; i < 2; i++) {
        if (infos[i]->DANGLESPEED == was) {
            infos[i]->DANGLESPEED = now;
        }
    }
    nw_monkey_bars_speed = speed;
}

/* The arms' rate grows as the speed to this power: 1 keeps the hands on the bar, 0 is retail's rate. */
#define NW_MONKEY_BARS_CADENCE 0.6f

/* hngwalk's rate from Crash's speed this frame */
void nw_monkey_bars_animate(struct creature_s *plr)
{
    int speed = nw_monkey_bars_target();
    float rate = 1.0f;
    if (speed > 100 && plr->obj.dangle == 1) {
        float moved = plr->obj.xz_distance / nw_monkey_bars_value(100);
        if (plr->obj.anim.newaction == NW_HNGABOUT && moved > 1.0f) {
            plr->obj.anim.newaction = NW_HNGWALK;
        }
        if (plr->obj.anim.newaction == NW_HNGWALK) {
            moved = moved < 1.0f ? 1.0f : moved > speed / 100.0f ? speed / 100.0f : moved;
            rate = powf(moved, NW_MONKEY_BARS_CADENCE);
        }
    }
    struct animlist *walk = plr->obj.model ? plr->obj.model->animlist[NW_HNGWALK] : NULL;
    if (walk && rate != nw_monkey_bars_rate && walk->speed == nw_monkey_bars_rate) {
        walk->speed = rate;
        nw_monkey_bars_rate = rate;
    }
}
