#include "port/wide.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "nu_port.h"
#include "gamelib/terrain.h"
#include "gfx/rndr.h"
#include "nu3d/nucamera.h"
#include "nu3d/nugscn.h"
#include "nu3d/numtl.h"
#include "nu3d/nuvport.h"
#include "numath/nutrig.h"

extern int PS2_VCNTR_X, PS2_REZ_W;
extern TerrainSetType *CurTerr;
extern struct nugscn_s *world_scene[32];
extern s32 SWIDTH, MAXVPSIZEX, MAXVPSIZEY;
extern struct nucamera_s *pNuCam;
extern volatile s32 bgload_complete;
extern char vu_load_glass_mpg[], vu_load_water_mpg[], vu_load_wateruv_mpg[];

struct nw_wide_s nw_wide = { .k = 1.0f, .aspect = 0.75f, .width = 640, .hud = 1.0f, .draw = 1.0f, .draw2 = 1.0f };

static SDL_AtomicInt nw_wide_shown = { 640 }; /* nw_wide.width for other threads */
static SDL_AtomicInt nw_wide_asked;           /* a request waits */
static SDL_AtomicInt nw_wide_asks[2];         /* its aspect and HUD reach, as float bits */

/* game/panel's HUD anchors in panel x (-1 to 1 spans retail's 4:3 picture at depth 1) and the side they keep to; the
   panel heads with their depth. The pause screen's (PAUSEPANELX) stay: it is drawn in retail's 4:3 area
   (nw_wide_vp). */
extern float WUMPAOBJSX, WUMPATXTSX, BONUSWUMPAOBJSX, BONUSWUMPATXTSX, LIVESOBJSX, LIVESTXTSX, BONUSLIVESOBJSX,
    BONUSLIVESTXTSX, NEWLIFEOBJSX, NEWBONUSLIFEOBJSX, PANELCRUNCHX, PANELSINKX, PANELBLIMPX, RUMBOSSBARX, ATMBOSSBARX,
    FIREBOSSBARX, PANELRINGX;
extern float PANELCRUNCHZ, PANELSINKZ, PANELBLIMPZ;
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
static const struct {
    const float *x, *z;
} nw_wide_heads[] = { { &PANELCRUNCHX, &PANELCRUNCHZ }, { &PANELSINKX, &PANELSINKZ }, { &PANELBLIMPX, &PANELBLIMPZ } };
#define NW_WIDE_HEAD_IN 0.08f /* how much less the panel heads move out, so the picture's edge does not cut them */

/* How far an anchor sits out for the HUD reach h: the extra reach; for a panel head a little less, over its depth
   since the screen takes panel x over depth */
static float nw_wide_out(const float *x, float h)
{
    float out = h - 1.0f;
    for (size_t i = 0; i < sizeof(nw_wide_heads) / sizeof(nw_wide_heads[0]); i++) {
        if (nw_wide_heads[i].x == x) {
            return (out - (out < NW_WIDE_HEAD_IN ? out : NW_WIDE_HEAD_IN)) * *nw_wide_heads[i].z;
        }
    }
    return out;
}

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
        float *x = nw_wide_anchors[i].x;
        *x += nw_wide_anchors[i].side * (nw_wide_out(x, h) - nw_wide_out(x, nw_wide.hud));
    }
    /* A radius r around the camera reaches depth r / sqrt(1 + zx^2) at the view's side edge (zx = x clip ratio,
       tan(fov / 2) / aspect, fov 0.75): the draw radii grow to keep retail's edge depth. */
    float zx0 = tanf(0.375f) / 0.75f, zx = zx0 * k;
    float draw = sqrtf(1.0f + zx * zx) / sqrtf(1.0f + zx0 * zx0);
    nw_wide = (struct nw_wide_s){ .k = k, .aspect = 0.75f / k, .width = width, .hud = h, .draw = draw,
                                  .draw2 = draw * draw, .pvs_all = width > 640 };
    SDL_SetAtomicInt(&nw_wide_shown, width);
}

int nw_wide_width(void)
{
    return SDL_GetAtomicInt(&nw_wide_shown);
}

void nw_wide_request(float aspect, float hud)
{
    int bits[2];
    memcpy(&bits[0], &aspect, sizeof(bits[0]));
    memcpy(&bits[1], &hud, sizeof(bits[1]));
    SDL_SetAtomicInt(&nw_wide_asks[0], bits[0]);
    SDL_SetAtomicInt(&nw_wide_asks[1], bits[1]);
    SDL_SetAtomicInt(&nw_wide_asked, 1);
}

/* Recomputes from the old width `was` what the game derived from it: main's SWIDTH, MAXVPSIZEX and video size, the
   camera's aspect, MaxVP's viewport, the level's frame-sized targets and the glass and water materials' VU words. */
static void nw_wide_frame(int was)
{
    int w = nw_wide.width;
    PS2_REZ_W = SWIDTH = MAXVPSIZEX = w;
    if (pNuCam) {
        pNuCam->aspect = nw_wide.aspect;
    }
    NuVpSetSize((float)(MAXVPSIZEX << 4), (float)(MAXVPSIZEY << 3));
    nw_wide_vp(0);
    if (w == was) {
        return;
    }
    nw_ps2tx_frame_width(was, w);
    const unsigned char *glass = (unsigned char *)vu_load_glass_mpg, *water = (unsigned char *)vu_load_water_mpg,
                        *wateruv = (unsigned char *)vu_load_wateruv_mpg;
    for (struct numtl_s *m = NuMtlIterate(1); m; m = NuMtlIterate(0)) {
        if (m->rs.mpg == glass) {
            m->vuattr.f32[0] = ((w * 0.5f) * 16.0 - 32768.0) / (NuPower2(w) * 16.0);
            m->vuattr.f32[4] = 1.0 / (NuPower2(w) * 16.0);
            m->vuattr.f32[6] = (w - 1.0f) / NuPower2(w);
        } else if (m->rs.mpg == water || m->rs.mpg == wateruv) {
            m->vuattr.mtx44->_10 = 1.0 / (NuPower2(w) * 16.0);
            m->vuattr.mtx44->_12 = w;
        }
    }
}

void nw_wide_poll(void)
{
    if (!bgload_complete || !SDL_CompareAndSwapAtomicInt(&nw_wide_asked, 1, 0)) {
        return;
    }
    int bits[2] = { SDL_GetAtomicInt(&nw_wide_asks[0]), SDL_GetAtomicInt(&nw_wide_asks[1]) };
    float ask[2];
    memcpy(ask, bits, sizeof(ask));
    int was = nw_wide.width;
    nw_wide_set(ask[0], ask[1]);
    nw_wide_frame(was);
}

/* While the pause screen shrinks the game's view (paused 1-25), its origin moves from the frame's left edge to retail's
   4:3 area, where the pause panels are. */
void nw_wide_vp(int paused)
{
    struct nuviewport_s *vp = NuVpGetCurrentViewport();
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
