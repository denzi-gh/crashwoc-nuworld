#include "port/wide.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "nu_port.h"
#include "hle/vu0.h"
#include "gamelib/gcutscn.h"
#include "gamelib/terrain.h"
#include "gfx/rndr.h"
#include "nu3d/nuanim.h"
#include "nu3d/nucamera.h"
#include "nu3d/nugobj.h"
#include "nu3d/nugscn.h"
#include "nu3d/nuhgobj.h"
#include "nu3d/numtl.h"
#include "nu3d/nuvport.h"
#include "numath/numtx.h"
#include "numath/nutrig.h"

extern int PS2_VCNTR_X, PS2_REZ_W;
extern TerrainSetType *CurTerr;
extern struct nugscn_s *world_scene[32];
extern s32 SWIDTH, MAXVPSIZEX, MAXVPSIZEY;
extern struct nucamera_s *pNuCam;
extern float PANEL3DMULX;
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

/* The screen takes x over depth, so a model's depth smears it sideways by its centre's x over depth. Past retail's
   picture edge (PANEL3DMULX at depth 1) that ratio is capped at the edge's: the depth's x share moves into x. */
float nw_wide_hud_shear(float x, float z)
{
    if (!(z > 0.0f)) {
        return 0.0f;
    }
    float r = x / z;
    return r - (r > PANEL3DMULX ? PANEL3DMULX : r < -PANEL3DMULX ? -PANEL3DMULX : r);
}

void nw_wide_hud(struct numtx_s *m)
{
    float s = nw_wide_hud_shear(m->_30, m->_32);
    if (s != 0.0f) {
        m->_00 += s * m->_02;
        m->_10 += s * m->_12;
        m->_20 += s * m->_22;
    }
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

/* Cutscenes. A cut shows and hides its rigids and characters with keys (a rigid's visibiltyanim, a character anim's
   visible channel), and puts what retail's 4:3 picture does not show just past its edge: a ship parked there until it
   flies in, characters swapped at their seats out of shot, one standing by the camera. The wider view would show them
   popping in and out, or reaching into the camera. Placement is in r, how far out the nearest point of the object is
   over retail's half width (1: retail's edge, nw_wide.k: the view's):
   - a non-animated rigid a key shows past retail's edge, and that then moves off (nitros in logos1.cut, Oxide's ship),
     flies in instead: over NW_CUT_RATE frames per r it crosses it comes from past the view's edge along its first motion
     run backwards, easing into its true place and speed where it starts to move; before that it waits past the view's
     edge. Only it slides: everything else keeps its place;
   - free locators (emitters) inside it move with it (its exhaust smoke);
   - a character a key shows or hides mid-shot past retail's picture stays in its place for the whole shot (seated
     characters swapped out of retail's view, inchambr.cut), see nw_wide_cut_char_hold;
   - a character wholly past retail's picture that reaches into the camera goes out of view (inchambr.cut's close-ups).
   A character is placed by its joints (its model's box is about the cut's origin for the group, not where it stands).
   Keys on one of the scene's camera changes are left alone: the whole picture changes there. */
#define NW_CUT_SLOTS 512
#define NW_CUT_EDGE 1.03f  /* past the view's edge by this much of it */
#define NW_CUT_LOOSE 0.06f /* how far into retail's picture a ship's box shown there may reach: boxes are loose */
#define NW_CUT_RATE 22.0f  /* cut frames an entry takes per r it crosses */
#define NW_CUT_MIN 15.0f   /* and at least */
#define NW_CUT_MAX 60.0f   /* and at most */
#define NW_CUT_STEP 8.0f   /* a bigger step of the cut's time between two sightings is a new start */
#define NW_CUT_NEAR 1.5f   /* a shape nearer the eye than this many near clips reaches into the camera */
#define NW_CUT_ASIDE 0.75f /* and this much of it past retail's side of the picture is out of it */
#define NW_CUT_JOINTS 0.2f /* a joints' box grows by this much of its size, for the skin around them */
#define NW_CUT_SKIN 0.12f  /* and a joint by this much of it, measured alone */
#define NUINSTANCE_VISIBLE 0x1

static struct {
    const void *key;  /* the instance's object */
    unsigned tick;    /* last seen, for reuse */
    float time;       /* the cut's time then */
    float span;       /* the frames its entry takes */
    unsigned char known;
    unsigned char forced; /* shown ahead of its key this frame: the cut's flag to take back */
    unsigned char coming; /* coming in (ahead of the key, then on until arrive) */
    unsigned char hold_known, hold_ok; /* a hidden character's hold, worked out for hold_lo..hold_hi */
    float hold_lo, hold_hi, hold_t;
    float on, arrive;     /* the key that shows it next, and when it starts to move after it */
} nw_cut[NW_CUT_SLOTS];
static unsigned nw_cut_tick;

static int nw_cut_find(const void *key)
{
    for (int i = 0; i < NW_CUT_SLOTS; i++) {
        if (nw_cut[i].key == key) {
            return i;
        }
    }
    return -1;
}

static int nw_cut_slot(const void *key)
{
    int i = nw_cut_find(key);
    if (i >= 0) {
        return i;
    }
    int old = 0;
    for (i = 1; i < NW_CUT_SLOTS; i++) {
        if (nw_cut[i].tick < nw_cut[old].tick) {
            old = i;
        }
    }
    memset(&nw_cut[old], 0, sizeof(nw_cut[old]));
    nw_cut[old].key = key;
    return old;
}

/* Cut frames to come in from past the view's edge to r, or go back out */
static float nw_cut_span(float r)
{
    float t = NW_CUT_RATE * (nw_wide.k * NW_CUT_EDGE - r);
    return t < NW_CUT_MIN ? NW_CUT_MIN : t > NW_CUT_MAX ? NW_CUT_MAX : t;
}

/* The world move that takes clip x by s per unit and keeps clip y and w: only how far out it shows changes */
static int nw_cut_dir(const struct numtx_s *vpc, float s, float dir[3])
{
    const float (*v)[4] = (const float (*)[4])vpc;
    float cx[3] = { v[0][0], v[1][0], v[2][0] }, cy[3] = { v[0][1], v[1][1], v[2][1] },
          cw[3] = { v[0][3], v[1][3], v[2][3] };
    float d[3] = { cy[1] * cw[2] - cy[2] * cw[1], cy[2] * cw[0] - cy[0] * cw[2], cy[0] * cw[1] - cy[1] * cw[0] };
    float a = d[0] * cx[0] + d[1] * cx[1] + d[2] * cx[2];
    if (!(fabsf(a) > 1e-12f)) {
        return 0;
    }
    for (int k = 0; k < 3; k++) {
        dir[k] = d[k] * s / a;
    }
    return 1;
}

/* What an object's place is measured on: its box (a rigid), or its joints, each grown by grow (a character); box is
   the joints' then, for the emitters around it */
typedef struct {
    const struct nuvec_s *min, *max;
    const struct numtx_s *joints;
    int n;
    float grow;
} NwCutShape;

#define NW_CUT_POINTS 256

/* The shape's points in clip x and w, and how far a point's grow can take clip x and w (cx, cw) */
static int nw_cut_clip(const struct numtx_s *vpc, const struct numtx_s *m, const NwCutShape *shape, float *x, float *w,
                       float *cx, float *cw)
{
    struct numtx_s c;
    nw_vu0_mtx_mul(&c, m, vpc);
    const float (*cr)[4] = (const float (*)[4])&c;
    int n = shape->joints ? (shape->n < NW_CUT_POINTS ? shape->n : NW_CUT_POINTS) : 8;
    for (int i = 0; i < n; i++) {
        float px, py, pz;
        if (shape->joints) {
            px = shape->joints[i]._30, py = shape->joints[i]._31, pz = shape->joints[i]._32;
        } else {
            px = i & 1 ? shape->max->x : shape->min->x;
            py = i & 2 ? shape->max->y : shape->min->y;
            pz = i & 4 ? shape->max->z : shape->min->z;
        }
        x[i] = cr[0][0] * px + cr[1][0] * py + cr[2][0] * pz + cr[3][0];
        w[i] = cr[0][3] * px + cr[1][3] * py + cr[2][3] * pz + cr[3][3];
    }
    const float (*v)[4] = (const float (*)[4])vpc;
    *cx = shape->grow * sqrtf(v[0][0] * v[0][0] + v[1][0] * v[1][0] + v[2][0] * v[2][0]);
    *cw = shape->grow * sqrtf(v[0][3] * v[0][3] + v[1][3] * v[1][3] + v[2][3] * v[2][3]);
    return n;
}

/* The object's r, the w of its nearest point and the move outwards of nw_cut_dir. 0 when part of a box is behind the
   eye; a character's joints there are left out (one standing by the camera), 0 when all are. */
static int nw_cut_extent(const struct numtx_s *vpc, const struct numtx_s *m, const NwCutShape *shape, float *r,
                         float *w_near, float dir[3])
{
    float x[NW_CUT_POINTS], w[NW_CUT_POINTS], cx, cw, sum = 0.0f;
    int n = nw_cut_clip(vpc, m, shape, x, w, &cx, &cw), seen = 0;
    for (int i = 0; i < n; i++) {
        if (!(w[i] - cw > 1e-3f)) {
            if (!shape->joints) {
                return 0;
            }
            continue;
        }
        sum += x[i] / w[i];
        seen++;
    }
    if (!seen) {
        return 0;
    }
    float s = sum < 0.0f ? -1.0f : 1.0f;
    int near = 0;
    float in = 1e30f;
    for (int i = 0; i < n; i++) {
        if (!(w[i] - cw > 1e-3f)) {
            continue;
        }
        float v = (s * x[i] - cx) / w[i];
        if (v < in) {
            in = v;
            near = i;
        }
    }
    if (!nw_cut_dir(vpc, s, dir)) {
        return 0;
    }
    *r = in * nw_wide.k;
    *w_near = w[near];
    return 1;
}

/* The move outwards (along dir, nw_cut_dir) that puts every point of the shape at r target or further: a uniform
   move takes near points further across the screen than far ones, so it is the most any point needs */
static float nw_cut_need(const struct numtx_s *vpc, const struct numtx_s *m, const NwCutShape *shape, float target,
                         float dir[3])
{
    float x[NW_CUT_POINTS], w[NW_CUT_POINTS], cx, cw, sum = 0.0f, need = 0.0f;
    int n = nw_cut_clip(vpc, m, shape, x, w, &cx, &cw);
    for (int i = 0; i < n; i++) {
        sum += w[i] > 1e-3f ? x[i] / w[i] : 0.0f;
    }
    float s = sum < 0.0f ? -1.0f : 1.0f;
    for (int i = 0; i < n; i++) {
        if (!(w[i] - cw > 1e-3f)) {
            continue; /* behind the eye: nowhere on screen */
        }
        float d = target / nw_wide.k * w[i] - s * x[i] + cx;
        need = d > need ? d : need;
    }
    return n && nw_cut_dir(vpc, s, dir) ? need : 0.0f;
}

/* A character reaching into the camera (a joint, grown, nearer the eye than NW_CUT_NEAR near clips) with at least
   NW_CUT_ASIDE of its joints in front of the eye past retail's side of the picture (the rest it keeps hidden behind
   what is nearer, or out at the top or bottom: retail's picture shows none of it, inchambr.cut's Dingodile by
   N. Tropy's close-up): the move that takes it wholly past the view's side, 0 when there is none to make */
static int nw_cut_near(const struct numtx_s *vpc, const struct numtx_s *m, const NwCutShape *shape, float move[3])
{
    float x[NW_CUT_POINTS], w[NW_CUT_POINTS], cx, cw;
    int n = nw_cut_clip(vpc, m, shape, x, w, &cx, &cw);
    float dx = shape->max->x - shape->min->x, dy = shape->max->y - shape->min->y, dz = shape->max->z - shape->min->z;
    float size = sqrtf(dx * dx + dy * dy + dz * dz), near_clip = NuCameraGetCam()->nearclip;
    float wmin = 1e30f;
    for (int i = 0; i < n; i++) {
        wmin = w[i] - cw < wmin ? w[i] - cw : wmin;
    }
    if (!(wmin < NW_CUT_NEAR * near_clip)) {
        return 0;
    }
    for (float s = -1.0f; s < 2.0f; s += 2.0f) {
        int front = 0, aside = 0;
        float need = -1e30f;
        for (int i = 0; i < n; i++) {
            if (w[i] > 1e-3f) {
                front++;
                aside += s * x[i] * nw_wide.k > w[i];
            }
            need = w[i] + cw - s * x[i] + cx > need ? w[i] + cw - s * x[i] + cx : need;
        }
        float dir[3];
        if (front && aside >= NW_CUT_ASIDE * front && need > 0.0f && nw_cut_dir(vpc, s, dir)) {
            float d = need * NW_CUT_EDGE + 0.01f * size;
            for (int k = 0; k < 3; k++) {
                move[k] = d * dir[k];
            }
            return 1;
        }
    }
    return 0;
}

/* The cut's camera as its last render drew: world to clip, and the camera's own matrix. */
static struct numtx_s nw_cut_vpc, nw_cut_cam;
static int nw_cut_have_cam;

void nw_wide_cut_camera(void)
{
    nw_cut_vpc = *NuCameraGetVPCMtx();
    nw_cut_cam = *NuCameraGetMtx();
    nw_cut_have_cam = 1;
}

/* World to clip for an update, before this frame's camera is set: the cut's camera of this update (cutscenecammtx, when
   its camera system set one) in place of the one the last render drew with, through that render's projection. */
static const struct numtx_s *nw_cut_update_vpc(struct numtx_s *out)
{
    extern struct numtx_s cutscenecammtx;
    extern s32 set_cutscenecammtx;
    if (!nw_cut_have_cam) {
        return NULL;
    }
    if (!set_cutscenecammtx) {
        return &nw_cut_vpc;
    }
    /* NuMtxMul is affine: the projection's w column needs the full product */
    struct numtx_s inv;
    NuMtxInv(&inv, &cutscenecammtx);
    nw_vu0_mtx_mul(out, &inv, &nw_cut_cam);
    nw_vu0_mtx_mul(out, out, &nw_cut_vpc);
    return out;
}

/* Whether the scene's camera changes at a time in lo..hi */
static int nw_cut_camera_change(const struct instNUGCUTSCENE_s *scene, float lo, float hi)
{
    const struct NUGCUTCAMSYS_s *cams = scene && scene->def ? scene->def->camdef : NULL;
    const struct NUSTATEANIM_s *changes = cams ? cams->camchanges : NULL;
    for (int i = 0; changes && i < changes->count; i++) {
        if (changes->times[i] >= lo && changes->times[i] <= hi) {
            return 1;
        }
    }
    return 0;
}

/* This frame's moves, for the free locators (emitters: an exhaust's smoke) inside the objects that got them */
#define NW_CUT_MOVES 64
#define NW_CUT_HOLD 0.25f /* a box holds what is this much of its size around it too */

static struct {
    const struct instNUGCUTSCENE_s *scene;
    float time;
    float lo[3], hi[3]; /* the object's world box before the move */
    float d[3];
} nw_cut_moves[NW_CUT_MOVES];
static unsigned nw_cut_nmoves;

static void nw_cut_moved(const struct instNUGCUTSCENE_s *scene, float time, const struct numtx_s *m,
                         const struct nuvec_s *min, const struct nuvec_s *max, const float d[3])
{
    unsigned i = nw_cut_nmoves++ % NW_CUT_MOVES;
    nw_cut_moves[i].scene = scene;
    nw_cut_moves[i].time = time;
    for (int k = 0; k < 3; k++) {
        nw_cut_moves[i].lo[k] = 1e30f;
        nw_cut_moves[i].hi[k] = -1e30f;
        nw_cut_moves[i].d[k] = d[k];
    }
    for (int c = 0; c < 8; c++) {
        float x = c & 1 ? max->x : min->x, y = c & 2 ? max->y : min->y, z = c & 4 ? max->z : min->z;
        float p[3] = { x * m->_00 + y * m->_10 + z * m->_20 + m->_30, x * m->_01 + y * m->_11 + z * m->_21 + m->_31,
                       x * m->_02 + y * m->_12 + z * m->_22 + m->_32 };
        for (int k = 0; k < 3; k++) {
            nw_cut_moves[i].lo[k] = p[k] < nw_cut_moves[i].lo[k] ? p[k] : nw_cut_moves[i].lo[k];
            nw_cut_moves[i].hi[k] = p[k] > nw_cut_moves[i].hi[k] ? p[k] : nw_cut_moves[i].hi[k];
        }
    }
    for (int k = 0; k < 3; k++) {
        float grow = (nw_cut_moves[i].hi[k] - nw_cut_moves[i].lo[k]) * NW_CUT_HOLD;
        nw_cut_moves[i].lo[k] -= grow;
        nw_cut_moves[i].hi[k] += grow;
    }
}

struct numtx_s *nw_wide_cut_locator(const struct instNUGCUTSCENE_s *scene, struct NUGCUTLOCATOR_s *data, float time,
                                    struct numtx_s *wm, struct numtx_s *out)
{
    if (!(nw_wide.k > 1.0f) || !nw_cut_nmoves) {
        return wm;
    }
    struct nuanimtime_s atime;
    struct numtx_s lm;
    if (data->anim) {
        NuAnimData2CalcTime(data->anim, time, &atime);
    }
    NuGCutLocatorCalcMtx(data, time, &lm, &atime);
    if (wm) {
        NuMtxMul(&lm, &lm, wm);
    }
    float p[3] = { lm._30, lm._31, lm._32 };
    unsigned n = nw_cut_nmoves < NW_CUT_MOVES ? nw_cut_nmoves : NW_CUT_MOVES;
    for (unsigned j = 0; j < n; j++) {
        unsigned i = (nw_cut_nmoves - 1 - j) % NW_CUT_MOVES;
        if (nw_cut_moves[i].scene != scene || fabsf(nw_cut_moves[i].time - time) > 0.01f) {
            continue;
        }
        int inside = 1;
        for (int k = 0; k < 3; k++) {
            inside &= p[k] >= nw_cut_moves[i].lo[k] && p[k] <= nw_cut_moves[i].hi[k];
        }
        if (inside) {
            if (wm) {
                *out = *wm;
            } else {
                NuMtxSetIdentity(out);
            }
            out->_30 += nw_cut_moves[i].d[0];
            out->_31 += nw_cut_moves[i].d[1];
            out->_32 += nw_cut_moves[i].d[2];
            return out;
        }
    }
    return wm;
}

static void nw_cut_move(const struct instNUGCUTSCENE_s *scene, float time, struct numtx_s *m,
                        const NwCutShape *shape, const float d[3])
{
    nw_cut_moved(scene, time, m, shape->min, shape->max, d);
    m->_30 += d[0];
    m->_31 += d[1];
    m->_32 += d[2];
}

/* Keeps an object's slot: seen now, at this time of the cut */
static void nw_cut_see(const void *key, float time)
{
    int i = nw_cut_slot(key);
    int known = nw_cut[i].known && time >= nw_cut[i].time - 0.01f && time <= nw_cut[i].time + NW_CUT_STEP;
    if (!known) {
        nw_cut[i].coming = 0;
        nw_cut[i].hold_known = 0;
    }
    nw_cut[i].tick = ++nw_cut_tick;
    nw_cut[i].time = time;
    nw_cut[i].known = 1;
}

/* A rigid's world matrix at a time, as the cut places it */
static void nw_cut_rigid_at(const struct instNUGCUTSCENE_s *scene, const struct NUGCUTRIGID_s *data, float time,
                            struct numtx_s *m)
{
    NuGCutRigidCalcMtx((struct NUGCUTRIGID_s *)data, time, m);
    if (scene->flags & NUGCUTSCENE_MATRIXSET) {
        NuMtxMul(m, m, (struct numtx_s *)&scene->mtx);
    }
}

/* When a rigid shown at on starts to move: the last frame of its place at on (nitros in logos1.cut waits 10 frames
   after its key, its exhaust smoking); on + NW_CUT_MAX when it does not within that */
static float nw_cut_go(const struct instNUGCUTSCENE_s *scene, const struct NUGCUTRIGID_s *data, float on)
{
    struct numtx_s a, b;
    nw_cut_rigid_at(scene, data, on, &a);
    for (float t = on + 1.0f; t < on + NW_CUT_MAX; t += 1.0f) {
        nw_cut_rigid_at(scene, data, t, &b);
        float dx = b._30 - a._30, dy = b._31 - a._31, dz = b._32 - a._32;
        if (dx * dx + dy * dy + dz * dz > 1e-6f) {
            return t - 1.0f;
        }
    }
    return on + NW_CUT_MAX; /* it stays: nothing flies in */
}

/* The place of a rigid coming in to arrive at its true place and speed at arrive, entry frames span: its motion from
   arrive run backwards, pushed out by e^2 of the way past the view's edge, e the part of the entry left. 0 when it
   cannot be placed. The move is recorded for the emitters around where it stands. */
static int nw_cut_coming(const struct instNUGCUTSCENE_s *scene, const struct NUGCUTRIGID_s *data,
                         const struct numtx_s *vpc, float time, float arrive, float span, const NwCutShape *shape,
                         struct numtx_s *m)
{
    struct numtx_s at, after, now;
    nw_cut_rigid_at(scene, data, arrive, &at);
    nw_cut_rigid_at(scene, data, arrive + 1.0f, &after);
    nw_cut_rigid_at(scene, data, time, &now);
    float ahead = arrive - time, r, w, dir[3], edge = nw_wide.k * NW_CUT_EDGE;
    *m = at;
    m->_30 -= (after._30 - at._30) * ahead;
    m->_31 -= (after._31 - at._31) * ahead;
    m->_32 -= (after._32 - at._32) * ahead;
    if (!nw_cut_extent(vpc, m, shape, &r, &w, dir)) {
        return 0;
    }
    if (r < edge) {
        float e = ahead / span, d = nw_cut_need(vpc, m, shape, r + (edge - r) * e * e, dir);
        m->_30 += d * dir[0];
        m->_31 += d * dir[1];
        m->_32 += d * dir[2];
    }
    float move[3] = { m->_30 - now._30, m->_31 - now._31, m->_32 - now._32 };
    nw_cut_moved(scene, time, &now, shape->min, shape->max, move);
    return 1;
}

/* A hidden rigid a key shows next past retail's edge, in this shot, comes in ahead of the key: it arrives where it
   starts to move (slot's arrive). Within its entry's frames m is where to show it, and 1 is returned; before them it
   waits past the view's edge, the move recorded for the emitters around it. */
static int nw_cut_ahead(const struct instNUGCUTSCENE_s *scene, const struct NUGCUTRIGID_s *data, int slot,
                        const struct numtx_s *vpc, float time, const NwCutShape *shape, struct numtx_s *m)
{
    const struct NUSTATEANIM_s *anim = data->visibiltyanim;
    float on = -1.0f;
    for (int i = 0; anim && i < anim->count && on < 0.0f; i++) {
        if (anim->times[i] > time && anim->values[i] != 0) {
            on = anim->times[i];
        }
    }
    if (on < 0.0f) {
        return 0;
    }
    if (nw_cut[slot].on != on) {
        nw_cut[slot].on = on;
        nw_cut[slot].arrive = nw_cut_go(scene, data, on);
    }
    float arrive = nw_cut[slot].arrive;
    if (arrive >= on + NW_CUT_MAX || nw_cut_camera_change(scene, time - 1.0f, arrive + 1.0f)) {
        return 0;
    }
    struct numtx_s at, now;
    nw_cut_rigid_at(scene, data, arrive, &at);
    float r, w, dir[3], edge = nw_wide.k * NW_CUT_EDGE;
    if (!nw_cut_extent(vpc, &at, shape, &r, &w, dir) || !(r > 1.0f - NW_CUT_LOOSE && r < edge)) {
        return 0;
    }
    float span = nw_cut_span(r);
    if (arrive - time > span || (data->flags & 0x2)) {
        /* Waiting: past the view's edge from where it stands */
        nw_cut_rigid_at(scene, data, time, &now);
        if (nw_cut_extent(vpc, &now, shape, &r, &w, dir) && r < edge) {
            float d = nw_cut_need(vpc, &now, shape, edge, dir), move[3] = { d * dir[0], d * dir[1], d * dir[2] };
            nw_cut_moved(scene, time, &now, shape->min, shape->max, move);
        }
        return 0;
    }
    if (!nw_cut_coming(scene, data, vpc, time, arrive, span, shape, m)) {
        return 0;
    }
    nw_cut[slot].span = span;
    nw_cut[slot].coming = 1;
    return 1;
}

void nw_wide_cut_unforce(const struct nuhspecial_s *obj)
{
    int i = nw_cut_find(obj);
    if (i >= 0 && nw_cut[i].forced) {
        nw_cut[i].forced = 0;
        if (obj->special && obj->special->instance) {
            obj->special->instance->flagword &= ~NUINSTANCE_VISIBLE;
        }
    }
}

int nw_wide_cut_rigid(const struct instNUGCUTSCENE_s *scene, const struct NUGCUTRIGID_s *data,
                      const struct nuhspecial_s *obj, int visible, float time, struct numtx_s *m)
{
    const struct nugobj_s *gobj = NULL;
    if (obj && obj->scene && obj->scene->gobjs && obj->special && obj->special->instance) {
        gobj = obj->scene->gobjs[obj->special->instance->objid];
    }
    if (!gobj || !(nw_wide.k > 1.0f)) {
        return 0;
    }
    /* An animated rigid is placed as the render draws it, the others by the update */
    struct numtx_s tmp;
    const struct numtx_s *vpc = data->flags & 0x2 ? NuCameraGetVPCMtx() : nw_cut_update_vpc(&tmp);
    NwCutShape shape = { &gobj->min, &gobj->max, NULL, 0, 0.0f };
    nw_cut_see(obj, time);
    int i = nw_cut_find(obj);
    if (i < 0 || !vpc) {
        return 0;
    }
    if (visible) {
        /* Shown by its key while still coming in, until it starts to move */
        if (nw_cut[i].coming && time < nw_cut[i].arrive &&
            nw_cut_coming(scene, data, vpc, time, nw_cut[i].arrive, nw_cut[i].span, &shape, m)) {
            return 0;
        }
        nw_cut[i].coming = 0;
        return 0;
    }
    if (!nw_cut_ahead(scene, data, i, vpc, time, &shape, m)) {
        nw_cut[i].coming = 0;
        return 0;
    }
    nw_cut[i].forced = 1;
    return 1;
}

/* A character's box from its joints, grown for the skin around them; its model's when it has none */
static void nw_cut_joint_box(const struct NUHGOBJ_s *hobj, const struct numtx_s *joints, struct nuvec_s *min,
                             struct nuvec_s *max)
{
    if (!joints || !hobj->num_joints) {
        *min = hobj->bbox_min;
        *max = hobj->bbox_max;
        return;
    }
    float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
    for (int j = 0; j < hobj->num_joints; j++) {
        const float *p = &joints[j]._30;
        for (int k = 0; k < 3; k++) {
            lo[k] = p[k] < lo[k] ? p[k] : lo[k];
            hi[k] = p[k] > hi[k] ? p[k] : hi[k];
        }
    }
    float size = 0.0f;
    for (int k = 0; k < 3; k++) {
        size = hi[k] - lo[k] > size ? hi[k] - lo[k] : size;
    }
    float grow = size * NW_CUT_JOINTS + 0.05f;
    *min = (struct nuvec_s){ lo[0] - grow, lo[1] - grow, lo[2] - grow };
    *max = (struct nuvec_s){ hi[0] + grow, hi[1] + grow, hi[2] + grow };
}

void nw_wide_cut_char(const struct instNUGCUTSCENE_s *scene, const struct NUGCUTCHAR_s *desc, const void *key,
                      int visible, float time, struct numtx_s *m, const struct NUHGOBJ_s *hobj,
                      const struct numtx_s *joints)
{
    (void)desc;
    if (!(nw_wide.k > 1.0f)) {
        return;
    }
    struct nuvec_s min = { 0 }, max = { 0 };
    NwCutShape shape = { &min, &max, NULL, 0, 0.0f };
    if (visible) {
        nw_cut_joint_box(hobj, joints, &min, &max);
        if (joints && hobj->num_joints) {
            /* Placed by the joints themselves: a box's corners, seen at an angle, reach far past the body */
            float dx = max.x - min.x, dy = max.y - min.y, dz = max.z - min.z;
            float size = dx > dy ? (dx > dz ? dx : dz) : (dy > dz ? dy : dz);
            shape.joints = joints;
            shape.n = hobj->num_joints;
            shape.grow = size * NW_CUT_SKIN;
        }
    }
    const struct numtx_s *vpc = NuCameraGetVPCMtx();
    nw_cut_see(key, time);
    float move[3];
    if (visible && nw_cut_near(vpc, m, &shape, move)) {
        nw_cut_move(scene, time, m, &shape, move);
    }
}

/* Characters are not slid: one a key shows or hides mid-shot while it sits past retail's picture (inchambr.cut's Tiny
   and Dingodile, swapped at their seats out of retail's view) stays in its place for the whole shot instead, in the pose
   of the frame the key shows it at, or the last it was shown at. A character whose joints' centre is inside retail's
   picture is left to its keys. */
extern void NuHGobjEvalAnim2(struct NUHGOBJ_s *obj, void *joint_anim, f32 frame, s32 njoints, void *joints,
                             struct numtx_s *tmtx);
extern void NuHGobjEval(struct NUHGOBJ_s *obj, s32 njoints, void *joints, struct numtx_s *tmtx);

static int nw_cut_char_shown(struct NUGCUTCHAR_s *desc, float time)
{
    struct numtx_s m;
    int vis = 0;
    NuGCutCharAnimProcess(desc, time, &m, &vis, 0, 0, 0);
    return vis != 0;
}

/* Whether a character's joints' centre, posed at time, is past retail's picture */
static int nw_cut_char_out(const struct instNUGCUTSCENE_s *scene, struct NUGCUTCHAR_s *desc, struct NUHGOBJ_s *hobj,
                           float time)
{
    static struct numtx_s joints[NW_CUT_POINTS];
    struct numtx_s m, c;
    int vis = 0;
    NuGCutCharAnimProcess(desc, time, &m, &vis, 0, 0, 0);
    if (!vis || !hobj->num_joints) {
        return 0;
    }
    if (scene->flags & NUGCUTSCENE_MATRIXSET) {
        NuMtxMul(&m, &m, (struct numtx_s *)&scene->mtx);
    }
    if (desc->joint_anim) {
        NuHGobjEvalAnim2(hobj, desc->joint_anim, time, 0, 0, joints);
    } else {
        NuHGobjEval(hobj, 0, 0, joints);
    }
    float p[3] = { 0.0f, 0.0f, 0.0f };
    for (int j = 0; j < hobj->num_joints; j++) {
        for (int k = 0; k < 3; k++) {
            p[k] += (&joints[j]._30)[k] / hobj->num_joints;
        }
    }
    nw_vu0_mtx_mul(&c, &m, NuCameraGetVPCMtx());
    float x = c._00 * p[0] + c._10 * p[1] + c._20 * p[2] + c._30;
    float w = c._03 * p[0] + c._13 * p[1] + c._23 * p[2] + c._33;
    return w > 1e-3f && fabsf(x / w) * nw_wide.k > 1.0f;
}

int nw_wide_cut_char_hold(const struct instNUGCUTSCENE_s *scene, struct NUGCUTCHAR_s *desc, const void *key,
                          struct NUHGOBJ_s *hobj, float *time)
{
    if (!(nw_wide.k > 1.0f) || !scene || !scene->def || !desc->char_anim || desc->char_anim->ncurves < 7) {
        return 0;
    }
    float t = *time;
    int i = nw_cut_slot(key);
    if (!nw_cut[i].hold_known || t < nw_cut[i].hold_lo || t >= nw_cut[i].hold_hi) {
        /* The shot around t, between the scene's camera changes */
        float lo = 0.0f, hi = scene->def->duration + 1.0f;
        const struct NUGCUTCAMSYS_s *cams = scene->def->camdef;
        const struct NUSTATEANIM_s *changes = cams ? cams->camchanges : NULL;
        for (int c = 0; changes && c < changes->count; c++) {
            float ct = changes->times[c];
            lo = ct <= t && ct > lo ? ct : lo;
            hi = ct > t && ct < hi ? ct : hi;
        }
        /* The hidden stretch around t within it, and the frames shown on either side */
        float before = -1.0f, after = -1.0f;
        for (float f = floorf(t); f >= lo; f -= 1.0f) {
            if (nw_cut_char_shown(desc, f)) {
                before = f;
                break;
            }
        }
        for (float f = floorf(t) + 1.0f; f < hi; f += 1.0f) {
            if (nw_cut_char_shown(desc, f)) {
                after = f;
                break;
            }
        }
        nw_cut[i].hold_known = 1;
        nw_cut[i].hold_lo = before >= 0.0f ? before + 1.0f : lo;
        nw_cut[i].hold_hi = after >= 0.0f ? after : hi;
        /* Shown just at the shot's start or end only: that is the camera change's, nothing to hold */
        if (before >= 0.0f && before > lo + 1.0f) {
            nw_cut[i].hold_t = before;
        } else if (after >= 0.0f && after < hi - 1.0f) {
            nw_cut[i].hold_t = after;
        } else {
            nw_cut[i].hold_ok = 0;
            return 0;
        }
        nw_cut[i].hold_ok = nw_cut_char_out(scene, desc, hobj, nw_cut[i].hold_t);
    }
    if (!nw_cut[i].hold_ok) {
        return 0;
    }
    *time = nw_cut[i].hold_t;
    return 1;
}
