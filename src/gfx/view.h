/* The --scene viewer: the game thread's scene (src/port/scene.c) shown under a fly camera. */
#ifndef NUWORLD_GFX_VIEW_H
#define NUWORLD_GFX_VIEW_H

#include "platform/platform.h"

/* Runs until the window closes, the game thread stops or max_frames (> 0) frames; returns the exit code. Without a
   name, it opens on the scene list. */
int nw_view_scene(NwPlatform *p, const char *name, int max_frames);

#endif
