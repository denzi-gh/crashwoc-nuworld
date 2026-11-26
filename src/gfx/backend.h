/* Render thread, with a current GL 3.3 context. */
#ifndef NUWORLD_GFX_BACKEND_H
#define NUWORLD_GFX_BACKEND_H

#include <stdbool.h>

#include "gfx/packet.h"

#define NW_BACKEND_SCALE_MAX 8

typedef struct NwRenderOptions {
    int scale; /* every surface's pixels across a GS pixel, 1-NW_BACKEND_SCALE_MAX; 0 = auto: the shown height */
    int msaa;  /* samples of the back buffer and the targets under its Z: 0 off, 2, 4 or 8, at most the GPU's */
    NwTexFilter filter;
    bool gems;
    bool clouds;      /* the geoms marked as clouds drawn as volumes (gfx/cloudvol.h) instead of their flat cards */
    bool silhouettes; /* control.cut's heroes as their models against a light, not the picture (port/silhouette.h) */
} NwRenderOptions;

bool nw_backend_init(void);
/* Wireframe and Screenshot, registered after the caller's own debug menus. */
void nw_backend_debug_menu(void);
void nw_backend_shutdown(void);
/* Taken at the next frame. */
void nw_backend_set_options(const NwRenderOptions *o);
int nw_backend_scale(void);
int nw_backend_msaa(void);
/* now >= 0 (clock edges) blends the newest packet from the one before; NULL before the first packet. */
const NwPacket *nw_backend_frame(int width, int height, double now);
/* The back buffer at the scale, without the overlay; false with SDL's error. */
bool nw_backend_save(const char *path);

#endif
