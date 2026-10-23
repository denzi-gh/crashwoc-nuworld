/* Builds a synthetic .cut in memory as the exporter lays it out: every pointer an address from CUT_BASE. */
#ifndef NUWORLD_TEST_CUT_BUILDER_H
#define NUWORLD_TEST_CUT_BUILDER_H

#include <stdint.h>
#include <string.h>

#include "gsc_builder.h"

#define CUT_BASE 0x53020020u

/* Where the test file put what the tests check. */
typedef struct CutFile {
    size_t names, camsys, cams, bounds, cam_times, cam_values, trig_times;
    size_t camanim_flags, locanim_flags, rigidanim_flags, charanim_flags, jointanim_flags;
} CutFile;

static size_t cut_rec(GscBuild *b, size_t size)
{
    size_t at = b->len;
    gsc_zero(b, size);
    return at;
}

static void cut_ptr(GscBuild *b, size_t field, size_t target)
{
    gsc_put32(b, field, CUT_BASE + (uint32_t)target);
}

static void cut_put16(GscBuild *b, size_t at, uint16_t v)
{
    memcpy(b->buf + at, &v, 2);
}

static void cut_putf(GscBuild *b, size_t at, float f)
{
    memcpy(b->buf + at, &f, 4);
}

/* An anim2 of n curves on one node over frames 1-4 in one chunk: flag 0 the constant i + 0.5, 1 a hermite from 2
   (frame 1) to 6 (frame 3), 3 a step from 7 to 9. Returns where its curve flags went. */
static size_t cut_anim2(GscBuild *b, size_t field, const int8_t *flags, int n)
{
    size_t at = cut_rec(b, 0x18);
    cut_ptr(b, field, at);
    cut_putf(b, at, 4.0f);
    cut_put16(b, at + 4, 1);
    cut_put16(b, at + 6, (uint16_t)n);
    cut_put16(b, at + 8, 1);
    size_t curves = cut_rec(b, 4 * (size_t)n);
    size_t cflags = b->len;
    gsc_bytes(b, flags, (size_t)n);
    size_t sflags = b->len;
    gsc_zero(b, 1);
    gsc_zero(b, (4 - b->len % 4) % 4);
    cut_ptr(b, at + 0xC, curves);
    cut_ptr(b, at + 0x10, cflags);
    cut_ptr(b, at + 0x14, sflags);
    for (int i = 0; i < n; i++) {
        if (!flags[i]) {
            cut_putf(b, curves + 4 * (size_t)i, (float)i + 0.5f);
            continue;
        }
        size_t cd = cut_rec(b, 12);
        cut_ptr(b, curves + 4 * (size_t)i, cd);
        size_t mask = b->len;
        gsc_u32(b, 0x5); /* keys at frames 1 and 3 */
        size_t ixs = cut_rec(b, 4);
        size_t keys = b->len;
        if (flags[i] == 1) {
            const float k[8] = { 1.0f, 0.5f, 2.0f, 0.0f, 3.0f, 0.0f, 6.0f, 0.0f };
            gsc_bytes(b, k, sizeof(k));
        } else {
            const float k[4] = { 7.0f, 0.0f, 9.0f, 0.0f };
            gsc_bytes(b, k, sizeof(k));
        }
        cut_ptr(b, cd, mask);
        cut_ptr(b, cd + 4, ixs);
        cut_ptr(b, cd + 8, keys);
    }
    return cflags;
}

/* A state anim of n keys at frames 1, 20, ...; values 0, 1, ...; returns where its times went. */
static size_t cut_stateanim(GscBuild *b, size_t field, int n, size_t *values)
{
    size_t at = cut_rec(b, 0xC);
    cut_ptr(b, field, at);
    gsc_put32(b, at, (uint32_t)n);
    size_t times = b->len;
    for (int i = 0; i < n; i++) {
        gsc_f32(b, i ? 20.0f * (float)i : 1.0f);
    }
    size_t v = b->len;
    for (int i = 0; i < n; i++) {
        uint8_t x = (uint8_t)i;
        gsc_bytes(b, &x, 1);
    }
    gsc_zero(b, (4 - b->len % 4) % 4);
    cut_ptr(b, at + 4, times);
    cut_ptr(b, at + 8, v);
    if (values) {
        *values = v;
    }
    return times;
}

/* Two cameras (one animated, camera switches), two locators of two types, two rigids (one named and animated,
   one with junk in locators), one character, one trigger and the bounds. */
static void cut_build(GscBuild *b, CutFile *f)
{
    memset(b, 0, sizeof(*b));
    memset(f, 0, sizeof(*f));
    size_t hdr = cut_rec(b, 0x30);
    gsc_put32(b, hdr, 1);
    gsc_put32(b, hdr + 4, CUT_BASE);
    cut_putf(b, hdr + 8, 40.0f);

    f->names = b->len;
    gsc_bytes(b, "rigid0\0char0\0debris\0func\0trig\0\0", 32);
    cut_ptr(b, hdr + 0xC, f->names);

    f->camsys = cut_rec(b, 0x20);
    cut_ptr(b, hdr + 0x10, f->camsys);
    gsc_put32(b, f->camsys, 2);
    b->buf[f->camsys + 0x10] = 1;
    const int8_t remap[15] = { 1, 0, -1, -1, -1, -1, -1, -1, -1, -1, 0, 0, 0, 0, 0x7E };
    memcpy(b->buf + f->camsys + 0x11, remap, sizeof(remap));
    f->cams = cut_rec(b, 2 * 0x50);
    cut_ptr(b, f->camsys + 4, f->cams);
    cut_putf(b, f->cams + 0x30, 1.0f);
    b->buf[f->cams + 0x40] = 1;
    cut_putf(b, f->cams + 0x50 + 0x30, 9.0f);
    b->buf[f->cams + 0x50 + 0x42] = 1;
    f->camanim_flags = cut_anim2(b, f->camsys + 8, (const int8_t[]){ 0, 1, 3 }, 3);
    f->cam_times = cut_stateanim(b, f->camsys + 0xC, 2, &f->cam_values);

    size_t locsys = cut_rec(b, 0xC);
    cut_ptr(b, hdr + 0x1C, locsys);
    b->buf[locsys + 8] = 2;
    b->buf[locsys + 9] = 2;
    size_t locs = cut_rec(b, 2 * 0x64);
    cut_ptr(b, locsys, locs);
    cut_putf(b, locs + 0x30, 5.0f);
    cut_putf(b, locs + 0x40, 0.25f);
    cut_putf(b, locs + 0x4C, 0.75f);
    gsc_put32(b, locs + 0x54, 3);
    b->buf[locs + 0x58] = 0x9;
    b->buf[locs + 0x59] = 1;
    b->buf[locs + 0x5A] = 0xFF;
    b->buf[locs + 0x64 + 0x58] = 0x8;
    b->buf[locs + 0x64 + 0x5A] = 4;
    size_t types = cut_rec(b, 2 * 0xC);
    cut_ptr(b, locsys + 4, types);
    gsc_put32(b, types, 14);
    b->buf[types + 4] = 1;
    cut_put16(b, types + 6, 0xFF);
    gsc_put32(b, types + 0xC, 21);
    b->buf[types + 0xC + 4] = 2;
    cut_put16(b, types + 0xC + 6, 0xFF);
    f->locanim_flags = cut_anim2(b, locs + 0x50, (const int8_t[]){ 1 }, 1);

    size_t rigidsys = cut_rec(b, 8);
    cut_ptr(b, hdr + 0x14, rigidsys);
    cut_put16(b, rigidsys + 4, 2);
    size_t rigids = cut_rec(b, 2 * 0x5C);
    cut_ptr(b, rigidsys, rigids);
    cut_putf(b, rigids + 0x30, 10.0f);
    gsc_put32(b, rigids + 0x40, 1);
    gsc_put32(b, rigids + 0x54, 1);
    b->buf[rigids + 0x58] = 3;
    b->buf[rigids + 0x59] = 1;
    gsc_put32(b, rigids + 0x5C + 0x54, 0x8BC618E8);
    b->buf[rigids + 0x5C + 0x58] = 1;
    f->rigidanim_flags = cut_anim2(b, rigids + 0x4C, (const int8_t[]){ 0, 0, 0, 0, 0, 0, 0, 0, 0 }, 9);
    cut_stateanim(b, rigids + 0x5C + 0x50, 1, NULL);

    size_t charsys = cut_rec(b, 8);
    cut_ptr(b, hdr + 0x18, charsys);
    cut_put16(b, charsys + 4, 1);
    size_t chars = cut_rec(b, 0x64);
    cut_ptr(b, charsys, chars);
    cut_putf(b, chars + 0x30, 11.0f);
    gsc_put32(b, chars + 0x40, 8);
    cut_putf(b, chars + 0x58, 0.5f);
    b->buf[chars + 0x5C] = 1;
    b->buf[chars + 0x5D] = 2;
    b->buf[chars + 0x5E] = 1;
    b->buf[chars + 0x61] = 7;
    f->charanim_flags = cut_anim2(b, chars + 0x44, (const int8_t[]){ 1, 0, 3 }, 3);
    f->jointanim_flags = cut_anim2(b, chars + 0x48, (const int8_t[]){ 0 }, 1);

    size_t trigsys = cut_rec(b, 8);
    cut_ptr(b, hdr + 0x24, trigsys);
    gsc_put32(b, trigsys, 1);
    size_t trig = cut_rec(b, 0xC);
    cut_ptr(b, trigsys + 4, trig);
    cut_put16(b, trig, 0xFFFF);
    cut_ptr(b, trig + 4, f->names + 25);
    f->trig_times = cut_stateanim(b, trig + 8, 3, NULL);

    f->bounds = cut_rec(b, 0x18);
    cut_ptr(b, hdr + 0x20, f->bounds);
    const float box[6] = { -1.0f, -2.0f, -3.0f, 1.0f, 2.0f, 3.0f };
    memcpy(b->buf + f->bounds, box, sizeof(box));
}

#endif
