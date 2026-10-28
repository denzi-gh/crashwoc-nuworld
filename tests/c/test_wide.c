/* src/port/wide.c: retail untouched, the frame width and camera aspect for each picture aspect, the HUD's reach
   and anchors, the draw radii, the clamps; a request taken once at a frame's end outside a load, with the frame, main's
   widths and the viewport. */
#include <math.h>
#include <stdio.h>

#include "nu_port.h"
#include "nu3d/nuvport.h"
#include "port/wide.h"

extern float WUMPAOBJSX, WUMPATXTSX, LIVESOBJSX, CRATEOBJSX;
extern int PS2_VCNTR_X, PS2_REZ_W;
extern int SWIDTH, MAXVPSIZEX, MAXVPSIZEY;
extern volatile int bgload_complete;

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
    CHECK(nw_wide.hud == 1.0f && nw_wide.draw == 1.0f && nw_wide.draw2 == 1.0f);

    nw_wide_set(4.0f / 3.0f, 16.0f / 9.0f);
    CHECK(nw_wide.width == 640 && nw_wide.k == 1.0f && nw_wide.aspect == 0.75f && nw_wide.hud == 1.0f);
    CHECK(nw_wide.draw == 1.0f && nw_wide.draw2 == 1.0f && !nw_wide.pvs_all);
    nw_wide_set(16.0f / 9.0f, 16.0f / 9.0f);
    CHECK(nw_wide.width == 854 && near(nw_wide.k, 854.0f / 640.0f) && near(nw_wide.aspect, 0.75f * 640.0f / 854.0f));
    CHECK(near(nw_wide.hud, nw_wide.k) && fabsf(nw_wide.draw - 1.08f) < 0.01f && nw_wide.pvs_all);
    CHECK(near(nw_wide.draw2, nw_wide.draw * nw_wide.draw));
    nw_wide_set(21.0f / 9.0f, 16.0f / 9.0f);
    CHECK(nw_wide.width == 1120);
    nw_wide_set(3440.0f / 1440.0f, 16.0f / 9.0f);
    CHECK(nw_wide.width == 1146);
    nw_wide_set(32.0f / 9.0f, 16.0f / 9.0f);
    CHECK(nw_wide.width == 1706 && near(nw_wide.hud, 854.0f / 640.0f));
    CHECK(near(WUMPAOBJSX, -0.8f - 214.0f / 640.0f) && near(WUMPATXTSX - WUMPAOBJSX, 0.1f) &&
          near(LIVESOBJSX, 0.53f + 214.0f / 640.0f) && CRATEOBJSX == -0.29f);
    nw_wide_set(32.0f / 9.0f, 0.0f);
    CHECK(near(nw_wide.hud, nw_wide.k) && near(WUMPAOBJSX, -0.8f - 1066.0f / 640.0f));
    nw_wide_set(32.0f / 9.0f, 4.0f / 3.0f);
    CHECK(nw_wide.hud == 1.0f && near(WUMPAOBJSX, -0.8f));
    nw_wide_set(16.0f / 9.0f, 0.5f);
    CHECK(nw_wide.hud == 1.0f);
    nw_wide_set(5.0f, 16.0f / 9.0f);
    CHECK(nw_wide.width == 1706 && fabsf(nw_wide.draw - 1.52f) < 0.01f);
    nw_wide_set(1.25f, 16.0f / 9.0f);
    CHECK(nw_wide.width == 640 && nw_wide.k == 1.0f);
    nw_wide_set(0.0f, 16.0f / 9.0f);
    CHECK(nw_wide.width == 640 && nw_wide_width() == 640 && near(WUMPAOBJSX, -0.8f) && near(LIVESOBJSX, 0.53f));

    struct nuviewport_s *vp = NuVpGetCurrentViewport();
    PS2_VCNTR_X = 0x800;
    PS2_REZ_W = SWIDTH = MAXVPSIZEX = 640;
    MAXVPSIZEY = 512;
    bgload_complete = 0;
    nw_wide_request(16.0f / 9.0f, 0.0f);
    nw_wide_poll();
    CHECK(nw_wide.width == 640 && PS2_REZ_W == 640);
    bgload_complete = 1;
    nw_wide_poll();
    CHECK(nw_wide.width == 854 && nw_wide_width() == 854 && PS2_REZ_W == 854 && SWIDTH == 854 && MAXVPSIZEX == 854);
    CHECK(vp->width == 854 << 4 && vp->height == 512 << 3 && vp->x == (0x800 - 427) << 4);
    CHECK(near(WUMPAOBJSX, -0.8f - 214.0f / 640.0f));
    nw_wide_request(4.0f / 3.0f, 0.0f);
    nw_wide_request(4.0f / 3.0f, 16.0f / 9.0f);
    nw_wide_poll();
    CHECK(nw_wide.width == 640 && PS2_REZ_W == 640 && vp->width == 640 << 4 && vp->x == (0x800 - 320) << 4);
    CHECK(near(WUMPAOBJSX, -0.8f) && near(LIVESOBJSX, 0.53f));
    nw_wide_set(16.0f / 9.0f, 16.0f / 9.0f);
    nw_wide_poll();
    CHECK(nw_wide.width == 854 && PS2_REZ_W == 640);
    nw_wide_set(4.0f / 3.0f, 16.0f / 9.0f);

    if (failures) {
        fprintf(stderr, "wide: %d checks failed\n", failures);
        return 1;
    }
    printf("wide: all checks passed\n");
    return 0;
}
