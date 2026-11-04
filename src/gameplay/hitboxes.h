/* Render > Hitboxes (NwProfile.hitboxes): every collision shape the game tests this frame, over the scene. */
#ifndef NUWORLD_GAMEPLAY_HITBOXES_H
#define NUWORLD_GAMEPLAY_HITBOXES_H

struct chase_s;
struct nuvec_s;

/* UpdateChase, after retail: its chasers. */
void nw_hitboxes_chase(const struct chase_s *chase);
/* DebrisCollisionCheck: a particle's sphere. */
void nw_hitboxes_particle(const struct nuvec_s *pos, float radius);
/* DrawCreatures' main pass for Character[0]. */
void nw_hitboxes_draw(void);
/* NuMtlRelease freed the material. */
void nw_hitboxes_release(void);

#endif
