/* The window on its display: windowed or fullscreen. Main thread. */
#ifndef NUWORLD_PLATFORM_DISPLAY_H
#define NUWORLD_PLATFORM_DISPLAY_H

#include <stdbool.h>

#include <SDL3/SDL.h>

typedef enum NwWindowMode {
    NW_WINDOW_WINDOWED,
    NW_WINDOW_BORDERLESS, /* fullscreen at the desktop's mode */
    NW_WINDOW_FULLSCREEN, /* exclusive: the display takes the window's mode */
    NW_WINDOW_MODES
} NwWindowMode;

/* window in mode; false with an SDL error, the window as it was. */
bool nw_display_set_mode(SDL_Window *window, NwWindowMode mode);

#endif
