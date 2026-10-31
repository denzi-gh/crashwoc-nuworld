/* The submarine: turning, speed, hitbox, weapons. */
#ifndef NUWORLD_GAMEPLAY_SUBMARINE_H
#define NUWORLD_GAMEPLAY_SUBMARINE_H

struct creature_s;
struct nuvec_s;
struct obj_s;

void nw_sub_apply(void);
/* MovePlayer, before retail. */
void nw_sub_move(const struct creature_s *c);
/* MoveSUBMARINE, before retail: after MovePlayer's heading, before the torpedo's. */
void nw_sub_turn(struct creature_s *plr);
/* MoveSUBMARINE around retail, and AddProjectile's mom (mom or *out) while it fires. */
void nw_sub_fire_before(const struct creature_s *plr);
void nw_sub_fire_after(struct creature_s *plr);
struct nuvec_s *nw_sub_launch(int type, struct nuvec_s *mom, const struct obj_s *owner, struct nuvec_s *out);
/* ObjectRotation around retail. */
void nw_sub_pitch_before(const struct obj_s *obj);
void nw_sub_pitch(struct obj_s *obj, int mode);
/* NewTerrainScaleY, before retail: MovePlayer has stepped the sub's speed. */
void nw_sub_accel(struct nuvec_s *vvel);
/* PlayerCreatureCollisions, MoveCreature and UpdateProjectiles around retail. */
void nw_sub_shrink(void);
void nw_sub_restore(void);
/* DebrisCollisionCheck's radius. */
float nw_sub_particle(float radius);

#endif
