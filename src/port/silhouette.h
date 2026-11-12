/* The intro's last cut (levels\b\intro2\control.cut) opens on a dark door going up on Crash, Coco and Aku Aku
   standing against the light: on the PS2 a picture of their shapes (the Silhouette card of control.gsc) while the
   models wait out of sight. With the silhouettes option the models stand there instead, drawn black against the
   scene's own light, and the picture is left out (gfx/backend.c). */
#ifndef NUWORLD_PORT_SILHOUETTE_H
#define NUWORLD_PORT_SILHOUETTE_H

#include <stdbool.h>

struct nugscn_s;
struct NUGCUTSCENE_s;
union variptr_u;

/* As a scene loads: control.gsc's Silhouette card marked (NwGpuMesh.heroes_picture). */
void nw_silhouette_mark(struct nugscn_s *scene, const char *path);
/* As a cutscene loads: whether it is control.cut, and if so its heroes' idles loaded after it on the heap. */
void nw_silhouette_cut(const char *path, const struct NUGCUTSCENE_s *def, union variptr_u *heap,
                       union variptr_u *heapend);
/* While a character draws as a silhouette: its draws are marked (NwCmd.silhouette). Game thread. */
extern bool nw_silhouette_drawing;

#endif
