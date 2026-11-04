/* What can kill Crash or the mech: hazard_hitboxes, the mech's hitbox. */
#ifndef NUWORLD_GAMEPLAY_HAZARDS_H
#define NUWORLD_GAMEPLAY_HAZARDS_H

struct obj_s;

/* PlayerCreatureCollisions around retail. */
void nw_hazards_shrink(struct obj_s *obj);
void nw_hazards_restore(void);
/* DebrisCollisionCheck's radius. */
float nw_hazards_particle(float radius);
/* UpdateChase, before retail: obj's radius against the chasers; returns retail's to put back after. */
float nw_hazards_chase(struct obj_s *obj);
/* The radius the hitbox view draws for the player: what the hazards meet. */
float nw_hazards_radius(const struct obj_s *obj);

#endif
