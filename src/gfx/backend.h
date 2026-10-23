/* The renderer: the newest frame packet drawn with the VU programs into a GS-sized framebuffer and shown 4:3 in the
   window. Render thread, with a current GL 3.3 context. */
#ifndef NUWORLD_GFX_BACKEND_H
#define NUWORLD_GFX_BACKEND_H

#include <stdbool.h>

#include "gfx/packet.h"

bool nw_backend_init(void);
void nw_backend_shutdown(void);
/* Runs the GPU jobs due, draws the newest packet (or the last one again) and shows it in a width x height window;
   returns the packet drawn, NULL before the first. */
const NwPacket *nw_backend_frame(int width, int height);

#endif
