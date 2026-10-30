/* The window on its display: windowed or fullscreen, its size or the display's mode, the swap interval. Main
   thread. */
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

typedef enum NwVsync {
    NW_VSYNC_OFF,
    NW_VSYNC_ON,
    NW_VSYNC_ADAPTIVE, /* late frames swap at once */
    NW_VSYNC_MODES
} NwVsync;

typedef struct NwResolution {
    int w, h; /* pixels; 0 x 0 = auto */
} NwResolution;

/* window in mode at res: windowed its size (auto leaves it), exclusive the display's mode (auto: the desktop's);
   borderless is the desktop. False with an SDL error, the window as it was. */

bool nw_display_set_mode(SDL_Window *window, NwWindowMode mode, NwResolution res);


/* Of count modes, the one of res pixels with the highest refresh rate; NULL when none is. */
const SDL_DisplayMode *nw_display_pick(SDL_DisplayMode *const *modes, int count, NwResolution res);


/* The sizes window can take in mode, largest first, at most max of them: the display's fullscreen modes, for
   windowed those that fit its usable area; none borderless. Returns how many. */
int nw_display_sizes(SDL_Window *window, NwWindowMode mode, NwResolution *sizes, int max);


/* What fullscreen at res shows: the mode's pixels, the desktop's for auto or borderless. */
NwResolution nw_display_fullscreen(SDL_Window *window, NwWindowMode mode, NwResolution res);


/* On Windows the OpenGL driver takes a window of exactly its monitor's size off the compositor: screen capture freezes
   and the cursor is not drawn. Such a fullscreen window gets one pixel column more, past the monitor's right edge. */
void nw_display_compose(SDL_Window *window);


/* window's size in pixels without that column: what the frame fills. */
void nw_display_size(SDL_Window *window, int *w, int *h);


/* The refresh rate of window's display mode in Hz, 0 when unknown. */
float nw_display_refresh(SDL_Window *window);


/* The current GL context's swap interval; returns the one in effect: adaptive falls back to on. */
NwVsync nw_display_set_vsync(NwVsync vsync);

#endif
