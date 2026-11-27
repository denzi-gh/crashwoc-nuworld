#include "frontend/settings.h"

#include <stddef.h>

#include <SDL3/SDL.h>

#include "app.h"
#include "platform/paths.h"
#include "save/savedoc.h"

typedef enum NwSettingKind {
    NW_SETTING_BOOL,
    NW_SETTING_INT,  /* clamped to lo..hi, kept when off the step from lo; 0 also by the name zero */
    NW_SETTING_ENUM, /* int-sized, by its names */
    NW_SETTING_TEXT,
    NW_SETTING_BINDS, /* the whole table: hi targets' sources, each a name or an array of names */
} NwSettingKind;

typedef struct NwSettingKey {
    const char *table, *name;
    const char *sub; /* a table in table ([game.movement]), or NULL */
    const char *sub2; /* a table in sub ([game.movement.crash]), or NULL */
    NwSettingKind kind;
    size_t offset, size;
    int lo, hi, step;
    const char *const *names; /* NW_SETTING_ENUM: hi of them */
    const char *zero;
    bool (*parse)(const char *text, void *field);
    const char *(*format)(const void *field, char *buf, int size);
    uint16_t (*source)(const char *name);
    const char *(*source_name)(uint16_t source);
    const char *help;
    bool hidden;
} NwSettingKey;

/* Raised when a default changes for files that already hold the old value; nw_settings_load moves them over. */
#define NW_SETTINGS_VERSION 2

#define NW_FIELD(f) .offset = offsetof(NwSettings, f), .size = sizeof(((NwSettings *)0)->f)

static size_t nw_settings_header(const NwSettingKey *k, const char *header[3])
{
    header[0] = k->table;
    header[1] = k->sub;
    header[2] = k->sub2;
    return k->sub2 ? 3 : k->sub ? 2 : 1;
}

static char *nw_settings_path(const char *root)
{
    char *path = NULL;
    return SDL_asprintf(&path, "%s%s", root, NW_PATHS_SETTINGS) < 0 ? NULL : path;
}

static const struct {
    const char *name;
    float w, h;
} nw_settings_aspects[] = { { "4:3", 4, 3 }, { "16:9", 16, 9 }, { "16:10", 16, 10 }, { "21:9", 21, 9 }, { "32:9", 32, 9 } };

bool nw_settings_aspect(const char *value, float *out)
{
    char buf[32];
    size_t len = SDL_strlen(value);
    if (len >= 2 && value[0] == '"' && value[len - 1] == '"') {
        value++;
        len -= 2;
    }
    if (len >= sizeof(buf)) {
        return false;
    }
    SDL_memcpy(buf, value, len);
    buf[len] = '\0';
    if (SDL_strcmp(buf, "auto") == 0) {
        *out = 0.0f;
        return true;
    }
    char *end;
    double w = SDL_strtod(buf, &end), h = 1.0;
    if (end != buf && *end == ':') {
        char *num = end + 1;
        h = SDL_strtod(num, &end);
        if (end == num) {
            return false;
        }
    }
    if (end == buf || *end != '\0' || !(w > 0.0) || !(h > 0.0) || w / h > 100.0) {
        return false;
    }
    *out = (float)(w / h);
    return true;
}

const char *nw_settings_aspect_name(float aspect, char *buf, int size)
{
    if (aspect <= 0.0f) {
        return "auto";
    }
    for (size_t i = 0; i < SDL_arraysize(nw_settings_aspects); i++) {
        float a = nw_settings_aspects[i].w / nw_settings_aspects[i].h;
        if (SDL_fabsf(aspect - a) < 0.001f) {
            return nw_settings_aspects[i].name;
        }
    }
    SDL_snprintf(buf, size, "%.4g", aspect);
    return buf;
}

bool nw_settings_hud(const char *value, float *out)
{
    float hud;
    if (SDL_strcmp(value, "edges") == 0 || SDL_strcmp(value, "\"edges\"") == 0) {
        *out = 0.0f;
        return true;
    }
    if (!nw_settings_aspect(value, &hud) || hud == 0.0f) {
        return false;
    }
    *out = hud;
    return true;
}

const char *nw_settings_hud_name(float hud, char *buf, int size)
{
    return hud <= 0.0f ? "edges" : nw_settings_aspect_name(hud, buf, size);
}

bool nw_settings_resolution(const char *value, NwResolution *out)
{
    char *end;
    if (SDL_strcmp(value, "auto") == 0) {
        *out = (NwResolution){ 0, 0 };
        return true;
    }
    long w = SDL_strtol(value, &end, 10);
    if (end == value || *end != 'x') {
        return false;
    }
    const char *num = end + 1;
    long h = SDL_strtol(num, &end, 10);
    if (end == num || *end != '\0' || w <= 0 || h <= 0 || w > 16384 || h > 16384) {
        return false;
    }
    *out = (NwResolution){ (int)w, (int)h };
    return true;
}

const char *nw_settings_resolution_name(NwResolution res, char *buf, int size)
{
    if (res.w <= 0 || res.h <= 0) {
        return "auto";
    }
    SDL_snprintf(buf, size, "%dx%d", res.w, res.h);
    return buf;
}

static bool nw_settings_resolution_parse(const char *text, void *field)
{
    return nw_settings_resolution(text, field);
}

static const char *nw_settings_resolution_format(const void *field, char *buf, int size)
{
    return nw_settings_resolution_name(*(const NwResolution *)field, buf, size);
}

static bool nw_settings_aspect_parse(const char *text, void *field)
{
    return nw_settings_aspect(text, field);
}

static const char *nw_settings_aspect_format(const void *field, char *buf, int size)
{
    return nw_settings_aspect_name(*(const float *)field, buf, size);
}

static bool nw_settings_hud_parse(const char *text, void *field)
{
    return nw_settings_hud(text, field);
}

static const char *nw_settings_hud_format(const void *field, char *buf, int size)
{
    return nw_settings_hud_name(*(const float *)field, buf, size);
}

static bool nw_settings_log_parse(const char *text, void *field)
{
    char *log = field;
    size_t size = sizeof(((NwSettings *)0)->log);
    if (SDL_strlen(text) >= size) {
        return false;
    }
    SDL_memset(log, 0, size);
    SDL_strlcpy(log, text, size);
    return true;
}

static const char *nw_settings_log_format(const void *field, char *buf, int size)
{
    (void)buf;
    (void)size;
    return field;
}

static bool nw_settings_gpu_parse(const char *text, void *field)
{
    char *gpu = field;
    size_t size = sizeof(((NwSettings *)0)->gpu);
    if (!*text || SDL_strlen(text) >= size) {
        return false;
    }
    SDL_memset(gpu, 0, size);
    SDL_strlcpy(gpu, text, size);
    return true;
}

static const char *nw_settings_gpu_format(const void *field, char *buf, int size)
{
    (void)buf;
    (void)size;
    return field;
}

static const char *const nw_settings_windows[NW_WINDOW_MODES] = { "windowed", "borderless", "fullscreen" };
static const char *const nw_settings_vsyncs[NW_VSYNC_MODES] = { "off", "on", "adaptive" };
static const char *const nw_settings_filters[NW_FILTERS] = { "retail", "nearest", "bilinear" };
_Static_assert(sizeof(NwWindowMode) == sizeof(int) && sizeof(NwVsync) == sizeof(int) &&
                   sizeof(NwTexFilter) == sizeof(int) && sizeof(NwPromptStyle) == sizeof(int),
               "NW_SETTING_ENUM fields are int-sized");

#define NW_CRASH_KEY(f) .table = "game", .sub = "movement", .sub2 = "crash", .name = #f, NW_FIELD(movement.crash.f)
#define NW_CRASH_SWITCH_KEY(f, label, on, text) { NW_CRASH_KEY(f), .kind = NW_SETTING_BOOL, .help = "true: " text },
#define NW_CRASH_FLAG_KEY(f, label, def, text) { NW_CRASH_KEY(f), .kind = NW_SETTING_BOOL, .help = "true: " text },
#define NW_CRASH_VALUE_KEY(f, label, def, lo_, hi_, text) \
    { NW_CRASH_KEY(f), .kind = NW_SETTING_INT, .lo = lo_, .hi = hi_, .help = #lo_ "-" #hi_ ", " text },
#define NW_COCO_KEY(f) .table = "game", .sub = "movement", .sub2 = "coco", .name = #f, NW_FIELD(movement.coco.f)
#define NW_COCO_SWITCH_KEY(f, label, on, text) { NW_COCO_KEY(f), .kind = NW_SETTING_BOOL, .help = "true: " text },
#define NW_COCO_FLAG_KEY(f, label, def, text) { NW_COCO_KEY(f), .kind = NW_SETTING_BOOL, .help = "true: " text },
#define NW_COCO_VALUE_KEY(f, label, def, lo_, hi_, text) \
    { NW_COCO_KEY(f), .kind = NW_SETTING_INT, .lo = lo_, .hi = hi_, .help = #lo_ "-" #hi_ ", " text },
#define NW_MECH_KEY(f) .table = "game", .sub = "movement", .sub2 = "mech", .name = #f, NW_FIELD(movement.mech.f)
#define NW_MECH_SWITCH_KEY(f, label, on, text) { NW_MECH_KEY(f), .kind = NW_SETTING_BOOL, .help = "true: " text },
#define NW_MECH_FLAG_KEY(f, label, def, text) { NW_MECH_KEY(f), .kind = NW_SETTING_BOOL, .help = "true: " text },
#define NW_MECH_VALUE_KEY(f, label, def, lo_, hi_, text) \
    { NW_MECH_KEY(f), .kind = NW_SETTING_INT, .lo = lo_, .hi = hi_, .help = #lo_ "-" #hi_ ", " text },
#define NW_SUB_KEY(f) \
    .table = "game", .sub = "movement", .sub2 = "submarine", .name = #f, NW_FIELD(movement.submarine.f)
#define NW_SUB_SWITCH_KEY(f, label, on, text) { NW_SUB_KEY(f), .kind = NW_SETTING_BOOL, .help = "true: " text },
#define NW_SUB_FLAG_KEY(f, label, def, text) { NW_SUB_KEY(f), .kind = NW_SETTING_BOOL, .help = "true: " text },
#define NW_SUB_VALUE_KEY(f, label, def, lo_, hi_, text) \
    { NW_SUB_KEY(f), .kind = NW_SETTING_INT, .lo = lo_, .hi = hi_, .help = #lo_ "-" #hi_ ", " text },
#define NW_JEEP_KEY(f) .table = "game", .sub = "movement", .sub2 = "jeep", .name = #f, NW_FIELD(movement.jeep.f)
#define NW_JEEP_SWITCH_KEY(f, label, on, text) { NW_JEEP_KEY(f), .kind = NW_SETTING_BOOL, .help = "true: " text },
#define NW_JEEP_FLAG_KEY(f, label, def, text) { NW_JEEP_KEY(f), .kind = NW_SETTING_BOOL, .help = "true: " text },
#define NW_JEEP_VALUE_KEY(f, label, def, lo_, hi_, text) \
    { NW_JEEP_KEY(f), .kind = NW_SETTING_INT, .lo = lo_, .hi = hi_, .help = #lo_ "-" #hi_ ", " text },
#define NW_ATLAS_KEY(f) \
    .table = "game", .sub = "movement", .sub2 = "atlasphere", .name = #f, NW_FIELD(movement.atlasphere.f)
#define NW_ATLAS_SWITCH_KEY(f, label, on, text) { NW_ATLAS_KEY(f), .kind = NW_SETTING_BOOL, .help = "true: " text },
#define NW_ATLAS_FLAG_KEY(f, label, def, text) { NW_ATLAS_KEY(f), .kind = NW_SETTING_BOOL, .help = "true: " text },
#define NW_ATLAS_VALUE_KEY(f, label, def, lo_, hi_, text) \
    { NW_ATLAS_KEY(f), .kind = NW_SETTING_INT, .lo = lo_, .hi = hi_, .help = #lo_ "-" #hi_ ", " text },
#define NW_FIREFLY_KEY(f) \
    .table = "game", .sub = "movement", .sub2 = "firefly", .name = #f, NW_FIELD(movement.firefly.f)
#define NW_FIREFLY_SWITCH_KEY(f, label, on, text) { NW_FIREFLY_KEY(f), .kind = NW_SETTING_BOOL, .help = "true: " text },
#define NW_FIREFLY_FLAG_KEY(f, label, def, text) { NW_FIREFLY_KEY(f), .kind = NW_SETTING_BOOL, .help = "true: " text },
#define NW_FIREFLY_VALUE_KEY(f, label, def, lo_, hi_, text) \
    { NW_FIREFLY_KEY(f), .kind = NW_SETTING_INT, .lo = lo_, .hi = hi_, .help = #lo_ "-" #hi_ ", " text },
#define NW_PLANE_KEY(f) .table = "game", .sub = "movement", .sub2 = "plane", .name = #f, NW_FIELD(movement.plane.f)
#define NW_PLANE_SWITCH_KEY(f, label, on, text) { NW_PLANE_KEY(f), .kind = NW_SETTING_BOOL, .help = "true: " text },
#define NW_PLANE_FLAG_KEY(f, label, def, text) { NW_PLANE_KEY(f), .kind = NW_SETTING_BOOL, .help = "true: " text },
#define NW_PLANE_VALUE_KEY(f, label, def, lo_, hi_, text) \
    { NW_PLANE_KEY(f), .kind = NW_SETTING_INT, .lo = lo_, .hi = hi_, .help = #lo_ "-" #hi_ ", " text },
#define NW_SPACE_KEY(f) \
    .table = "game", .sub = "movement", .sub2 = "spacefighter", .name = #f, NW_FIELD(movement.spacefighter.f)
#define NW_SPACE_SWITCH_KEY(f, label, on, text) { NW_SPACE_KEY(f), .kind = NW_SETTING_BOOL, .help = "true: " text },
#define NW_SPACE_FLAG_KEY(f, label, def, text) { NW_SPACE_KEY(f), .kind = NW_SETTING_BOOL, .help = "true: " text },
#define NW_SPACE_VALUE_KEY(f, label, def, lo_, hi_, text) \
    { NW_SPACE_KEY(f), .kind = NW_SETTING_INT, .lo = lo_, .hi = hi_, .help = #lo_ "-" #hi_ ", " text },
#define NW_TT_KEY(f) .table = "game", .sub = "movement", .sub2 = "time_trial", .name = #f, NW_FIELD(movement.time_trial.f)
#define NW_TT_SWITCH_KEY(f, label, on, text) { NW_TT_KEY(f), .kind = NW_SETTING_BOOL, .help = "true: " text },

static const NwSettingKey nw_settings_keys[] = {
    { .table = "video", .name = "window", .kind = NW_SETTING_ENUM, NW_FIELD(window), .hi = NW_WINDOW_MODES,
      .names = nw_settings_windows,
      .help = "windowed, borderless (desktop resolution) or fullscreen (exclusive)" },
    { .table = "video", .name = "resolution", .kind = NW_SETTING_TEXT, NW_FIELD(resolution),
      .parse = nw_settings_resolution_parse, .format = nw_settings_resolution_format,
      .help = "auto or WxH: window size, or display mode in fullscreen; auto: desktop" },
    { .table = "video", .name = "vsync", .kind = NW_SETTING_ENUM, NW_FIELD(vsync), .hi = NW_VSYNC_MODES,
      .names = nw_settings_vsyncs, .help = "off, on or adaptive (late frames show at once; on where unsupported)" },
    { .table = "video", .name = "fps", .kind = NW_SETTING_INT, NW_FIELD(fps), .lo = 0, .hi = NW_FPS_MAX,
      .zero = "refresh", .help = "refresh (display rate) or a frame cap: 60, 120, 240, ..." },
    { .table = "video", .name = "interpolation", .kind = NW_SETTING_BOOL, NW_FIELD(interpolation),
      .help = "true: blends frames between game frames, one game frame of delay; false: PS2" },
    { .table = "video", .name = "scale", .kind = NW_SETTING_INT, NW_FIELD(scale), .lo = 0, .hi = NW_BACKEND_SCALE_MAX,
      .zero = "auto", .help = "internal resolution: auto (screen or higher) or 1-8x PS2" },
    { .table = "video", .name = "msaa", .kind = NW_SETTING_INT, NW_FIELD(msaa), .lo = 0, .hi = 16,
      .help = "multisampling: 0 (off), 2, 4 or 8, capped by the GPU" },
    { .table = "video", .name = "filter", .kind = NW_SETTING_ENUM, NW_FIELD(filter), .hi = NW_FILTERS,
      .names = nw_settings_filters, .help = "texture filter: retail (PS2, per texture), nearest or bilinear" },
    { .table = "video", .name = "gems", .kind = NW_SETTING_BOOL, NW_FIELD(gems),
      .help = "true: gems, crystals and relics as cut stone and polished metal; false: PS2" },
    { .table = "video", .name = "draw_distance", .kind = NW_SETTING_INT, NW_FIELD(draw_distance), .lo = 100,
      .hi = NW_DRAW_DISTANCE_MAX, .help = "100-400, object draw distance, % of PS2" },
    { .table = "video", .name = "fog", .kind = NW_SETTING_INT, NW_FIELD(fog), .lo = 0, .hi = 100,
      .help = "0-100, fog strength" },
    { .table = "video", .name = "blur", .kind = NW_SETTING_BOOL, NW_FIELD(blur),
      .help = "true: blur under the fog" },
    { .table = "video", .name = "haze", .kind = NW_SETTING_BOOL, NW_FIELD(haze),
      .help = "true: heat haze in the fire and underwater levels and over candles" },
    { .table = "video", .name = "aspect", .kind = NW_SETTING_TEXT, NW_FIELD(aspect), .parse = nw_settings_aspect_parse,
      .format = nw_settings_aspect_format,
      .help = "auto (screen or window shape), 4:3, 16:9, 16:10, 21:9, 32:9 or a ratio; PS2: 4:3" },
    { .table = "video", .name = "hud", .kind = NW_SETTING_TEXT, NW_FIELD(hud), .parse = nw_settings_hud_parse,
      .format = nw_settings_hud_format, .help = "HUD area: edges, or a centred aspect" },
    { .table = "video", .name = "gpu", .kind = NW_SETTING_TEXT, NW_FIELD(gpu), .parse = nw_settings_gpu_parse,
      .format = nw_settings_gpu_format,
      .help = "auto (Windows and the driver pick, NuWorld asks for the faster card) or a card name from the "
              "Video menu (Windows); next start" },
    { .table = "game", .name = "hardware_speed", .kind = NW_SETTING_BOOL, NW_FIELD(hardware_speed),
      .help = "true: PS2 disc and sound load times" },
    { .table = "game", .name = "subtitles", .kind = NW_SETTING_BOOL, NW_FIELD(subtitles),
      .help = "true: subtitles in the selected game language" },
    { .table = "game", .name = "title_subtitles", .kind = NW_SETTING_BOOL, NW_FIELD(title_subtitles),
      .help = "true: include the opening logos and title when subtitles are enabled" },
    { .table = "game", .name = "retail_menu", .kind = NW_SETTING_BOOL, NW_FIELD(retail_menu),
      .help = "true: PS2 logos and title cutscene; false: NuWorld title menu; next start" },
    { .table = "game", .name = "air_indicator", .kind = NW_SETTING_BOOL, NW_FIELD(air_indicator),
      .help = "true: ring under Crash in the air, where he lands" },
    { .table = "game", .name = "boss_health", .kind = NW_SETTING_BOOL, NW_FIELD(boss_health),
      .help = "true: boss bar split into phases" },
    { .table = "game", .name = "wombat", .kind = NW_SETTING_BOOL, NW_FIELD(wombat),
      .help = "true: new game named WOMBAT starts with everything open, as on GameCube and Xbox" },
    { .table = "game", .name = "hub_minigame", .kind = NW_SETTING_BOOL, NW_FIELD(hub_minigame),
      .help = "true: Crunch on the warp room top floor; X next to him plays the cut loading screen minigame" },
    { .table = "game", .name = "long_time_names", .kind = NW_SETTING_BOOL, NW_FIELD(long_time_names),
      .help = "true: time trial names up to 8 letters; false: 3, as on PS2" },
    { .table = "game", .name = "relic_spin", .kind = NW_SETTING_BOOL, NW_FIELD(relic_spin),
      .help = "true: the relic next to a new time trial record turns, its glow widening and narrowing with it" },
    { .table = "game", .name = "camera_stability", .kind = NW_SETTING_INT, NW_FIELD(camera_stability), .lo = 0,
      .hi = 100, .help = "0-100, share of Crash's jump the camera ignores; 0: PS2, 100: holds height until landing" },
    { .table = "game", .name = "hub_gem", .kind = NW_SETTING_BOOL, NW_FIELD(hub_gem), .hidden = true },
    { .table = "game", .name = "first_boss_cut", .kind = NW_SETTING_BOOL, NW_FIELD(first_boss_cut),
      .help = "true: Aku Aku's cutscene after Warp Room 1's boss is first beaten, never shown on PS2" },
    { .table = "game", .name = "vehicle_hints", .kind = NW_SETTING_BOOL, NW_FIELD(vehicle_hints),
      .help = "true: a vehicle's buttons in a corner the first time a save rides it" },
    { .table = "game", .name = "bug_fixes", .kind = NW_SETTING_BOOL, NW_FIELD(bug_fixes),
      .help = "true: fixes PS2 bugs: a body slam starting on the ground, an exit portal missed in a slide, the Firefly's "
              "goggles left behind in the victory dance, blurred character shadows, invisible spots in Crashteroids "
              "that blow up Coco's ship, Crashteroids' time trial clock still there after stations are hit" },
    { .table = "game", .sub = "movement", .name = "monkey_bar_speed", .kind = NW_SETTING_INT,
      NW_FIELD(movement.monkey_bar_speed), .lo = 100, .hi = 500,
      .help = "100-500, Crash's monkey bar speed, % of PS2; N. Hanced: 335" },
    NW_CRASH_MOVEMENT(NW_CRASH_SWITCH_KEY, NW_CRASH_FLAG_KEY, NW_CRASH_VALUE_KEY)
    NW_COCO_MOVEMENT(NW_COCO_SWITCH_KEY, NW_COCO_FLAG_KEY, NW_COCO_VALUE_KEY)
    NW_MECH_MOVEMENT(NW_MECH_SWITCH_KEY, NW_MECH_FLAG_KEY, NW_MECH_VALUE_KEY)
    NW_SUB_MOVEMENT(NW_SUB_SWITCH_KEY, NW_SUB_FLAG_KEY, NW_SUB_VALUE_KEY)
    NW_JEEP_MOVEMENT(NW_JEEP_SWITCH_KEY, NW_JEEP_FLAG_KEY, NW_JEEP_VALUE_KEY)
    NW_ATLAS_MOVEMENT(NW_ATLAS_SWITCH_KEY, NW_ATLAS_FLAG_KEY, NW_ATLAS_VALUE_KEY)
    NW_FIREFLY_MOVEMENT(NW_FIREFLY_SWITCH_KEY, NW_FIREFLY_FLAG_KEY, NW_FIREFLY_VALUE_KEY)
    NW_PLANE_MOVEMENT(NW_PLANE_SWITCH_KEY, NW_PLANE_FLAG_KEY, NW_PLANE_VALUE_KEY)
    NW_SPACE_MOVEMENT(NW_SPACE_SWITCH_KEY, NW_SPACE_FLAG_KEY, NW_SPACE_VALUE_KEY)
    NW_TT_MOVEMENT(NW_TT_SWITCH_KEY, NW_CRASH_NONE, NW_CRASH_NONE)
    { .table = "audio", .name = "master_volume", .kind = NW_SETTING_INT, NW_FIELD(master_volume), .lo = 0, .hi = 100,
      .help = "0-100, on top of the game's music and SFX volumes" },
    { .table = "audio", .name = "voice_volume", .kind = NW_SETTING_INT, NW_FIELD(voice_volume), .lo = 0, .hi = 100,
      .help = "0-100, cutscene and hint voices" },
    { .table = "discord", .name = "enabled", .kind = NW_SETTING_BOOL, NW_FIELD(discord),
      .help = "true: level, warp room or menu on your Discord profile" },
    { .table = "updates", .name = "check", .kind = NW_SETTING_BOOL, NW_FIELD(updates),
      .help = "true: checks GitHub for a newer NuWorld at start, shown in the title menu" },
    { .table = "log", .name = "level", .kind = NW_SETTING_TEXT, NW_FIELD(log), .parse = nw_settings_log_parse,
      .format = nw_settings_log_format,
      .help = "nuworld.log level: trace, debug, info, warn or error, then per-area overrides, e.g. "
              "\"info,gfx=debug,decomp=debug\" (app, game, decomp, gfx, gl, audio, load, save, input, net, port, "
              "stdio, sdl)" },
    { .table = "log", .name = "gl_debug", .kind = NW_SETTING_BOOL, NW_FIELD(gl_debug),
      .help = "true: logs graphics driver warnings and errors; next start" },
    { .table = "log", .name = "minidump", .kind = NW_SETTING_BOOL, NW_FIELD(minidump),
      .help = "true: minidump to crashes/ on a crash (Windows), for bug reports" },
    { .table = "input", .name = "left_deadzone", .kind = NW_SETTING_INT, NW_FIELD(controls.left_deadzone), .lo = 0,
      .hi = 90, .help = "0-90, left stick deadzone %; the game adds its own" },
    { .table = "input", .name = "right_deadzone", .kind = NW_SETTING_INT, NW_FIELD(controls.right_deadzone), .lo = 0,
      .hi = 90, .help = "0-90, right stick deadzone %" },
    { .table = "input", .name = "trigger_press", .kind = NW_SETTING_INT, NW_FIELD(controls.trigger_press), .lo = 1,
      .hi = 100, .help = "1-100, trigger press point %" },
    { .table = "input", .name = "rumble", .kind = NW_SETTING_INT, NW_FIELD(controls.rumble), .lo = 0, .hi = 100,
      .help = "0-100, rumble strength; the game's Vibration option turns it on and off" },
    { .table = "input", .name = "prompts", .kind = NW_SETTING_ENUM, NW_FIELD(controls.prompts),
      .hi = NW_PROMPT_STYLES, .names = nw_control_prompt_keys,
      .help = "button prompts: auto (last device used), xbox, playstation, nintendo, deck or keyboard" },
    { .table = "pad", .name = "<button>", .kind = NW_SETTING_BINDS, NW_FIELD(controls.pad), .hi = NW_CONTROL_BUTTONS,
      .source = nw_controls_pad_source, .source_name = nw_controls_pad_name,
      .help = "gamepad buttons by position: south (cross), east, west, north, leftshoulder, "
              "lefttrigger, leftstick, back, start, dpup, ...; [] none" },
    { .table = "keyboard", .name = "<button>", .kind = NW_SETTING_BINDS, NW_FIELD(controls.keys),
      .hi = NW_CONTROL_TARGETS, .source = nw_controls_key_source, .source_name = nw_controls_key_name,
      .help = "keys (\"Space\", \"Left Shift\", \"E\", ...), also left_stick_up, left_stick_down, "
              "left_stick_left, left_stick_right and right_stick_*; [] none" },
};

void nw_settings_defaults(NwSettings *s)
{
    s->subtitles = false;
    s->title_subtitles = false;
    s->hardware_speed = false;
    s->retail_menu = false;
    s->movement = (NwMovement)NW_MOVEMENT_NHANCED;
    s->air_indicator = true;
    s->boss_health = true;
    s->wombat = true;
    s->hub_minigame = true;
    s->long_time_names = false;
    s->relic_spin = true;
    s->camera_stability = 50;
    s->hub_gem = true;
    s->first_boss_cut = true;
    s->vehicle_hints = true;
    s->bug_fixes = true;
    s->master_volume = 100;
    s->voice_volume = 100;
    s->discord = true;
    s->updates = true;
    SDL_zero(s->log);
    SDL_strlcpy(s->log, "info", sizeof(s->log));
    s->gl_debug = false;
    s->minidump = true;
    s->aspect = 0.0f;
    s->hud = 16.0f / 9.0f;
    s->window = NW_WINDOW_WINDOWED;
    s->resolution = (NwResolution){ 0, 0 };
    s->vsync = NW_VSYNC_ON;
    s->fps = 0;
    s->interpolation = true;
    s->scale = 0;
    s->msaa = 0;
    s->filter = NW_FILTER_RETAIL;
    s->gems = true;
    s->draw_distance = 400;
    s->fog = 40;
    s->blur = false;
    s->haze = false;
    SDL_zero(s->gpu);
    SDL_strlcpy(s->gpu, "auto", sizeof(s->gpu));
    nw_controls_defaults(&s->controls);
}

bool nw_settings_equal(const NwSettings *a, const NwSettings *b)
{
    for (size_t i = 0; i < SDL_arraysize(nw_settings_keys); i++) {
        const NwSettingKey *k = &nw_settings_keys[i];
        if (SDL_memcmp((const char *)a + k->offset, (const char *)b + k->offset, k->size) != 0) {
            return false;
        }
    }
    return true;
}

void nw_settings_level_fog(NwSettings *s, int level, int *kept)
{
    bool skyless = level == NW_SETTINGS_SKYLESS_LEVEL;
    if (skyless && *kept < 0) {
        *kept = s->fog;
        s->fog = 100;
    } else if (!skyless && *kept >= 0) {
        s->fog = *kept;
        *kept = -1;
    }
}

/* Returns 1, 0 when a non-TOML file was moved to .corrupt, -1 on a failure (doc is then freed). */
static int nw_settings_doc(const char *path, NwDoc *doc)
{
    if (!nw_doc_init(doc)) {
        return -1;
    }
    if (!SDL_GetPathInfo(path, NULL)) {
        return 1;
    }
    size_t size = 0;
    char *text = SDL_LoadFile(path, &size);
    if (text && nw_doc_parse(doc, text, size)) {
        SDL_free(text);
        return 1;
    }
    char why[256], *aside = NULL;
    SDL_strlcpy(why, SDL_GetError(), sizeof(why));
    nw_doc_free(doc);
    bool moved = text && nw_doc_init(doc);
    SDL_free(text);
    if (moved) {
        moved = SDL_asprintf(&aside, "%s.corrupt", path) >= 0 && SDL_RenamePath(path, aside);
        SDL_free(aside);
        if (!moved) {
            nw_doc_free(doc);
        }
    }
    if (moved) {
        SDL_SetError("%s; moved to " NW_PATHS_SETTINGS ".corrupt", why);
    }
    return moved ? 0 : -1;
}

static void nw_settings_take(const NwSettingKey *k, const NwDocValue *v, NwSettings *s)
{
    char *field = (char *)s + k->offset, text[NW_GPU_NAME];
    if (k->kind == NW_SETTING_BOOL && v->type == NW_DOC_BOOL) {
        *(bool *)field = v->i != 0;
    } else if (k->kind == NW_SETTING_INT && v->type == NW_DOC_INT) {
        int i = v->i < k->lo ? k->lo : v->i > k->hi ? k->hi : (int)v->i;
        if (!k->step || (i - k->lo) % k->step == 0) {
            *(int *)field = i;
        }
    } else if (k->kind == NW_SETTING_INT && k->zero && v->type == NW_DOC_STR && v->len == SDL_strlen(k->zero) &&
               SDL_memcmp(v->bytes, k->zero, v->len) == 0) {
        *(int *)field = 0;
    } else if ((k->kind == NW_SETTING_ENUM || k->kind == NW_SETTING_TEXT) && v->type == NW_DOC_STR &&
               v->len < sizeof(text)) {
        SDL_memcpy(text, v->bytes, v->len);
        text[v->len] = '\0';
        for (int i = 0; k->kind == NW_SETTING_ENUM && i < k->hi; i++) {
            if (SDL_strcmp(text, k->names[i]) == 0) {
                *(int *)field = i;
            }
        }
        if (k->kind == NW_SETTING_TEXT) {
            k->parse(text, field);
        }
    }
}

static void nw_settings_take_binds(const NwSettingKey *k, const NwDocValue *v, uint16_t *binds)
{
    uint16_t got[NW_CONTROL_BINDS] = { 0 };
    const NwDocValue *items = v->type == NW_DOC_ARRAY ? v->items : v;
    size_t count = v->type == NW_DOC_ARRAY ? v->len : 1;
    int n = 0;
    if (v->type != NW_DOC_ARRAY && v->type != NW_DOC_STR) {
        return;
    }
    for (size_t i = 0; i < count && n < NW_CONTROL_BINDS; i++) {
        char name[32];
        if (items[i].type != NW_DOC_STR || items[i].len >= sizeof(name)) {
            continue;
        }
        SDL_memcpy(name, items[i].bytes, items[i].len);
        name[items[i].len] = '\0';
        uint16_t source = k->source(name);
        bool seen = false;
        for (int j = 0; j < n; j++) {
            seen = seen || got[j] == source;
        }
        if (source && !seen) {
            got[n++] = source;
        }
    }
    SDL_memcpy(binds, got, sizeof(got));
}

bool nw_settings_load(NwSettings *s, const char *root)
{
    NwDoc doc;
    char *path = nw_settings_path(root);
    int got = path ? nw_settings_doc(path, &doc) : -1;
    SDL_free(path);
    if (got < 0) {
        return false;
    }
    for (size_t i = 0; i < SDL_arraysize(nw_settings_keys); i++) {
        const NwSettingKey *k = &nw_settings_keys[i];
        const char *header[3];
        const NwDocTable *t = nw_doc_table(&doc, header, nw_settings_header(k, header));
        if (k->kind == NW_SETTING_BINDS) {
            uint16_t *binds = (uint16_t *)((char *)s + k->offset);
            for (int j = 0; t && j < k->hi; j++) {
                const NwDocValue *v = nw_doc_get(t, nw_control_targets[j].key);
                if (v) {
                    nw_settings_take_binds(k, v, binds + j * NW_CONTROL_BINDS);
                }
            }
            continue;
        }
        const NwDocValue *v = t ? nw_doc_get(t, k->name) : NULL;
        if (v) {
            nw_settings_take(k, v, s);
        }
    }
    /* A file from before the vehicles' options that chose the PS2's movement keeps it. */
    const char *crash[] = { "game", "movement", "crash" }, *mech[] = { "game", "movement", "mech" };
    const char *coco[] = { "game", "movement", "coco" };
    const char *submarine[] = { "game", "movement", "submarine" }, *jeep[] = { "game", "movement", "jeep" };
    const char *atlasphere[] = { "game", "movement", "atlasphere" }, *firefly[] = { "game", "movement", "firefly" };
    const char *plane[] = { "game", "movement", "plane" }, *spacefighter[] = { "game", "movement", "spacefighter" };
    const char *time_trial[] = { "game", "movement", "time_trial" };
    if (s->movement.monkey_bar_speed <= 100 && !nw_doc_table(&doc, crash, 3)) {
        s->movement.crash = ((NwMovement)NW_MOVEMENT_RETAIL).crash;
    }
#define NW_CRASH_SET(f, ...) || s->movement.crash.f
    bool retail =
        s->movement.monkey_bar_speed <= 100 && !(false NW_CRASH_MOVEMENT(NW_CRASH_SET, NW_CRASH_NONE, NW_CRASH_NONE));
#undef NW_CRASH_SET
    if (retail && !nw_doc_table(&doc, coco, 3)) {
        s->movement.coco = ((NwMovement)NW_MOVEMENT_RETAIL).coco;
    }
    if (retail && !nw_doc_table(&doc, mech, 3)) {
        s->movement.mech = ((NwMovement)NW_MOVEMENT_RETAIL).mech;
    }
    if (retail && !nw_doc_table(&doc, submarine, 3)) {
        s->movement.submarine = ((NwMovement)NW_MOVEMENT_RETAIL).submarine;
    }
    if (retail && !nw_doc_table(&doc, jeep, 3)) {
        s->movement.jeep = ((NwMovement)NW_MOVEMENT_RETAIL).jeep;
    }
    if (retail && !nw_doc_table(&doc, atlasphere, 3)) {
        s->movement.atlasphere = ((NwMovement)NW_MOVEMENT_RETAIL).atlasphere;
    }
    if (retail && !nw_doc_table(&doc, firefly, 3)) {
        s->movement.firefly = ((NwMovement)NW_MOVEMENT_RETAIL).firefly;
    }
    if (retail && !nw_doc_table(&doc, plane, 3)) {
        s->movement.plane = ((NwMovement)NW_MOVEMENT_RETAIL).plane;
    }
    if (retail && !nw_doc_table(&doc, spacefighter, 3)) {
        s->movement.spacefighter = ((NwMovement)NW_MOVEMENT_RETAIL).spacefighter;
    }
    if (retail && !nw_doc_table(&doc, time_trial, 3)) {
        s->movement.time_trial = ((NwMovement)NW_MOVEMENT_RETAIL).time_trial;
    }
    /* Version 2 changed these defaults: an older file still on the old ones takes the new ones. */
    const NwDocValue *version = nw_doc_get(doc.tables[0], "version");
    if (doc.count > 1 && !(version && version->type == NW_DOC_INT && version->i >= 2)) {
        if (s->camera_stability == 100) {
            s->camera_stability = 50;
        }
        if (s->fog == 100) {
            s->fog = 40;
        }
        s->blur = false;
        s->haze = false;
    }
    nw_doc_free(&doc);
    return got > 0;
}

static bool nw_settings_put_binds(NwDoc *doc, NwDocTable *t, const NwSettingKey *k, const uint16_t *binds)
{
    for (int j = 0; j < k->hi; j++, binds += NW_CONTROL_BINDS) {
        NwDocValue v;
        size_t n = 0;
        while (n < NW_CONTROL_BINDS && binds[n]) {
            n++;
        }
        if (!nw_doc_array(&v, n)) {
            return false;
        }
        for (size_t i = 0; i < n; i++) {
            const char *name = k->source_name(binds[i]);
            if (!nw_doc_str(&v.items[i], name, SDL_strlen(name))) {
                nw_doc_value_free(&v);
                return false;
            }
        }
        if (!nw_doc_set(doc, t, nw_control_targets[j].key, &v)) {
            return false;
        }
    }
    return true;
}

static bool nw_settings_put(NwDoc *doc, const NwSettingKey *k, const NwSettings *s)
{
    const char *field = (const char *)s + k->offset;
    const char *header[3];
    NwDocTable *t = nw_doc_add_table(doc, header, nw_settings_header(k, header));
    NwDocValue v;
    char buf[32];
    if (!t) {
        return false;
    }
    if (k->kind == NW_SETTING_BINDS) {
        return nw_settings_put_binds(doc, t, k, (const uint16_t *)field);
    }
    if (k->kind == NW_SETTING_BOOL) {
        v = nw_doc_bool(*(const bool *)field);
    } else if (k->kind == NW_SETTING_INT && !(k->zero && *(const int *)field == 0)) {
        v = nw_doc_int(*(const int *)field, 0);
    } else {
        const char *text = k->kind == NW_SETTING_INT    ? k->zero
                           : k->kind == NW_SETTING_ENUM ? k->names[*(const int *)field]
                                                        : k->format(field, buf, sizeof(buf));
        if (!nw_doc_str(&v, text, SDL_strlen(text))) {
            return false;
        }
    }
    return nw_doc_set(doc, t, k->name, &v);
}

static char *nw_settings_help(void)
{
    char *help = SDL_strdup(NW_APP_NAME " settings. Keys this version does not know are kept.\n\n"
                            "version: the defaults this file has taken, set by " NW_APP_NAME "\n");
    for (size_t i = 0; help && i < SDL_arraysize(nw_settings_keys); i++) {
        const NwSettingKey *k = &nw_settings_keys[i];
        char *more = NULL;
        if (k->hidden) {
            continue;
        }
        if (SDL_asprintf(&more, "%s%s.%s%s%s%s%s: %s\n", help, k->table, k->sub ? k->sub : "", k->sub ? "." : "",
                         k->sub2 ? k->sub2 : "", k->sub2 ? "." : "", k->name, k->help) < 0) {
            more = NULL;
        }
        SDL_free(help);
        help = more;
    }
    return help;
}

bool nw_settings_save(const NwSettings *s, const char *root)
{
    NwDoc doc;
    char *path = nw_settings_path(root), *tmp = NULL, *help = nw_settings_help(), *text = NULL;
    size_t size = 0;
    bool ok = path && help && SDL_asprintf(&tmp, "%s.tmp", path) >= 0 && nw_settings_doc(path, &doc) >= 0;
    if (ok) {
        NwDocValue version = nw_doc_int(NW_SETTINGS_VERSION, 0);
        ok = nw_doc_set(&doc, doc.tables[0], "version", &version);
        NwSettings def;
        nw_settings_defaults(&def);
        for (size_t i = 0; ok && i < SDL_arraysize(nw_settings_keys); i++) {
            const NwSettingKey *k = &nw_settings_keys[i];
            if (k->hidden && SDL_memcmp((const char *)s + k->offset, (const char *)&def + k->offset, k->size) == 0) {
                const char *header[3];
                NwDocTable *t = nw_doc_table(&doc, header, nw_settings_header(k, header));
                if (t) {
                    nw_doc_unset(t, k->name);
                }
                continue;
            }
            ok = nw_settings_put(&doc, k, s);
        }
        text = ok ? nw_doc_write(&doc, help, &size) : NULL;
        nw_doc_free(&doc);
        ok = text && SDL_SaveFile(tmp, text, size);
        if (ok && !SDL_RenamePath(tmp, path)) {
            char why[256];
            SDL_strlcpy(why, SDL_GetError(), sizeof(why));
            SDL_RemovePath(tmp);
            SDL_SetError("%s", why);
            ok = false;
        }
    }
    SDL_free(text);
    SDL_free(help);
    SDL_free(tmp);
    SDL_free(path);
    return ok;
}

static const char *const nw_settings_parts[NW_MOVEMENT_PARTS] = { "crash", "coco", "mech", "submarine", "jeep",
                                                                  "atlasphere", "firefly", "plane", "spacefighter",
                                                                  "time_trial" };

static int nw_settings_part(const NwSettingKey *k)
{
    if (!k->sub || SDL_strcmp(k->sub, "movement") != 0) {
        return -1;
    }
    if (!k->sub2) {
        return NW_PART_CRASH;
    }
    for (int p = 0; p < NW_MOVEMENT_PARTS; p++) {
        if (SDL_strcmp(k->sub2, nw_settings_parts[p]) == 0) {
            return p;
        }
    }
    return -1;
}

void nw_settings_profile_name(char *name)
{
    size_t n = 0;
    for (const char *c = name; *c; c++) {
        if (*c >= 0x20 && *c < 0x7F && !SDL_strchr("<>:\"/\\|?*", *c) && (n || *c != ' ')) {
            name[n++] = *c;
        }
    }
    while (n && (name[n - 1] == ' ' || name[n - 1] == '.')) {
        n--;
    }
    name[n] = '\0';
}

char *nw_settings_profile_text(const NwMovement *m, unsigned parts, const char *name, size_t *size)
{
    NwSettings s = { .movement = *m };
    const char *profile[] = { "profile" };
    NwDoc doc;
    NwDocValue v;
    if (!nw_doc_init(&doc)) {
        return NULL;
    }
    NwDocTable *t = nw_doc_add_table(&doc, profile, 1);
    bool ok = t && nw_doc_str(&v, name, SDL_strlen(name)) && nw_doc_set(&doc, t, "name", &v);
    for (size_t i = 0; ok && i < SDL_arraysize(nw_settings_keys); i++) {
        int p = nw_settings_part(&nw_settings_keys[i]);
        if (p >= 0 && (parts & (1u << p))) {
            ok = nw_settings_put(&doc, &nw_settings_keys[i], &s);
        }
    }
    char *text = ok ? nw_doc_write(&doc, NW_APP_NAME " movement profile", size) : NULL;
    nw_doc_free(&doc);
    return text;
}

void nw_settings_profile_apply(NwMovement *to, const NwMovement *from, unsigned parts)
{
    if (parts & (1u << NW_PART_CRASH)) {
        to->monkey_bar_speed = from->monkey_bar_speed;
        to->crash = from->crash;
    }
    if (parts & (1u << NW_PART_COCO)) {
        to->coco = from->coco;
    }
    if (parts & (1u << NW_PART_MECH)) {
        to->mech = from->mech;
    }
    if (parts & (1u << NW_PART_SUBMARINE)) {
        to->submarine = from->submarine;
    }
    if (parts & (1u << NW_PART_JEEP)) {
        to->jeep = from->jeep;
    }
    if (parts & (1u << NW_PART_ATLASPHERE)) {
        to->atlasphere = from->atlasphere;
    }
    if (parts & (1u << NW_PART_FIREFLY)) {
        to->firefly = from->firefly;
    }
    if (parts & (1u << NW_PART_PLANE)) {
        to->plane = from->plane;
    }
    if (parts & (1u << NW_PART_SPACEFIGHTER)) {
        to->spacefighter = from->spacefighter;
    }
    if (parts & (1u << NW_PART_TIME_TRIAL)) {
        to->time_trial = from->time_trial;
    }
}

unsigned nw_settings_profile_read(const char *text, size_t size, NwMovement *m, char *name, size_t name_size)
{
    static const NwMovement nhanced = NW_MOVEMENT_NHANCED;
    const char *path[] = { "game", "movement", NULL }, *profile[] = { "profile" };
    unsigned parts = 0;
    NwDoc doc;
    if (name_size) {
        name[0] = '\0';
    }
    if (!nw_doc_init(&doc)) {
        return 0;
    }
    if (!nw_doc_parse(&doc, text, size)) {
        nw_doc_free(&doc);
        return 0;
    }
    for (int p = 0; p < NW_MOVEMENT_PARTS; p++) {
        path[2] = nw_settings_parts[p];
        if (nw_doc_table(&doc, path, 3)) {
            parts |= 1u << p;
        }
    }
    const NwDocTable *movement = nw_doc_table(&doc, path, 2);
    if (movement && nw_doc_get(movement, "monkey_bar_speed")) {
        parts |= 1u << NW_PART_CRASH;
    }
    NwSettings s = { .movement = *m };
    nw_settings_profile_apply(&s.movement, &nhanced, parts);
    for (size_t i = 0; i < SDL_arraysize(nw_settings_keys); i++) {
        const NwSettingKey *k = &nw_settings_keys[i];
        int p = nw_settings_part(k);
        if (p < 0 || !(parts & (1u << p))) {
            continue;
        }
        const char *header[3];
        const NwDocTable *t = nw_doc_table(&doc, header, nw_settings_header(k, header));
        const NwDocValue *v = t ? nw_doc_get(t, k->name) : NULL;
        if (v) {
            nw_settings_take(k, v, &s);
        }
    }
    const NwDocTable *t = nw_doc_table(&doc, profile, 1);
    const NwDocValue *v = t ? nw_doc_get(t, "name") : NULL;
    if (parts && v && v->type == NW_DOC_STR && name_size) {
        size_t n = SDL_min(v->len, name_size - 1);
        SDL_memcpy(name, v->bytes, n);
        name[n] = '\0';
        nw_settings_profile_name(name);
    }
    nw_doc_free(&doc);
    if (parts) {
        *m = s.movement;
    }
    return parts;
}
