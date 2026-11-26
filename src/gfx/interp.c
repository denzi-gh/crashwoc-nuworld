#include "gfx/interp.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

static uint64_t nw_mix(uint64_t h, uint64_t v)
{
    h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    h ^= h >> 31;
    h *= 0xBF58476D1CE4E5B9ull;
    return h ^ (h >> 29);
}

static bool nw_grow(void **items, uint32_t *cap, uint32_t n, size_t size)
{
    if (n <= *cap) {
        return true;
    }
    uint32_t c = *cap ? *cap : 256;
    while (c < n) {
        c *= 2;
    }
    void *grown = SDL_realloc(*items, (size_t)c * size);
    if (!grown) {
        return false;
    }
    *items = grown;
    *cap = c;
    return true;
}

/* An affine matrix as rotation, axis lengths and position. */
typedef struct NwPose {
    float q[4]; /* x, y, z, w */
    float s[3]; /* the axes' lengths, the first negative for a mirror */
    float p[3];
} NwPose;

static bool nw_pose(NwPose *o, const float m[16])
{
    if (m[3] != 0.0f || m[7] != 0.0f || m[11] != 0.0f || m[15] != 1.0f) {
        return false;
    }
    float r[3][3];
    for (int i = 0; i < 3; i++) {
        float len = sqrtf(m[i * 4] * m[i * 4] + m[i * 4 + 1] * m[i * 4 + 1] + m[i * 4 + 2] * m[i * 4 + 2]);
        if (!(len > 1e-12f)) {
            return false;
        }
        o->s[i] = len;
        for (int j = 0; j < 3; j++) {
            r[i][j] = m[i * 4 + j] / len;
        }
        o->p[i] = m[12 + i];
    }
    float det = r[0][0] * (r[1][1] * r[2][2] - r[1][2] * r[2][1]) - r[0][1] * (r[1][0] * r[2][2] - r[1][2] * r[2][0]) +
                r[0][2] * (r[1][0] * r[2][1] - r[1][1] * r[2][0]);
    if (det < 0.0f) {
        o->s[0] = -o->s[0];
        for (int j = 0; j < 3; j++) {
            r[0][j] = -r[0][j];
        }
    }
    for (int i = 0; i < 3; i++) {
        for (int k = i + 1; k < 3; k++) {
            if (fabsf(r[i][0] * r[k][0] + r[i][1] * r[k][1] + r[i][2] * r[k][2]) > 1e-3f) {
                return false; /* sheared */
            }
        }
    }
    /* Row i is where axis i goes: the rotation's column i. */
    float trace = r[0][0] + r[1][1] + r[2][2];
    float *q = o->q;
    if (trace > 0.0f) {
        float s = sqrtf(trace + 1.0f) * 2.0f;
        q[3] = 0.25f * s;
        q[0] = (r[1][2] - r[2][1]) / s;
        q[1] = (r[2][0] - r[0][2]) / s;
        q[2] = (r[0][1] - r[1][0]) / s;
    } else if (r[0][0] > r[1][1] && r[0][0] > r[2][2]) {
        float s = sqrtf(1.0f + r[0][0] - r[1][1] - r[2][2]) * 2.0f;
        q[3] = (r[1][2] - r[2][1]) / s;
        q[0] = 0.25f * s;
        q[1] = (r[1][0] + r[0][1]) / s;
        q[2] = (r[2][0] + r[0][2]) / s;
    } else if (r[1][1] > r[2][2]) {
        float s = sqrtf(1.0f + r[1][1] - r[0][0] - r[2][2]) * 2.0f;
        q[3] = (r[2][0] - r[0][2]) / s;
        q[0] = (r[1][0] + r[0][1]) / s;
        q[1] = 0.25f * s;
        q[2] = (r[2][1] + r[1][2]) / s;
    } else {
        float s = sqrtf(1.0f + r[2][2] - r[0][0] - r[1][1]) * 2.0f;
        q[3] = (r[0][1] - r[1][0]) / s;
        q[0] = (r[2][0] + r[0][2]) / s;
        q[1] = (r[2][1] + r[1][2]) / s;
        q[2] = 0.25f * s;
    }
    return true;
}

static void nw_pose_mtx(float m[16], const NwPose *o)
{
    float x = o->q[0], y = o->q[1], z = o->q[2], w = o->q[3];
    float r[3][3] = {
        { 1.0f - 2.0f * (y * y + z * z), 2.0f * (x * y + z * w), 2.0f * (x * z - y * w) },
        { 2.0f * (x * y - z * w), 1.0f - 2.0f * (x * x + z * z), 2.0f * (y * z + x * w) },
        { 2.0f * (x * z + y * w), 2.0f * (y * z - x * w), 1.0f - 2.0f * (x * x + y * y) },
    };
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            m[i * 4 + j] = r[i][j] * o->s[i];
        }
        m[i * 4 + 3] = 0.0f;
        m[12 + i] = o->p[i];
    }
    m[15] = 1.0f;
}

static void nw_slerp(float out[4], const float a[4], const float b[4], float t)
{
    float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    float sign = d < 0.0f ? -1.0f : 1.0f;
    d *= sign;
    float wa = 1.0f - t, wb = t * sign;
    if (d < 0.9995f) {
        float theta = acosf(d), sn = sinf(theta);
        wa = sinf((1.0f - t) * theta) / sn;
        wb = sinf(t * theta) / sn * sign;
    }
    float len = 0.0f;
    for (int i = 0; i < 4; i++) {
        out[i] = a[i] * wa + b[i] * wb;
        len += out[i] * out[i];
    }
    len = sqrtf(len);
    for (int i = 0; i < 4; i++) {
        out[i] /= len;
    }
}

static float nw_quat_near(const float a[4], const float b[4])
{
    return fabsf(a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]);
}

#define NW_QUARTER_TURN 0.70710678f /* nw_quat_near at 90 degrees */

void nw_interp_mtx(float out[16], const float a[16], const float b[16], float t)
{
    NwPose pa, pb, o;
    if (nw_pose(&pa, a) && nw_pose(&pb, b) && (pa.s[0] < 0.0f) == (pb.s[0] < 0.0f)) {
        nw_slerp(o.q, pa.q, pb.q, t);
        for (int i = 0; i < 3; i++) {
            o.s[i] = pa.s[i] + (pb.s[i] - pa.s[i]) * t;
            o.p[i] = pa.p[i] + (pb.p[i] - pa.p[i]) * t;
        }
        nw_pose_mtx(out, &o);
        return;
    }
    for (int i = 0; i < 16; i++) {
        out[i] = a[i] + (b[i] - a[i]) * t;
    }
}

bool nw_interp_jump(const float a[16], const float b[16])
{
    float dx = b[12] - a[12], dy = b[13] - a[13], dz = b[14] - a[14];
    if (dx * dx + dy * dy + dz * dz > NW_INTERP_JUMP * NW_INTERP_JUMP) {
        return true;
    }
    NwPose pa, pb;
    if (!nw_pose(&pa, a) || !nw_pose(&pb, b)) {
        return false;
    }
    return nw_quat_near(pa.q, pb.q) < NW_QUARTER_TURN;
}

/* Game thread. */
#define NW_ROOT 0x243F6A8885A308D3ull
#define NW_MATCH_MAX 16384u
#define NW_CAMS 16

typedef struct NwObj {
    uint64_t base;
    uint64_t id;
    uint32_t next; /* 1 + the next of its base */
    uint32_t body; /* 1 + its packet body; UINT32_MAX: none could be made */
    uint32_t bones, nbones;
    float wm[16];
    float q[4];
    float vel[3];
    float radius; /* 0 unknown */
    float turn;   /* radians; below 0 unknown */
    bool posed;
    bool fresh;   /* no step before it: vel is unknown */
} NwObj;

typedef struct NwGroup {
    uint64_t base;
    uint32_t first, last; /* 1 + object */
    uint32_t count;
} NwGroup;

typedef struct NwFrame {
    NwObj *objs;
    uint32_t n, cap;
    NwGroup *groups; /* open addressing by base; 0 free */
    uint32_t gcap, gused;
} NwFrame;

typedef struct NwCam {
    float m[16];
    uint32_t key;
} NwCam;

typedef struct NwCand {
    double cost;
    uint32_t b, a;
} NwCand;

static struct {
    const void *tag;
    const void *owner;
    uint64_t place;
    uint64_t obj;   /* 0 outside any scope */
    uint32_t at, n;
    uint32_t scope; /* 1 + object */
    uint16_t joint;
    uint32_t frame;
    bool snap;
    uint64_t serial;
    NwFrame now, was;
    float (*bones)[16];
    int8_t *parents;
    uint32_t nbones, bonecap, parentcap;
    NwCam cams[2][NW_CAMS];
    uint32_t ncams, camkey;
    uint32_t *ai, *bi;
    uint32_t aicap, bicap;
    uint8_t *ta, *tb;
    uint32_t tacap, tbcap;
    NwCand *cands;
    uint32_t candcap;
} nw_ik;

static const NwGroup *nw_group_find(const NwFrame *f, uint64_t base)
{
    if (!f->gused) {
        return NULL;
    }
    uint32_t h = (uint32_t)base & (f->gcap - 1);
    while (f->groups[h].base && f->groups[h].base != base) {
        h = (h + 1) & (f->gcap - 1);
    }
    return f->groups[h].base ? &f->groups[h] : NULL;
}

static NwGroup *nw_group_add(NwFrame *f, uint64_t base)
{
    if (f->gused * 2 >= f->gcap) {
        uint32_t cap = f->gcap ? f->gcap * 2 : 1024;
        NwGroup *groups = SDL_calloc(cap, sizeof(*groups));
        if (!groups) {
            return NULL;
        }
        for (uint32_t i = 0; i < f->gcap; i++) {
            if (f->groups[i].base) {
                uint32_t h = (uint32_t)f->groups[i].base & (cap - 1);
                while (groups[h].base) {
                    h = (h + 1) & (cap - 1);
                }
                groups[h] = f->groups[i];
            }
        }
        SDL_free(f->groups);
        f->groups = groups;
        f->gcap = cap;
    }
    uint32_t h = (uint32_t)base & (f->gcap - 1);
    while (f->groups[h].base && f->groups[h].base != base) {
        h = (h + 1) & (f->gcap - 1);
    }
    if (!f->groups[h].base) {
        f->groups[h].base = base;
        f->gused++;
    }
    return &f->groups[h];
}

static uint32_t nw_interp_object(const void *src, const void *obj, const void *pose, float radius)
{
    NwFrame *f = &nw_ik.now;
    if (!pose || !nw_grow((void **)&f->objs, &f->cap, f->n + 1, sizeof(*f->objs))) {
        return 0;
    }
    NwObj *o = &f->objs[f->n];
    NwPose p;
    float scale = 1.0f;
    memset(o, 0, sizeof(*o));
    memcpy(o->wm, pose, sizeof(o->wm));
    o->posed = nw_pose(&p, o->wm);
    if (o->posed) {
        memcpy(o->q, p.q, sizeof(o->q));
        scale = SDL_max(fabsf(p.s[0]), SDL_max(p.s[1], p.s[2]));
    }
    o->radius = radius * scale;
    o->turn = -1.0f;
    o->fresh = true;
    uint64_t base = nw_ik.owner ? nw_mix(nw_mix(2, (uintptr_t)nw_ik.owner), nw_ik.place)
                                : nw_mix(nw_mix(nw_mix(1, (uintptr_t)nw_ik.tag), (uintptr_t)src), (uintptr_t)obj);
    /* A reflection never pairs with what it mirrors. */
    base = nw_mix(base, o->posed && p.s[0] < 0.0f);
    base += !base;
    NwGroup *g = nw_group_add(f, base);
    if (!g) {
        return 0;
    }
    o->base = base;
    uint32_t at = ++f->n;
    if (g->count++) {
        f->objs[g->last - 1].next = at;
    } else {
        g->first = at;
    }
    g->last = at;
    return at;
}

NwInterpScope nw_interp_begin(const void *src, const void *obj, const void *pose, float radius)
{
    NwInterpScope was = { nw_ik.obj, nw_ik.at, nw_ik.n, nw_ik.joint };
    if (nw_ik.obj) {
        uint64_t key = nw_mix(nw_mix(nw_ik.obj, nw_ik.at), (uintptr_t)obj);
        nw_ik.obj = key + !key;
    } else {
        nw_ik.scope = nw_interp_object(src, obj, pose, radius);
        nw_ik.obj = NW_ROOT;
        nw_ik.joint = NW_JOINT_NONE;
    }
    nw_ik.at = 0;
    nw_ik.n = 0;
    return was;
}

void nw_interp_end(NwInterpScope was)
{
    nw_ik.obj = was.obj;
    nw_ik.at = was.at;
    nw_ik.n = was.n;
    nw_ik.joint = was.joint;
    if (!nw_ik.obj) {
        nw_ik.scope = 0;
    }
}

void nw_interp_at(uint32_t a, uint32_t b, uint32_t c)
{
    nw_ik.at = (uint32_t)nw_mix(nw_mix(a, b), c);
    nw_ik.n = 0;
}

void nw_interp_joints(const void *joints, const void *parents, size_t stride, uint32_t n)
{
    if (nw_ik.obj != NW_ROOT || !nw_ik.scope || !n || n >= NW_JOINT_NONE) {
        return;
    }
    NwObj *o = &nw_ik.now.objs[nw_ik.scope - 1];
    if (o->nbones || !nw_grow((void **)&nw_ik.bones, &nw_ik.bonecap, nw_ik.nbones + n, sizeof(*nw_ik.bones)) ||
        !nw_grow((void **)&nw_ik.parents, &nw_ik.parentcap, nw_ik.nbones + n, sizeof(*nw_ik.parents))) {
        return;
    }
    memcpy(nw_ik.bones[nw_ik.nbones], joints, (size_t)n * sizeof(*nw_ik.bones));
    for (uint32_t i = 0; i < n; i++) {
        nw_ik.parents[nw_ik.nbones + i] = ((const int8_t *)parents)[i * stride];
    }
    o->bones = nw_ik.nbones;
    o->nbones = n;
    nw_ik.nbones += n;
}

void nw_interp_joint(uint32_t joint)
{
    nw_ik.joint = joint < NW_JOINT_NONE ? (uint16_t)joint : NW_JOINT_NONE;
}

void nw_interp_cmd(NwCmd *c)
{
    NwCmdOp op = (NwCmdOp)c->op;
    c->key = 0;
    c->scope = 0;
    if (!nw_ik.obj || !nw_ik.scope || op == NW_CMD_CLEAR || op == NW_CMD_RECT || op == NW_CMD_TARGET ||
        op == NW_CMD_VOLUMES || op == NW_CMD_WAVES) {
        return;
    }
    uint64_t key = nw_mix(nw_mix(nw_mix(nw_ik.obj, nw_ik.at), nw_ik.n++), op);
    c->key = key + !key;
    c->scope = nw_ik.scope;
    c->epoch = (uint16_t)nw_ik.frame;
    c->body = 0;
    c->joint = nw_ik.joint;
}

static uint64_t nw_interp_new(uint64_t base)
{
    uint64_t id = nw_mix(base, ++nw_ik.serial);
    return id + !id;
}

static void nw_interp_fresh(NwObj *b)
{
    b->id = nw_interp_new(b->base);
    b->fresh = true;
    b->turn = -1.0f;
    b->vel[0] = b->vel[1] = b->vel[2] = 0.0f;
}

static bool nw_interp_reach(const NwObj *a, const NwObj *b)
{
    float dx = b->wm[12] - a->wm[12], dy = b->wm[13] - a->wm[13], dz = b->wm[14] - a->wm[14];
    float d2 = dx * dx + dy * dy + dz * dz;
    if (d2 > NW_INTERP_JUMP * NW_INTERP_JUMP) {
        return false;
    }
    if (a->fresh) {
        return true;
    }
    float r = SDL_max(a->radius, b->radius);
    float room = r > 0.0f ? 2.0f * r : NW_INTERP_STEP;
    float v2 = a->vel[0] * a->vel[0] + a->vel[1] * a->vel[1] + a->vel[2] * a->vel[2];
    return d2 <= room * room || d2 <= 16.0f * v2;
}

static void nw_interp_follow(NwObj *b, const NwObj *a)
{
    bool sudden = false;
    for (int k = 0; k < 3; k++) {
        b->vel[k] = b->wm[12 + k] - a->wm[12 + k];
    }
    b->fresh = false;
    b->turn = -1.0f;
    if (a->posed && b->posed) {
        float same = nw_quat_near(a->q, b->q);
        b->turn = 2.0f * acosf(SDL_min(same, 1.0f));
        sudden = same < NW_QUARTER_TURN && !(a->turn >= 0.0f && a->turn * 2.0f > b->turn);
    }
    b->id = sudden ? nw_interp_new(b->base) : a->id;
}

static int nw_cand_cmp(const void *x, const void *y)
{
    const NwCand *a = x, *b = y;
    if (a->cost != b->cost) {
        return a->cost < b->cost ? -1 : 1;
    }
    return a->b != b->b ? (a->b < b->b ? -1 : 1) : (a->a > b->a) - (a->a < b->a);
}

/* Each takes the one nearest to where its last step leads, one-to-one. */
static void nw_interp_match(const NwGroup *gb, const NwGroup *ga)
{
    NwObj *now = nw_ik.now.objs;
    const NwObj *was = nw_ik.was.objs;
    uint32_t nb = gb->count, na = ga->count;
    if (nb == 1 && na == 1) {
        NwObj *b = &now[gb->first - 1];
        const NwObj *a = &was[ga->first - 1];
        if (nw_interp_reach(a, b)) {
            nw_interp_follow(b, a);
        } else {
            nw_interp_fresh(b);
        }
        return;
    }
    uint64_t pairs = (uint64_t)na * nb;
    if (pairs > NW_MATCH_MAX || !nw_grow((void **)&nw_ik.ai, &nw_ik.aicap, na, sizeof(*nw_ik.ai)) ||
        !nw_grow((void **)&nw_ik.bi, &nw_ik.bicap, nb, sizeof(*nw_ik.bi)) ||
        !nw_grow((void **)&nw_ik.ta, &nw_ik.tacap, na, 1) || !nw_grow((void **)&nw_ik.tb, &nw_ik.tbcap, nb, 1) ||
        !nw_grow((void **)&nw_ik.cands, &nw_ik.candcap, (uint32_t)pairs, sizeof(*nw_ik.cands))) {
        /* Too many to weigh: by draw order. */
        uint32_t ia = ga->first;
        for (uint32_t ib = gb->first; ib; ib = now[ib - 1].next) {
            if (ia && nw_interp_reach(&was[ia - 1], &now[ib - 1])) {
                nw_interp_follow(&now[ib - 1], &was[ia - 1]);
            } else {
                nw_interp_fresh(&now[ib - 1]);
            }
            ia = ia ? was[ia - 1].next : 0;
        }
        return;
    }
    uint32_t n = 0;
    for (uint32_t i = ga->first; i; i = was[i - 1].next) {
        nw_ik.ai[n++] = i - 1;
    }
    n = 0;
    for (uint32_t i = gb->first; i; i = now[i - 1].next) {
        nw_ik.bi[n++] = i - 1;
    }
    uint32_t ncands = 0;
    for (uint32_t ib = 0; ib < nb; ib++) {
        const NwObj *b = &now[nw_ik.bi[ib]];
        for (uint32_t ia = 0; ia < na; ia++) {
            const NwObj *a = &was[nw_ik.ai[ia]];
            if (!nw_interp_reach(a, b)) {
                continue;
            }
            double cost = 0.0;
            for (int k = 0; k < 3; k++) {
                double d = (double)b->wm[12 + k] - ((double)a->wm[12 + k] + (double)a->vel[k]);
                cost += d * d;
            }
            /* Ties: the nearer rotation, then draw order. */
            double r = SDL_max(a->radius, b->radius);
            double r2 = r > 0.0 ? r * r : (double)NW_INTERP_STEP * NW_INTERP_STEP;
            double turn = a->posed && b->posed ? 1.0 - (double)nw_quat_near(a->q, b->q) : 0.0;
            double order = ib > ia ? ib - ia : ia - ib;
            nw_ik.cands[ncands++] = (NwCand){ cost + r2 * (1e-4 * turn + 1e-8 * order), ib, ia };
        }
    }
    qsort(nw_ik.cands, ncands, sizeof(*nw_ik.cands), nw_cand_cmp);
    memset(nw_ik.ta, 0, na);
    memset(nw_ik.tb, 0, nb);
    for (uint32_t i = 0; i < ncands; i++) {
        const NwCand *c = &nw_ik.cands[i];
        if (!nw_ik.tb[c->b] && !nw_ik.ta[c->a]) {
            nw_interp_follow(&now[nw_ik.bi[c->b]], &was[nw_ik.ai[c->a]]);
            nw_ik.tb[c->b] = nw_ik.ta[c->a] = 1;
        }
    }
    for (uint32_t ib = 0; ib < nb; ib++) {
        if (!nw_ik.tb[ib]) {
            nw_interp_fresh(&now[nw_ik.bi[ib]]);
        }
    }
}

static void nw_interp_identify(void)
{
    NwFrame *now = &nw_ik.now;
    for (uint32_t h = 0; now->gused && h < now->gcap; h++) {
        const NwGroup *g = &now->groups[h];
        if (!g->base) {
            continue;
        }
        const NwGroup *was = nw_group_find(&nw_ik.was, g->base);
        if (was) {
            nw_interp_match(g, was);
            continue;
        }
        for (uint32_t i = g->first; i; i = now->objs[i - 1].next) {
            nw_interp_fresh(&now->objs[i - 1]);
        }
    }
}

static uint32_t nw_interp_body(NwPacket *p, const NwObj *o)
{
    uint32_t index;
    int8_t *parents;
    NwBody *body = nw_packet_body(p, o->nbones, &index, &parents);
    if (!body) {
        return UINT32_MAX;
    }
    memcpy(body->wm, o->wm, sizeof(body->wm));
    if (o->nbones) {
        memcpy(p->bones[body->first], nw_ik.bones[o->bones], (size_t)o->nbones * sizeof(*p->bones));
        memcpy(parents, nw_ik.parents + o->bones, o->nbones);
    }
    return index + 1;
}

static void nw_interp_cameras(NwPacket *p)
{
    NwCam *now = nw_ik.cams[nw_ik.frame & 1], *was = nw_ik.cams[~nw_ik.frame & 1];
    uint32_t n = 0;
    bool taken[NW_CAMS] = { false };
    for (uint32_t i = 0; i < p->nviews; i++) {
        NwView *v = &p->views[i];
        uint32_t k = 0;
        while (k < n && memcmp(now[k].m, v->cam, sizeof(v->cam)) != 0) {
            k++;
        }
        if (k == NW_CAMS) {
            v->key = 0;
            continue;
        }
        if (k == n) {
            memcpy(now[n].m, v->cam, sizeof(v->cam));
            now[n++].key = 0;
        }
        v->key = k + 1;
    }
    for (;;) {
        float best = 0.0f;
        uint32_t bk = NW_CAMS, bj = 0;
        for (uint32_t k = 0; k < n; k++) {
            for (uint32_t j = 0; !now[k].key && j < nw_ik.ncams; j++) {
                if (taken[j] || nw_interp_jump(was[j].m, now[k].m)) {
                    continue;
                }
                NwPose a, b;
                float dx = now[k].m[12] - was[j].m[12], dy = now[k].m[13] - was[j].m[13],
                      dz = now[k].m[14] - was[j].m[14];
                float cost = dx * dx + dy * dy + dz * dz;
                if (nw_pose(&a, was[j].m) && nw_pose(&b, now[k].m)) {
                    cost += 100.0f * (1.0f - nw_quat_near(a.q, b.q));
                }
                if (bk == NW_CAMS || cost < best) {
                    best = cost;
                    bk = k;
                    bj = j;
                }
            }
        }
        if (bk == NW_CAMS) {
            break;
        }
        now[bk].key = was[bj].key;
        taken[bj] = true;
    }
    for (uint32_t k = 0; k < n; k++) {
        if (!now[k].key) {
            if (!++nw_ik.camkey) {
                nw_ik.camkey = 1;
            }
            now[k].key = nw_ik.camkey;
        }
    }
    for (uint32_t i = 0; i < p->nviews; i++) {
        if (p->views[i].key) {
            p->views[i].key = now[p->views[i].key - 1].key;
        }
    }
    nw_ik.ncams = n;
}

void nw_interp_publish(NwPacket *p)
{
    NwFrame *now = &nw_ik.now;
    p->snap = nw_ik.snap;
    nw_ik.snap = false;
    nw_interp_identify();
    for (uint32_t i = 0; i < p->ncmds; i++) {
        NwCmd *c = &p->cmds[i];
        if (!c->key) {
            continue;
        }
        /* A record an unflushed stream kept from the frame before. */
        if (c->epoch != (uint16_t)nw_ik.frame || !c->scope || c->scope > now->n) {
            c->key = 0;
            continue;
        }
        NwObj *o = &now->objs[c->scope - 1];
        if (!o->body) {
            o->body = nw_interp_body(p, o);
        }
        c->body = o->body == UINT32_MAX ? 0 : o->body;
        if (!c->body || c->joint >= o->nbones) {
            c->joint = NW_JOINT_NONE;
        }
        uint64_t key = nw_mix(o->id, c->key);
        c->key = key + !key;
    }
    nw_interp_cameras(p);
    NwFrame t = nw_ik.was;
    nw_ik.was = *now;
    *now = t;
    now->n = 0;
    if (now->gused) {
        memset(now->groups, 0, now->gcap * sizeof(*now->groups));
        now->gused = 0;
    }
    nw_ik.nbones = 0;
    nw_ik.frame++;
}

void nw_interp_key(const void *key)
{
    nw_ik.tag = key;
}

void nw_interp_slot(const void *owner, unsigned long long place)
{
    nw_ik.owner = owner;
    nw_ik.place = place;
}

uint32_t nw_interp_frame(void)
{
    return nw_ik.frame;
}

void nw_interp_snap(void)
{
    nw_ik.snap = true;
}

/* Render thread. */
#define NW_NONE UINT32_MAX
#define NW_FAIL (UINT32_MAX - 1)

enum {
    NW_LOCAL_KEPT,
    NW_LOCAL_MTX,    /* no body: blended by itself */
    NW_LOCAL_ROOT,   /* its body's matrix, or its joint's times it */
    NW_LOCAL_OFFSET, /* nw_ir.rel times that */
};

enum {
    NW_BONE_KEPT,
    NW_BONE_BLEND,
    NW_BONE_TURN, /* b's rotation and size at once; its position still blends */
};

typedef struct NwBone {
    NwPose a, b;
    float kept[16];
    float bind[16]; /* the inverse bind: b's palette over b's joint */
    uint8_t mode;
    bool root;      /* no parent, or one with no inverse */
    bool bound;
} NwBone;

typedef struct NwBodyPair {
    uint32_t a; /* NW_NONE not looked at yet, NW_FAIL another skeleton */
    bool still;
    bool bound;
    float wm[16];
} NwBodyPair;

typedef struct NwPalette {
    uint32_t body, first;
} NwPalette;

static struct {
    uint64_t a, b; /* the pair below is of these seqs */
    uint32_t *pair, paircap;   /* b's cmd -> a's, NW_NONE */
    uint32_t *vpair, vpaircap; /* b's view -> a's */
    uint64_t *keys;            /* a's cmd keys, open addressing */
    uint32_t keycap;
    uint32_t *at, atcap;
    uint8_t *how;
    uint32_t howcap;
    float (*rel)[16];          /* b's cmd over its root */
    uint32_t relcap;
    NwBodyPair *bodies;
    uint32_t bodycap;
    NwBone *bones;
    uint32_t bonecap;
    float (*jm)[16], (*jw)[16]; /* the blended bones: in the model, in the world */
    uint32_t jmcap, jwcap;
    NwPalette *pals;
    uint32_t npals, palcap;
    uint8_t *palseen;
    uint32_t palseencap;
    NwInterpStats stats;
    NwPacket out;
    NwCmd *cmds;
    uint32_t cmdcap;
    NwView *views;
    uint32_t viewcap;
    NwPtlList *ptllists;
    uint32_t ptllistcap;
    float (*joints)[16];
    uint32_t jointcap;
    float *weights;
    uint32_t weightcap;
    uint32_t *mpair, mpaircap; /* b's material -> a's */
    NwMtlSnap *mtls;
    uint32_t mtlcap;
} nw_ir;

float nw_interp_t(const NwPacket *a, const NwPacket *b, double now)
{
    if (!a || !b || b->snap || b->edge <= a->edge || b->edge - a->edge > NW_INTERP_GAP) {
        return 1.0f;
    }
    double t = (now - (double)b->edge) / (double)(b->edge - a->edge);
    return t <= 0.0 ? 0.0f : t >= 1.0 ? 1.0f : (float)t;
}

bool nw_interp_redraws(const NwPacket *p)
{
    int32_t target = NW_SURF_BACK;
    for (uint32_t i = 0; i < p->ncmds; i++) {
        const NwCmd *c = &p->cmds[i];
        if (c->op == NW_CMD_TARGET) {
            target = c->target.surf.addr;
        } else if (target != NW_SURF_BACK || c->op == NW_CMD_WAVES) {
            continue;
        } else if (c->op == NW_CMD_CLEAR) {
            if (c->clear.mask & 1) {
                return true;
            }
        } else {
            return false;
        }
    }
    return false;
}

static void nw_mul(double out[16], const double a[16], const double b[16])
{
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            out[i * 4 + j] = a[i * 4] * b[j] + a[i * 4 + 1] * b[4 + j] + a[i * 4 + 2] * b[8 + j] +
                             a[i * 4 + 3] * b[12 + j];
        }
    }
}

static void nw_mulf(float out[16], const float a[16], const float b[16])
{
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            out[i * 4 + j] = a[i * 4] * b[j] + a[i * 4 + 1] * b[4 + j] + a[i * 4 + 2] * b[8 + j] +
                             a[i * 4 + 3] * b[12 + j];
        }
    }
}

static bool nw_affine_inv(double out[16], const double m[16])
{
    double c00 = m[5] * m[10] - m[6] * m[9], c01 = m[6] * m[8] - m[4] * m[10], c02 = m[4] * m[9] - m[5] * m[8];
    double det = m[0] * c00 + m[1] * c01 + m[2] * c02;
    if (fabs(det) < 1e-20) {
        return false;
    }
    double r = 1.0 / det;
    out[0] = c00 * r;
    out[1] = (m[2] * m[9] - m[1] * m[10]) * r;
    out[2] = (m[1] * m[6] - m[2] * m[5]) * r;
    out[4] = c01 * r;
    out[5] = (m[0] * m[10] - m[2] * m[8]) * r;
    out[6] = (m[2] * m[4] - m[0] * m[6]) * r;
    out[8] = c02 * r;
    out[9] = (m[1] * m[8] - m[0] * m[9]) * r;
    out[10] = (m[0] * m[5] - m[1] * m[4]) * r;
    for (int j = 0; j < 3; j++) {
        out[12 + j] = -(m[12] * out[j] + m[13] * out[4 + j] + m[14] * out[8 + j]);
        out[j * 4 + 3] = 0.0;
    }
    out[15] = 1.0;
    return true;
}

/* m times the inverse of over. */
static bool nw_within(float out[16], const float m[16], const double over[16])
{
    double inv[16], md[16], rel[16];
    if (!nw_affine_inv(inv, over)) {
        return false;
    }
    for (int i = 0; i < 16; i++) {
        md[i] = m[i];
    }
    nw_mul(rel, md, inv);
    for (int i = 0; i < 16; i++) {
        out[i] = (float)rel[i];
    }
    return true;
}

static bool nw_withinf(float out[16], const float m[16], const float over[16])
{
    double od[16];
    for (int i = 0; i < 16; i++) {
        od[i] = over[i];
    }
    return nw_within(out, m, od);
}

static void nw_interp_bone(NwBone *bone, const float (*ja)[16], const float (*jb)[16], uint32_t j, int up)
{
    float la[16], lb[16];
    bone->root = !(up >= 0 && (uint32_t)up < j && nw_withinf(la, ja[j], ja[up]) && nw_withinf(lb, jb[j], jb[up]));
    if (bone->root) {
        memcpy(la, ja[j], sizeof(la));
        memcpy(lb, jb[j], sizeof(lb));
    }
    bone->bound = false;
    bone->mode = NW_BONE_KEPT;
    memcpy(bone->kept, lb, sizeof(bone->kept));
    if (memcmp(la, lb, sizeof(la)) == 0 || !nw_pose(&bone->a, la) || !nw_pose(&bone->b, lb) ||
        (bone->a.s[0] < 0.0f) != (bone->b.s[0] < 0.0f)) {
        return;
    }
    bone->mode = nw_quat_near(bone->a.q, bone->b.q) < NW_QUARTER_TURN ? NW_BONE_TURN : NW_BONE_BLEND;
    for (int k = 0; k < 3; k++) {
        float grown = bone->b.s[k] / bone->a.s[k];
        if (grown > 2.0f || grown < 0.5f) {
            bone->mode = NW_BONE_TURN; /* a part shown or hidden by its size */
        }
    }
}

static void nw_interp_body_pair(const NwPacket *a, const NwPacket *b, NwBodyPair *bp, uint32_t ia, uint32_t ib)
{
    const NwBody *ba = &a->bodies[ia], *bb = &b->bodies[ib];
    bp->a = NW_FAIL;
    bp->bound = false;
    if (ba->count != bb->count || (uint64_t)ba->first + ba->count > a->nbones ||
        (uint64_t)bb->first + bb->count > b->nbones ||
        (bb->count && memcmp(a->parents + ba->first, b->parents + bb->first, bb->count) != 0)) {
        return;
    }
    bp->a = ia;
    bp->still = memcmp(ba->wm, bb->wm, sizeof(bb->wm)) == 0 &&
                (!bb->count ||
                 memcmp(a->bones[ba->first], b->bones[bb->first], (size_t)bb->count * sizeof(*b->bones)) == 0);
    for (uint32_t j = 0; !bp->still && j < bb->count; j++) {
        nw_interp_bone(&nw_ir.bones[bb->first + j], &a->bones[ba->first], &b->bones[bb->first], j,
                       b->parents[bb->first + j]);
    }
}

static void nw_interp_bodies(const NwPacket *a, const NwPacket *b)
{
    nw_ir.npals = 0;
    for (uint32_t i = 0; i < b->nbodies; i++) {
        nw_ir.bodies[i].a = NW_NONE;
    }
    if (b->njoints) {
        memset(nw_ir.palseen, 0, b->njoints);
    }
    for (uint32_t i = 0; i < b->ncmds; i++) {
        nw_ir.how[i] = NW_LOCAL_MTX;
        if (nw_ir.pair[i] == NW_NONE) {
            continue;
        }
        const NwCmd *c = &b->cmds[i], *ca = &a->cmds[nw_ir.pair[i]];
        if (!c->body || c->body > b->nbodies || !ca->body || ca->body > a->nbodies) {
            continue;
        }
        NwBodyPair *bp = &nw_ir.bodies[c->body - 1];
        const NwBody *bb = &b->bodies[c->body - 1];
        if (bp->a == NW_NONE) {
            nw_interp_body_pair(a, b, bp, ca->body - 1, c->body - 1);
        }
        if (bp->a != ca->body - 1) {
            continue;
        }
        nw_ir.how[i] = NW_LOCAL_KEPT;
        if (bp->still) {
            continue;
        }
        double root[16];
        for (int k = 0; k < 16; k++) {
            root[k] = bb->wm[k];
        }
        if (c->joint < bb->count) {
            double jm[16], wm[16];
            for (int k = 0; k < 16; k++) {
                jm[k] = b->bones[bb->first + c->joint][k];
                wm[k] = bb->wm[k];
            }
            nw_mul(root, jm, wm);
        }
        float *rel = nw_ir.rel[i];
        if (!nw_within(rel, c->local, root)) {
            continue;
        }
        nw_ir.how[i] = NW_LOCAL_ROOT;
        for (int k = 0; k < 16; k++) {
            if (fabsf(rel[k] - (k % 5 == 0 ? 1.0f : 0.0f)) > 1e-5f) {
                nw_ir.how[i] = NW_LOCAL_OFFSET;
            }
        }
        if (c->op != NW_CMD_SKIN || !bb->count || c->count != bb->count ||
            (uint64_t)c->first + c->count > b->njoints || nw_ir.palseen[c->first] ||
            !nw_grow((void **)&nw_ir.pals, &nw_ir.palcap, nw_ir.npals + 1, sizeof(*nw_ir.pals))) {
            continue;
        }
        nw_ir.palseen[c->first] = 1;
        nw_ir.pals[nw_ir.npals++] = (NwPalette){ c->body - 1, c->first };
        for (uint32_t j = 0; !bp->bound && j < bb->count; j++) {
            NwBone *bone = &nw_ir.bones[bb->first + j];
            bone->bound = nw_withinf(bone->bind, b->joints[c->first + j], b->bones[bb->first + j]);
        }
        bp->bound = true;
    }
}

static bool nw_interp_pair(const NwPacket *a, const NwPacket *b)
{
    if (nw_ir.a == a->seq && nw_ir.b == b->seq) {
        return true;
    }
    uint32_t cap = 16;
    while (cap < a->ncmds * 2 || cap < a->nmtls * 2) {
        cap *= 2;
    }
    if (!nw_grow((void **)&nw_ir.pair, &nw_ir.paircap, b->ncmds, sizeof(*nw_ir.pair)) ||
        !nw_grow((void **)&nw_ir.vpair, &nw_ir.vpaircap, b->nviews, sizeof(*nw_ir.vpair)) ||
        !nw_grow((void **)&nw_ir.mpair, &nw_ir.mpaircap, b->nmtls, sizeof(*nw_ir.mpair)) ||
        !nw_grow((void **)&nw_ir.at, &nw_ir.atcap, cap, sizeof(*nw_ir.at)) ||
        !nw_grow((void **)&nw_ir.keys, &nw_ir.keycap, cap, sizeof(*nw_ir.keys)) ||
        !nw_grow((void **)&nw_ir.how, &nw_ir.howcap, b->ncmds, sizeof(*nw_ir.how)) ||
        !nw_grow((void **)&nw_ir.rel, &nw_ir.relcap, b->ncmds, sizeof(*nw_ir.rel)) ||
        !nw_grow((void **)&nw_ir.bodies, &nw_ir.bodycap, b->nbodies, sizeof(*nw_ir.bodies)) ||
        !nw_grow((void **)&nw_ir.bones, &nw_ir.bonecap, b->nbones, sizeof(*nw_ir.bones)) ||
        !nw_grow((void **)&nw_ir.jm, &nw_ir.jmcap, b->nbones, sizeof(*nw_ir.jm)) ||
        !nw_grow((void **)&nw_ir.jw, &nw_ir.jwcap, b->nbones, sizeof(*nw_ir.jw)) ||
        !nw_grow((void **)&nw_ir.palseen, &nw_ir.palseencap, b->njoints, sizeof(*nw_ir.palseen))) {
        return false;
    }
    memset(nw_ir.keys, 0, cap * sizeof(*nw_ir.keys));
    for (uint32_t i = 0; i < a->ncmds; i++) {
        uint64_t k = a->cmds[i].key;
        if (!k) {
            continue;
        }
        uint32_t h = (uint32_t)k & (cap - 1);
        while (nw_ir.keys[h] && nw_ir.keys[h] != k) {
            h = (h + 1) & (cap - 1);
        }
        if (!nw_ir.keys[h]) { /* a key twice keeps its first */
            nw_ir.keys[h] = k;
            nw_ir.at[h] = i;
        }
    }
    nw_ir.stats = (NwInterpStats){ 0 };
    for (uint32_t i = 0; i < b->ncmds; i++) {
        const NwCmd *c = &b->cmds[i];
        nw_ir.pair[i] = NW_NONE;
        if (!c->key) {
            continue;
        }
        uint32_t h = (uint32_t)c->key & (cap - 1);
        while (nw_ir.keys[h] && nw_ir.keys[h] != c->key) {
            h = (h + 1) & (cap - 1);
        }
        const NwCmd *ca = nw_ir.keys[h] ? &a->cmds[nw_ir.at[h]] : NULL;
        /* An effect whose time ran back began again. */
        if (!ca || ca->op != c->op ||
            (c->op == NW_CMD_PARTICLES && c->ptllist < b->nptllists && ca->ptllist < a->nptllists &&
             b->ptllists[c->ptllist].head[3] < a->ptllists[ca->ptllist].head[3])) {
            nw_ir.stats.unpaired++;
        } else {
            nw_ir.pair[i] = nw_ir.at[h];
            nw_ir.stats.paired++;
        }
    }
    nw_interp_bodies(a, b);
    for (uint32_t i = 0; i < b->nviews; i++) {
        nw_ir.vpair[i] = NW_NONE;
        for (uint32_t j = 0; b->views[i].key && j < a->nviews; j++) {
            if (a->views[j].key == b->views[i].key) {
                if (nw_interp_jump(a->views[j].cam, b->views[i].cam)) {
                    nw_ir.stats.snapped++;
                } else {
                    nw_ir.vpair[i] = j;
                    nw_ir.stats.views++;
                }
                break;
            }
        }
    }
    memset(nw_ir.keys, 0, cap * sizeof(*nw_ir.keys));
    for (uint32_t i = 0; i < a->nmtls; i++) {
        uint64_t k = (uintptr_t)a->mtls[i].key;
        uint32_t h = (uint32_t)nw_mix(a->mtls[i].uv.binding, k) & (cap - 1);
        while (nw_ir.keys[h]) {
            h = (h + 1) & (cap - 1);
        }
        nw_ir.keys[h] = k;
        nw_ir.at[h] = i;
    }
    for (uint32_t i = 0; i < b->nmtls; i++) {
        uint64_t k = (uintptr_t)b->mtls[i].key;
        uint32_t h = (uint32_t)nw_mix(b->mtls[i].uv.binding, k) & (cap - 1);
        while (nw_ir.keys[h]) {
            const NwMtlSnap *s = &a->mtls[nw_ir.at[h]];
            if (nw_ir.keys[h] == k && s->tex == b->mtls[i].tex && s->layer == b->mtls[i].layer &&
                s->uv.binding == b->mtls[i].uv.binding) break;
            h = (h + 1) & (cap - 1);
        }
        /* A texture animation's swap shows at once. */
        nw_ir.mpair[i] = k && nw_ir.keys[h] ? nw_ir.at[h] : NW_NONE;
    }
    nw_ir.a = a->seq;
    nw_ir.b = b->seq;
    return true;
}

/* vs = inv(cam) x scale x (projection x screen), the last part b's, as NuCameraSetEx makes it. */
static void nw_interp_view_blend(NwView *out, const NwView *a, const NwView *b, float t)
{
    double cam[16], inv[16], ps[16], vs[16], tmp[16];
    for (int i = 0; i < 16; i++) {
        cam[i] = b->cam[i];
        vs[i] = b->vs[i];
    }
    for (int i = 0; i < 3; i++) {
        if (b->camscale[i] == 0.0f) {
            return;
        }
        for (int j = 0; j < 4; j++) {
            cam[i * 4 + j] /= b->camscale[i];
        }
    }
    nw_mul(ps, cam, vs);
    float blend[16];
    nw_interp_mtx(blend, a->cam, b->cam, t);
    for (int i = 0; i < 16; i++) {
        cam[i] = blend[i];
    }
    if (!nw_affine_inv(inv, cam)) {
        return;
    }
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 3; j++) {
            inv[i * 4 + j] *= a->camscale[j] + (b->camscale[j] - a->camscale[j]) * t;
        }
    }
    nw_mul(tmp, inv, ps);
    for (int i = 0; i < 16; i++) {
        out->vs[i] = (float)tmp[i];
    }
    float ya = atan2f(-a->faceon[2], a->faceon[0]), yb = atan2f(-b->faceon[2], b->faceon[0]);
    float d = yb - ya;
    d -= 2.0f * (float)M_PI * floorf((d + (float)M_PI) / (2.0f * (float)M_PI));
    float y = ya + d * t;
    out->faceon[0] = cosf(y);
    out->faceon[2] = -sinf(y);
}

/* NuMtlAnimate's UV offset; a scrolling one blends the short way round 1. */
static void nw_interp_uv(NwMtlSnap *out, const NwMtlSnap *a, const NwMtlSnap *b, float t)
{
    if (b->uv.binding) for (int axis=0; axis<3; axis++) {
        if (!(b->uv.linear & a->uv.linear & (1u<<axis)) || b->uv.segment[axis]<0 ||
            a->uv.segment[axis]!=b->uv.segment[axis] || a->uv.owner[axis]!=b->uv.owner[axis] ||
            a->uv.cycle[axis]!=b->uv.cycle[axis]) continue;
        for (int j=axis*2; j<axis*2+(axis==2 ? 1 : 2); j++)
            out->uv.value[j]=a->uv.value[j]+(b->uv.value[j]-a->uv.value[j])*t;
    }
    for (int i = 0; i < 2; i++) {
        if (a->vu[i] == b->vu[i] && a->row[i] == b->row[i]) {
            continue;
        }
        float d = b->vu[i] - a->vu[i];
        if (out->wrap & (1 << i)) {
            d -= floorf(d + 0.5f);
        }
        float uv = a->vu[i] + d * t;
        if (out->wrap & (1 << i)) {
            uv -= floorf(uv);
        }
        out->vu[i] = uv;
        out->row[i] = (int32_t)(uv * 4096.0f);
    }
}

/* Each joint over its blended parent, so it turns about where it hangs. */
static void nw_interp_skeleton(const NwPacket *b, const NwBody *bb, const NwBodyPair *bp, float t)
{
    for (uint32_t j = 0; j < bb->count; j++) {
        const NwBone *bone = &nw_ir.bones[bb->first + j];
        float local[16];
        if (bone->mode == NW_BONE_KEPT) {
            memcpy(local, bone->kept, sizeof(local));
        } else {
            NwPose o = bone->b;
            if (bone->mode == NW_BONE_BLEND) {
                nw_slerp(o.q, bone->a.q, bone->b.q, t);
            }
            for (int k = 0; k < 3; k++) {
                if (bone->mode == NW_BONE_BLEND) {
                    o.s[k] = bone->a.s[k] + (bone->b.s[k] - bone->a.s[k]) * t;
                }
                o.p[k] = bone->a.p[k] + (bone->b.p[k] - bone->a.p[k]) * t;
            }
            nw_pose_mtx(local, &o);
        }
        float *jm = nw_ir.jm[bb->first + j];
        if (bone->root) {
            memcpy(jm, local, sizeof(local));
        } else {
            nw_mulf(jm, local, nw_ir.jm[bb->first + b->parents[bb->first + j]]);
        }
        nw_mulf(nw_ir.jw[bb->first + j], jm, bp->wm);
    }
}

/* memcpy that takes an empty array's NULL. */
static void nw_copy(void *dst, const void *src, size_t size)
{
    if (size) {
        memcpy(dst, src, size);
    }
}

const NwPacket *nw_interp_blend(const NwPacket *a, const NwPacket *b, float t)
{
    if (!a || !b || t >= 1.0f || !nw_interp_pair(a, b) ||
        !nw_grow((void **)&nw_ir.cmds, &nw_ir.cmdcap, b->ncmds, sizeof(*nw_ir.cmds)) ||
        !nw_grow((void **)&nw_ir.views, &nw_ir.viewcap, b->nviews, sizeof(*nw_ir.views)) ||
        !nw_grow((void **)&nw_ir.ptllists, &nw_ir.ptllistcap, b->nptllists, sizeof(*nw_ir.ptllists)) ||
        !nw_grow((void **)&nw_ir.joints, &nw_ir.jointcap, b->njoints, sizeof(*nw_ir.joints)) ||
        !nw_grow((void **)&nw_ir.weights, &nw_ir.weightcap, b->nweights, sizeof(*nw_ir.weights)) ||
        !nw_grow((void **)&nw_ir.mtls, &nw_ir.mtlcap, b->nmtls, sizeof(*nw_ir.mtls))) {
        return b;
    }
    nw_ir.out = *b;
    nw_ir.out.cmds = nw_ir.cmds;
    nw_ir.out.views = nw_ir.views;
    nw_ir.out.ptllists = nw_ir.ptllists;
    nw_ir.out.joints = nw_ir.joints;
    nw_ir.out.weights = nw_ir.weights;
    nw_ir.out.mtls = nw_ir.mtls;
    nw_copy(nw_ir.cmds, b->cmds, b->ncmds * sizeof(*b->cmds));
    nw_copy(nw_ir.views, b->views, b->nviews * sizeof(*b->views));
    nw_copy(nw_ir.ptllists, b->ptllists, b->nptllists * sizeof(*b->ptllists));
    nw_copy(nw_ir.joints, b->joints, b->njoints * sizeof(*b->joints));
    nw_copy(nw_ir.weights, b->weights, b->nweights * sizeof(*b->weights));
    nw_copy(nw_ir.mtls, b->mtls, b->nmtls * sizeof(*b->mtls));
    for (uint32_t i = 0; i < b->nmtls; i++) {
        if (nw_ir.mpair[i] != NW_NONE) {
            nw_interp_uv(&nw_ir.mtls[i], &a->mtls[nw_ir.mpair[i]], &b->mtls[i], t);
        }
    }
    for (uint32_t i = 0; i < b->nbodies; i++) {
        NwBodyPair *bp = &nw_ir.bodies[i];
        if (bp->a >= NW_FAIL || bp->still) {
            continue;
        }
        nw_interp_mtx(bp->wm, a->bodies[bp->a].wm, b->bodies[i].wm, t);
        nw_interp_skeleton(b, &b->bodies[i], bp, t);
    }
    for (uint32_t i = 0; i < b->ncmds; i++) {
        if (nw_ir.pair[i] == NW_NONE) {
            continue;
        }
        const NwCmd *ca = &a->cmds[nw_ir.pair[i]];
        NwCmd *c = &nw_ir.cmds[i];
        if (nw_ir.how[i] == NW_LOCAL_MTX) {
            nw_interp_mtx(c->local, ca->local, b->cmds[i].local, t);
        } else if (nw_ir.how[i] != NW_LOCAL_KEPT) {
            const NwBody *bb = &b->bodies[c->body - 1];
            const float *root = c->joint < bb->count ? nw_ir.jw[bb->first + c->joint] : nw_ir.bodies[c->body - 1].wm;
            if (nw_ir.how[i] == NW_LOCAL_OFFSET) {
                nw_mulf(c->local, nw_ir.rel[i], root);
            } else {
                memcpy(c->local, root, sizeof(c->local));
            }
        }
        if (c->op == NW_CMD_PARTICLES && c->ptllist < b->nptllists && ca->ptllist < a->nptllists) {
            float ta = a->ptllists[ca->ptllist].head[3], tb = b->ptllists[c->ptllist].head[3];
            nw_ir.ptllists[c->ptllist].head[3] = ta + (tb - ta) * t;
        }
        if (c->op == NW_CMD_SKIN && c->nweights && c->nweights == ca->nweights &&
            (uint64_t)c->weights + c->nweights <= b->nweights && (uint64_t)ca->weights + ca->nweights <= a->nweights) {
            for (uint32_t w = 0; w < c->nweights; w++) {
                float wa = a->weights[ca->weights + w], wb = b->weights[c->weights + w];
                nw_ir.weights[c->weights + w] = wa + (wb - wa) * t;
            }
        }
    }
    for (uint32_t i = 0; i < nw_ir.npals; i++) {
        const NwBody *bb = &b->bodies[nw_ir.pals[i].body];
        for (uint32_t j = 0; j < bb->count; j++) {
            const NwBone *bone = &nw_ir.bones[bb->first + j];
            if (bone->bound) {
                nw_mulf(nw_ir.joints[nw_ir.pals[i].first + j], bone->bind, nw_ir.jm[bb->first + j]);
            }
        }
    }
    for (uint32_t i = 0; i < b->nviews; i++) {
        if (nw_ir.vpair[i] != NW_NONE) {
            nw_interp_view_blend(&nw_ir.views[i], &a->views[nw_ir.vpair[i]], &b->views[i], t);
        }
    }
    return &nw_ir.out;
}

NwInterpStats nw_interp_stats(void)
{
    return nw_ir.stats;
}
