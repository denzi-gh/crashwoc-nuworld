/* F1 debug overlay of the --scene and --boot windows. Main thread only. */
#ifndef NUWORLD_FRONTEND_DEBUG_H
#define NUWORLD_FRONTEND_DEBUG_H

#include <stdbool.h>

#include <SDL3/SDL.h>

#include "platform/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

bool nw_debug_init(NwPlatform *p, const char *data_root);
void nw_debug_shutdown(void);

/* True when the window should ignore the event. */
bool nw_debug_event(const SDL_Event *e);
bool nw_debug_wants_keyboard(void);
void nw_debug_show(bool shown);

void nw_debug_frame(void);

/* Registered by the module that owns the item, dropped by nw_debug_shutdown. */
typedef void (*NwDebugFn)(void *user);

void nw_debug_panel(const char *menu, const char *title, NwDebugFn draw, void *user, bool open);
void nw_debug_toggle(const char *menu, const char *label, bool *value);
/* key (or SDL_SCANCODE_UNKNOWN) also works while the overlay is hidden. */
void nw_debug_action(const char *menu, const char *label, SDL_Scancode key, NwDebugFn run, void *user);

/* <data root><folder>/<date-time>.<ext>; SDL_free it. */
char *nw_debug_file(const char *folder, const char *ext);

void nw_debug_text(SDL_PRINTF_FORMAT_STRING const char *fmt, ...) SDL_PRINTF_VARARG_FUNC(1);
/* Text level with the buttons on its line. */
void nw_debug_label(const char *text);
bool nw_debug_button(const char *label);
/* The next item on this line, column font sizes from the window's left edge. */
void nw_debug_same_line(float column);
bool nw_debug_slider(const char *label, float *value, float lo, float hi, bool log);
bool nw_debug_slider_int(const char *label, int *value, int lo, int hi);
bool nw_debug_combo(const char *label, int *index, const char *const *items, int count);
bool nw_debug_filter(const char *hint, char *buf, size_t size);
/* A scrolling list rows lines high; always closed with nw_debug_list_end. */
void nw_debug_list_begin(const char *id, int rows);
void nw_debug_list_end(void);
bool nw_debug_selectable(const char *label, bool selected);

#ifdef __cplusplus
}
#endif

#endif
