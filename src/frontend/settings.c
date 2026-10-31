#include "frontend/settings.h"

#include <stddef.h>

#include <SDL3/SDL.h>

#include "app.h"
#include "platform/paths.h"
#include "save/savedoc.h"

typedef enum NwSettingKind {
    NW_SETTING_BOOL,
    NW_SETTING_INT,  /* clamped to lo..hi, kept when off the step from lo; 0 also by the name zero */
    NW_SETTING_ENUM, /* an enum (int-sized) by its names */
    NW_SETTING_TEXT, /* a string, through parse and format */
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
} NwSettingKey;

#define NW_FIELD(f) .offset = offsetof(NwSettings, f), .size = sizeof(((NwSettings *)0)->f)

/* k's table as a path; returns its depth. */
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

static const char *const nw_settings_windows[NW_WINDOW_MODES] = { "windowed", "borderless", "fullscreen" };
static const char *const nw_settings_vsyncs[NW_VSYNC_MODES] = { "off", "on", "adaptive" };
static const char *const nw_settings_filters[NW_FILTERS] = { "retail", "nearest", "bilinear" };
_Static_assert(sizeof(NwWindowMode) == sizeof(int) && sizeof(NwVsync) == sizeof(int) &&
                   sizeof(NwTexFilter) == sizeof(int) && sizeof(NwPromptStyle) == sizeof(int),
               "NW_SETTING_ENUM fields are int-sized");

#define NW_CRASH_KEY(f) .table = "game", .sub = "movement", .sub2 = "crash", .name = #f, NW_FIELD(movement.crash.f)
#define NW_CRASH_SWITCH_KEY(f, label, text) { NW_CRASH_KEY(f), .kind = NW_SETTING_BOOL, .help = "true: " text },
#define NW_CRASH_FLAG_KEY(f, label, def, text) { NW_CRASH_KEY(f), .kind = NW_SETTING_BOOL, .help = "true: " text },
#define NW_CRASH_VALUE_KEY(f, label, def, lo_, hi_, text) \
    { NW_CRASH_KEY(f), .kind = NW_SETTING_INT, .lo = lo_, .hi = hi_, .help = #lo_ "-" #hi_ ", " text },
#define NW_MECH_KEY(f) .table = "game", .sub = "movement", .sub2 = "mech", .name = #f, NW_FIELD(movement.mech.f)
#define NW_MECH_SWITCH_KEY(f, label, text) { NW_MECH_KEY(f), .kind = NW_SETTING_BOOL, .help = "true: " text },
#define NW_MECH_FLAG_KEY(f, label, def, text) { NW_MECH_KEY(f), .kind = NW_SETTING_BOOL, .help = "true: " text },
#define NW_MECH_VALUE_KEY(f, label, def, lo_, hi_, text) \
    { NW_MECH_KEY(f), .kind = NW_SETTING_INT, .lo = lo_, .hi = hi_, .help = #lo_ "-" #hi_ ", " text },

static const NwSettingKey nw_settings_keys[] = {
    { .table = "video", .name = "window", .kind = NW_SETTING_ENUM, NW_FIELD(window), .hi = NW_WINDOW_MODES,
      .names = nw_settings_windows,
      .help = "windowed, borderless (fullscreen at the desktop's resolution) or fullscreen (exclusive)" },
    { .table = "video", .name = "resolution", .kind = NW_SETTING_TEXT, NW_FIELD(resolution),
      .parse = nw_settings_resolution_parse, .format = nw_settings_resolution_format,
      .help = "auto or WxH pixels: the window's size, or fullscreen the display's mode (auto: the desktop's)" },
    { .table = "video", .name = "vsync", .kind = NW_SETTING_ENUM, NW_FIELD(vsync), .hi = NW_VSYNC_MODES,
      .names = nw_settings_vsyncs, .help = "off, on, or adaptive (a late frame shows at once; on where unsupported)" },
    { .table = "video", .name = "fps", .kind = NW_SETTING_INT, NW_FIELD(fps), .lo = 0, .hi = NW_FPS_MAX,
      .zero = "refresh", .help = "refresh (the display's rate) or the most frames a second: 60, 120, 240, ..." },
    { .table = "video", .name = "interpolation", .kind = NW_SETTING_BOOL, NW_FIELD(interpolation),
      .help = "true draws the frames between the game's own blended, one game frame later; false as the PS2" },
    { .table = "video", .name = "scale", .kind = NW_SETTING_INT, NW_FIELD(scale), .lo = 0, .hi = NW_BACKEND_SCALE_MAX,
      .zero = "auto", .help = "internal resolution: auto (the screen's or more) or 1-8 times the PS2's" },
    { .table = "video", .name = "msaa", .kind = NW_SETTING_INT, NW_FIELD(msaa), .lo = 0, .hi = 16,
      .help = "multisampling: 0 (off), 2, 4 or 8 samples, at most the GPU's" },
    { .table = "video", .name = "filter", .kind = NW_SETTING_ENUM, NW_FIELD(filter), .hi = NW_FILTERS,
      .names = nw_settings_filters, .help = "textures: retail (the PS2's, per texture), nearest or bilinear" },
    { .table = "video", .name = "aspect", .kind = NW_SETTING_TEXT, NW_FIELD(aspect), .parse = nw_settings_aspect_parse,
      .format = nw_settings_aspect_format,
      .help = "auto (the screen's or window's shape), 4:3, 16:9, 16:10, 21:9, 32:9 or a ratio; 4:3 is the PS2's" },
    { .table = "video", .name = "hud", .kind = NW_SETTING_TEXT, NW_FIELD(hud), .parse = nw_settings_hud_parse,
      .format = nw_settings_hud_format, .help = "how far out the HUD goes: edges, or an aspect it stays inside, centred" },
    { .table = "game", .name = "hardware_speed", .kind = NW_SETTING_BOOL, NW_FIELD(hardware_speed),
      .help = "true simulates original hardware speed: the PS2's disc and sound load times" },
    { .table = "game", .name = "hz", .kind = NW_SETTING_INT, NW_FIELD(hz), .lo = 50, .hi = 60, .step = 10,
      .help = "the game's speed: 60 (NTSC) or 50 (PAL, the original's); from the next start" },
    { .table = "game", .name = "unused_title", .kind = NW_SETTING_BOOL, NW_FIELD(unused_title),
      .help = "true starts in the game's unused title menu, false with the logos and the title cut as the PS2; "
              "from the next start" },
    { .table = "game", .name = "air_indicator", .kind = NW_SETTING_BOOL, NW_FIELD(air_indicator),
      .help = "true shows a ring on the floor under Crash while he is in the air, where he lands" },
    { .table = "game", .sub = "movement", .name = "monkey_bar_speed", .kind = NW_SETTING_INT,
      NW_FIELD(movement.monkey_bar_speed), .lo = 100, .hi = 500,
      .help = "N. Hanced Movement: Crash's speed on the monkey bars, 100-500 percent of the PS2's; 350 is N. Hanced's" },
    NW_CRASH_MOVEMENT(NW_CRASH_SWITCH_KEY, NW_CRASH_FLAG_KEY, NW_CRASH_VALUE_KEY)
    NW_MECH_MOVEMENT(NW_MECH_SWITCH_KEY, NW_MECH_FLAG_KEY, NW_MECH_VALUE_KEY)
    { .table = "audio", .name = "master_volume", .kind = NW_SETTING_INT, NW_FIELD(master_volume), .lo = 0, .hi = 100,
      .help = "0-100, over the game's own music and SFX volumes" },
    { .table = "audio", .name = "voice_volume", .kind = NW_SETTING_INT, NW_FIELD(voice_volume), .lo = 0, .hi = 100,
      .help = "0-100, the cutscene and hint voices" },
    { .table = "input", .name = "left_deadzone", .kind = NW_SETTING_INT, NW_FIELD(controls.left_deadzone), .lo = 0,
      .hi = 90, .help = "0-90: how far (percent) the left stick counts as centred; the game has its own after it" },
    { .table = "input", .name = "right_deadzone", .kind = NW_SETTING_INT, NW_FIELD(controls.right_deadzone), .lo = 0,
      .hi = 90, .help = "0-90, the right stick's" },
    { .table = "input", .name = "trigger_press", .kind = NW_SETTING_INT, NW_FIELD(controls.trigger_press), .lo = 1,
      .hi = 100, .help = "1-100: how far (percent) a trigger goes down before it presses its button" },
    { .table = "input", .name = "rumble", .kind = NW_SETTING_INT, NW_FIELD(controls.rumble), .lo = 0, .hi = 100,
      .help = "0-100: the gamepad's rumble strength; the game's Vibration option turns it on and off" },
    { .table = "input", .name = "prompts", .kind = NW_SETTING_ENUM, NW_FIELD(controls.prompts),
      .hi = NW_PROMPT_STYLES, .names = nw_control_prompt_keys,
      .help = "the buttons the game shows: auto (the device used last), xbox, playstation, nintendo, deck or "
              "keyboard" },
    { .table = "pad", .name = "<button>", .kind = NW_SETTING_BINDS, NW_FIELD(controls.pad), .hi = NW_CONTROL_BUTTONS,
      .source = nw_controls_pad_source, .source_name = nw_controls_pad_name,
      .help = "the gamepad's buttons for it, by position: south (cross), east, west, north, leftshoulder, "
              "lefttrigger, leftstick, back, start, dpup, ...; [] none" },
    { .table = "keyboard", .name = "<button>", .kind = NW_SETTING_BINDS, NW_FIELD(controls.keys),
      .hi = NW_CONTROL_TARGETS, .source = nw_controls_key_source, .source_name = nw_controls_key_name,
      .help = "the keys for it (\"Space\", \"Left Shift\", \"E\", ...), also for left_stick_up, left_stick_down, "
              "left_stick_left, left_stick_right and right_stick_*; [] none" },
};

void nw_settings_defaults(NwSettings *s)
{
    s->hardware_speed = false;
    s->hz = 60;
    s->unused_title = true;
    s->movement = (NwMovement)NW_MOVEMENT_NHANCED;
    s->air_indicator = true;
    s->master_volume = 100;
    s->voice_volume = 100;
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

/* path's document, empty when there is no file; one that is not TOML is moved to .corrupt first. Returns 1, 0 when
   it was moved (the SDL error says why), -1 on a failure (doc is then freed). */
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
    char *field = (char *)s + k->offset, text[64];
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

/* A target's sources from a name or an array of names; names k does not know are passed over. */
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
    /* A file from before Crash's or the mech's options that chose the PS2's movement keeps it. */
    const char *crash[] = { "game", "movement", "crash" }, *mech[] = { "game", "movement", "mech" };
    if (s->movement.monkey_bar_speed <= 100 && !nw_doc_table(&doc, crash, 3)) {
        s->movement.crash = ((NwMovement)NW_MOVEMENT_RETAIL).crash;
    }
#define NW_CRASH_SET(f, ...) || s->movement.crash.f
    bool retail =
        s->movement.monkey_bar_speed <= 100 && !(false NW_CRASH_MOVEMENT(NW_CRASH_SET, NW_CRASH_NONE, NW_CRASH_NONE));
#undef NW_CRASH_SET
    if (retail && !nw_doc_table(&doc, mech, 3)) {
        s->movement.mech = ((NwMovement)NW_MOVEMENT_RETAIL).mech;
    }
    nw_doc_free(&doc);
    return got > 0;
}

/* Every target's sources as an array of names. */
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

/* The file's header: what each key takes. */
static char *nw_settings_help(void)
{
    char *help = SDL_strdup(NW_APP_NAME " settings. Keys this version does not know are kept.\n\n");
    for (size_t i = 0; help && i < SDL_arraysize(nw_settings_keys); i++) {
        const NwSettingKey *k = &nw_settings_keys[i];
        char *more = NULL;
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
        for (size_t i = 0; ok && i < SDL_arraysize(nw_settings_keys); i++) {
            ok = nw_settings_put(&doc, &nw_settings_keys[i], s);
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
