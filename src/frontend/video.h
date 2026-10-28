/* [video] in the game window: the F1 Video panel over the settings, and what changes applied. Main thread. */
#ifndef NUWORLD_FRONTEND_VIDEO_H
#define NUWORLD_FRONTEND_VIDEO_H

#include "frontend/settings.h"
#include "platform/platform.h"

/* The window as s has it, the panel and F11 over s; returns the picture's aspect for nw_wide_set. */
float nw_video_start(NwPlatform *p, NwSettings *s);
/* A drag that resizes the window becomes its resolution. */
void nw_video_event(const SDL_Event *e);
/* What changed in s since was, applied. */
void nw_video_apply(const NwSettings *s, const NwSettings *was);

#endif
