#include "port/wide.h"

#include <math.h>
#include <stddef.h>

#include "nu_port.h"
#include "gamelib/terrain.h"
#include "nu3d/nugscn.h"
#include "nu3d/nuvport.h"

extern int PS2_VCNTR_X;
extern TerrainSetType *CurTerr;
extern struct nugscn_s *world_scene[32];

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
    /* A radius r around the camera reaches depth r / sqrt(1 + zx^2) at the view's side edge (zx = x clip ratio,
       tan(fov / 2) / aspect, fov 0.75): the draw radii grow to keep retail's edge depth. */
    float zx0 = tanf(0.375f) / 0.75f, zx = zx0 * k;
    float draw = sqrtf(1.0f + zx * zx) / sqrtf(1.0f + zx0 * zx0);
    nw_wide = (struct nw_wide_s){ .k = k, .aspect = 0.75f / k, .width = width, .hud = h, .draw = draw,
                                  .draw2 = draw * draw, .pvs_all = width > 640 };
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

static int nw_wide_special(const struct nuspecial_s *specials, int n, const struct nuinstance_s *inst)
{
    for (int i = 0; i < n; i++) {
        if (specials[i].instance == inst) {
            return 1;
        }
    }
    return 0;
}

/* An animated instance the PVS hid runs its animation while the wider view draws it, unless the game reads where it
   is: a moving platform (CurTerr) or a special (PlayerObjectAnimCollision and the like) keeps retail's test. */
int nw_wide_anim(struct nuinstance_s *inst, int test)
{
    if (test || !nw_wide.pvs_all || !inst->anim) {
        return test;
    }
    for (int i = 0; CurTerr && i < 128; i++) {
        if (CurTerr->platdata[i].curmtx == &inst->anim->mtx) {
            return test;
        }
    }
    for (int s = 0; s < 32; s++) {
        struct nugscn_s *sc = world_scene[s];
        if (sc && inst >= sc->instances && inst < sc->instances + sc->numinstance &&
            (nw_wide_special(sc->specials, sc->numspecial, inst) ||
             nw_wide_special(sc->exspecials, sc->numexspecials, inst))) {
            return test;
        }
    }
    return 1;
}

static int nw_wide_tests, nw_wide_retail;

void nw_wide_mark(void)
{
    nw_wide_tests = 0;
    nw_wide_retail = 0;
}

void nw_wide_clip(int retail_visible)
{
    nw_wide_tests++;
    nw_wide_retail |= retail_visible;
}

int nw_wide_seen(int drawn)
{
    return drawn && (nw_wide_tests == 0 || nw_wide_retail);
}
