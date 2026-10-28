/* src/port/wide.c: retail untouched, the frame width and camera aspect for each picture aspect, the clamps. */
#include <math.h>
#include <stdio.h>

#include "nu_port.h"
#include "port/wide.h"

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

static int near(float a, float b)
{
    return fabsf(a - b) < 1e-4f;
}

int main(void)
{
    CHECK(nw_wide.k == 1.0f && nw_wide.aspect == 0.75f && nw_wide.width == 640 && !nw_wide.pvs_all);
    CHECK(nw_wide.hud == 1.0f && nw_wide.panel_x == 1.0f && nw_wide.draw == 1.0f && nw_wide.draw2 == 1.0f);

    nw_wide_set(4.0f / 3.0f);
    CHECK(nw_wide.width == 640 && nw_wide.k == 1.0f && nw_wide.aspect == 0.75f && nw_wide.panel_x == 1.0f);
    nw_wide_set(16.0f / 9.0f);
    CHECK(nw_wide.width == 854 && near(nw_wide.k, 854.0f / 640.0f) && near(nw_wide.aspect, 0.75f * 640.0f / 854.0f));
    CHECK(nw_wide.hud == 1.0f && near(nw_wide.panel_x, nw_wide.k));
    nw_wide_set(21.0f / 9.0f);
    CHECK(nw_wide.width == 1120);
    nw_wide_set(3440.0f / 1440.0f);
    CHECK(nw_wide.width == 1146);
    nw_wide_set(32.0f / 9.0f);
    CHECK(nw_wide.width == 1706);
    nw_wide_set(5.0f);
    CHECK(nw_wide.width == 1706);
    nw_wide_set(1.25f);
    CHECK(nw_wide.width == 640 && nw_wide.k == 1.0f);
    nw_wide_set(0.0f);
    CHECK(nw_wide.width == 640 && nw_wide_width() == 640);

    if (failures) {
        fprintf(stderr, "wide: %d checks failed\n", failures);
        return 1;
    }
    printf("wide: all checks passed\n");
    return 0;
}
