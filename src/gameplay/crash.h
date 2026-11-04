#ifndef NUWORLD_GAMEPLAY_CRASH_H
#define NUWORLD_GAMEPLAY_CRASH_H

#include <stdbool.h>

#include "gameplay/gameplay.h"

struct creature_s;

/* Crash, not Coco, out of vehicles and water. */
bool nw_crash_on_foot(const struct creature_s *c);
/* Coco, the same. */
bool nw_coco_on_foot(const struct creature_s *c);

/* Crash's options for Crash on foot, Coco's for Coco, all off for anyone else. */
const NwCrashMovement *nw_foot(const struct creature_s *c);
#define NW_FOOT_DEVIATION(c, sw) \
    ((c) == &nw_profile.movement.crash ? NW_DEVIATION(movement.crash.sw) : NW_DEVIATION(movement.coco.sw))

#endif
