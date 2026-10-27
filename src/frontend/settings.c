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

static void nw_settings_set(NwSettings *s, const char *table, const char *key, const char *value)
{
    if (SDL_strcmp(table, "game") == 0 && SDL_strcmp(key, "hardware_speed") == 0) {
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
    char *path = nw_settings_path(root), *text = NULL;
    int len = SDL_asprintf(&text,
                           "[game]\n"
                           "# Simulate original hardware speed: the PS2's disc and sound load times\n"
                           "hardware_speed = %s\n"
                           "\n"
                           "[audio]\n"
                           "# 0-100, over the game's own music and SFX volumes\n"
                           "master_volume = %d\n"
                           "voice_volume = %d\n",
                           s->hardware_speed ? "true" : "false", s->master_volume, s->voice_volume);
    bool ok = path && len >= 0 && SDL_SaveFile(path, text, (size_t)len);
    SDL_free(text);
    SDL_free(path);
    return ok;
}
