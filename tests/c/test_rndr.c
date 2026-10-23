/* The port's Nu render layer (src/gfx/nurndr.c, nustream.c, numtl.c, nutex.c, ps2txtr.c) under the decomp code
   that drives it, on synthetic data. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "gfx/rndr.h"
#include "nu3d/numtl.h"
#include "nu3d/nutex.h"

#include "gsc_builder.h"

extern char vu_load_tri_mpg[], vu_load_tric_mpg[], vu_load_tricu_mpg[], vu_load_trica_mpg[], vu_load_xformuv_mpg[];
extern struct nutexslot_s *D_0062EBEC;

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

/* An 8x8 PSMT4 texture with a 4x4 second level: texel i is (x + 2y) & 15. */
static int make_native(GscBuild *b, size_t *up)
{
    uint8_t clut[16][4], packed[32];
    for (int c = 0; c < 16; c++) {
        const uint8_t rgba[4] = { (uint8_t)(c * 16), (uint8_t)(255 - c * 16), (uint8_t)(c * 3), (uint8_t)(c * 8) };
        memcpy(clut[c], rgba, 4);
    }
    memset(packed, 0, sizeof(packed));
    for (int i = 0; i < 64; i++) {
        packed[i >> 1] |= (uint8_t)((((i & 7) + 2 * (i >> 3)) & 15) << (i & 1) * 4);
    }
    *up = gsc_texture(b, 4, 8, 8, clut, packed, 8, 8, sizeof(packed), 2, 2, 2);
    return NuTexCreateNative(b->buf);
}

/* NuTexCreateNative: retail's slot over the record in place, and the decoded texture. */
static void test_native(void)
{
    GscBuild b = { 0 };
    size_t up;
    int tid = make_native(&b, &up);
    CHECK(tid == 1);
    const struct nutexslot_s *e = &D_0062EBEC[0];
    CHECK(e->valid && e->native && e->type == 4 && e->width == 8 && e->height == 8 && e->mmcnt == 1);
    CHECK(e->ps2tex.mipcount == 2 && e->ps2tex.psm == 0x14 && e->ps2tex.tcc && e->ps2tex.tw == 3);
    CHECK(e->ps2tex.mipdata[0] == b.buf + up && e->ps2tex.tbw[1] == 2 && e->ps2tex.reqsize[0] == 128);
    const NwTex *t = nw_rndr_texture(tid);
    CHECK(t && t->nlevels == 2 && t->ncolours == 16 && t->levels[1].width == 4);
    CHECK(t && t->levels[0].texels[9] == 3 && t->clut[3][1] == 255 - 48);
    CHECK(nw_rndr_texture(0) == NULL && nw_rndr_texture(-1) == NULL && nw_rndr_texture(2) == NULL);

    /* A texture before b goes away: its slot, reused, replaces the decoded one. */
    NuTexDestroy(tid);
    CHECK(make_native(&b, &up) == 1 && nw_rndr_texture(1) && nw_rndr_texture(1)->nlevels == 2);
    gsc_free(&b);
}

/* NuTexCreate's bitmaps through NuPs2TxCreate as GenTextureLoad orders the texels. */
static void test_bitmap(void)
{
    static uint8_t bits[1024 * 16 * 4];
    static int pal[256];
    for (size_t i = 0; i < sizeof(bits); i++) {
        bits[i] = (uint8_t)(i * 7 + (i >> 8));
    }
    for (int i = 0; i < 256; i++) {
        pal[i] = 0x80000000 | i << 8 | (255 - i);
    }
    struct nutex_s spec = { 3, 8, 8, 0, bits, NULL };
    int tid = NuTexCreate(&spec);
    const NwTex *t = nw_rndr_texture(tid);
    CHECK(tid > 0 && t && t->type == 3 && t->tcc && t->ncolours == 0 && t->nlevels == 1);
    CHECK(t && memcmp(t->levels[0].texels + 4, (uint8_t[]){ bits[7], bits[6], bits[5], bits[4] >> 1 }, 4) == 0);
    CHECK(D_0062EBEC[tid - 1].ps2tex.psm == 0 && D_0062EBEC[tid - 1].ps2tex.mipcount == 1);

    spec = (struct nutex_s){ 2, 8, 8, 0, bits, NULL };
    t = nw_rndr_texture(tid = NuTexCreate(&spec));
    CHECK(t && !t->tcc && memcmp(t->levels[0].texels + 4, (uint8_t[]){ bits[5], bits[4], bits[3], 0x80 }, 4) == 0);

    /* Type 4 has the even texel in the high nibble; mmcnt levels follow each other. */
    spec = (struct nutex_s){ 4, 16, 8, 2, bits, pal };
    t = nw_rndr_texture(tid = NuTexCreate(&spec));
    CHECK(t && t->ncolours == 16 && t->nlevels == 1); /* the 8x4 second level is under 8 texels */
    CHECK(t && t->levels[0].texels[0] == bits[0] >> 4 && t->levels[0].texels[1] == (bits[0] & 15));
    CHECK(t && memcmp(t->clut[5], &pal[5], 4) == 0);
    spec = (struct nutex_s){ 5, 16, 16, 2, bits, pal };
    t = nw_rndr_texture(tid = NuTexCreate(&spec));
    CHECK(t && t->ncolours == 256 && t->nlevels == 2 && t->levels[1].width == 8);
    CHECK(t && t->levels[1].texels[9] == bits[256 + 9] && D_0062EBEC[tid - 1].ps2tex.mipcount == 2);

    /* Over 512 texels a side, the level takes every second texel of every second row. */
    spec = (struct nutex_s){ 5, 1024, 16, 0, bits, pal };
    t = nw_rndr_texture(tid = NuTexCreate(&spec));
    CHECK(t && t->nlevels == 1 && t->levels[0].width == 512 && t->levels[0].height == 8);
    CHECK(t && t->levels[0].texels[513] == bits[2048 + 2] && D_0062EBEC[tid - 1].ps2tex.tw == 10);
}

/* NuMtlUpdate with CreateStateBlock on gfx/mtl: the state block, its VU words and the microcode packages. */
static void test_state(void)
{
    struct numtl_s *m = NuMtlCreate(1);
    m->attrib.lighting = 2;
    m->attrib.alpha = 3;
    m->attrib.zmode = 1;
    m->diffuse.r = 0.5f;
    m->diffuse.g = 0.25f;
    m->diffuse.b = 1.0f;
    m->alpha = 0.5f;
    NuMtlUpdate(m);
    const NwStateBlock *sb = nw_rndr_state(m);
    CHECK(sb->gs.abe && sb->gs.alpha == 0x42 && sb->gs.zmsk && sb->gs.ztst == 2 && sb->gs.atst == 5);
    CHECK(sb->diffuse[0] == 64.0f && sb->diffuse[1] == 32.0f && sb->diffuse[2] == 128.0f && sb->diffuse[3] == 64.0f);
    CHECK(m->rs.mpg == (unsigned char *)vu_load_tricu_mpg && m->rs.attr == 0x3C && !m->zupd);
    CHECK(m->vuattr.u32 == sb->vuattr && (char *)(sb->vuattr + 13) <= (char *)m->sbdata + sizeof(m->sbdata));

    static const struct {
        int lighting, colour, fx;
        const char *mpg;
        unsigned attr;
    } packages[] = {
        { 0, 0, 0, vu_load_tri_mpg, 0x3D },     { 0, 1, 0, vu_load_tric_mpg, 0x3D },
        { 1, 0, 0, vu_load_xformuv_mpg, 0x3E }, { 3, 0, 0, vu_load_trica_mpg, 0x3D },
        { 2, 0, 1, vu_load_xformuv_mpg, 0x7C },
    };
    for (size_t i = 0; i < SDL_arraysize(packages); i++) {
        m->attrib.lighting = packages[i].lighting;
        m->attrib.colour = packages[i].colour;
        m->attrib.fx = packages[i].fx;
        NuMtlUpdate(m);
        CHECK(m->rs.mpg == (const unsigned char *)packages[i].mpg && m->rs.attr == packages[i].attr);
    }

    /* keep (flags 2) leaves VU 0x14-0x15 and zeroes the row; a plain update clears both. */
    m->vuattr.f32[0] = 0.25f;
    m->vuattr.s32[9] = 1024;
    NuMtlUpdateEx(m, 2);
    CHECK(m->vuattr.f32[0] == 0.25f && m->vuattr.s32[9] == 0);
    NuMtlUpdateEx(m, 0);
    CHECK(m->vuattr.f32[0] == 0.0f);

    /* TEX1's last level from the texture's mip count, with filter 2 only. */
    GscBuild b = { 0 };
    size_t up;
    m->tid = make_native(&b, &up);
    m->attrib.filter = 2;
    m->K = -32;
    m->L = 1;
    NuMtlUpdate(m);
    sb = nw_rndr_state(m);
    CHECK(sb->gs.mxl == 1 && sb->gs.k == -32 && sb->gs.l == 1 && sb->gs.mmin == 5);
    m->attrib.filter = 1;
    NuMtlUpdate(m);
    CHECK(sb->gs.mxl == 0 && sb->gs.mmin == 1);
    gsc_free(&b);
}

int main(void)
{
    NuTexInit();
    NuMtlInit();
    test_native();
    test_bitmap();
    test_state();
    printf("%s\n", failures ? "FAILED" : "rndr: all checks passed");
    return failures ? 1 : 0;
}
