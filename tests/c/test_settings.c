/* settings.toml: the defaults, no file, a save read back, comments and blanks, the volumes clamped, and what the
   reader does not take.
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

/* The defaults with root's settings.toml holding text read over them; false when it does not load. */
static bool read_text(const char *text, NwSettings *s)
{
    char path[1200];
    nw_settings_defaults(s);
    SDL_snprintf(path, sizeof(path), "%s%s", root, NW_PATHS_SETTINGS);
    return SDL_SaveFile(path, text, strlen(text)) && nw_settings_load(s, root);
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
    CHECK(read_speed("[game]\nhardware_speed = true\nhardware_speed = false\n") == 0);
    CHECK(read_speed("hardware_speed = true\n") == 0);
    CHECK(read_speed("[video]\nhardware_speed = true\n[game]\nspeed = true\n") == 0);
    CHECK(read_speed("[game]\nhardware_speed = yes\n") == 0);
    CHECK(read_speed("[game]\nhardware_speed = true\n[game\nhardware_speed = false\n") == 1);
    CHECK(read_speed("") == 0);

    nw_settings_defaults(&s);
    CHECK(s.master_volume == 100 && s.voice_volume == 100);
    s.master_volume = 40;
    s.voice_volume = 7;
    CHECK(nw_settings_save(&s, root));
    nw_settings_defaults(&s);
    CHECK(nw_settings_load(&s, root) && s.master_volume == 40 && s.voice_volume == 7);
    CHECK(read_volumes("[audio]\nmaster_volume = 55\nvoice_volume=0\n") == 55000);
    CHECK(read_volumes("[audio]\nmaster_volume = 250\nvoice_volume = -3\n") == 100000);
    CHECK(read_volumes("[audio]\nmaster_volume = 5x\nvoice_volume = \n") == 100100);
    CHECK(read_volumes("[game]\nmaster_volume = 5\n") == 100100);

    if (failures) {
        fprintf(stderr, "settings: %d checks failed\n", failures);
        return 1;
    }
    printf("settings: all checks passed\n");
    return 0;
}
