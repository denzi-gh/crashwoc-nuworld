#include "frontend/video.h"

#include <stdio.h>

#include "app.h"
#include "frontend/debug.h"
#include "port/wide.h"

static struct {
    NwPlatform *platform;
    NwWindowMode fullscreen; /* where F11 goes from windowed */
} nw_video;

/* The picture's aspect: settings.toml's, or the display's for auto; a window takes it at the height it opened with,
   narrower where it would not fit the display. */
static float nw_video_aspect(const NwSettings *s)
{
    SDL_Window *window = nw_video.platform->window;
    SDL_DisplayID display = SDL_GetDisplayForWindow(window);
    const SDL_DisplayMode *mode = SDL_GetDesktopDisplayMode(display);
    float aspect = s->aspect;
    if (aspect <= 0.0f) {
        aspect = mode && mode->h > 0 ? (float)mode->w / (float)mode->h : 16.0f / 9.0f;
    }
    if (aspect < NW_WIDE_MIN) {
        aspect = NW_WIDE_MIN;
    } else if (aspect > NW_WIDE_MAX) {
        aspect = NW_WIDE_MAX;
    }
    int w, h;
    SDL_Rect usable;
    SDL_GetWindowSize(window, &w, &h);
    w = (int)((float)h * aspect + 0.5f);
    if (SDL_GetDisplayUsableBounds(display, &usable) && w > usable.w * 9 / 10) {
        w = usable.w * 9 / 10;
        h = (int)((float)w / aspect + 0.5f);
    }
    SDL_SetWindowSize(window, w, h);
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    return aspect;
}

/* The index of value in values, -1 when it is none of them. */
static int nw_video_index(float value, const float *values, int count)
{
    for (int i = 0; i < count; i++) {
        if (SDL_fabsf(value - values[i]) < 0.001f) {
            return i;
        }
    }
    return -1;
}

static void nw_video_panel(void *user)
{
    static const char *const windows[NW_WINDOW_MODES] = { "Windowed", "Borderless", "Fullscreen" };
    static const float aspects[] = { 0.0f, 4.0f / 3.0f, 16.0f / 9.0f, 16.0f / 10.0f, 21.0f / 9.0f, 32.0f / 9.0f };
    static const char *const aspect_names[] = { "auto", "4:3", "16:9", "16:10", "21:9", "32:9" };
    static const float huds[] = { 0.0f, 16.0f / 9.0f, 4.0f / 3.0f };
    static const char *const hud_names[] = { "at the edges", "inside 16:9", "inside 4:3" };
    NwSettings *s = user;
    int window = s->window;
    int aspect = nw_video_index(s->aspect, aspects, (int)SDL_arraysize(aspects));
    int hud = nw_video_index(s->hud, huds, (int)SDL_arraysize(huds));
    if (nw_debug_combo("Window", &window, windows, NW_WINDOW_MODES)) {
        s->window = window;
    }
    if (nw_debug_combo("Aspect", &aspect, aspect_names, (int)SDL_arraysize(aspects))) {
        s->aspect = aspects[aspect];
    }
    if (nw_debug_combo("HUD", &hud, hud_names, (int)SDL_arraysize(huds))) {
        s->hud = huds[hud];
    }
    nw_debug_text("frame %d px wide now; aspect and HUD apply at the next start", nw_wide_width());
}

static void nw_video_toggle(void *user)
{
    NwSettings *s = user;
    s->window = s->window == NW_WINDOW_WINDOWED ? nw_video.fullscreen : NW_WINDOW_WINDOWED;
}

float nw_video_start(NwPlatform *p, NwSettings *s)
{
    nw_video.platform = p;
    nw_video.fullscreen = s->window == NW_WINDOW_WINDOWED ? NW_WINDOW_BORDERLESS : s->window;
    nw_debug_panel("Video", "Settings", nw_video_panel, s, false);
    nw_debug_action("Video", "Fullscreen", SDL_SCANCODE_F11, nw_video_toggle, s);
    float aspect = nw_video_aspect(s);
    if (s->window != NW_WINDOW_WINDOWED && !nw_display_set_mode(p->window, s->window)) {
        fprintf(stderr, NW_APP_NAME ": window mode: %s\n", SDL_GetError());
    }
    return aspect;
}

void nw_video_apply(const NwSettings *s, const NwSettings *was)
{
    if (s->window != was->window) {
        if (s->window != NW_WINDOW_WINDOWED) {
            nw_video.fullscreen = s->window;
        }
        if (!nw_display_set_mode(nw_video.platform->window, s->window)) {
            fprintf(stderr, NW_APP_NAME ": window mode: %s\n", SDL_GetError());
        }
    }
}
