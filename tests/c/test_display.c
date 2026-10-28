/* The fullscreen mode for a resolution: matched in pixels, the highest refresh rate first; none for a size the display
   does not have. Usage: test_display. */
#include <stdio.h>

#include "platform/display.h"

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

int main(void)
{
    SDL_DisplayMode m[] = {
        { .w = 2560, .h = 1440, .pixel_density = 1.0f, .refresh_rate = 60.0f },
        { .w = 2560, .h = 1440, .pixel_density = 1.0f, .refresh_rate = 144.0f },
        { .w = 1920, .h = 1080, .pixel_density = 1.0f, .refresh_rate = 60.0f },
        { .w = 960, .h = 540, .pixel_density = 2.0f, .refresh_rate = 120.0f },
        { .w = 1280, .h = 720, .pixel_density = 0.0f, .refresh_rate = 50.0f },
    };
    SDL_DisplayMode *modes[] = { &m[0], &m[1], &m[2], &m[3], &m[4] };
    CHECK(nw_display_pick(modes, 5, (NwResolution){ 2560, 1440 }) == &m[1]);
    CHECK(nw_display_pick(modes, 5, (NwResolution){ 1920, 1080 }) == &m[3]);
    CHECK(nw_display_pick(modes, 5, (NwResolution){ 1280, 720 }) == &m[4]);
    CHECK(nw_display_pick(modes, 5, (NwResolution){ 1600, 900 }) == NULL);
    CHECK(nw_display_pick(modes, 0, (NwResolution){ 2560, 1440 }) == NULL);

    if (failures) {
        fprintf(stderr, "display: %d checks failed\n", failures);
        return 1;
    }
    printf("display: all checks passed\n");
    return 0;
}
