#include "platform/display.h"

static NwResolution nw_display_pixels(const SDL_DisplayMode *m)
{
    float d = m->pixel_density > 0.0f ? m->pixel_density : 1.0f;
    return (NwResolution){ (int)SDL_lroundf((float)m->w * d), (int)SDL_lroundf((float)m->h * d) };
}

const SDL_DisplayMode *nw_display_pick(SDL_DisplayMode *const *modes, int count, NwResolution res)
{
    const SDL_DisplayMode *best = NULL;
    for (int i = 0; i < count; i++) {
        NwResolution px = nw_display_pixels(modes[i]);
        if (px.w == res.w && px.h == res.h && (!best || modes[i]->refresh_rate > best->refresh_rate)) {
            best = modes[i];
        }
    }
    return best;
}

/* The exclusive mode for res on window's display: the listed one, else SDL's closest. */
static bool nw_display_exclusive(SDL_Window *window, NwResolution res, SDL_DisplayMode *out)
{
    SDL_DisplayID display = SDL_GetDisplayForWindow(window);
    if (res.w <= 0 || res.h <= 0) {
        const SDL_DisplayMode *desktop = SDL_GetDesktopDisplayMode(display);
        if (!desktop) {
            return false;
        }
        res = nw_display_pixels(desktop);
    }
    int count = 0;
    SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes(display, &count);
    const SDL_DisplayMode *m = modes ? nw_display_pick(modes, count, res) : NULL;
    bool ok = m != NULL;
    if (ok) {
        *out = *m;
    }
    SDL_free(modes);
    return ok || SDL_GetClosestFullscreenDisplayMode(display, res.w, res.h, 0.0f, false, out);
}

bool nw_display_set_mode(SDL_Window *window, NwWindowMode mode, NwResolution res)
{
    SDL_DisplayMode exclusive;
    if (mode == NW_WINDOW_WINDOWED) {
        float d = SDL_GetWindowPixelDensity(window);
        d = d > 0.0f ? d : 1.0f;
        if (!SDL_SetWindowFullscreen(window, false)) {
            return false;
        }
        if (res.w > 0 && res.h > 0 &&
            SDL_SetWindowSize(window, (int)SDL_lroundf((float)res.w / d), (int)SDL_lroundf((float)res.h / d))) {
            SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        }
        return true;
    }
    if (mode == NW_WINDOW_FULLSCREEN && !nw_display_exclusive(window, res, &exclusive)) {
        return false;
    }
    return SDL_SetWindowFullscreenMode(window, mode == NW_WINDOW_FULLSCREEN ? &exclusive : NULL) &&
           SDL_SetWindowFullscreen(window, true);
}

int nw_display_sizes(SDL_Window *window, NwWindowMode mode, NwResolution *sizes, int max)
{
    SDL_DisplayID display = SDL_GetDisplayForWindow(window);
    SDL_Rect usable;
    float d = SDL_GetWindowPixelDensity(window);
    int count = 0, n = 0;
    if (mode == NW_WINDOW_BORDERLESS) {
        return 0;
    }
    if (mode == NW_WINDOW_WINDOWED && !SDL_GetDisplayUsableBounds(display, &usable)) {
        return 0;
    }
    d = d > 0.0f ? d : 1.0f;
    SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes(display, &count);
    for (int i = 0; modes && i < count && n < max; i++) {
        NwResolution px = nw_display_pixels(modes[i]);
        bool seen = false;
        for (int k = 0; k < n && !seen; k++) {
            seen = sizes[k].w == px.w && sizes[k].h == px.h;
        }
        if (!seen && (mode == NW_WINDOW_FULLSCREEN || ((float)px.w <= (float)usable.w * d &&
                                                       (float)px.h <= (float)usable.h * d))) {
            sizes[n++] = px;
        }
    }
    SDL_free(modes);
    return n;
}

NwResolution nw_display_fullscreen(SDL_Window *window, NwWindowMode mode, NwResolution res)
{
    SDL_DisplayMode m;
    const SDL_DisplayMode *desktop = SDL_GetDesktopDisplayMode(SDL_GetDisplayForWindow(window));
    if (mode == NW_WINDOW_FULLSCREEN && nw_display_exclusive(window, res, &m)) {
        return nw_display_pixels(&m);
    }
    return desktop ? nw_display_pixels(desktop) : (NwResolution){ 0, 0 };
}
