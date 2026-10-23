/* F1 debug overlay of the --scene and --boot windows. Main thread only. */
#ifndef NUWORLD_FRONTEND_DEBUG_H
#define NUWORLD_FRONTEND_DEBUG_H

#include <stdbool.h>

#include <SDL3/SDL.h>

#include "platform/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

bool nw_debug_init(NwPlatform *p);
void nw_debug_shutdown(void);

/* True when the window should ignore the event. */
bool nw_debug_event(const SDL_Event *e);
bool nw_debug_wants_keyboard(void);

void nw_debug_frame(void);

#ifdef __cplusplus
}
#endif

#endif
