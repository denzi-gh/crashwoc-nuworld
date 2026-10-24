/* settings.toml: the defaults, no file, a save read back, comments and blanks, and what the reader does not take.
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

/* hardware_speed as root's settings.toml with text reads it; -1 when it does not load. */
static int read_speed(const char *text)
{
    char path[1200];
    NwSettings s;
    nw_settings_defaults(&s);
    SDL_snprintf(path, sizeof(path), "%s%s", root, NW_PATHS_SETTINGS);
    if (!SDL_SaveFile(path, text, strlen(text)) || !nw_settings_load(&s, root)) {
        return -1;
    }
    return s.hardware_speed;
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

    if (failures) {
        fprintf(stderr, "settings: %d checks failed\n", failures);
        return 1;
    }
    printf("settings: all checks passed\n");
    return 0;
}
