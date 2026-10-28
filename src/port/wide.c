#include "port/wide.h"

#include <math.h>

#include "nu_port.h"
#include "nu3d/nuvport.h"

extern int PS2_VCNTR_X;

struct nw_wide_s nw_wide = { .k = 1.0f, .aspect = 0.75f, .width = 640, .hud = 1.0f, .draw = 1.0f, .draw2 = 1.0f };

/* game/panel's HUD anchors in panel x (-1 to 1 spans retail's 4:3 picture) and the side they keep to. The pause
   screen's (PAUSEPANELX) stay: it is drawn in retail's 4:3 area (nw_wide_vp). */
extern float WUMPAOBJSX, WUMPATXTSX, BONUSWUMPAOBJSX, BONUSWUMPATXTSX, LIVESOBJSX, LIVESTXTSX, BONUSLIVESOBJSX,
    BONUSLIVESTXTSX, NEWLIFEOBJSX, NEWBONUSLIFEOBJSX, PANELCRUNCHX, PANELSINKX, PANELBLIMPX, RUMBOSSBARX, ATMBOSSBARX,
    FIREBOSSBARX, PANELRINGX;
static const struct {
    float *x;
    float side;
} nw_wide_anchors[] = {
    { &WUMPAOBJSX, -1.0f },      { &WUMPATXTSX, -1.0f },   { &BONUSWUMPAOBJSX, -1.0f }, { &BONUSWUMPATXTSX, -1.0f },
    { &LIVESOBJSX, 1.0f },       { &LIVESTXTSX, 1.0f },    { &BONUSLIVESOBJSX, 1.0f },  { &BONUSLIVESTXTSX, 1.0f },
    { &NEWLIFEOBJSX, 1.0f },     { &NEWBONUSLIFEOBJSX, 1.0f }, { &PANELCRUNCHX, 1.0f }, { &PANELSINKX, 1.0f },
    { &PANELBLIMPX, 1.0f },      { &RUMBOSSBARX, 1.0f },   { &ATMBOSSBARX, 1.0f },      { &FIREBOSSBARX, 1.0f },
    { &PANELRINGX, 1.0f },
};

void nw_wide_set(float aspect, float hud)
{
    float a = aspect > NW_WIDE_MIN ? aspect < NW_WIDE_MAX ? aspect : NW_WIDE_MAX : NW_WIDE_MIN;
    int width = 2 * (int)lroundf(320.0f * a / NW_WIDE_MIN);
    /* k from the rounded width: the camera then matches the frame's pixels exactly */
    float k = width / 640.0f;
    /* The HUD's x reach: the edges up to hud's aspect, at most the picture's */
    float h = hud > 0.0f ? 2 * (int)lroundf(320.0f * hud / NW_WIDE_MIN) / 640.0f : k;
    h = h < 1.0f ? 1.0f : h > k ? k : h;
    /* Each anchor moves out by the HUD's extra reach, so an icon and its count keep their spacing */
    for (size_t i = 0; i < sizeof(nw_wide_anchors) / sizeof(nw_wide_anchors[0]); i++) {
        *nw_wide_anchors[i].x += nw_wide_anchors[i].side * (h - nw_wide.hud);
    }
    nw_wide = (struct nw_wide_s){ .k = k, .aspect = 0.75f / k, .width = width, .hud = h, .draw = 1.0f, .draw2 = 1.0f };
}

int nw_wide_width(void)
{
    return nw_wide.width;
}

/* While the pause screen shrinks the game's view (paused 1-25), its origin moves from the frame's left edge to retail's
   4:3 area, where the pause panels are. */
void nw_wide_vp(int paused)
{
    struct nuviewport_s *vp = NuVpGetCurrentViewport();
    if (nw_wide.width == 640) {
        return;
    }
    NuVpSetPosition((float)(((PS2_VCNTR_X - (nw_wide.width >> 1)) << 4) + ((nw_wide.width - 640) << 3) * paused / 25),
                    (float)vp->y);
}

int nw_wide_vp_x(void)
{
    return NuVpGetCurrentViewport()->x - ((PS2_VCNTR_X - (nw_wide.width >> 1)) << 4);
}

int nw_wide_anim(struct nuinstance_s *inst, int test)
{
    (void)inst;
    return test;
}

void nw_wide_mark(void)
{
}

int nw_wide_seen(int drawn)
{
    return drawn;
}
