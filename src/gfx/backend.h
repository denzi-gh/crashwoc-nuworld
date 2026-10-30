/* The renderer: the newest packet drawn GS-sized and shown 4:3, on the render thread with a current GL 3.3 context. */
#ifndef NUWORLD_GFX_BACKEND_H
#define NUWORLD_GFX_BACKEND_H

#include <stdbool.h>

#include "gfx/packet.h"

#define NW_BACKEND_SCALE_MAX 8

typedef struct NwRenderOptions {
    int scale; /* every surface's pixels across a GS pixel, 1-NW_BACKEND_SCALE_MAX; 0 = auto: the shown height */
    int msaa;  /* samples of the back buffer and the targets under its Z: 0 off, 2, 4 or 8, at most the GPU's */
    NwTexFilter filter;
} NwRenderOptions;

bool nw_backend_init(void);
void nw_backend_shutdown(void);
/* Taken at the next frame. */
void nw_backend_set_options(const NwRenderOptions *o);
/* The scale the last frame was drawn at. */
int nw_backend_scale(void);
/* Its MSAA samples, 0 without. */
int nw_backend_msaa(void);
/* Runs the due GPU jobs, draws and shows the newest packet (NULL before the first); now >= 0 (clock edges) blends it
   from the one before (src/gfx/interp.h). */
const NwPacket *nw_backend_frame(int width, int height, double now);
/* The back buffer at the scale, without the overlay, as a PNG; false with SDL's error. */
bool nw_backend_save(const char *path);

#endif
