/* The port's .gsc loader (src/port/nugscn.c): what it keeps outside the scene buffer. */
#ifndef NUWORLD_PORT_NUGSCN_H
#define NUWORLD_PORT_NUGSCN_H

struct nugscn_s;

/* Frees the geom meshes and sprites (dmastream) of every scene loaded so far, on the render thread. */
void nw_gscn_free_meshes(void);
/* NuGScnRemove, then the scene's meshes, sprites and textures. */
void nw_gscn_remove(struct nugscn_s *scene);

#endif
