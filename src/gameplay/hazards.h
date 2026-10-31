/* What can kill Crash: hazard_hitboxes. */
#ifndef NUWORLD_GAMEPLAY_HAZARDS_H
#define NUWORLD_GAMEPLAY_HAZARDS_H

struct obj_s;

/* PlayerCreatureCollisions around retail. */
void nw_hazards_shrink(struct obj_s *obj);
void nw_hazards_restore(void);
/* DebrisCollisionCheck's radius. */
float nw_hazards_particle(float radius);

#endif
