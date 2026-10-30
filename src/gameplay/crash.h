/* [game.movement.crash]: what Crash's N. Hanced Movement switches have in common. */
#ifndef NUWORLD_GAMEPLAY_CRASH_H
#define NUWORLD_GAMEPLAY_CRASH_H

#include <stdbool.h>

struct creature_s;

/* Crash himself (not Coco) on foot: no vehicle, not swimming. */
bool nw_crash_on_foot(const struct creature_s *c);

#endif
