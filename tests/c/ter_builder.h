/* Builds a synthetic .ter in memory: a static object, a platform, two linked wall splines, an unlinked one, the
   crash data and a skipped entry, then the directory. */
#ifndef NUWORLD_TEST_TER_BUILDER_H
#define NUWORLD_TEST_TER_BUILDER_H

#include "gsc_builder.h"

#define TER_LINK 0x12345678

/* Where the test file put what the tests check. */
typedef struct TerFile {
    size_t model[2], wall[2], crash, dir;
} TerFile;

/* A flat quad at height y over [x0, x1] x [z0, z1] (two triangles' worth of points, normal up). */
static void ter_quad(GscBuild *b, float x0, float x1, float z0, float z1, float y, const uint8_t info[4])
{
    const float box[6] = { x0, x1, y, y, z0, z1 };
    const float pnts[12] = { x0, y, z0, x1, y, z0, x0, y, z1, x1, y, z1 };
    const float norm[6] = { 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f };
    gsc_bytes(b, box, sizeof(box));
    gsc_bytes(b, pnts, sizeof(pnts));
    gsc_bytes(b, norm, sizeof(norm));
    gsc_bytes(b, info, 4);
}

/* A group of n quads tiling [x0, x0 + n] x [-1, 1], then the end group. */
static void ter_model(GscBuild *b, float x0, int n, float y, const uint8_t info[4])
{
    int16_t head[2] = { 0, (int16_t)n };
    const float bounds[4] = { x0, x0 + (float)n, -1.0f, 1.0f };
    gsc_bytes(b, head, sizeof(head));
    gsc_bytes(b, bounds, sizeof(bounds));
    for (int i = 0; i < n; i++) {
        ter_quad(b, x0 + (float)i, x0 + (float)i + 1.0f, -1.0f, 1.0f, y, info);
    }
    int16_t end[2] = { -1, 0 };
    gsc_bytes(b, end, sizeof(end));
    gsc_zero(b, 16);
}

/* A wall spline of n points: point i at (i, 0, 2i); linked ones start with the 0x12345678 slot. */
static void ter_wall(GscBuild *b, uint32_t first, uint16_t n, uint16_t val)
{
    gsc_u32(b, first);
    uint16_t head[2] = { n, val };
    gsc_bytes(b, head, sizeof(head));
    for (uint16_t i = 0; i < n; i++) {
        gsc_f32(b, (float)i);
        gsc_f32(b, 0.0f);
        gsc_f32(b, 2.0f * (float)i);
    }
}

/* OffType: offset (halfwords), translation, type, info, rx ry rz pad, rotation, flags, prim, id, datapos. */
static void ter_entry(GscBuild *b, size_t len, float x, int16_t type, int16_t info, uint32_t flags, int16_t id)
{
    gsc_u32(b, (uint32_t)(len / 2));
    gsc_f32(b, x);
    gsc_f32(b, 0.0f);
    gsc_f32(b, 0.0f);
    int16_t s[6] = { type, info, 0, 0, 0, 0 };
    gsc_bytes(b, s, sizeof(s));
    gsc_zero(b, 12);
    gsc_u32(b, flags);
    int16_t pid[2] = { 0, id };
    gsc_bytes(b, pid, sizeof(pid));
    gsc_u32(b, 0);
}

static inline void ter_build(GscBuild *b, TerFile *f)
{
    static const uint8_t plain[4] = { 1, 0, 0, 0 }, crush[4] = { 0xFF, 6, 1, 0 };
    size_t start[8];
    memset(b, 0, sizeof(*b));
    memset(f, 0, sizeof(*f));
    gsc_u32(b, 0);
    start[0] = b->len;
    gsc_zero(b, 8);                       /* type -1 */
    start[1] = f->model[0] = b->len;
    ter_model(b, -1.0f, 2, 0.0f, crush);  /* static object: floor at y 0 over x [9, 11] once placed */
    start[2] = f->wall[0] = b->len;
    ter_wall(b, TER_LINK, 18, 7);
    start[3] = b->len;
    ter_wall(b, 0, 2, 0);                 /* not linked: skipped */
    start[4] = f->model[1] = b->len;
    ter_model(b, 0.0f, 1, 1.0f, plain);   /* platform on instance 0 */
    start[5] = f->wall[1] = b->len;
    ter_wall(b, TER_LINK, 3, 9);
    start[6] = f->crash = b->len;
    gsc_u32(b, 0);
    start[7] = f->dir = b->len;
    gsc_put32(b, 0, (uint32_t)(f->dir / 2));
    gsc_u32(b, 7);
    ter_entry(b, start[1] - start[0], 0.0f, -1, 0, 0, 0);
    ter_entry(b, start[2] - start[1], 10.0f, 0, 3, 2, 5);
    ter_entry(b, start[3] - start[2], 0.0f, 2, 0, 0, 0);
    ter_entry(b, start[4] - start[3], 0.0f, 2, 0, 0, 0);
    ter_entry(b, start[5] - start[4], 20.0f, 1, 0, 1, 6);
    ter_entry(b, start[6] - start[5], 0.0f, 2, 0, 0, 0);
    ter_entry(b, start[7] - start[6], 0.0f, 3, 0, 0, 0);
}

#endif
