/* [game] title_menu: the NuWorld mark (tools/title_logo.py) is a second character of the title cuts, in the movie's
   model cache so CloseCutMovie frees it. */
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "nu_types.h"
#include "audio/nusound.h"
#include "gamelib/gcutscn.h"
#include "numath/numtx.h"
#include "numath/nuvec.h"
#include "nu3d/nuanim.h"
#include "nu3d/nugobj.h"
#include "nu3d/nugscn.h"
#include "nu3d/nuhgobj.h"
#include "nu3d/numtl.h"
#include "nu3d/nutexanm.h"
#include "game/cut.h"
#include "game/cut.retail.h"
#include "gameplay/gameplay.h"
#include "gameplay/titlemenu.h"
#include "port/nuhgobj.h"

extern struct csccache_s ghg_insts[];
extern s32 ghg_inst_count;
extern s32 cutmovie;
extern struct instNUGCUTSCENE_s *CutInst[];
extern s32 logos_played;
extern s32 texanimbits;
extern union variptr_u superbuffer_ptr;
extern const unsigned char nw_title_logo_ghg[];
extern const size_t nw_title_logo_ghg_size;

/* Declared by no header. */
struct NUJOINTANIM_s;
void NuHGobjEvalAnim2(struct NUHGOBJ_s *hobj, struct nuanimdata2_s *animdata, float time, int njanims,
                      struct NUJOINTANIM_s *janim, struct numtx_s *mtx_array);
int NuHGobjRndrMtxDwa(struct NUHGOBJ_s *hobj, struct numtx_s *wm, int nlayers, short *layer, struct numtx_s *mtx_array,
                      float **dwa);

#define MARK_CACHE 32       /* ghg_insts' size */
#define MARK_CURVES 9       /* per joint: translate, rotate, scale */
#define MARK_CHAR_CURVES 10
#define MARK_CHARS 8
#define MARK_SHARDS 10

/* Logos cut frames (30/s): TT voice until 722 and a "tss" until 731, the title sets off at 710, camera dive until 872,
   letters from 876. */
#define MENU_START 700.0f
#define MENU_SOUND 732.0f /* silent until here */
#define MENU_SOUND_RISE 0.2f
#define MENU_FPS 30.0f
#define MENU_RIGID_FOUND 0x4      /* gcutscn.c's NUGCUTRIGID_FOUND */
#define MENU_INSTANCE_VISIBLE 0x1 /* and NUINSTANCE_VISIBLE */
#define MENU_LOCATOR_OFF 0x2      /* a locator flag instNuGCutLocatorSysUpdate skips */

/* The Traveller's Tales logo's rigids. */
static const char *const menu_hidden[] = { "island", "bag", "stick4", "ttLogo100", "w1", "w2", "w3", "dot1", "t1",
                                           "dash", "t2", "a", "l", "e", "s", "dot2", "c", "o", "m" };
#define MENU_SPARKLE "sparkle"
#define MENU_EXHAUST "exhaust2" /* the title's green exhaust */

#define TITLE_SETOFF 710
#define TITLE_ENTER 700   /* shown from here, still off the screen */
#define TITLE_DELAY 31.0f /* its flight runs late, into view as the sound comes in */
#define TITLE_CATCH 735   /* and catches up from here */
#define TITLE_CAUGHT 820  /* to here, before the camera's dive */
#define TITLE_CURVES 7    /* translate, rotate, visible */
#define TITLE_SLOTS 3     /* the title and its two exhausts */
#define TITLE_CHUNKS 64
#define TITLE_KEYS 192

#define MARK_GATHER TITLE_ENTER
#define MARK_SETOFF 876 /* "Crash": the first shard spirals in */
#define MARK_STEP 7
#define MARK_FLIGHT 14 /* the last lands at 953 */
#define MARK_PULSE 6
#define MARK_SWELL 0.08f
#define MARK_FIRST (MARK_GATHER - 1)
#define MARK_LAST (MARK_SETOFF + (MARK_SHARDS - 1) * MARK_STEP + MARK_FLIGHT + MARK_PULSE)
#define MARK_CUT 200 /* longer title joint animations are the logos cut's */
#define MARK_SETTLE 830
#define MARK_SETTLE_FRAMES 40.0f
#define MARK_EDGE 3.2f
#define MARK_WIDE 1.3f
#define MARK_TALL 0.85f
#define MARK_KEYS (1 + MARK_LAST - MARK_FIRST + 1)
#define MARK_REST 100000.0f /* past every title curve */
#define MARK_TURN (2.0f * SDL_PI_F / (40.0f * MENU_FPS))
#define MARK_EASE_IN 60.0f
#define MARK_BOB 0.06f
#define MARK_BOB_FRAMES 130.0f
#define MARK_SWAY 0.035f
#define MARK_SWAY_FRAMES 190.0f
#define MARK_ROCK 0.05f
#define MARK_ROCK_FRAMES 160.0f
#define VORTEX_JOINT "Character|Vortex"
#define VORTEX_OPEN (MARK_LAST - MARK_PULSE) /* glass until the last shard lands */
#define VORTEX_FRAMES 30.0f

static char mark_path[] = "nuworld\\title_logo.ghg"; /* its cache key, matched by address */
static char mark_name[] = "nwlogo";
static struct NUGCUTCHAR_s mark_chars[2][MARK_CHARS]; /* logos cut, title loop */
static bool menu_skipping;
static float mark_clock;
static struct instNUGCUTSCENE_s *mark_clock_scene;
static float mark_clock_at;
static struct numtl_s *vortex_mtl; /* while its tid is ours */
static int vortex_own_tid, vortex_set_tid;

/* The mark's character motion: the title's rest pose and visibility. */
static struct nuanimdata2_s mark_motion[2];
static union nuanimcurve2_u mark_motion_curves[2][MARK_CHAR_CURVES];
static signed char mark_motion_flags[2][MARK_CHAR_CURVES];
static char mark_motion_set[2];
static float mark_rest[6];
static bool mark_rested;

typedef struct MarkChase {
    float lag;
    float back;
    float spread;
    float phase, orbit;
    float edge_back;
    float reach;
    float edge_phase, edge_orbit;
    float sway, swayrate;
    float turn0[3], turn[3];
} MarkChase;

/* In the mark's joint 0 space. */
typedef struct MarkTrack {
    struct nuvec_s centre[MARK_LAST + 1];
    struct nuvec_s mid;
} MarkTrack;

static struct NUHGOBJ_s *mark_model(void)
{
    for (int i = 0; i < ghg_inst_count; i++) {
        if (ghg_insts[i].path == mark_path) {
            return ghg_insts[i].obj;
        }
    }
    if (ghg_inst_count >= MARK_CACHE) {
        return NULL;
    }
    struct NUHGOBJ_s *obj = nw_ghg_read_image(&superbuffer_ptr, nw_title_logo_ghg, nw_title_logo_ghg_size, mark_path);
    if (obj) {
        ghg_insts[ghg_inst_count].path = mark_path;
        ghg_insts[ghg_inst_count].obj = obj;
        ghg_inst_count++;
    }
    return obj;
}

static float mark_ease(float p)
{
    float q = 1.0f - SDL_clamp(p, 0.0f, 1.0f);
    return 1.0f - q * q * q * q;
}

static float mark_hash(int j, int k)
{
    uint32_t h = (uint32_t)j * 2654435761u ^ ((uint32_t)k * 40503u + 0x9E37u);
    h ^= h >> 13;
    h *= 0x5BD1E995u;
    h ^= h >> 15;
    return (float)(h & 0xFFFF) / 65536.0f;
}

static MarkChase mark_chase(int j)
{
    MarkChase c;
    c.lag = 4.0f + 14.0f * mark_hash(j, 0);
    c.back = 4.0f + 3.0f * mark_hash(j, 1);
    c.spread = 1.3f + 0.9f * mark_hash(j, 2);
    c.phase = 2.0f * SDL_PI_F * mark_hash(j, 3);
    c.orbit = (j % 2 ? 1.0f : -1.0f) * (0.012f + 0.02f * mark_hash(j, 4));
    c.edge_back = 4.0f + 1.5f * mark_hash(j, 13); /* behind the camera's dive */
    c.reach = MARK_EDGE + 0.4f * mark_hash(j, 14);
    c.edge_phase = (mark_hash(j, 15) - 0.5f) * 0.6f;
    c.edge_orbit = 0.004f + 0.004f * mark_hash(j, 16);
    c.sway = 0.25f + 0.35f * mark_hash(j, 5);
    c.swayrate = 0.05f + 0.06f * mark_hash(j, 6);
    for (int k = 0; k < 3; k++) {
        c.turn0[k] = 2.0f * SDL_PI_F * mark_hash(j, 7 + k);
        c.turn[k] = (mark_hash(j, 10 + k) - 0.5f) * 0.12f;
    }
    return c;
}

static int mark_start(int j)
{
    bool inner = (j - 1) % 2 == 0; /* the icon's order: inner, outer per element */
    return MARK_SETOFF + ((inner ? MARK_SHARDS / 2 : 0) + (j - 1) / 2) * MARK_STEP;
}

static void mark_chase_pose(const MarkTrack *tr, const struct nuvec_s *place, int j, float f, struct nuvec_s *pos,
                            float rot[3])
{
    MarkChase c = mark_chase(j);
    float g = SDL_max(f - c.lag, (float)MARK_GATHER);
    int g0 = (int)g;
    float w = g - (float)g0;
    const struct nuvec_s *a = &tr->centre[g0], *b = &tr->centre[SDL_min(g0 + 1, MARK_LAST)];
    float t = f - MARK_GATHER;
    float dx = place->x - tr->mid.x, dz = place->z - tr->mid.z, len = SDL_sqrtf(dx * dx + dz * dz);
    float ux = len > 0.0f ? dx / len : 1.0f, uz = len > 0.0f ? dz / len : 0.0f;
    float ang = c.phase + c.orbit * t, ca = SDL_cosf(ang), sa = SDL_sinf(ang);
    float eang = c.edge_phase + c.edge_orbit * t, ce = SDL_cosf(eang), se = SDL_sinf(eang);
    float e = SDL_clamp((f - MARK_SETTLE) / MARK_SETTLE_FRAMES, 0.0f, 1.0f);
    e = e * e * (3.0f - 2.0f * e);
    float ox = (1.0f - e) * (dx * ca - dz * sa) * c.spread + e * (ux * ce - uz * se) * c.reach * MARK_WIDE;
    float oz = (1.0f - e) * (dx * sa + dz * ca) * c.spread + e * (ux * se + uz * ce) * c.reach * MARK_TALL;
    float back = c.back + (c.edge_back - c.back) * e;
    pos->x = a->x + (b->x - a->x) * w + ox + c.sway * SDL_sinf(c.swayrate * f + j);
    pos->y = a->y + (b->y - a->y) * w - back + 0.5f * c.sway * SDL_sinf(0.7f * c.swayrate * f + 3 * j);
    pos->z = a->z + (b->z - a->z) * w + oz + c.sway * SDL_cosf(1.3f * c.swayrate * f + 2 * j);
    for (int k = 0; k < 3; k++) {
        rot[k] = c.turn0[k] + c.turn[k] * t;
    }
}

/* The curves' z is down, the matrices' up. */
static void mark_pose(const MarkTrack *tr, const struct NUHGOBJ_s *obj, int j, float f, float out[MARK_CURVES])
{
    struct nuvec_s place = { obj->T[j]._30, obj->T[j]._31, obj->T[j]._32 };
    struct nuvec_s pos;
    float rot[3];
    float start = (float)mark_start(j), land = start + MARK_FLIGHT;
    mark_chase_pose(tr, &place, j, SDL_min(SDL_max(f, (float)MARK_GATHER), start), &pos, rot);
    if (f > start) {
        float k = mark_ease((f - start) / MARK_FLIGHT);
        float x0 = pos.x - tr->mid.x, z0 = pos.z - tr->mid.z, x1 = place.x - tr->mid.x, z1 = place.z - tr->mid.z;
        float a0 = SDL_atan2f(z0, x0), a1 = SDL_atan2f(z1, x1), r0 = SDL_sqrtf(x0 * x0 + z0 * z0);
        float r1 = SDL_sqrtf(x1 * x1 + z1 * z1), da = a1 - a0;
        while (da > SDL_PI_F) {
            da -= 2.0f * SDL_PI_F;
        }
        while (da < -SDL_PI_F) {
            da += 2.0f * SDL_PI_F;
        }
        float a = a0 + da * k, r = r0 + (r1 - r0) * k;
        pos.x = tr->mid.x + r * SDL_cosf(a);
        pos.z = tr->mid.z + r * SDL_sinf(a);
        pos.y += (place.y - pos.y) * k;
        for (int i = 0; i < 3; i++) {
            float whole = 2.0f * SDL_PI_F * SDL_roundf(rot[i] / (2.0f * SDL_PI_F));
            rot[i] += (whole - rot[i]) * k;
        }
    }
    out[0] = pos.x;
    out[1] = pos.y;
    out[2] = -pos.z;
    out[3] = rot[0];
    out[4] = rot[1];
    out[5] = rot[2];
    float scale = 1.0f;
    if (f > land && f < land + MARK_PULSE) {
        scale += MARK_SWELL * SDL_sinf(SDL_PI_F * (f - land) / MARK_PULSE);
    }
    out[6] = out[7] = out[8] = scale;
}

static struct nuanimdata2_s *mark_motion_of(struct nuanimdata2_s *title, bool flight)
{
    if (!title || title->ncurves < 6 || title->ncurves > MARK_CHAR_CURVES) {
        return NULL;
    }
    if (!mark_rested) {
        struct nuanimtime_s atime;
        NuAnimData2CalcTime(title, MARK_REST, &atime);
        for (int c = 0; c < 6; c++) {
            mark_rest[c] = NuAnimCurve2CalcVal(&title->curves[c], &atime, title->curveflags[c]);
        }
        mark_rested = true;
    }
    struct nuanimdata2_s *a = &mark_motion[flight];
    *a = *title;
    a->nnodes = 1;
    for (int c = 0; c < title->ncurves; c++) {
        bool own = c < 6;
        if (own) {
            mark_motion_curves[flight][c].constant = mark_rest[c];
        } else {
            mark_motion_curves[flight][c] = title->curves[c];
        }
        mark_motion_flags[flight][c] = own ? 0 : title->curveflags[c];
    }
    mark_motion_set[flight] = title->curvesetflags[0];
    a->curves = mark_motion_curves[flight];
    a->curveflags = mark_motion_flags[flight];
    a->curvesetflags = &mark_motion_set[flight];
    return a;
}

static bool mark_track(MarkTrack *tr, struct NUHGOBJ_s *obj, struct nuanimdata2_s *rest, struct NUGCUTCHAR_s *title)
{
    static struct numtx_s tmtx[256];
    struct numtx_s m, at_rest, rest_inv, root_inv, k;
    int vis = 0;
    NuHGobjEvalAnim2(obj, rest, 1.0f, 0, NULL, tmtx);
    NuMtxInv(&root_inv, &tmtx[0]);
    NuGCutCharAnimProcess(title, MARK_REST, &at_rest, &vis, NULL, NULL, NULL);
    if (!vis) {
        return false;
    }
    NuMtxInv(&rest_inv, &at_rest);
    tr->mid = (struct nuvec_s){ 0.0f, 0.0f, 0.0f };
    for (int j = 1; j < obj->num_joints; j++) {
        tr->mid.x += obj->T[j]._30 / (float)(obj->num_joints - 1);
        tr->mid.y += obj->T[j]._31 / (float)(obj->num_joints - 1);
        tr->mid.z += obj->T[j]._32 / (float)(obj->num_joints - 1);
    }
    int first = -1;
    for (int f = MARK_GATHER; f <= MARK_LAST; f++) {
        NuGCutCharAnimProcess(title, (float)f, &m, &vis, NULL, NULL, NULL);
        if (!vis) {
            tr->centre[f] = first < 0 ? tr->mid : tr->centre[f - 1];
            continue;
        }
        /* R0 M(f) M(rest)^-1 R0^-1 */
        NuMtxMul(&k, &tmtx[0], &m);
        NuMtxMul(&m, &k, &rest_inv);
        NuMtxMul(&k, &m, &root_inv);
        NuVecMtxTransform(&tr->centre[f], &tr->mid, &k);
        if (first < 0) {
            first = f;
        }
    }
    for (int f = MARK_GATHER; first > MARK_GATHER && f < first; f++) {
        tr->centre[f] = tr->centre[first];
    }
    return first >= 0;
}

/* The chase in the logos cut, else at rest. */
static struct nuanimdata2_s *mark_anim(struct NUHGOBJ_s *obj, struct NUGCUTCHAR_s *title, bool flight)
{
    static struct nuanimdata2_s *built[2];
    if (built[flight]) {
        return built[flight];
    }
    const struct nuanimdata2_s *tja = title->joint_anim;
    int nn = obj->num_joints, nkeys = flight ? MARK_KEYS : 0, nkeyed = flight ? (nn - 1) * MARK_CURVES : 0;
    int nchunks = flight ? (MARK_LAST - 1) / 32 + 1 : 1;
    struct nuanimdata2_s *a = SDL_calloc(1, sizeof(*a));
    union nuanimcurve2_u *curves = SDL_calloc((size_t)nn * MARK_CURVES, sizeof(*curves));
    signed char *flags = SDL_calloc((size_t)nn * MARK_CURVES, 1);
    char *setflags = SDL_calloc((size_t)nn, 1);
    union nuanimmask_u *mask = SDL_calloc((size_t)nchunks, sizeof(*mask));
    unsigned short *ixs = SDL_calloc((size_t)nchunks, sizeof(*ixs));
    struct nuanimcurvedata2_s *data = nkeyed ? SDL_calloc((size_t)nkeyed, sizeof(*data)) : NULL;
    struct nuanimkey2_s *keys = nkeyed ? SDL_calloc((size_t)nkeyed * nkeys, sizeof(*keys)) : NULL;
    MarkTrack *tr = flight ? SDL_calloc(1, sizeof(*tr)) : NULL;
    if (!a || !curves || !flags || !setflags || !mask || !ixs || (nkeyed && (!data || !keys || !tr))) {
        return NULL;
    }
    static const float root[MARK_CURVES] = { 0, 0, 0, SDL_PI_F / 2, 0, 0, 1, 1, 1 };
    for (int c = 0; c < MARK_CURVES; c++) {
        bool own = tja && tja->ncurves == MARK_CURVES && tja->curveflags[c] == 0;
        curves[c].constant = own ? tja->curves[c].constant : root[c];
    }
    setflags[0] = tja ? tja->curvesetflags[0] : 1;
    for (int j = 1; j < nn; j++) {
        union nuanimcurve2_u *place = &curves[j * MARK_CURVES];
        setflags[j] = 1 | 8; /* rotate and scale */
        place[0].constant = obj->T[j]._30;
        place[1].constant = obj->T[j]._31;
        place[2].constant = -obj->T[j]._32;
        place[6].constant = place[7].constant = place[8].constant = 1.0f;
    }
    a->time = 2.0f;
    a->nnodes = (short)nn;
    a->ncurves = MARK_CURVES;
    a->nchunks = 1;
    a->curves = curves;
    a->curveflags = flags;
    a->curvesetflags = setflags;
    if (!flight) {
        built[flight] = a;
        return a;
    }
    if (!mark_track(tr, obj, a, title)) {
        SDL_free(tr);
        return NULL;
    }
    short frames[MARK_KEYS];
    frames[0] = 1;
    for (int n = 1; n < nkeys; n++) {
        frames[n] = (short)(MARK_FIRST + n - 1);
    }
    /* A key at frame f is bit (f - 1) % 32 of chunk (f - 1) / 32. */
    for (int n = 0; n < nkeys; n++) {
        mask[(frames[n] - 1) >> 5].bits |= 1u << ((frames[n] - 1) & 31);
    }
    for (int ch = 0, run = 0; ch < nchunks; ch++) {
        ixs[ch] = (unsigned short)run;
        for (unsigned int bits = mask[ch].bits; bits; bits &= bits - 1) {
            run++;
        }
    }
    for (int j = 1; j < nn; j++) {
        static float pose[MARK_KEYS][MARK_CURVES];
        for (int n = 0; n < nkeys; n++) {
            mark_pose(tr, obj, j, (float)frames[n], pose[n]);
        }
        for (int c = 0; c < MARK_CURVES; c++) {
            int at = (j - 1) * MARK_CURVES + c;
            struct nuanimkey2_s *kk = &keys[(size_t)at * nkeys];
            for (int n = 0; n < nkeys; n++) {
                bool inside = n > 1 && n < nkeys - 1;
                float span = (float)(frames[SDL_min(n + 1, nkeys - 1)] - frames[SDL_max(n - 1, 0)]);
                kk[n].time = (float)frames[n];
                kk[n].dtime = n + 1 < nkeys ? 1.0f / (float)(frames[n + 1] - frames[n]) : 0.0f;
                kk[n].value = pose[n][c];
                kk[n].slope = inside ? (pose[n + 1][c] - pose[n - 1][c]) / span : 0.0f;
            }
            data[at].mask = mask;
            data[at].key_ixs = ixs;
            data[at].keys = kk;
            curves[j * MARK_CURVES + c].curvedata = &data[at];
            flags[j * MARK_CURVES + c] = 1; /* NUANIMKEYTYPE_HERMITE */
        }
    }
    SDL_free(tr);
    a->time = (float)MARK_LAST;
    a->nchunks = (short)nchunks;
    built[flight] = a;
    return a;
}

/* Frames since the first shard set off, on through the title loop. */
static float mark_float_clock(struct instNUGCUTSCENE_s *scene, const struct NUGCUTCHAR_s *desc, float f)
{
    if (desc->joint_anim->time > (float)MARK_CUT) {
        mark_clock = f - (float)MARK_SETOFF;
    } else if (scene == mark_clock_scene && f >= mark_clock_at) {
        mark_clock += f - mark_clock_at;
    } else {
        mark_clock = SDL_max(mark_clock, 0.0f) + scene->speed;
    }
    mark_clock_scene = scene;
    mark_clock_at = f;
    return mark_clock;
}

/* The float t frames in, in joint 0's space; a landing shard takes w of it. */
static void mark_float(struct nuvec_s mid, float t, float w, struct numtx_s *s)
{
    float x = SDL_min(t / MARK_EASE_IN, 1.0f), ease = w * x * x * (3.0f - 2.0f * x);
    /* the ease's integral */
    float turned = x < 1.0f ? MARK_EASE_IN * x * x * x * (1.0f - 0.5f * x) : t - 0.5f * MARK_EASE_IN;
    NuMtxSetIdentity(s);
    s->_30 = -mid.x;
    s->_31 = -mid.y;
    s->_32 = -mid.z;
    NuMtxRotateYR(-MARK_TURN * turned * w, s);
    NuMtxRotateXR(ease * MARK_ROCK * SDL_sinf(2.0f * SDL_PI_F * t / MARK_ROCK_FRAMES), s);
    NuMtxRotateZR(ease * MARK_ROCK * SDL_sinf(2.0f * SDL_PI_F * t / (1.37f * MARK_ROCK_FRAMES)), s);
    mid.x += ease * MARK_SWAY * SDL_sinf(2.0f * SDL_PI_F * t / MARK_SWAY_FRAMES);
    mid.z += ease * MARK_BOB * SDL_sinf(2.0f * SDL_PI_F * t / MARK_BOB_FRAMES);
    NuMtxTranslate(s, &mid);
}

/* AppCutSceneCharacterRender for the mark, floating. */
static void mark_render(struct instNUGCUTSCENE_s *scene, struct instNUGCUTCHAR_s *ichar, struct NUGCUTCHAR_s *desc,
                        float f)
{
    static struct numtx_s tmtx[256];
    static short layers[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
    struct NUHGOBJ_s *obj = ichar->data;
    struct numtx_s mtx, s, inv, k;
    int vis = 0;
    NuGCutCharAnimProcess(desc, f, &mtx, &vis, NULL, NULL, NULL);
    if (scene->flags & NUGCUTSCENE_MATRIXSET) {
        NuMtxMul(&mtx, &mtx, &scene->mtx);
    }
    if (!vis) {
        return;
    }
    NuHGobjEvalAnim2(obj, desc->joint_anim, f, 0, NULL, tmtx);
    float t = mark_float_clock(scene, desc, f);
    if (t > 0.0f) {
        bool flight = desc->joint_anim->time > (float)MARK_CUT;
        struct numtx_s j0 = tmtx[0];
        struct nuvec_s mid = { 0.0f, 0.0f, 0.0f };
        for (int j = 1; j < obj->num_joints; j++) {
            mid.x += obj->T[j]._30 / (float)(obj->num_joints - 1);
            mid.y += obj->T[j]._31 / (float)(obj->num_joints - 1);
            mid.z += obj->T[j]._32 / (float)(obj->num_joints - 1);
        }
        NuMtxInv(&inv, &j0);
        /* J0^-1 S J0 after each joint */
        for (int j = 0; j < obj->num_joints; j++) {
            float w = flight && j > 0 ? mark_ease((f - (float)mark_start(j)) / MARK_FLIGHT) : 1.0f;
            mark_float(mid, t, w, &s);
            NuMtxMul(&k, &inv, &s);
            NuMtxMul(&k, &k, &j0);
            NuMtxMul(&tmtx[j], &tmtx[j], &k);
        }
    }
    NuHGobjRndrMtxDwa(obj, &mtx, obj->num_layers, layers, tmtx, NULL);
}

static int vortex_joint(const struct NUHGOBJ_s *obj)
{
    for (int j = 0; j < obj->num_joints; j++) {
        if (obj->joints[j].name && strcmp(obj->joints[j].name, VORTEX_JOINT) == 0) {
            return j;
        }
    }
    return -1;
}

/* Its first frame is the glass. */
static struct nutexanim_s *vortex_flipbook(const struct NUHGOBJ_s *obj, int v)
{
    struct nugobj_s *gobj = obj->num_layers > 0 ? obj->layers[0].gobjs[v] : NULL;
    struct numtl_s *mtl = gobj && gobj->geom ? gobj->geom->mtl : NULL;
    for (int i = 0; mtl && i < obj->numtexanims; i++) {
        if (obj->texanims[i].mtl == mtl && obj->texanims[i].numtids > 0) {
            return &obj->texanims[i];
        }
    }
    return NULL;
}

/* A flipbook frame, or (frame < 0) the flipbook's own back. Materials draw after the scene; the flipbook writes tid
   only when it steps. */
static void vortex_frame(struct nutexanim_s *book, int frame)
{
    struct numtl_s *mtl = book->mtl;
    if (frame >= 0) {
        if (vortex_mtl != mtl || mtl->tid != vortex_set_tid) {
            vortex_own_tid = mtl->tid;
        }
        vortex_mtl = mtl;
        vortex_set_tid = book->tids[SDL_min(frame, book->numtids - 1)];
        mtl->tid = vortex_set_tid;
    } else if (vortex_mtl == mtl) {
        if (mtl->tid == vortex_set_tid) {
            mtl->tid = vortex_own_tid;
        }
        vortex_mtl = NULL;
    }
}

/* The title with its vortex glass until the last shard lands, then forming; false once formed. */
static bool vortex_render(struct instNUGCUTSCENE_s *scene, struct NUGCUTSCENE_s *def, struct instNUGCUTCHAR_s *ichar,
                          struct NUGCUTCHAR_s *desc, float f)
{
    struct NUHGOBJ_s *obj = ichar->data;
    struct nuanimdata2_s *a = desc->joint_anim;
    int v = vortex_joint(obj);
    if (v < 0 || v >= a->nnodes || a->ncurves != MARK_CURVES || !obj->layers) {
        return false;
    }
    float u = mark_float_clock(scene, desc, f) - (float)(VORTEX_OPEN - MARK_SETOFF);
    float p = SDL_max(u, 0.0f) / VORTEX_FRAMES;
    struct nutexanim_s *book = vortex_flipbook(obj, v);
    if (book) {
        vortex_frame(book, p < 1.0f ? (int)(p * (float)book->numtids) : -1);
    }
    if (p >= 1.0f) {
        return false;
    }
    /* x scale is the funnel's depth */
    float q = p - 1.0f, c = 1.70158f;
    float depth = 1.0f + (c + 1.0f) * q * q * q + c * q * q;
    union nuanimcurve2_u *scale = &a->curves[v * MARK_CURVES + 6], kept = *scale;
    signed char *flags = &a->curveflags[v * MARK_CURVES + 6], kept_flags = *flags;
    char kept_set = a->curvesetflags[v];
    a->curvesetflags[v] |= 8;
    scale->constant = depth;
    *flags = 0;
    AppCutSceneCharacterRender__retail(scene, def, ichar, desc, f);
    a->curvesetflags[v] = kept_set;
    *scale = kept;
    *flags = kept_flags;
    return true;
}

bool nw_title_menu_render(struct instNUGCUTSCENE_s *scene, struct NUGCUTSCENE_s *def, struct instNUGCUTCHAR_s *ichar,
                          struct NUGCUTCHAR_s *desc, float f)
{
    if (!desc->name || !ichar->data || !desc->joint_anim) {
        return false;
    }
    if (desc->name == mark_name) {
        mark_render(scene, ichar, desc, f);
        return true;
    }
    struct NUGCUTCHARSYS_s *sys = def ? def->chardef : NULL;
    bool marked = sys && (sys->chars == mark_chars[0] || sys->chars == mark_chars[1]);
    return marked && strcmp(desc->name, "title") == 0 && vortex_render(scene, def, ichar, desc, f);
}

/* Hides the Traveller's Tales logo. The scene draws a rigid's instance unless it is invisible. */
static void menu_hide(struct NUGCUTSCENE_s *def, struct NUGCUTCHARSYS_s *sys)
{
    bool logos = false;
    for (int i = 0; i < sys->count; i++) {
        if (sys->chars[i].name && strcmp(sys->chars[i].name, "TTChar") == 0) {
            NW_DEVIATION(title_menu);
            sys->chars[i].character = NULL;
            logos = true;
        }
    }
    for (int i = 0; logos && def->rigiddef && i < def->rigiddef->count; i++) {
        struct NUGCUTRIGID_s *rigid = &def->rigiddef->rigids[i];
        for (size_t h = 0; rigid->name && h < SDL_arraysize(menu_hidden); h++) {
            if (strcmp(rigid->name, menu_hidden[h]) == 0 && (rigid->flags & MENU_RIGID_FOUND)) {
                rigid->special.special->instance->flagword &= ~MENU_INSTANCE_VISIBLE;
                rigid->flags &= ~MENU_RIGID_FOUND;
            }
        }
    }
    struct NUGCUTLOCATORSYS_s *loc = def->locatordef;
    for (int i = 0; logos && loc && i < loc->count; i++) {
        const char *type = loc->types[loc->locators[i].locator_type].name;
        if (type && strcmp(type, MENU_SPARKLE) == 0) {
            loc->locators[i].flags |= MENU_LOCATOR_OFF;
        }
    }
}

static void title_key_at(union nuanimmask_u *mask, int frame)
{
    mask[(frame - 1) >> 5].bits |= 1u << ((frame - 1) & 31);
}

static int title_count(const union nuanimmask_u *mask, unsigned short *ixs, int nchunks)
{
    int run = 0;
    for (int ch = 0; ch < nchunks; ch++) {
        if (ixs) {
            ixs[ch] = (unsigned short)run;
        }
        for (unsigned int bits = mask[ch].bits; bits; bits &= bits - 1) {
            run++;
        }
    }
    return run;
}

/* Frame f on the title's own clock: TITLE_DELAY late, caught up from TITLE_CATCH to TITLE_CAUGHT. */
static float title_clock(float f)
{
    float u = SDL_clamp((f - TITLE_CATCH) / (float)(TITLE_CAUGHT - TITLE_CATCH), 0.0f, 1.0f);
    return f - TITLE_DELAY * (1.0f - u * u * (3.0f - 2.0f * u));
}

static float title_curve(struct nuanimdata2_s *a, union nuanimcurve2_u *curve, float t)
{
    struct nuanimtime_s atime;
    NuAnimData2CalcTime(a, t, &atime);
    return NuAnimCurve2CalcVal(curve, &atime, 1);
}

/* A curve of the title at time t; before its first move (key k) that move carried back on the same line and turn. */
static float title_value(struct nuanimdata2_s *a, union nuanimcurve2_u *curve, const struct nuanimkey2_s *k, float t)
{
    return t < k->time ? k->value + k->slope * (t - k->time) : title_curve(a, curve, t);
}

/* The logos cut's title (slot 0) and its exhausts fly in late and from further out: a key a frame on the title's own
   clock until it has caught up, retail's keys after. The title is shown from TITLE_ENTER, an exhaust once it moves. */
static void title_flight(struct nuanimdata2_s *a, int slot)
{
    static struct nuanimcurvedata2_s data[TITLE_SLOTS][TITLE_CURVES];
    static union nuanimmask_u mask[TITLE_SLOTS][TITLE_CURVES][TITLE_CHUNKS];
    static unsigned short ixs[TITLE_SLOTS][TITLE_CURVES][TITLE_CHUNKS];
    static struct nuanimkey2_s keys[TITLE_SLOTS][TITLE_CURVES][TITLE_KEYS];
    static struct nuanimstepkey_s shown[2] = { { 0.0f, 0 }, { 1.0f, 0 } };
    const int own = TITLE_CAUGHT - TITLE_ENTER + 1, v = TITLE_CURVES - 1;
    bool title = slot == 0;
    int nch = a->nchunks;
    if (slot >= TITLE_SLOTS || a->ncurves < TITLE_CURVES || nch > TITLE_CHUNKS || (TITLE_ENTER - 1) >> 5 >= nch ||
        a->curveflags[v] != (title ? 3 : 1) /* NUANIMKEYTYPE_STEP, NUANIMKEYTYPE_HERMITE */ ||
        a->curves[v].curvedata == &data[slot][v]) {
        return;
    }
    memset(mask[slot], 0, sizeof(mask[slot]));
    for (int c = 0; c < (title ? v : TITLE_CURVES); c++) {
        union nuanimcurve2_u curve = a->curves[c];
        const struct nuanimcurvedata2_s *old = curve.curvedata;
        if (a->curveflags[c] != 1) { /* NUANIMKEYTYPE_HERMITE */
            continue;
        }
        int n = title_count(old->mask, NULL, nch), s = 0, after = 0;
        while (s < n && old->keys[s].time < (float)TITLE_SETOFF) {
            s++;
        }
        while (after < n && old->keys[after].time <= (float)TITLE_CAUGHT) {
            after++;
        }
        if (s == n || after == n || 1 + own + n - after > TITLE_KEYS) {
            continue;
        }
        const struct nuanimkey2_s *k = &old->keys[s];
        struct nuanimkey2_s *out = keys[slot][c];
        union nuanimmask_u *m = mask[slot][c];
        bool shows = c == v; /* an exhaust's visibility: off until the title moves */
        float start = shows ? 0.0f : title_value(a, &curve, k, title_clock((float)TITLE_ENTER));
        out[0] = (struct nuanimkey2_s){ 1.0f, 1.0f / (float)(TITLE_ENTER - 1), start, 0.0f };
        title_key_at(m, 1);
        for (int i = 0; i < own; i++) {
            float f = (float)(TITLE_ENTER + i), h = 0.05f, t = title_clock(f), value, slope = 0.0f;
            float next = i + 1 < own ? f + 1.0f : old->keys[after].time;
            if (shows) {
                value = t < (float)TITLE_SETOFF ? 0.0f : title_curve(a, &curve, t);
            } else {
                value = title_value(a, &curve, k, t);
                slope = (title_value(a, &curve, k, title_clock(f + h)) - title_value(a, &curve, k, title_clock(f - h))) /
                        (2.0f * h);
            }
            out[1 + i] = (struct nuanimkey2_s){ f, 1.0f / (next - f), value, slope };
            title_key_at(m, TITLE_ENTER + i);
        }
        memcpy(&out[1 + own], &old->keys[after], (size_t)(n - after) * sizeof(*out));
        for (int i = after; i < n; i++) {
            title_key_at(m, (int)old->keys[i].time);
        }
        title_count(m, ixs[slot][c], nch);
        data[slot][c] = (struct nuanimcurvedata2_s){ m, ixs[slot][c], out };
        a->curves[c].curvedata = &data[slot][c];
    }
    if (title) {
        title_key_at(mask[slot][v], 1);
        title_key_at(mask[slot][v], TITLE_ENTER);
        title_count(mask[slot][v], ixs[slot][v], nch);
        data[slot][v] = (struct nuanimcurvedata2_s){ mask[slot][v], ixs[slot][v], (struct nuanimkey2_s *)shown };
        a->curves[v].curvedata = &data[slot][v];
    }
}

bool nw_title_menu_quiet(void)
{
    return nw_profile.title_menu && cutmovie == 0;
}

void nw_title_menu_fixup(struct NUGCUTSCENE_s *def)
{
    struct NUGCUTCHARSYS_s *sys = def ? def->chardef : NULL;
    if (!nw_profile.title_menu || !sys) {
        return;
    }
    menu_hide(def, sys);
    int title = -1;
    for (int i = 0; i < sys->count; i++) {
        const struct NUGCUTCHAR_s *c = &sys->chars[i];
        if (c->name && strcmp(c->name, "title") == 0 && c->character && c->joint_anim && c->char_anim) {
            title = i;
        }
    }
    if (title < 0 || sys->count >= MARK_CHARS) {
        return;
    }
    NW_DEVIATION(title_menu);
    vortex_mtl = NULL; /* a new load */
    struct NUGCUTCHAR_s *t = &sys->chars[title];
    bool flight = t->joint_anim->time > (float)MARK_CUT;
    if (flight) {
        title_flight(t->char_anim, 0);
        struct NUGCUTLOCATORSYS_s *loc = def->locatordef;
        for (int i = 0, slot = 1; loc && i < loc->count; i++) {
            const char *type = loc->types[loc->locators[i].locator_type].name;
            if (type && strcmp(type, MENU_EXHAUST) == 0 && loc->locators[i].anim) {
                title_flight(loc->locators[i].anim, slot++);
            }
        }
    }
    struct NUHGOBJ_s *obj = mark_model();
    struct nuanimdata2_s *motion = mark_motion_of(t->char_anim, flight);
    struct nuanimdata2_s *anim = NULL;
    if (obj && motion && obj->num_joints == MARK_SHARDS + 1) {
        anim = mark_anim(obj, t, flight);
    }
    if (!anim) {
        SDL_Log("title_menu: the NuWorld mark did not load");
        return;
    }
    struct NUGCUTCHAR_s *chars = mark_chars[flight];
    memcpy(chars, sys->chars, (size_t)sys->count * sizeof(*chars));
    struct NUGCUTCHAR_s *mark = &chars[sys->count];
    *mark = *t;
    mark->name = mark_name;
    mark->character = obj;
    mark->char_anim = motion;
    mark->joint_anim = anim;
    mark->face_anim = NULL;
    mark->locators = NULL;
    mark->nlocators = 0;
    mark->first_locator = 0xFF;
    mark->flags &= ~2; /* no per-instance data */
    sys->chars = chars;
    sys->count++;
}

bool nw_title_menu_starting(void)
{
    if (!nw_profile.title_menu || cutmovie != 0 || logos_played != 0 || !CutInst[0]) {
        return false;
    }
    NW_DEVIATION(title_menu);
    nw_nusound_skip(MENU_START / MENU_FPS, (MENU_SOUND - MENU_START) / MENU_FPS, MENU_SOUND_RISE);
    menu_skipping = true;
    return true;
}

void nw_title_menu_started(void)
{
    CutInst[0]->time = MENU_START;
    /* texture animations run the frames the jump skips */
    SetCutMovieRate__retail();
    int frames = CutInst[0]->speed > 0.0f ? (int)(MENU_START / CutInst[0]->speed + 0.5f) : 0;
    for (int i = 0; i < frames; i++) {
        NuTexAnimSetSignals(texanimbits);
        NuTexAnimProcess();
    }
}

void nw_title_menu_rate(void)
{
    if (menu_skipping) {
        menu_skipping = false;
        nw_nusound_skip(0.0f, 0.0f, 0.0f);
    }
}
