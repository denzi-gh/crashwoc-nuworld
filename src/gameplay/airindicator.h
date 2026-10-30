/* [game] air_indicator: a ring on the floor straight under Crash while he is in the air, where he lands. */
#ifndef NUWORLD_GAMEPLAY_AIRINDICATOR_H
#define NUWORLD_GAMEPLAY_AIRINDICATOR_H

#include <stdbool.h>

struct creature_s;

/* The floor height the ring lies at for c this frame and its alpha (0-1); false for no ring. Reads only. */
bool nw_air_indicator_at(const struct creature_s *c, float *y, float *alpha);
/* DrawCreatures' main pass for Character[0], after retail drew him. */
void nw_air_indicator_draw(struct creature_s *c);
/* NuMtlRelease (a level load) freed the ring's material. */
void nw_air_indicator_release(void);

#endif
