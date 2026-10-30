/* The fx2 microcode's shadow volumes (ShadCyl, ShadElp, ShadPol at MSCAL 0x300, 0x302, 0x304) on the CPU, for
   nurndr's port/units.toml overrides; quads fx2 tints +1 or -1 count into the stencil as VERTS records instead. */
#include <math.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "gfx/rndr.h"
#include "nu3d/nucamera.h"
#include "nu3d/nuhgobj.h"
#include "numath/numtx.h"

extern struct rndrstream_s *rndrstream_fx;
extern struct nucamera_s global_camera;
extern struct nuvec_s lightfan[9];
extern float NuTrigTable[];

/* nurndr.c's; a shadow mesh's normals and points: the face's normal and its count of points, the points (the first
   one's count the faces'). */
struct nurndrshadface_s {
    struct nuvec_s v;
    int count;
};

#define NW_FX2_POINTS 16

typedef struct NwFx2Tris {
    NwVuSpriteVertex *v;
    uint32_t n, cap;
} NwFx2Tris;

static NwFx2Tris nw_fx2_tris[2]; /* the triangles counting +1 and -1 */

static struct nuvec_s nw_fx2_add(struct nuvec_s a, struct nuvec_s b)
{
    return (struct nuvec_s){ a.x + b.x, a.y + b.y, a.z + b.z };
}

static struct nuvec_s nw_fx2_sub(struct nuvec_s a, struct nuvec_s b)
{
    return (struct nuvec_s){ a.x - b.x, a.y - b.y, a.z - b.z };
}

static struct nuvec_s nw_fx2_scale(struct nuvec_s a, float k)
{
    return (struct nuvec_s){ a.x * k, a.y * k, a.z * k };
}

static float nw_fx2_dot(struct nuvec_s a, struct nuvec_s b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static struct nuvec_s nw_fx2_cross(struct nuvec_s a, struct nuvec_s b)
{
    return (struct nuvec_s){ a.y * b.z - b.y * a.z, a.z * b.x - b.z * a.x, a.x * b.y - b.x * a.y };
}

/* A point under the joint matrix (row vectors), w 1. */
static struct nuvec_s nw_fx2_point(const struct numtx_s *m, struct nuvec_s p)
{
    return (struct nuvec_s){ p.x * m->_00 + p.y * m->_10 + p.z * m->_20 + m->_30,
                             p.x * m->_01 + p.y * m->_11 + p.z * m->_21 + m->_31,
                             p.x * m->_02 + p.y * m->_12 + p.z * m->_22 + m->_32 };
}

static void nw_fx2_tri(int sign, struct nuvec_s a, struct nuvec_s b, struct nuvec_s c)
{
    NwFx2Tris *t = &nw_fx2_tris[sign < 0];
    if (t->n + 3 > t->cap) {
        uint32_t cap = t->cap ? t->cap * 2 : 768;
        NwVuSpriteVertex *v = SDL_realloc(t->v, cap * sizeof(*v));
        if (!v) {
            return;
        }
        t->v = v;
        t->cap = cap;
    }
    const struct nuvec_s p[3] = { a, b, c };
    const uint8_t tint = sign < 0 ? 0xFF : 1;
    for (int k = 0; k < 3; k++) {
        NwVuSpriteVertex *v = &t->v[t->n++];
        memset(v, 0, sizeof(*v));
        v->pos[0] = p[k].x;
        v->pos[1] = p[k].y;
        v->pos[2] = p[k].z;
        v->col[0] = v->col[1] = v->col[2] = tint;
        v->col[3] = 0x80;
    }
}

/* shadtint: the quads between P[i], P[i] + light and the next pair, +1 when the camera is behind the first
   triangle's plane by 0.01, -1 otherwise; with a cap, the zig-zag strip over P[0] .. P[n - 2] (P[n - 1] closes the
   loop), the other way by P[0], P[1], P[2]'s plane. */
static void nw_fx2_volume(const struct nuvec_s *p, int n, const struct nuvec_s *light, bool cap)
{
    const struct nuvec_s cam = { global_camera.mtx._30, global_camera.mtx._31, global_camera.mtx._32 };
    for (int i = 0; i + 1 < n; i++) {
        struct nuvec_s q0 = nw_fx2_add(p[i], *light), q1 = nw_fx2_add(p[i + 1], *light);
        struct nuvec_s nrm = nw_fx2_cross(nw_fx2_sub(p[i + 1], p[i]), nw_fx2_sub(q1, p[i]));
        int sign = nw_fx2_dot(nw_fx2_sub(p[i], cam), nrm) - 0.01f < 0.0f ? -1 : 1;
        nw_fx2_tri(sign, p[i], q0, p[i + 1]);
        nw_fx2_tri(sign, q0, p[i + 1], q1);
    }
    if (!cap || n < 4) {
        return;
    }
    struct nuvec_s nrm = nw_fx2_cross(nw_fx2_sub(p[1], p[0]), nw_fx2_sub(p[2], p[0]));
    int sign = nw_fx2_dot(nw_fx2_sub(p[0], cam), nrm) < 0.0f ? 1 : -1;
    int order[NW_FX2_POINTS], m = 0;
    for (int lo = 0, hi = n - 2; lo <= hi; lo++, hi--) {
        order[m++] = lo;
        if (hi != lo) {
            order[m++] = hi;
        }
    }
    for (int k = 2; k < m; k++) {
        nw_fx2_tri(sign, p[order[k - 2]], p[order[k - 1]], p[order[k]]);
    }
}

/* The volumes so far as two VERTS records in the fx stream, counting +1 and -1. */
static void nw_fx2_flush(void)
{
    for (int s = 0; s < 2; s++) {
        NwFx2Tris *t = &nw_fx2_tris[s];
        NwVuSpriteVertex *v;
        if (!t->n) {
            continue;
        }
        nw_rndr_verts(rndrstream_fx, NW_VERTS_TRIS, t->n, &v)->stencil = (int8_t)(s ? -1 : 1);
        memcpy(v, t->v, t->n * sizeof(*v));
        t->n = 0;
    }
}

/* The shape's centre and axes under the joint matrix: the axes as differences of moved points. */
static void nw_fx2_frame(const struct numtx_s *m, struct nuvec_s centre, struct nuvec_s ax[3], struct nuvec_s *c,
                         struct nuvec_s out[3])
{
    *c = nw_fx2_point(m, centre);
    for (int k = 0; k < 3; k++) {
        out[k] = nw_fx2_sub(nw_fx2_point(m, nw_fx2_add(centre, ax[k])), *c);
    }
}

/* ShadCyl: NRCylData's rectangle (flags & 1: NRCylCapData's, with rounded ends) at each point (a, b, c, d) put at
   centre + y_axis * a + |y| * b + n * (c + d), n across the axis and the light, both as long as the cylinder's radius
   in n (x_axis and z_axis along it). */
void NuRndrShadowCylinderCap(struct numtx_s *mtx, struct NUCYLINDER_s *cyl, int count, struct nuvec_s *light)
{
    const float s45 = NuTrigTable[0x2000], s135 = NuTrigTable[0x6000], s225 = NuTrigTable[0xA000];
    const float cap[11][4] = {
        { 1.0f, 0.0f, 1.0f, 0.0f },     { -1.0f, 0.0f, 1.0f, 0.0f },   { -1.0f, -s45, 0.0f, s135 },
        { -1.0f, -1.0f, 0.0f, 0.0f },   { -1.0f, -s135, 0.0f, s225 },  { -1.0f, 0.0f, -1.0f, 0.0f },
        { 1.0f, 0.0f, -1.0f, 0.0f },    { 1.0f, s45, 0.0f, -s135 },    { 1.0f, 1.0f, 0.0f, 0.0f },
        { 1.0f, s135, 0.0f, -s225 },    { 1.0f, 0.0f, 1.0f, 0.0f },
    };
    static const float plain[5][4] = {
        { 1.0f, 0.0f, 1.0f, 0.0f }, { -1.0f, 0.0f, 1.0f, 0.0f }, { -1.0f, 0.0f, -1.0f, 0.0f },
        { 1.0f, 0.0f, -1.0f, 0.0f }, { 1.0f, 0.0f, 1.0f, 0.0f },
    };
    for (int i = 0; i < count; i++, cyl++) {
        struct nuvec_s ax[3] = { cyl->y_axis, { cyl->x_axis.x, cyl->x_axis.y, cyl->x_axis.z },
                                 { cyl->z_axis.x, cyl->z_axis.y, cyl->z_axis.z } };
        struct nuvec_s c, w[3], p[11];
        nw_fx2_frame(mtx, cyl->centre, ax, &c, w);
        struct nuvec_s n = nw_fx2_cross(w[0], *light);
        n = nw_fx2_scale(n, 1.0f / sqrtf(nw_fx2_dot(n, n)));
        struct nuvec_s y = nw_fx2_scale(w[0], 1.0f / sqrtf(nw_fx2_dot(w[0], w[0])));
        float nx = nw_fx2_dot(n, w[1]), nz = nw_fx2_dot(n, w[2]);
        float r = sqrtf(nx * nx + nz * nz);
        y = nw_fx2_scale(y, r);
        n = nw_fx2_scale(n, r);
        const float(*t)[4] = cyl->flags & 1 ? cap : plain;
        int np = cyl->flags & 1 ? 11 : 5;
        for (int k = 0; k < np; k++) {
            p[k] = nw_fx2_add(nw_fx2_add(nw_fx2_add(c, nw_fx2_scale(w[0], t[k][0])), nw_fx2_scale(y, t[k][1])),
                              nw_fx2_scale(n, t[k][2] + t[k][3]));
        }
        nw_fx2_volume(p, np, light, true);
    }
    nw_fx2_flush();
}

/* ShadElp: NRFanData's directions about the light (lightfan, its first again) each taken into the ellipsoid's frame:
   centre + each axis by the direction's part along it. */
void NuRndrShadowEllipse(struct numtx_s *mtx, struct NUELLIPSOID_s *ellipse, int count, struct nuvec_s *light)
{
    for (int i = 0; i < count; i++, ellipse++) {
        struct nuvec_s ax[3] = { ellipse->y_axis, ellipse->x_axis, ellipse->z_axis };
        struct nuvec_s c, w[3], unit[3], p[9];
        nw_fx2_frame(mtx, ellipse->centre, ax, &c, w);
        for (int k = 0; k < 3; k++) {
            unit[k] = nw_fx2_scale(w[k], 1.0f / sqrtf(nw_fx2_dot(w[k], w[k])));
        }
        for (int k = 0; k < 9; k++) {
            const struct nuvec_s *d = &lightfan[k & 7];
            p[k] = c;
            for (int a = 0; a < 3; a++) {
                p[k] = nw_fx2_add(p[k], nw_fx2_scale(w[a], nw_fx2_dot(unit[a], *d)));
            }
        }
        nw_fx2_volume(p, 9, light, true);
    }
    nw_fx2_flush();
}

/* ShadPol: each face whose turned normal is against the light by more than 2 (light . n < -2), its points moved
   and closed back to the first. */
void NuRndrShadowPols(struct numtx_s *mtx, struct nurndrshadface_s *face, struct nurndrshadface_s *vtx,
                      struct nuvec_s *light)
{
    struct nuvec_s p[NW_FX2_POINTS + 1];
    for (int i = vtx->count; i > 0; i--, face++) {
        struct nuvec_s n = { face->v.x * mtx->_00 + face->v.y * mtx->_10 + face->v.z * mtx->_20,
                             face->v.x * mtx->_01 + face->v.y * mtx->_11 + face->v.z * mtx->_21,
                             face->v.x * mtx->_02 + face->v.y * mtx->_12 + face->v.z * mtx->_22 };
        bool lit = nw_fx2_dot(n, *light) + 1.0f + 1.0f < 0.0f;
        int k = 0;
        for (int j = face->count; j > 0; j--, vtx++) {
            if (lit && k < NW_FX2_POINTS) {
                p[k++] = nw_fx2_point(mtx, vtx->v);
            }
        }
        if (k) {
            p[k] = p[0];
            nw_fx2_volume(p, k + 1, light, false);
        }
    }
    nw_fx2_flush();
}
