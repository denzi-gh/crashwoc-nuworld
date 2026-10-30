#ifndef NUWORLD_GAMEPLAY_CRASH_H
#define NUWORLD_GAMEPLAY_CRASH_H

#include <stdbool.h>

struct creature_s;

/* Crash, not Coco, out of vehicles and water. */
bool nw_crash_on_foot(const struct creature_s *c);

#endif
