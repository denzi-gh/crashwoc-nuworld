/* [prototype] The opening's flyers (src/port/opening/internal.h): Oxide and the space levels' ships crossing the
   title's sky one after another, clear of the earth, Oxide with his exhaust. */
#include "port/opening/internal.h"

struct nudathdr_s;
void *NuDatOpen(char *name, char **mem, s32 *used);
void NuDatSet(struct nudathdr_s *ndh);
void NuDatClose(struct nudathdr_s *ndh);
struct NUJOINTANIM_s;
struct nuanimdata_s;
void NuHGobjEval(struct NUHGOBJ_s *hobj, int njanims, struct NUJOINTANIM_s *janim, struct numtx_s *mtx_array);
void NuHGobjEvalAnim(struct NUHGOBJ_s *hobj, struct nuanimdata_s *data, float time, int njanims,
                     struct NUJOINTANIM_s *janim, struct numtx_s *mtx_array);
int NuHGobjRndrMtxDwa(struct NUHGOBJ_s *hobj, struct numtx_s *wm, int nlayers, short *layer, struct numtx_s *mtx_array,
                      float **dwa);

/* The flyers: models crossing the title's sky one after another, in from off the screen and out past its other side.
   Oxide's saucer is the logos scene's (its "nitros"); the others opening.txt's (flyer_model, from CHARS.DAT), on its
   ways (flyer_way). */
static struct NUHGOBJ_s *nw_flyer_models[SDL_arraysize(nw_lists.model)];
static struct nuanimdata_s *nw_flyer_revolve; /* flyer_anim: the UFOs' one animation in their levels */
static struct nugobj_s *nw_flyer_oxide;

void nw_opening_load_flyers(void)
{
    SDL_memset(nw_flyer_models, 0, sizeof(nw_flyer_models));
    void *dat = NuDatOpen("chars.dat", 0, 0);
    NuDatSet(dat);
    for (int i = 0; i < nw_lists.models; i++) {
        nw_flyer_models[i] = NuGHGRead(&superbuffer_ptr, nw_lists.model[i].path);
    }
    nw_flyer_revolve = nw_lists.anim[0] ? NuAnimDataLoadBuff(nw_lists.anim, &superbuffer_ptr, &superbuffer_end) : NULL;
    NuDatSet(0);
    if (dat) {
        NuDatClose(dat);
    }
    nw_flyer_oxide = NULL;
    struct nugscn_s *l = nw_op.logos_scene;
    for (int k = 0; l && k < l->numspecial; k++) {
        struct nuspecial_s *sp = &l->specials[k];
        if (sp->name && SDL_strcasecmp(sp->name, "nitros") == 0 && sp->instance && sp->instance->objid >= 0 &&
            sp->instance->objid < l->numgobj) {
            nw_flyer_oxide = l->gobjs[sp->instance->objid];
        }
    }
}

/* A flyer's way: its two ends and the point it bows through, its depths times depth. */
static void nw_flyer_way(int way, int pass, float depth, float a[3], float b[3], float c[3])
{
    const float *w = &nw_lists.way[way].x0;
    float bow = nw_opening.flyer_bow[(pass & 1) ? 0 : 1];
    nw_opening_view_point(&nw_op.station_cam, w[0], w[1], w[2] * depth, a);
    nw_opening_view_point(&nw_op.station_cam, w[3], w[4], w[5] * depth, b);
    nw_opening_view_point(&nw_op.station_cam, (w[0] + w[3]) * 0.5f, (w[1] + w[4]) * 0.5f,
                          ((w[2] + w[5]) * 0.5f + bow) * depth, c);
}

/* A quadratic curve a, c, b at u: its point and its direction. */
static void nw_flyer_curve(const float a[3], const float c[3], const float b[3], float u, float p[3], float d[3])
{
    float v = 1 - u;
    for (int i = 0; i < 3; i++) {
        float pi = v * v * a[i] + 2 * v * u * c[i] + u * u * b[i];
        d[i] = 2 * v * (c[i] - a[i]) + 2 * u * (b[i] - c[i]);
        p[i] = pi;
    }
}

/* Whether a way comes into the earth: its clouds' shell and room round it (the flyer's size). */
static bool nw_flyer_hits_earth(const float a[3], const float c[3], const float b[3], float room)
{
    const float *e = nw_opening.earth_c;
    float r = nw_opening.cloud_r[1] + room;
    for (int i = 0; i <= 48; i++) {
        float p[3], d[3];
        nw_flyer_curve(a, c, b, (float)i / 48.0f, p, d);
        float x = p[0] - e[0], y = p[1] - e[1], z = p[2] - e[2];
        if (x * x + y * y + z * z < r * r) {
            return true;
        }
    }
    return false;
}

/* Oxide's pass in the logos (their "nitros", cut frames ox_frames): how long it takes (the cut's 0.6 of a frame each
   60th of a second), and its turn about its way at each moment of it, so the title's Oxide flies as the logos' one
   does. */
#define NW_OX_T0 nw_opening.ox_frames[0]
#define NW_OX_T1 nw_opening.ox_frames[1]
#define NW_OX_SECS ((NW_OX_T1 - NW_OX_T0) / 36.0f)

/* A frame looking along d, level: its rows x, y, z (z along d). */
static void nw_flyer_frame(const float d[3], float f[3][3])
{
    float l = SDL_sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    l = l > 1e-6f ? l : 1;
    for (int i = 0; i < 3; i++) {
        f[2][i] = d[i] / l;
    }
    float x[3] = { f[2][2], 0, -f[2][0] }; /* up (0, 1, 0) across z */
    float lx = SDL_sqrtf(x[0] * x[0] + x[2] * x[2]);
    lx = lx > 1e-6f ? lx : 1;
    for (int i = 0; i < 3; i++) {
        f[0][i] = x[i] / lx;
    }
    f[1][0] = f[2][1] * f[0][2] - f[2][2] * f[0][1];
    f[1][1] = f[2][2] * f[0][0] - f[2][0] * f[0][2];
    f[1][2] = f[2][0] * f[0][1] - f[2][1] * f[0][0];
}

/* Oxide's turn about its way at u of its pass: the logos rigid's rows over the frame of its own heading. false when
   the logos cut is not there. */
static bool nw_flyer_oxide_turn(float u, float rel[3][3])
{
    struct NUGCUTRIGIDSYS_s *rigids = CutScene[0] ? CutScene[0]->rigiddef : NULL;
    struct NUGCUTRIGID_s *ox = NULL;
    for (int i = 0; rigids && i < rigids->count && !ox; i++) {
        if (rigids->rigids[i].name && SDL_strcmp(rigids->rigids[i].name, "nitros") == 0) {
            ox = &rigids->rigids[i];
        }
    }
    if (!ox) {
        return false;
    }
    float t = NW_OX_T0 + (NW_OX_T1 - NW_OX_T0) * u;
    struct numtx_s m, ma, mb;
    NuGCutRigidCalcMtx__retail(ox, t, &m);
    NuGCutRigidCalcMtx__retail(ox, fmaxf(t - 2.0f, NW_OX_T0 - 2.0f), &ma);
    NuGCutRigidCalcMtx__retail(ox, t + 2.0f, &mb);
    float v[3] = { mb._30 - ma._30, mb._31 - ma._31, mb._32 - ma._32 }, f[3][3], r[3][3];
    nw_flyer_frame(v, f);
    for (int i = 0; i < 3; i++) {
        const float *row = &m._00 + 4 * i;
        float l = SDL_sqrtf(row[0] * row[0] + row[1] * row[1] + row[2] * row[2]);
        l = l > 1e-6f ? l : 1;
        for (int j = 0; j < 3; j++) {
            r[i][j] = row[j] / l;
        }
    }
    for (int i = 0; i < 3; i++) { /* rel = r f^T: each row in the heading's frame */
        for (int j = 0; j < 3; j++) {
            rel[i][j] = r[i][0] * f[j][0] + r[i][1] * f[j][1] + r[i][2] * f[j][2];
        }
    }
    return true;
}

/* One flyer at a time: each pass a new way and a new flyer, Oxide every third. */
void nw_opening_draw_flyers(void)
{
    if (nw_opening.flyers == 0 || !nw_op.have_station_cam || nw_lists.models == 0 || nw_lists.ways == 0) {
        return;
    }
    const float secs = nw_opening.flyer_pass, gap = nw_opening.flyer_gap;
    float t = (float)(nw_op.sky_frames - nw_op.passers_from) / 60.0f - nw_opening.flyer_first;
    if (t < 0) {
        return;
    }
    int pass = (int)(t / (secs + gap));
    int way = (pass * 3) % nw_lists.ways;
    int who = pass % 3 == 0 ? -1 : (pass / 3 * 2 + pass % 3) % nw_lists.models;
    if (who < 0 && (!nw_flyer_oxide || cutmovie != 0)) {
        who = pass % nw_lists.models;
    }
    /* Oxide as fast as in the logos: its pass in their time */
    float u = (t - (float)pass * (secs + gap)) / (who < 0 ? NW_OX_SECS : secs);
    if (u > 1) {
        return;
    }
    /* the way: from off one side to off the other, bowed in depth (a quadratic curve through a point nearer or
       farther than its middle), so the flyer wheels round as the levels' UFOs do while its line on the screen
       stays clear of the logo */
    float a[3], b[3], c[3];
    /* Oxide's passes each a little farther (ox_far), round again every third; then every way as near to that as it
       can be with none of it through the earth: farther (behind it, or above or below it) first, else nearer */
    static int ox_count = -1, ox_pass = -1, ox_from = -1;
    if (who < 0 && (pass != ox_pass || nw_op.passers_from != ox_from)) {
        ox_count++; /* every pass of his, through the title's visits: the demo cuts each after one or two */
        ox_pass = pass;
        ox_from = nw_op.passers_from;
    }
    float far = who < 0 ? 1.0f + nw_opening.ox_far * (float)(ox_count % 3) : 1.0f;
    static const float tries[] = { 1.0f, 1.15f, 1.3f, 1.5f, 1.75f, 2.0f, 2.4f, 2.8f, 3.4f, 0.85f, 0.7f, 0.6f };
    bool clear = false;
    for (size_t i = 0; i < SDL_arraysize(tries) && !clear; i++) {
        nw_flyer_way(way, pass, far * tries[i], a, b, c);
        clear = !nw_flyer_hits_earth(a, c, b, 3.0f * nw_opening.flyer_size * 0.6f);
    }
    if (!clear) {
        return;
    }
    float pos[3], d[3];
    nw_flyer_curve(a, c, b, u, pos, d);
    /* MoveZoffaUFO's angles: yaw the way's heading, pitch its climb (up negative), roll the heading's turn rate */
    float yaw = atan2f(d[0], d[2]) * 180.0f / (float)M_PI;
    float pitch = atan2f(-d[1], SDL_sqrtf(d[0] * d[0] + d[2] * d[2])) * 180.0f / (float)M_PI;
    float ua = fmaxf(u - 0.02f, 0.0f), ub = fminf(u + 0.02f, 1.0f), da[3], db[3], dummy[3];
    nw_flyer_curve(a, c, b, ua, dummy, da);
    nw_flyer_curve(a, c, b, ub, dummy, db);
    float turn = atan2f(db[0], db[2]) - atan2f(da[0], da[2]);
    turn = turn > (float)M_PI ? turn - 2 * (float)M_PI : turn < -(float)M_PI ? turn + 2 * (float)M_PI : turn;
    float rate = turn * 180.0f / (float)M_PI / ((ub - ua) * secs); /* degrees a second */
    float roll = fmaxf(fminf(rate * nw_opening.flyer_bank, 40.0f), -40.0f);
    /* DrawZoffa's matrix: the models' nose is their -z */
    struct numtx_s m;
    NuMtxSetRotationZ(&m, (int)(roll * 182.04445f));
    NuMtxRotateX(&m, (int)(-pitch * 182.04445f));
    NuMtxRotateY(&m, (int)((yaw + 180.0f) * 182.04445f));
    float sc = nw_opening.flyer_size * 0.6f * (who < 0 ? 0.6f : 1.0f);
    for (int i = 0; i < 3; i++) {
        (&m._00)[i] *= sc;
        (&m._10)[i] *= sc;
        (&m._20)[i] *= sc;
    }
    m._30 = pos[0];
    m._31 = pos[1];
    m._32 = pos[2];
    float rel[3][3], f[3][3];
    if (who < 0 && nw_flyer_oxide_turn(u, rel)) {
        /* the logos' Oxide's turn about its way, on this way */
        nw_flyer_frame(d, f);
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                (&m._00)[4 * i + j] = (rel[i][0] * f[0][j] + rel[i][1] * f[1][j] + rel[i][2] * f[2][j]) * sc;
            }
        }
    }
    if (who < 0) {
        NuRndrGScnObj(nw_flyer_oxide, &m);
        static int emitted = -1;
        int ox = LookupDebrisEffect("exhaustOX");
        if (nw_opening.ox_shots > 0 && ox > 0 && ox < 128 && debtab[ox] && nw_op.switched && cutmovie == 0 &&
            emitted != nw_op.sky_frames) {
            /* his exhaust from behind his saucer (his model's back, +z), let off backwards, as the logos' locators
               do: INTROR.PTL's "exhaustOX", the logos' "exhaust1" as much larger as he is than the logos' Oxide */
            emitted = nw_op.sky_frames;
            float back = nw_flyer_oxide->max.z;
            struct nuvec_s at = { m._30 + m._20 * back, m._31 + m._21 * back, m._32 + m._22 * back };
            float dir[3] = { m._20, m._21, m._22 };
            short roty = (short)NuAngSub((s32)(NuAtan2(dir[0], dir[2]) * 10430.378f), 0x4000);
            short rotz = (short)(NuAtan2(SDL_sqrtf(dir[0] * dir[0] + dir[2] * dir[2]), -dir[1]) * 10430.378f);
            AddVariableShotDebrisEffect(ox, &at, (int)nw_opening.ox_shots, rotz, roty);
        }
        return;
    }
    struct NUHGOBJ_s *h = nw_flyer_models[who];
    if (!h) {
        return;
    }
    static struct numtx_s joints[256];
    static short layers[32] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
                                16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31 };
    if (nw_flyer_revolve && nw_lists.model[who].anim && nw_flyer_revolve->time > 1) {
        /* the levels' revolve as MyAnimateModelNew plays it: 0.6 of a frame a 50th of a second */
        float f = (float)nw_op.sky_frames * 0.6f * 50.0f / 60.0f;
        NuHGobjEvalAnim(h, nw_flyer_revolve, 1.0f + fmodf(f, nw_flyer_revolve->time - 1.0f), 0, 0, joints);
    } else {
        NuHGobjEval(h, 0, 0, joints);
    }
    NuHGobjRndrMtxDwa(h, &m, h->num_layers < 32 ? h->num_layers : 32, layers, joints, 0);
}
