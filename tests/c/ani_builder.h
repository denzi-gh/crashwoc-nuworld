/* Builds a synthetic .ani in memory as the exporter lays it out: every pointer an address from ANI_BASE. */
#ifndef NUWORLD_TEST_ANI_BUILDER_H
#define NUWORLD_TEST_ANI_BUILDER_H

#include <stdint.h>
#include <string.h>

#include "gsc_builder.h"

#define ANI_BASE 0x19190020u

/* Where the test file put what the tests check. */
typedef struct AniFile {
    size_t names, constants, keys[2];
} AniFile;

static size_t ani_rec(GscBuild *b, size_t size)
{
    size_t at = b->len;
    gsc_zero(b, size);
    return at;
}

static void ani_ptr(GscBuild *b, size_t field, size_t target)
{
    gsc_put32(b, field, ANI_BASE + (uint32_t)target);
}

/* Two keys (frames 1 and 3) from d0 to d1, flat. */
static size_t ani_keys(GscBuild *b, float d0, float d1)
{
    size_t at = b->len;
    const float k[8] = { 1.0f, 0.5f, 0.0f, d0, 3.0f, 0.0f, 0.0f, d1 };
    gsc_bytes(b, k, sizeof(k));
    return at;
}

/* One anim of two chunks (the second null), its chunk two nodes (the second null), its curve set nine constants
   0, 0.25, ... with curve 0 a hermite from 2 to 6 and curve 3 a step at 7; chunk keys and curves file values. */
static void ani_build(GscBuild *b, AniFile *f)
{
    memset(b, 0, sizeof(*b));
    memset(f, 0, sizeof(*f));
    size_t hdr = ani_rec(b, 0x10);
    gsc_put32(b, hdr, 1);
    gsc_put32(b, hdr + 4, ANI_BASE);
    f->names = b->len;
    gsc_bytes(b, "root\0arm\0\0\0", 12);
    size_t data = ani_rec(b, 0x10);
    ani_ptr(b, hdr + 8, data);
    float time = 2.5f;
    memcpy(b->buf + data, &time, 4);
    ani_ptr(b, data + 4, f->names);
    gsc_put32(b, data + 8, 2);
    size_t chunks = ani_rec(b, 8);
    ani_ptr(b, data + 0xC, chunks);
    size_t chunk = ani_rec(b, 0x14);
    ani_ptr(b, chunks, chunk);
    gsc_put32(b, chunk, 2);
    gsc_put32(b, chunk + 4, 1);
    gsc_put32(b, chunk + 0xC, 0x55);
    gsc_put32(b, chunk + 0x10, 0x66);
    size_t sets = ani_rec(b, 8);
    ani_ptr(b, chunk + 8, sets);
    size_t set = ani_rec(b, 0x10);
    ani_ptr(b, sets, set);
    gsc_put32(b, set, 1);
    b->buf[set + 0xC] = 9;
    f->constants = b->len;
    for (int i = 0; i < 9; i++) {
        gsc_f32(b, 0.25f * (float)i);
    }
    ani_ptr(b, set + 4, f->constants);
    size_t curves = ani_rec(b, 9 * 4);
    ani_ptr(b, set + 8, curves);
    for (int c = 0; c < 2; c++) {
        size_t curve = ani_rec(b, 0x10);
        ani_ptr(b, curves + (c ? 12 : 0), curve);
        gsc_put32(b, curve, 0x5);
        gsc_put32(b, curve + 8, 2);
        gsc_put32(b, curve + 0xC, (uint32_t)c);
        f->keys[c] = c ? ani_keys(b, 7.0f, 9.0f) : ani_keys(b, 2.0f, 6.0f);
        ani_ptr(b, curve + 4, f->keys[c]);
    }
}

#endif
