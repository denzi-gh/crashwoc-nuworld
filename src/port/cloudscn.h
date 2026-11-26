/* The intro's storm (levels\b\intro\volcano.gsc under sunny.cut, then storm.cut): its cloud cards (Cloud1 growing over
   the island, SCloud passing the sun) marked for the renderer to draw as volumes, its sky domes to get a living sky
   of their own and its sun card to give way to that sky's sun, when the clouds option is on (src/gfx/cloudvol.c);
   and how far the storm is, from the cutscenes' time. Game thread. */
#ifndef NUWORLD_PORT_CLOUDSCN_H
#define NUWORLD_PORT_CLOUDSCN_H

#include <stdbool.h>

struct nugscn_s;
struct NUGCUTSCENE_s;

/* Marks the geoms of a scene the game just loaded (path: as the game opens it); other scenes stay as they are. */
void nw_cloudscn_mark(struct nugscn_s *scene, const char *path);
/* Notes a cutscene the game just loaded (path: as the game opens it), so that its running is known. */
void nw_cloudscn_cut(const char *path, const struct NUGCUTSCENE_s *def);
/* While sunny.cut or storm.cut runs: true, with *storm from 0 (a clear sky) to 1 (overcast). */
bool nw_cloudscn_storm(float *storm);

#endif
