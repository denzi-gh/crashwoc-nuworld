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

/* Registered by the module that owns the item, dropped by nw_debug_shutdown. */
typedef void (*NwDebugFn)(void *user);

void nw_debug_panel(const char *menu, const char *title, NwDebugFn draw, void *user, bool open);

void nw_debug_text(SDL_PRINTF_FORMAT_STRING const char *fmt, ...) SDL_PRINTF_VARARG_FUNC(1);
bool nw_debug_button(const char *label);
bool nw_debug_slider(const char *label, float *value, float lo, float hi, bool log);

#ifdef __cplusplus
}
#endif

#endif
