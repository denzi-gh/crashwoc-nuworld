/* The port's .gsc loader (src/port/nugscn.c): what it keeps outside the scene buffer. */
#ifndef NUWORLD_PORT_NUGSCN_H
#define NUWORLD_PORT_NUGSCN_H

/* Frees the decoded geom meshes (dmastream) of every scene loaded so far. */
void nw_gscn_free_meshes(void);

#endif
