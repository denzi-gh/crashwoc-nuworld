/* [game.movement.crash] hazard_hitboxes: what can kill Crash reaches about as far as it is drawn, part of N. Hanced
   Movement. */
#ifndef NUWORLD_GAMEPLAY_HAZARDS_H
#define NUWORLD_GAMEPLAY_HAZARDS_H

struct obj_s;

/* PlayerCreatureCollisions around retail. */
void nw_hazards_shrink(struct obj_s *obj);
void nw_hazards_restore(void);
/* DebrisCollisionCheck's radius for Crash against fire, steam and the like. */
float nw_hazards_particle(float radius);

#endif
