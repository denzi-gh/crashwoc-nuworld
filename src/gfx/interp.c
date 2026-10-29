#include "gfx/interp.h"

#include <math.h>
#include <string.h>

#include <SDL3/SDL.h>

static uint64_t nw_mix(uint64_t h, uint64_t v)
{
    h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    h ^= h >> 31;
    h *= 0xBF58476D1CE4E5B9ull;
    return h ^ (h >> 29);
}

/* Game thread. */
static struct {
    const void *tag;
    uint64_t obj;
    uint32_t at, n;
    uint32_t views;
    bool snap;
    uint64_t *seen; /* the frame's object bases, open addressing; 0 free */
    uint32_t *count;
    uint32_t cap, used;
} nw_ik;

/* How often base came before in the frame. */
static uint32_t nw_interp_ordinal(uint64_t base)
{
    if (nw_ik.used * 2 >= nw_ik.cap) {
        uint32_t cap = nw_ik.cap ? nw_ik.cap * 2 : 1024;
        uint64_t *seen = SDL_calloc(cap, sizeof(*seen));
        uint32_t *count = SDL_calloc(cap, sizeof(*count));
        if (!seen || !count) {
            SDL_free(seen);
            SDL_free(count);
            return 0;
        }
        for (uint32_t i = 0; i < nw_ik.cap; i++) {
            if (nw_ik.seen[i]) {
                uint32_t h = (uint32_t)nw_ik.seen[i] & (cap - 1);
                while (seen[h]) {
                    h = (h + 1) & (cap - 1);
                }
                seen[h] = nw_ik.seen[i];
                count[h] = nw_ik.count[i];
            }
        }
        SDL_free(nw_ik.seen);
        SDL_free(nw_ik.count);
        nw_ik.seen = seen;
        nw_ik.count = count;
        nw_ik.cap = cap;
    }
    uint32_t h = (uint32_t)base & (nw_ik.cap - 1);
    while (nw_ik.seen[h] && nw_ik.seen[h] != base) {
        h = (h + 1) & (nw_ik.cap - 1);
    }
    if (!nw_ik.seen[h]) {
        nw_ik.seen[h] = base;
        nw_ik.used++;
    }
    return nw_ik.count[h]++;
}

NwInterpScope nw_interp_begin(const void *src, const void *obj)
{
    NwInterpScope was = { nw_ik.obj, nw_ik.at, nw_ik.n };
    uint64_t key;
    if (nw_ik.obj) {
        key = nw_mix(nw_mix(nw_ik.obj, nw_ik.at), (uintptr_t)obj);
    } else {
        uint64_t base = nw_mix(nw_mix(nw_mix(1, (uintptr_t)nw_ik.tag), (uintptr_t)src), (uintptr_t)obj);
        base += !base;
        key = nw_mix(base, nw_interp_ordinal(base));
    }
    nw_ik.obj = key + !key;
    nw_ik.at = 0;
    nw_ik.n = 0;
    return was;
}

void nw_interp_end(NwInterpScope was)
{
    nw_ik.obj = was.obj;
    nw_ik.at = was.at;
    nw_ik.n = was.n;
}

void nw_interp_at(uint32_t a, uint32_t b, uint32_t c)
{
    nw_ik.at = (uint32_t)nw_mix(nw_mix(a, b), c);
    nw_ik.n = 0;
}

uint64_t nw_interp_cmd(NwCmdOp op)
{
    if (!nw_ik.obj || op == NW_CMD_CLEAR || op == NW_CMD_RECT || op == NW_CMD_TARGET || op == NW_CMD_VOLUMES ||
        op == NW_CMD_WAVES) {
        return 0;
    }
    uint64_t key = nw_mix(nw_mix(nw_mix(nw_ik.obj, nw_ik.at), nw_ik.n++), op);
    return key + !key;
}

uint32_t nw_interp_view(void)
{
    return ++nw_ik.views;
}

void nw_interp_publish(NwPacket *p)
{
    p->snap = nw_ik.snap;
    nw_ik.snap = false;
    nw_ik.views = 0;
    if (nw_ik.used) {
        memset(nw_ik.seen, 0, nw_ik.cap * sizeof(*nw_ik.seen));
        memset(nw_ik.count, 0, nw_ik.cap * sizeof(*nw_ik.count));
        nw_ik.used = 0;
    }
}

void nw_interp_key(const void *key)
{
    nw_ik.tag = key;
}

void nw_interp_snap(void)
{
    nw_ik.snap = true;
}

/* Matrices. */
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

/* Render thread. */
#define NW_NONE UINT32_MAX

static struct {
    uint64_t a, b; /* the pair below is of these seqs */
    uint32_t *pair, paircap;   /* b's cmd -> a's, NW_NONE */
    uint32_t *vpair, vpaircap; /* b's view -> a's */
    uint64_t *keys;            /* a's cmd keys, open addressing */
    uint32_t keycap;
    uint32_t *at, atcap;
    NwInterpStats stats;
    NwPacket out;
    NwCmd *cmds;
    uint32_t cmdcap;
    NwView *views;
    uint32_t viewcap;
    NwPtlList *ptllists;
    uint32_t ptllistcap;
} nw_ir;

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

float nw_interp_t(const NwPacket *a, const NwPacket *b, double now)
{
    if (!a || !b || b->edge <= a->edge) {
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

/* b's draws and views with their partners in a. */
static bool nw_interp_pair(const NwPacket *a, const NwPacket *b)
{
    if (nw_ir.a == a->seq && nw_ir.b == b->seq) {
        return true;
    }
    uint32_t cap = 16;
    while (cap < a->ncmds * 2) {
        cap *= 2;
    }
    if (!nw_grow((void **)&nw_ir.pair, &nw_ir.paircap, b->ncmds, sizeof(*nw_ir.pair)) ||
        !nw_grow((void **)&nw_ir.vpair, &nw_ir.vpaircap, b->nviews, sizeof(*nw_ir.vpair)) ||
        !nw_grow((void **)&nw_ir.at, &nw_ir.atcap, cap, sizeof(*nw_ir.at)) ||
        !nw_grow((void **)&nw_ir.keys, &nw_ir.keycap, cap, sizeof(*nw_ir.keys))) {
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
        if (nw_ir.keys[h] && a->cmds[nw_ir.at[h]].op == c->op) {
            nw_ir.pair[i] = nw_ir.at[h];
            nw_ir.stats.paired++;
        } else {
            nw_ir.stats.unpaired++;
        }
    }
    for (uint32_t i = 0; i < b->nviews; i++) {
        nw_ir.vpair[i] = NW_NONE;
        for (uint32_t j = 0; b->views[i].key && j < a->nviews; j++) {
            if (a->views[j].key == b->views[i].key) {
                nw_ir.vpair[i] = j;
                nw_ir.stats.views++;
                break;
            }
        }
    }
    nw_ir.a = a->seq;
    nw_ir.b = b->seq;
    return true;
}

/* A 4x4 row-vector product in double. */
static void nw_mul(double out[16], const double a[16], const double b[16])
{
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            out[i * 4 + j] = a[i * 4] * b[j] + a[i * 4 + 1] * b[4 + j] + a[i * 4 + 2] * b[8 + j] +
                             a[i * 4 + 3] * b[12 + j];
        }
    }
}

/* The inverse of an affine row-vector matrix. */
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

/* b's view moved to the camera between a's and b's by t: vs = inv(cam) x scale x (projection x screen), the last
   part b's, as NuCameraSetEx makes it. */
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

const NwPacket *nw_interp_blend(const NwPacket *a, const NwPacket *b, float t)
{
    if (!a || !b || t >= 1.0f || !nw_interp_pair(a, b) ||
        !nw_grow((void **)&nw_ir.cmds, &nw_ir.cmdcap, b->ncmds, sizeof(*nw_ir.cmds)) ||
        !nw_grow((void **)&nw_ir.views, &nw_ir.viewcap, b->nviews, sizeof(*nw_ir.views)) ||
        !nw_grow((void **)&nw_ir.ptllists, &nw_ir.ptllistcap, b->nptllists, sizeof(*nw_ir.ptllists))) {
        return b;
    }
    nw_ir.out = *b;
    nw_ir.out.cmds = nw_ir.cmds;
    nw_ir.out.views = nw_ir.views;
    nw_ir.out.ptllists = nw_ir.ptllists;
    memcpy(nw_ir.cmds, b->cmds, b->ncmds * sizeof(*b->cmds));
    memcpy(nw_ir.views, b->views, b->nviews * sizeof(*b->views));
    memcpy(nw_ir.ptllists, b->ptllists, b->nptllists * sizeof(*b->ptllists));
    for (uint32_t i = 0; i < b->ncmds; i++) {
        if (nw_ir.pair[i] == NW_NONE) {
            continue;
        }
        const NwCmd *ca = &a->cmds[nw_ir.pair[i]];
        NwCmd *c = &nw_ir.cmds[i];
        nw_interp_mtx(c->local, ca->local, b->cmds[i].local, t);
        if (c->op == NW_CMD_PARTICLES && c->ptllist < b->nptllists && ca->ptllist < a->nptllists) {
            float ta = a->ptllists[ca->ptllist].head[3], tb = b->ptllists[c->ptllist].head[3];
            if (tb >= ta) {
                nw_ir.ptllists[c->ptllist].head[3] = ta + (tb - ta) * t;
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
