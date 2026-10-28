/* settings.toml: the defaults, no file, a save read back, comments and blanks, the volumes clamped, the aspect's
   and the HUD's forms, what the reader does not take, a file that is not TOML, and keys a save keeps.
   Usage: test_settings <scratch dir>. */
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "frontend/settings.h"
#include "platform/paths.h"

static int failures;
static char root[1100];

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: CHECK(%s) failed; last error: %s\n", __FILE__, __LINE__, #cond, SDL_GetError()); \
            failures++; \
        } \
    } while (0)

/* root's settings.toml holding text. */
static bool write_text(const char *text)
{
    char path[1200];
    SDL_snprintf(path, sizeof(path), "%s%s", root, NW_PATHS_SETTINGS);
    return SDL_SaveFile(path, text, strlen(text));
}

/* The defaults with root's settings.toml holding text read over them; false when it does not load. */
static bool read_text(const char *text, NwSettings *s)
{
    nw_settings_defaults(s);
    return write_text(text) && nw_settings_load(s, root);
}

/* Whether root holds name. */
static bool exists(const char *name)
{
    char path[1200];
    SDL_snprintf(path, sizeof(path), "%s%s", root, name);
    return SDL_GetPathInfo(path, NULL);
}

/* root's settings.toml as text; SDL_free it. */
static char *file_text(void)
{
    char path[1200];
    SDL_snprintf(path, sizeof(path), "%s%s", root, NW_PATHS_SETTINGS);
    return SDL_LoadFile(path, NULL);
}

/* hardware_speed as root's settings.toml with text reads it; -1 when it does not load. */
static int read_speed(const char *text)
{
    NwSettings s;
    return read_text(text, &s) ? s.hardware_speed : -1;
}

/* master_volume * 1000 + voice_volume; -1 when it does not load. */
static int read_volumes(const char *text)
{
    NwSettings s;
    return read_text(text, &s) ? s.master_volume * 1000 + s.voice_volume : -1;
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "usage: test_settings <scratch dir>\n");
        return 2;
    }
    if (!nw_paths_remove_tree(argv[1]) || !SDL_CreateDirectory(argv[1])) {
        fprintf(stderr, "%s: %s\n", argv[1], SDL_GetError());
        return 1;
    }
    SDL_snprintf(root, sizeof(root), "%s/", argv[1]);

    NwSettings s;
    nw_settings_defaults(&s);
    CHECK(!s.hardware_speed);
    s.hardware_speed = true;
    CHECK(nw_settings_load(&s, root) && s.hardware_speed); /* no file: s stays */
    CHECK(nw_settings_save(&s, root));
    s.hardware_speed = false;
    CHECK(nw_settings_load(&s, root) && s.hardware_speed);

    CHECK(read_speed("# settings\n\n  [ game ]  \r\n\thardware_speed=true # on\r\n") == 1);
    CHECK(read_speed("hardware_speed = true\n") == 0);
    CHECK(read_speed("[video]\nhardware_speed = true\n[game]\nspeed = true\n") == 0);
    CHECK(read_speed("[game]\nhardware_speed = \"true\"\n") == 0);
    CHECK(read_speed("") == 0);

    /* Not TOML: moved aside, s keeps its values, and the next save starts a new file. */
    CHECK(read_speed("[game]\nhardware_speed = true\nhardware_speed = false\n") == -1);
    CHECK(exists(NW_PATHS_SETTINGS ".corrupt") && !exists(NW_PATHS_SETTINGS));
    CHECK(read_speed("[game]\nhardware_speed = yes\n") == -1);
    CHECK(read_speed("[game]\nhardware_speed = true\n[game\nhardware_speed = false\n") == -1);
    nw_settings_defaults(&s);
    s.hardware_speed = true;
    CHECK(write_text("[game]\nhardware_speed = false\n[game]\n") && !nw_settings_load(&s, root) && s.hardware_speed);
    s.hardware_speed = true;
    CHECK(nw_settings_save(&s, root));
    nw_settings_defaults(&s);
    CHECK(nw_settings_load(&s, root) && s.hardware_speed);

    nw_settings_defaults(&s);
    CHECK(s.master_volume == 100 && s.voice_volume == 100);
    s.master_volume = 40;
    s.voice_volume = 7;
    CHECK(nw_settings_save(&s, root));
    nw_settings_defaults(&s);
    CHECK(nw_settings_load(&s, root) && s.master_volume == 40 && s.voice_volume == 7);
    CHECK(read_volumes("[audio]\nmaster_volume = 55\nvoice_volume=0\n") == 55000);
    CHECK(read_volumes("[audio]\nmaster_volume = 250\nvoice_volume = -3\n") == 100000);
    CHECK(read_volumes("[audio]\nmaster_volume = \"5\"\nvoice_volume = 0x10\n") == 100016);
    CHECK(read_volumes("[game]\nmaster_volume = 5\n") == 100100);

    float a = -1.0f;
    char name[16];
    CHECK(nw_settings_aspect("auto", &a) && a == 0.0f);
    CHECK(nw_settings_aspect("\"16:9\"", &a) && SDL_fabsf(a - 16.0f / 9.0f) < 1e-6f);
    CHECK(nw_settings_aspect("2.39", &a) && SDL_fabsf(a - 2.39f) < 1e-6f);
    CHECK(nw_settings_aspect("3440:1440", &a) && SDL_fabsf(a - 3440.0f / 1440.0f) < 1e-6f);
    a = 1.5f;
    CHECK(!nw_settings_aspect("wide", &a) && !nw_settings_aspect("16:", &a) && !nw_settings_aspect("0:9", &a) &&
          !nw_settings_aspect("-2", &a) && !nw_settings_aspect("\"16:9", &a) && !nw_settings_aspect("", &a) &&
          a == 1.5f);
    CHECK(SDL_strcmp(nw_settings_aspect_name(0.0f, name, sizeof(name)), "auto") == 0);
    CHECK(SDL_strcmp(nw_settings_aspect_name(21.0f / 9.0f, name, sizeof(name)), "21:9") == 0);
    CHECK(SDL_strcmp(nw_settings_aspect_name(2.39f, name, sizeof(name)), "2.39") == 0);
    nw_settings_defaults(&s);
    CHECK(s.aspect == 0.0f);
    s.aspect = 32.0f / 9.0f;
    CHECK(nw_settings_save(&s, root));
    nw_settings_defaults(&s);
    CHECK(nw_settings_load(&s, root) && SDL_fabsf(s.aspect - 32.0f / 9.0f) < 1e-6f);
    CHECK(read_text("[video]\naspect = \"16:10\" # quoted\n", &s) && SDL_fabsf(s.aspect - 1.6f) < 1e-6f);
    CHECK(read_text("[video]\naspect = \"square\"\n", &s) && s.aspect == 0.0f);
    CHECK(s.hud == 16.0f / 9.0f);
    CHECK(nw_settings_hud("edges", &a) && a == 0.0f && nw_settings_hud("\"4:3\"", &a) &&
          SDL_fabsf(a - 4.0f / 3.0f) < 1e-6f);
    CHECK(!nw_settings_hud("auto", &a) && !nw_settings_hud("left", &a) && SDL_fabsf(a - 4.0f / 3.0f) < 1e-6f);
    CHECK(SDL_strcmp(nw_settings_hud_name(0.0f, name, sizeof(name)), "edges") == 0);
    s.hud = 0.0f;
    CHECK(nw_settings_save(&s, root));
    nw_settings_defaults(&s);
    CHECK(nw_settings_load(&s, root) && s.hud == 0.0f);

    /* An enum by its name; another name keeps the value. */
    CHECK(read_text("[video]\nwindow = \"borderless\"\n", &s) && s.window == NW_WINDOW_BORDERLESS);
    CHECK(read_text("[video]\nwindow = \"maximised\"\n", &s) && s.window == NW_WINDOW_WINDOWED);
    s.window = NW_WINDOW_FULLSCREEN;
    CHECK(nw_settings_save(&s, root));
    nw_settings_defaults(&s);
    CHECK(nw_settings_load(&s, root) && s.window == NW_WINDOW_FULLSCREEN);

    /* A resolution: auto or WxH. */
    NwResolution res = { 1, 1 };
    CHECK(nw_settings_resolution("auto", &res) && res.w == 0 && res.h == 0);
    CHECK(nw_settings_resolution("2560x1440", &res) && res.w == 2560 && res.h == 1440);
    CHECK(!nw_settings_resolution("1920", &res) && !nw_settings_resolution("1920x", &res) &&
          !nw_settings_resolution("x1080", &res) && !nw_settings_resolution("0x1080", &res) &&
          !nw_settings_resolution("1920x1080p", &res) && !nw_settings_resolution("99999x1080", &res) &&
          res.w == 2560 && res.h == 1440);
    CHECK(SDL_strcmp(nw_settings_resolution_name((NwResolution){ 0, 0 }, name, sizeof(name)), "auto") == 0);
    CHECK(SDL_strcmp(nw_settings_resolution_name(res, name, sizeof(name)), "2560x1440") == 0);
    CHECK(read_text("[video]\nresolution = \"1280x800\"\n", &s) && s.resolution.w == 1280 &&
          s.resolution.h == 800);
    CHECK(read_text("[video]\nresolution = \"big\"\n", &s) && s.resolution.w == 0);

    /* A save keeps what it does not know, writes the keys' help first and leaves no .tmp. */
    NwSettings t;
    nw_settings_defaults(&t);
    s = t;
    s.resolution.h = 1;
    CHECK(nw_settings_equal(&t, &t) && !nw_settings_equal(&s, &t));
    CHECK(read_text("[video]\nfov = 90\naspect = \"4:3\"\n\n[mods.example]\nenabled = true\n", &s));
    s.aspect = 0.0f;
    CHECK(nw_settings_save(&s, root) && !exists(NW_PATHS_SETTINGS ".tmp"));
    char *text = file_text();
    CHECK(text && SDL_strncmp(text, "# ", 2) == 0 && SDL_strstr(text, "# video.aspect: ") &&
          SDL_strstr(text, "[video]\nfov = 90\naspect = \"auto\"\n") &&
          SDL_strstr(text, "[mods.example]\nenabled = true\n") && SDL_strstr(text, "[audio]\n"));
    SDL_free(text);

    if (failures) {
        fprintf(stderr, "settings: %d checks failed\n", failures);
        return 1;
    }
    printf("settings: all checks passed\n");
    return 0;
}
