#include "frontend/settings.h"

#include <stddef.h>

#include <SDL3/SDL.h>

#include "app.h"
#include "platform/paths.h"
#include "save/savedoc.h"

typedef enum NwSettingKind {
    NW_SETTING_BOOL,
    NW_SETTING_INT,  /* clamped to lo..hi; 0 also by the name zero */
    NW_SETTING_ENUM, /* an enum (int-sized) by its names */
    NW_SETTING_TEXT, /* a string, through parse and format */
} NwSettingKind;

typedef struct NwSettingKey {
    const char *table, *name;
    NwSettingKind kind;
    size_t offset, size;
    int lo, hi;
    const char *const *names; /* NW_SETTING_ENUM: hi of them */
    const char *zero;
    bool (*parse)(const char *text, void *field);
    const char *(*format)(const void *field, char *buf, int size);
    const char *help;
} NwSettingKey;

#define NW_FIELD(f) .offset = offsetof(NwSettings, f), .size = sizeof(((NwSettings *)0)->f)

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
_Static_assert(sizeof(NwWindowMode) == sizeof(int) && sizeof(NwVsync) == sizeof(int),
               "NW_SETTING_ENUM fields are int-sized");

static const NwSettingKey nw_settings_keys[] = {
    { .table = "video", .name = "window", .kind = NW_SETTING_ENUM, NW_FIELD(window), .hi = NW_WINDOW_MODES,
      .names = nw_settings_windows,
      .help = "windowed, borderless (fullscreen at the desktop's resolution) or fullscreen (exclusive)" },
    { .table = "video", .name = "resolution", .kind = NW_SETTING_TEXT, NW_FIELD(resolution),
      .parse = nw_settings_resolution_parse, .format = nw_settings_resolution_format,
      .help = "auto or WxH pixels: the window's size, or fullscreen the display's mode (auto: the desktop's)" },
    { .table = "video", .name = "vsync", .kind = NW_SETTING_ENUM, NW_FIELD(vsync), .hi = NW_VSYNC_MODES,
      .names = nw_settings_vsyncs, .help = "off, on, or adaptive (a late frame shows at once; on where unsupported)" },
    { .table = "video", .name = "scale", .kind = NW_SETTING_INT, NW_FIELD(scale), .lo = 0, .hi = NW_BACKEND_SCALE_MAX,
      .zero = "auto", .help = "internal resolution: auto (the screen's or more) or 1-8 times the PS2's" },
    { .table = "video", .name = "msaa", .kind = NW_SETTING_INT, NW_FIELD(msaa), .lo = 0, .hi = 16,
      .help = "multisampling: 0 (off), 2, 4 or 8 samples, at most the GPU's" },
    { .table = "video", .name = "aspect", .kind = NW_SETTING_TEXT, NW_FIELD(aspect), .parse = nw_settings_aspect_parse,
      .format = nw_settings_aspect_format,
      .help = "auto (the screen's or window's shape), 4:3, 16:9, 16:10, 21:9, 32:9 or a ratio; 4:3 is the PS2's" },
    { .table = "video", .name = "hud", .kind = NW_SETTING_TEXT, NW_FIELD(hud), .parse = nw_settings_hud_parse,
      .format = nw_settings_hud_format, .help = "how far out the HUD goes: edges, or an aspect it stays inside, centred" },
    { .table = "game", .name = "hardware_speed", .kind = NW_SETTING_BOOL, NW_FIELD(hardware_speed),
      .help = "true simulates original hardware speed: the PS2's disc and sound load times" },
    { .table = "audio", .name = "master_volume", .kind = NW_SETTING_INT, NW_FIELD(master_volume), .lo = 0, .hi = 100,
      .help = "0-100, over the game's own music and SFX volumes" },
    { .table = "audio", .name = "voice_volume", .kind = NW_SETTING_INT, NW_FIELD(voice_volume), .lo = 0, .hi = 100,
      .help = "0-100, the cutscene and hint voices" },
};

void nw_settings_defaults(NwSettings *s)
{
    s->hardware_speed = false;
    s->master_volume = 100;
    s->voice_volume = 100;
    s->aspect = 0.0f;
    s->hud = 16.0f / 9.0f;
    s->window = NW_WINDOW_WINDOWED;
    s->resolution = (NwResolution){ 0, 0 };
    s->vsync = NW_VSYNC_ON;
    s->scale = 0;
    s->msaa = 0;
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
        *(int *)field = v->i < k->lo ? k->lo : v->i > k->hi ? k->hi : (int)v->i;
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
        const NwDocTable *t = nw_doc_table(&doc, &k->table, 1);
        const NwDocValue *v = t ? nw_doc_get(t, k->name) : NULL;
        if (v) {
            nw_settings_take(k, v, s);
        }
    }
    nw_doc_free(&doc);
    return got > 0;
}

static bool nw_settings_put(NwDoc *doc, const NwSettingKey *k, const NwSettings *s)
{
    const char *field = (const char *)s + k->offset;
    NwDocTable *t = nw_doc_add_table(doc, &k->table, 1);
    NwDocValue v;
    char buf[32];
    if (!t) {
        return false;
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
        if (SDL_asprintf(&more, "%s%s.%s: %s\n", help, k->table, k->name, k->help) < 0) {
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
