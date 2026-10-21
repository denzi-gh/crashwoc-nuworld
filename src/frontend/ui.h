/* Dear ImGui on the platform window: context, SDL3 + OpenGL 3 backends, the port's OFL font (Atkinson
   Hyperlegible, assets/fonts/) and style. Main thread only. */
#ifndef NUWORLD_FRONTEND_UI_H
#define NUWORLD_FRONTEND_UI_H

#include <stdbool.h>

#include <SDL3/SDL.h>

#include "platform/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum NwUiFont {
    NW_UI_REGULAR,
    NW_UI_BOLD,
} NwUiFont;

bool nw_ui_init(NwPlatform *p);
void nw_ui_shutdown(void);

/* Feeds an event to ImGui; also follows display scale changes. */
void nw_ui_event(const SDL_Event *e);

/* One ImGui frame, drawn over whatever was rendered before nw_ui_end. */
void nw_ui_begin(void);
void nw_ui_end(void);

/* Pushes a font at scale times the base size; pop with nw_ui_pop_font. */
void nw_ui_push_font(NwUiFont font, float scale);
void nw_ui_pop_font(void);

#ifdef __cplusplus
}
#endif

#endif
