#include "frontend/video.h"

#include <stdio.h>

#include "app.h"
#include "frontend/debug.h"
#include "port/wide.h"

#define NW_VIDEO_SIZES 32  /* resolutions the panel lists */
#define NW_VIDEO_AUTO_H 720 /* an auto window's height */

static struct {
    NwPlatform *platform;
    NwSettings *settings;
    NwWindowMode fullscreen; /* where F11 goes from windowed */
    NwResolution asked;      /* the window size the port set last, which no drag made */
    NwVsync vsync;           /* in effect */
} nw_video;

static bool nw_video_near(NwResolution a, NwResolution b)
{
    return SDL_abs(a.w - b.w) <= 2 && SDL_abs(a.h - b.h) <= 2;
}

/* The output's width over height: a windowed resolution's, else what fullscreen shows. */
static float nw_video_shape(const NwSettings *s)
{
    NwResolution r = s->window == NW_WINDOW_WINDOWED && s->resolution.w > 0
                         ? s->resolution
                         : nw_display_fullscreen(nw_video.platform->window, s->window, s->resolution);
    return r.w > 0 && r.h > 0 ? (float)r.w / (float)r.h : 16.0f / 9.0f;
}

/* The picture's aspect: settings.toml's, or the output's for auto. */
static float nw_video_aspect(const NwSettings *s)
{
    float aspect = s->aspect > 0.0f ? s->aspect : nw_video_shape(s);
    return aspect < NW_WIDE_MIN ? NW_WIDE_MIN : aspect > NW_WIDE_MAX ? NW_WIDE_MAX : aspect;
}

/* A window of auto size: the picture's aspect NW_VIDEO_AUTO_H high, narrower where it would not fit the display. */
static void nw_video_autosize(float aspect)
{
    SDL_Window *window = nw_video.platform->window;
    int h = NW_VIDEO_AUTO_H, w = (int)((float)h * aspect + 0.5f);
    SDL_Rect usable;
    if (SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(window), &usable) && w > usable.w * 9 / 10) {
        w = usable.w * 9 / 10;
        h = (int)((float)w / aspect + 0.5f);
    }
    SDL_SetWindowSize(window, w, h);
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    float d = SDL_GetWindowPixelDensity(window);
    d = d > 0.0f ? d : 1.0f;
    nw_video.asked = (NwResolution){ (int)SDL_lroundf((float)w * d), (int)SDL_lroundf((float)h * d) };
}

static void nw_video_set(const NwSettings *s)
{
    if (s->window == NW_WINDOW_WINDOWED && s->resolution.w > 0) {
        nw_video.asked = s->resolution;
    }
    if (!nw_display_set_mode(nw_video.platform->window, s->window, s->resolution)) {
        fprintf(stderr, NW_APP_NAME ": window mode: %s\n", SDL_GetError());
    }
}

static void nw_video_render(const NwSettings *s)
{
    nw_backend_set_options(&(NwRenderOptions){ .scale = s->scale, .msaa = s->msaa, .filter = s->filter });
}

static void nw_video_vsync(NwVsync vsync)
{
    static const char *const names[NW_VSYNC_MODES] = { "off", "on", "adaptive" };
    nw_video.vsync = nw_display_set_vsync(vsync);
    if (nw_video.vsync != vsync) {
        fprintf(stderr, NW_APP_NAME ": vsync %s is not supported, it is %s\n", names[vsync], names[nw_video.vsync]);
    }
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

/* auto and the sizes the mode can take, the current one added when it is none of them. */
static void nw_video_resolution(NwSettings *s)
{
    static NwResolution sizes[NW_VIDEO_SIZES + 2];
    static char labels[NW_VIDEO_SIZES + 2][24];
    const char *items[NW_VIDEO_SIZES + 2];
    if (s->window == NW_WINDOW_BORDERLESS) {
        nw_debug_text("Resolution: the desktop's");
        return;
    }
    int n = 1 + nw_display_sizes(nw_video.platform->window, s->window, sizes + 1, NW_VIDEO_SIZES), current = -1;
    sizes[0] = (NwResolution){ 0, 0 };
    for (int i = 0; i < n && current < 0; i++) {
        current = sizes[i].w == s->resolution.w && sizes[i].h == s->resolution.h ? i : -1;
    }
    if (current < 0) {
        sizes[n] = s->resolution;
        current = n++;
    }
    for (int i = 0; i < n; i++) {
        items[i] = nw_settings_resolution_name(sizes[i], labels[i], sizeof(labels[i]));
    }
    int pick = current;
    if (nw_debug_combo("Resolution", &pick, items, n) && pick != current) {
        s->resolution = sizes[pick];
    }
}

static void nw_video_panel(void *user)
{
    static const char *const windows[NW_WINDOW_MODES] = { "Windowed", "Borderless", "Fullscreen" };
    static const float aspects[] = { 0.0f, 4.0f / 3.0f, 16.0f / 9.0f, 16.0f / 10.0f, 21.0f / 9.0f, 32.0f / 9.0f };
    static const char *const aspect_names[] = { "auto", "4:3", "16:9", "16:10", "21:9", "32:9" };
    static const float huds[] = { 0.0f, 16.0f / 9.0f, 4.0f / 3.0f };
    static const char *const hud_names[] = { "at the edges", "inside 16:9", "inside 4:3" };
    static const char *const vsyncs[NW_VSYNC_MODES] = { "Off", "On", "Adaptive" };
    static const char *const scales[NW_BACKEND_SCALE_MAX + 1] = { "auto", "1x (PS2)", "2x", "3x", "4x",
                                                                  "5x", "6x", "7x", "8x" };
    static const int samples[] = { 0, 2, 4, 8 };
    static const char *const msaas[] = { "Off", "2x", "4x", "8x" };
    static const char *const filters[NW_FILTERS] = { "Retail", "Nearest", "Bilinear" };
    NwSettings *s = user;
    int window = s->window, vsync = s->vsync, scale = s->scale, msaa = 0, filter = s->filter;
    while (msaa < 3 && samples[msaa + 1] <= s->msaa) {
        msaa++;
    }
    int aspect = nw_video_index(s->aspect, aspects, (int)SDL_arraysize(aspects));
    int hud = nw_video_index(s->hud, huds, (int)SDL_arraysize(huds));
    if (nw_debug_combo("Window", &window, windows, NW_WINDOW_MODES)) {
        s->window = window;
    }
    nw_video_resolution(s);
    if (nw_debug_combo("VSync", &vsync, vsyncs, NW_VSYNC_MODES)) {
        s->vsync = vsync;
    }
    if (nw_video.vsync != s->vsync) {
        nw_debug_text("not supported here: %s", vsyncs[nw_video.vsync]);
    }
    if (nw_debug_combo("Internal scale", &scale, scales, NW_BACKEND_SCALE_MAX + 1)) {
        s->scale = scale;
    }
    if (s->scale != nw_backend_scale()) {
        nw_debug_text("drawn at %dx", nw_backend_scale());
    }
    if (nw_debug_combo("MSAA", &msaa, msaas, (int)SDL_arraysize(msaas))) {
        s->msaa = samples[msaa];
    }
    if (s->msaa != nw_backend_msaa()) {
        nw_debug_text("drawn with %d samples", nw_backend_msaa());
    }
    if (nw_debug_combo("Texture filter", &filter, filters, NW_FILTERS)) {
        s->filter = filter;
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
    nw_video.settings = s;
    nw_video.fullscreen = s->window == NW_WINDOW_WINDOWED ? NW_WINDOW_BORDERLESS : s->window;
    nw_debug_panel("Video", "Settings", nw_video_panel, s, false);
    nw_debug_action("Video", "Fullscreen", SDL_SCANCODE_F11, nw_video_toggle, s);
    float aspect = nw_video_aspect(s);
    if (s->resolution.w <= 0) {
        nw_video_autosize(aspect);
    }
    if (s->window != NW_WINDOW_WINDOWED || s->resolution.w > 0) {
        nw_video_set(s);
    }
    nw_video_vsync(s->vsync);
    nw_video_render(s);
    return aspect;
}

void nw_video_event(const SDL_Event *e)
{
    NwSettings *s = nw_video.settings;
    if (e->type != SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED || !s || s->window != NW_WINDOW_WINDOWED ||
        (SDL_GetWindowFlags(nw_video.platform->window) &
         (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MAXIMIZED | SDL_WINDOW_MINIMIZED))) {
        return;
    }
    NwResolution px = { e->window.data1, e->window.data2 };
    if (!nw_video_near(px, nw_video.asked)) {
        nw_video.asked = px;
        s->resolution = px;
    }
}

void nw_video_apply(const NwSettings *s, const NwSettings *was)
{
    if (s->vsync != was->vsync) {
        nw_video_vsync(s->vsync);
    }
    if (s->scale != was->scale || s->msaa != was->msaa || s->filter != was->filter) {
        nw_video_render(s);
    }
    if (s->window == was->window && s->resolution.w == was->resolution.w && s->resolution.h == was->resolution.h) {
        return;
    }
    if (s->window != NW_WINDOW_WINDOWED) {
        nw_video.fullscreen = s->window;
    }
    int w, h;
    SDL_GetWindowSizeInPixels(nw_video.platform->window, &w, &h);
    bool windowed = s->window == NW_WINDOW_WINDOWED && was->window == NW_WINDOW_WINDOWED;
    if (windowed && s->resolution.w <= 0) {
        nw_video_autosize(nw_video_aspect(s));
    } else if (!windowed || !nw_video_near(s->resolution, (NwResolution){ w, h })) { /* not the drag that set it */
        nw_video_set(s);
    }
}
