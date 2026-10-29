/* [video] in the game window: the F1 Video panel over the settings, and what changes applied. Main thread. */
#ifndef NUWORLD_FRONTEND_VIDEO_H
#define NUWORLD_FRONTEND_VIDEO_H

#include "frontend/settings.h"
#include "platform/platform.h"

/* The window as s has it, the panel and F11 over s; returns the picture's aspect for nw_wide_set. */
float nw_video_start(NwPlatform *p, NwSettings *s);
/* A new window shape the auto aspect follows; a drag that resizes the window becomes its resolution. */
void nw_video_event(const SDL_Event *e);
/* The frame cap the window loop keeps: s's, else the display's refresh when VSync is off; 0 none (VSync paces). */
unsigned nw_video_fps(const NwSettings *s);
/* What changed in s since was, applied. */
void nw_video_apply(const NwSettings *s, const NwSettings *was);

#endif
