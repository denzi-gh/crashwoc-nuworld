/* The port's .gsc loader (src/port/nugscn.c): what it keeps outside the scene buffer. */
#ifndef NUWORLD_PORT_NUGSCN_H
#define NUWORLD_PORT_NUGSCN_H

struct nugscn_s;

/* NuGScnRemove, then the scene's textures and its load (src/port/heap.h): records, meshes and sprites. A scene the
   game drops without it goes when a later load writes over its heap range. */
void nw_gscn_remove(struct nugscn_s *scene);

#endif
