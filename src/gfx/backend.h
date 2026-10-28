/* The renderer: the newest frame packet drawn with the VU programs into a GS-sized framebuffer and shown 4:3 in the
   window. Render thread, with a current GL 3.3 context. Its options start as retail's. */
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
/* Runs the GPU jobs due, draws the newest packet (or the last one again) and shows it in a width x height window;
   returns the packet drawn, NULL before the first. */
const NwPacket *nw_backend_frame(int width, int height);
/* The back buffer at its own size (the GS frame's at the scale), without the overlay, as a PNG; false with SDL's
   error. */
bool nw_backend_save(const char *path);

#endif
