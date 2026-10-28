#include "platform/display.h"

bool nw_display_set_mode(SDL_Window *window, NwWindowMode mode)
{
    if (mode == NW_WINDOW_WINDOWED) {
        return SDL_SetWindowFullscreen(window, false);
    }
    const SDL_DisplayMode *desktop = SDL_GetDesktopDisplayMode(SDL_GetDisplayForWindow(window));
    SDL_DisplayMode exclusive;
    if (mode == NW_WINDOW_FULLSCREEN &&
        (!desktop || !SDL_GetClosestFullscreenDisplayMode(desktop->displayID, desktop->w, desktop->h,
                                                          desktop->refresh_rate, false, &exclusive))) {
        return false;
    }
    return SDL_SetWindowFullscreenMode(window, mode == NW_WINDOW_FULLSCREEN ? &exclusive : NULL) &&
           SDL_SetWindowFullscreen(window, true);
}
