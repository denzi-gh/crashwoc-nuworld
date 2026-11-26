#include "frontend/video.h"

#include <stdio.h>

#include "app.h"
#include "frontend/debug.h"
#include "gfx/fog.h"
#include "gfx/gl.h"
#include "gfx/interp.h"
#include "platform/log.h"
#include "port/wide.h"

#define NW_VIDEO_SIZES 32
#define NW_VIDEO_AUTO_H 720

static struct {
    NwPlatform *platform;
    NwSettings *settings;
    NwWindowMode fullscreen; /* where F11 goes from windowed */
    NwResolution asked;      /* the window size the port set last, which no drag made */
    NwVsync vsync;
    float aspect, hud;
    char gpu[NW_GPU_NAME];
    char gpus[NW_GPU_CARDS][NW_GPU_NAME];
    int ngpus;
} nw_video;

static bool nw_video_near(NwResolution a, NwResolution b)
{
    return SDL_abs(a.w - b.w) <= 2 && SDL_abs(a.h - b.h) <= 2;
}

static float nw_video_shape(const NwSettings *s)
{
    NwResolution r = s->window == NW_WINDOW_WINDOWED && s->resolution.w > 0
                         ? s->resolution
                         : nw_display_fullscreen(nw_video.platform->window, s->window, s->resolution);
    return r.w > 0 && r.h > 0 ? (float)r.w / (float)r.h : 16.0f / 9.0f;
}

static float nw_video_picture(const NwSettings *s, float shape)
{
    float aspect = s->aspect > 0.0f ? s->aspect : shape;
    return aspect < NW_WIDE_MIN ? NW_WIDE_MIN : aspect > NW_WIDE_MAX ? NW_WIDE_MAX : aspect;
}

static float nw_video_aspect(const NwSettings *s)
{
    return nw_video_picture(s, nw_video_shape(s));
}

static void nw_video_wide(const NwSettings *s, float shape)
{
    float aspect = nw_video_picture(s, shape);
    if (aspect != nw_video.aspect || s->hud != nw_video.hud) {
        nw_video.aspect = aspect;
        nw_video.hud = s->hud;
        nw_wide_request(aspect, s->hud);
    }
}

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
        nw_log_warn(NW_LOG_APP, "window mode: %s", SDL_GetError());
    }
}

static void nw_video_render(const NwSettings *s)
{
    nw_backend_set_options(
        &(NwRenderOptions){ .scale = s->scale, .msaa = s->msaa, .filter = s->filter, .gems = s->gems,
                            .clouds = s->clouds, .silhouettes = s->silhouettes });
}

static void nw_video_vsync(NwVsync vsync)
{
    static const char *const names[NW_VSYNC_MODES] = { "off", "on", "adaptive" };
    nw_video.vsync = nw_display_set_vsync(vsync);
    if (nw_video.vsync != vsync) {
        nw_log_warn(NW_LOG_APP, "vsync %s is not supported, it is %s", names[vsync], names[nw_video.vsync]);
    }
}

static int nw_video_index(float value, const float *values, int count)
{
    for (int i = 0; i < count; i++) {
        if (SDL_fabsf(value - values[i]) < 0.001f) {
            return i;
        }
    }
    return -1;
}

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
    static const int rates[] = { 0, 60, 120, 240 };
    static char custom[16];
    const char *rate_names[5] = { "Display refresh", "60", "120", "240" };
    int rate = 0, nrates = 4;
    while (rate < nrates && rates[rate] != s->fps) {
        rate++;
    }
    if (rate == nrates) {
        SDL_snprintf(custom, sizeof(custom), "%d", s->fps);
        rate_names[nrates++] = custom;
    }
    if (nw_debug_combo("Frame rate", &rate, rate_names, nrates) && rate < 4) {
        s->fps = rates[rate];
    }
    static const char *const offon[] = { "Off", "On" };
    int interp = s->interpolation;
    if (nw_debug_combo("Interpolation", &interp, offon, 2)) {
        s->interpolation = interp;
    }
    if (s->interpolation) {
        NwInterpStats st = nw_interp_stats();
        nw_debug_text("blended %u draws, %u new, %u views, %u snapped", st.paired, st.unpaired, st.views, st.snapped);
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
    static const char *const looks[] = { "Retail", "Enhanced" };
    int gems = s->gems;
    if (nw_debug_combo("Gems and relics", &gems, looks, 2)) {
        s->gems = gems;
    }
    static const char *const clouds_names[] = { "Retail", "Volumetric" };
    int clouds = s->clouds;
    if (nw_debug_combo("Clouds", &clouds, clouds_names, 2)) {
        s->clouds = clouds;
    }
    static const char *const silhouette_names[] = { "Retail", "Models" };
    int silhouettes = s->silhouettes;
    if (nw_debug_combo("Silhouettes", &silhouettes, silhouette_names, 2)) {
        s->silhouettes = silhouettes;
    }
    nw_debug_slider_int("Draw distance %", &s->draw_distance, 100, NW_DRAW_DISTANCE_MAX);
    nw_debug_tooltip("How far objects are drawn; 100 is the PS2's");
    nw_debug_slider_int("Fog %", &s->fog, 0, 100);
    nw_debug_tooltip("How strong the levels' fog is; Smokey and the Bandicoot starts at 100 and puts yours back after");
    int blur = s->blur, haze = s->haze;
    if (nw_debug_combo("Distance blur", &blur, offon, 2)) {
        s->blur = blur;
    }
    nw_debug_tooltip("The PS2 blurs what the fog covers");
    if (nw_debug_combo("Heat haze", &haze, offon, 2)) {
        s->haze = haze;
    }
    nw_debug_tooltip("The shimmer in the fire and underwater levels and over candle flames");
    if (nw_debug_combo("Aspect", &aspect, aspect_names, (int)SDL_arraysize(aspects))) {
        s->aspect = aspects[aspect];
    }
    if (nw_debug_combo("HUD", &hud, hud_names, (int)SDL_arraysize(huds))) {
        s->hud = huds[hud];
    }
    nw_debug_text("frame %d px wide", nw_wide_width());
#ifdef _WIN32
    const char *gpus[NW_GPU_CARDS + 2] = { "Auto" };
    int n = 1, current = 0;
    for (int i = 0; i < nw_video.ngpus; i++) {
        gpus[n++] = nw_video.gpus[i];
    }
    for (int i = 1; i < n && !current; i++) {
        current = SDL_strcmp(s->gpu, gpus[i]) == 0 ? i : 0;
    }
    if (!current && SDL_strcmp(s->gpu, "auto") != 0) {
        gpus[n] = s->gpu;
        current = n++;
    }
    int pick = current;
    if (nw_debug_combo("Graphics card", &pick, gpus, n) && pick != current) {
        SDL_zero(s->gpu);
        SDL_strlcpy(s->gpu, pick ? gpus[pick] : "auto", sizeof(s->gpu));
    }
    nw_debug_tooltip("Auto lets Windows and the graphics driver choose; NuWorld asks for the faster card.");
    if (SDL_strcmp(s->gpu, nw_video.gpu) != 0) {
        nw_debug_text("takes effect at the next start");
    }
#endif
    const GLubyte *renderer = glGetString(GL_RENDERER);
    nw_debug_text("drawn by %s", renderer ? (const char *)renderer : "?");
}

static void nw_video_gpus(const char *gpu)
{
    SDL_strlcpy(nw_video.gpu, gpu, sizeof(nw_video.gpu));
    nw_video.ngpus = nw_gpu_list(nw_video.gpus);
    for (int i = 0; i < nw_video.ngpus; i++) {
        nw_log_info(NW_LOG_GL, "graphics card: %s", nw_video.gpus[i]);
    }
}

unsigned nw_video_fps(const NwSettings *s)
{
    if (s->fps > 0) {
        return (unsigned)s->fps;
    }
    if (nw_video.vsync != NW_VSYNC_OFF) {
        return 0;
    }
    float hz = nw_display_refresh(nw_video.platform->window);
    return hz > 0.0f ? (unsigned)SDL_lroundf(hz) : 60;
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
    nw_video_gpus(s->gpu);
    nw_debug_panel("Settings", "Video", nw_video_panel, s, false);
    nw_debug_action("Settings", "Fullscreen", SDL_SCANCODE_F11, nw_video_toggle, s);
    float aspect = nw_video_aspect(s);
    nw_video.aspect = aspect;
    nw_video.hud = s->hud;
    if (s->resolution.w <= 0) {
        nw_video_autosize(aspect);
    }
    if (s->window != NW_WINDOW_WINDOWED || s->resolution.w > 0) {
        nw_video_set(s);
    }
    nw_video_vsync(s->vsync);
    nw_video_render(s);
    nw_wide_draw_distance(s->draw_distance);
    nw_fog_options(s->fog, s->blur, s->haze);
    return aspect;
}

void nw_video_event(const SDL_Event *e)
{
    NwSettings *s = nw_video.settings;
    if (e->type != SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED || !s || e->window.data2 <= 0) {
        return;
    }
    int w, h;
    nw_display_compose(nw_video.platform->window); /* SDL puts a restored fullscreen window back at monitor size */
    nw_display_size(nw_video.platform->window, &w, &h);
    nw_video_wide(s, h > 0 ? (float)w / (float)h : nw_video.aspect);
    if (s->window != NW_WINDOW_WINDOWED || (SDL_GetWindowFlags(nw_video.platform->window) &
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
    if (s->scale != was->scale || s->msaa != was->msaa || s->filter != was->filter || s->gems != was->gems ||
        s->clouds != was->clouds || s->silhouettes != was->silhouettes) {
        nw_video_render(s);
    }
    if (s->draw_distance != was->draw_distance) {
        nw_wide_draw_distance(s->draw_distance);
    }
    if (s->fog != was->fog || s->blur != was->blur || s->haze != was->haze) {
        nw_fog_options(s->fog, s->blur, s->haze);
    }
    if (SDL_strcmp(s->gpu, was->gpu) != 0) {
        nw_gpu_prefer(s->gpu);
    }
    int w, h;
    nw_display_size(nw_video.platform->window, &w, &h);
    if (s->aspect != was->aspect || s->hud != was->hud) {
        nw_video_wide(s, h > 0 ? (float)w / (float)h : nw_video.aspect);
    }
    if (s->window == was->window && s->resolution.w == was->resolution.w && s->resolution.h == was->resolution.h) {
        return;
    }
    if (s->window != NW_WINDOW_WINDOWED) {
        nw_video.fullscreen = s->window;
    }
    bool windowed = s->window == NW_WINDOW_WINDOWED && was->window == NW_WINDOW_WINDOWED;
    if (windowed && s->resolution.w <= 0) {
        nw_video_autosize(nw_video_aspect(s));
    } else if (!windowed || !nw_video_near(s->resolution, (NwResolution){ w, h })) { /* not the drag that set it */
        nw_video_set(s);
    }
}
