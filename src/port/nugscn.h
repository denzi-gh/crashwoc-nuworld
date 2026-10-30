/* The port's .gsc loader (src/port/nugscn.c): what it keeps outside the scene buffer. */
#ifndef NUWORLD_PORT_NUGSCN_H
#define NUWORLD_PORT_NUGSCN_H

struct nugscn_s;

/* NuGScnRemove plus the scene's textures and host records, meshes and sprites; a scene the game drops without it is
   freed when a later load overwrites its heap range. */
void nw_gscn_remove(struct nugscn_s *scene);

#endif
