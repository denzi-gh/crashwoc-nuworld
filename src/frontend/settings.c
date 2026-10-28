#include "frontend/settings.h"

#include <SDL3/SDL.h>

#include "platform/paths.h"

#define NW_SETTINGS_TABLE 64

static char *nw_settings_path(const char *root)
{
    char *path = NULL;
    return SDL_asprintf(&path, "%s%s", root, NW_PATHS_SETTINGS) < 0 ? NULL : path;
}

static char *nw_settings_trim(char *s)
{
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    char *end = s + SDL_strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) {
        end--;
    }
    *end = '\0';
    return s;
}

static void nw_settings_bool(const char *value, bool *out)
{
    if (SDL_strcmp(value, "true") == 0) {
        *out = true;
    } else if (SDL_strcmp(value, "false") == 0) {
        *out = false;
    }
}

/* A whole number, clamped to the volumes' 0-100. */
static void nw_settings_volume(const char *value, int *out)
{
    char *end;
    long v = SDL_strtol(value, &end, 10);
    if (end != value && *end == '\0') {
        *out = v < 0 ? 0 : v > 100 ? 100 : (int)v;
    }
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

static void nw_settings_set(NwSettings *s, const char *table, const char *key, const char *value)
{
    if (SDL_strcmp(table, "video") == 0 && SDL_strcmp(key, "aspect") == 0) {
        nw_settings_aspect(value, &s->aspect);
    } else if (SDL_strcmp(table, "video") == 0 && SDL_strcmp(key, "hud") == 0) {
        nw_settings_hud(value, &s->hud);
    } else if (SDL_strcmp(table, "game") == 0 && SDL_strcmp(key, "hardware_speed") == 0) {
        nw_settings_bool(value, &s->hardware_speed);
    } else if (SDL_strcmp(table, "audio") == 0 && SDL_strcmp(key, "master_volume") == 0) {
        nw_settings_volume(value, &s->master_volume);
    } else if (SDL_strcmp(table, "audio") == 0 && SDL_strcmp(key, "voice_volume") == 0) {
        nw_settings_volume(value, &s->voice_volume);
    }
}

void nw_settings_defaults(NwSettings *s)
{
    s->hardware_speed = false;
    s->master_volume = 100;
    s->voice_volume = 100;
    s->aspect = 0.0f;
    s->hud = 16.0f / 9.0f;
}

bool nw_settings_load(NwSettings *s, const char *root)
{
    char *path = nw_settings_path(root);
    if (!path) {
        return false;
    }
    if (!SDL_GetPathInfo(path, NULL)) {
        SDL_free(path);
        return true;
    }
    char *text = SDL_LoadFile(path, NULL);
    SDL_free(path);
    if (!text) {
        return false;
    }
    char table[NW_SETTINGS_TABLE] = "";
    for (char *line = text, *next; line; line = next) {
        next = SDL_strchr(line, '\n');
        if (next) {
            *next++ = '\0';
        }
        char *comment = SDL_strchr(line, '#');
        if (comment) {
            *comment = '\0';
        }
        line = nw_settings_trim(line);
        size_t len = SDL_strlen(line);
        char *eq = SDL_strchr(line, '=');
        if (line[0] == '[') { /* a header that does not close names no table */
            table[0] = '\0';
            if (line[len - 1] == ']') {
                line[len - 1] = '\0';
                SDL_strlcpy(table, nw_settings_trim(line + 1), sizeof(table));
            }
        } else if (eq) {
            *eq = '\0';
            nw_settings_set(s, table, nw_settings_trim(line), nw_settings_trim(eq + 1));
        }
    }
    SDL_free(text);
    return true;
}

bool nw_settings_save(const NwSettings *s, const char *root)
{
    char *path = nw_settings_path(root), *text = NULL, aspect[16], hud[16];
    int len = SDL_asprintf(&text,
                           "[video]\n"
                           "# auto (the display's), 4:3, 16:9, 16:10, 21:9, 32:9 or a ratio; 4:3 is the PS2's picture\n"
                           "aspect = \"%s\"\n"
                           "# How far out the HUD goes: edges, or an aspect it stays inside, centred\n"
                           "hud = \"%s\"\n"
                           "\n"
                           "[game]\n"
                           "# Simulate original hardware speed: the PS2's disc and sound load times\n"
                           "hardware_speed = %s\n"
                           "\n"
                           "[audio]\n"
                           "# 0-100, over the game's own music and SFX volumes\n"
                           "master_volume = %d\n"
                           "voice_volume = %d\n",
                           nw_settings_aspect_name(s->aspect, aspect, sizeof(aspect)),
                           nw_settings_hud_name(s->hud, hud, sizeof(hud)), s->hardware_speed ? "true" : "false", s->master_volume, s->voice_volume);
    bool ok = path && len >= 0 && SDL_SaveFile(path, text, (size_t)len);
    SDL_free(text);
    SDL_free(path);
    return ok;
}
