/* The --scene viewer: a .gsc's visible instances drawn with the VU programs under a fly camera. */
#ifndef NUWORLD_GFX_VIEW_H
#define NUWORLD_GFX_VIEW_H

#include "gfx/gsc.h"
#include "platform/platform.h"

/* Runs until the window closes or max_frames (> 0) frames; returns the exit code. */
int nw_view_scene(NwPlatform *p, const NwGsc *g, const char *name, int max_frames);

#endif
