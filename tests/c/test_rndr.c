/* The port's Nu render layer (src/gfx/nurndr.c, nustream.c, numtl.c, nutex.c, ps2txtr.c) under the decomp code
   that drives it, on synthetic data. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "gfx/rndr.h"
#include "nu3d/nucamera.h"
#include "nu3d/numtl.h"
#include "nu3d/nurndr.h"
#include "nu3d/nustream.h"
#include "nu3d/nutex.h"
#include "nu3d/nuvport.h"
#include "numath/nutrig.h"

#include "gsc_builder.h"

extern char vu_load_tri_mpg[], vu_load_tric_mpg[], vu_load_tricu_mpg[], vu_load_trica_mpg[], vu_load_xformuv_mpg[];
extern struct nutexslot_s *D_0062EBEC;
extern int no_default_font, PS2_REZ_W, PS2_REZ_H, PS2_VCNTR_X, PS2_VCNTR_Y;
extern struct numtl_s *numtl_defaultmtl2d;
extern struct nucamera_s global_camera;

/* Declared by no header. */
int NuRndrBeginScene(int target);
void NuRndrEndScene(void);
void NuRndrStreamLink(struct rndrstream_s *stream);
void NuMtlAnimate(float dt);
void FaceYDirStream(int angle);
void NuRndrClear(int flags, int colour, float depth);
void NuRndrRect2di(int x, int y, int width, int height, int colour, struct numtl_s *mtl);
void NuRndrRectUV2di(int x, int y, int width, int height, float u0, float v0, float u1, float v1, int colour,
                     struct numtl_s *mtl);
extern u8 *rndrstream_free;
extern struct rndrstream_s *rndrstream_system, *rndrstream_mtl_sort, *rndrstream_faceon, *rndrstream_3d_u,
    *rndrstream_3d_l, *rndrstream_2d, *rndrstream_ptl, *rndrstream_fx, *rndrstream_ot_sort;

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
    const NwTex *t = &nw_rndr_texture(tid)->tex;
    CHECK(nw_rndr_texture(tid) && t->nlevels == 2 && t->ncolours == 16 && t->levels[1].width == 4);
    CHECK(t && t->levels[0].texels[9] == 3 && t->clut[3][1] == 255 - 48);
    CHECK(nw_rndr_texture(0) == NULL && nw_rndr_texture(-1) == NULL && nw_rndr_texture(2) == NULL);

    /* A texture before b goes away: its slot, reused, replaces the decoded one. */
    NuTexDestroy(tid);
    CHECK(make_native(&b, &up) == 1 && nw_rndr_texture(1) && nw_rndr_texture(1)->tex.nlevels == 2);
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
    const NwTex *t = &nw_rndr_texture(tid)->tex;
    CHECK(tid > 0 && t && t->type == 3 && t->tcc && t->ncolours == 0 && t->nlevels == 1);
    CHECK(t && memcmp(t->levels[0].texels + 4, (uint8_t[]){ bits[7], bits[6], bits[5], bits[4] >> 1 }, 4) == 0);
    CHECK(D_0062EBEC[tid - 1].ps2tex.psm == 0 && D_0062EBEC[tid - 1].ps2tex.mipcount == 1);

    spec = (struct nutex_s){ 2, 8, 8, 0, bits, NULL };
    t = &nw_rndr_texture(tid = NuTexCreate(&spec))->tex;
    CHECK(t && !t->tcc && memcmp(t->levels[0].texels + 4, (uint8_t[]){ bits[5], bits[4], bits[3], 0x80 }, 4) == 0);

    /* Type 4 has the even texel in the high nibble; mmcnt levels follow each other. */
    spec = (struct nutex_s){ 4, 16, 8, 2, bits, pal };
    t = &nw_rndr_texture(tid = NuTexCreate(&spec))->tex;
    CHECK(t && t->ncolours == 16 && t->nlevels == 1); /* the 8x4 second level is under 8 texels */
    CHECK(t && t->levels[0].texels[0] == bits[0] >> 4 && t->levels[0].texels[1] == (bits[0] & 15));
    CHECK(t && memcmp(t->clut[5], &pal[5], 4) == 0);
    spec = (struct nutex_s){ 5, 16, 16, 2, bits, pal };
    t = &nw_rndr_texture(tid = NuTexCreate(&spec))->tex;
    CHECK(t && t->ncolours == 256 && t->nlevels == 2 && t->levels[1].width == 8);
    CHECK(t && t->levels[1].texels[9] == bits[256 + 9] && D_0062EBEC[tid - 1].ps2tex.mipcount == 2);

    /* Over 512 texels a side, the level takes every second texel of every second row. */
    spec = (struct nutex_s){ 5, 1024, 16, 0, bits, pal };
    t = &nw_rndr_texture(tid = NuTexCreate(&spec))->tex;
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


/* A command whose mesh field only marks where it came from. */
static void mark(struct rndrstream_s *s, uintptr_t id)
{
    nw_rndr_cmd(s, NW_CMD_DRAW)->mesh = (const NwGpuMesh *)id;
}

static bool packet_is(const uintptr_t *ids, uint32_t n)
{
    const NwPacket *p = nw_rndr_packet();
    bool same = p->ncmds == n;
    for (uint32_t i = 0; i < n && same; i++) {
        same = (uintptr_t)p->cmds[i].mesh == ids[i];
    }
    if (!same) {
        fprintf(stderr, "packet:");
        for (uint32_t i = 0; i < p->ncmds; i++) {
            fprintf(stderr, " %u", (unsigned)(uintptr_t)p->cmds[i].mesh);
        }
        fprintf(stderr, "\n");
    }
    return same;
}

/* Retail's stream order (descending drawpri, a later stream before an equal key), the material order (sort_pri,
   then tid) inside mtl_sort, the ordering table back to front with the latest item first in a bin, and one sorted
   group per flush. */
static void test_order(void)
{
    struct rndrstream_s *pool[] = { rndrstream_2d, rndrstream_ptl, rndrstream_fx, rndrstream_ot_sort,
                                    rndrstream_mtl_sort, rndrstream_faceon, rndrstream_3d_u, rndrstream_3d_l,
                                    rndrstream_system };
    for (size_t i = 0; i < SDL_arraysize(pool); i++) {
        mark(pool[i], i + 1);
    }
    NuRndrStreamFlush();
    CHECK(packet_is((const uintptr_t[]){ 9, 8, 7, 6, 5, 4, 3, 2, 1 }, 9));

    struct numtl_s *m[3];
    static const int pri[3] = { 5, 2, 2 }, tid[3] = { 0, 3, 1 };
    for (int i = 0; i < 3; i++) {
        m[i] = NuMtlCreate(1);
        m[i]->sort_pri = pri[i];
        m[i]->tid = tid[i];
        NuMtlUpdateEx(m[i], 1);
    }
    NuRndrSwapStreamBuffers();
    mark(&m[0]->rs, 10);
    mark(&m[1]->rs, 11);
    mark(&m[2]->rs, 12);
    mark(&m[0]->rs, 13);
    mark(rndrstream_3d_l, 14);
    static const float depth[5] = { 0.0f, 1000.0f, 2000.0f, 100.25f, 100.0f };
    for (int i = 0; i < 5; i++) {
        NwRecord *item = nw_rndr_record(NW_CMD_DRAW, m[0], m[0]->rs.mpg, true);
        item->cmd.mesh = (const NwGpuMesh *)(uintptr_t)(20 + i);
        nw_rndr_ot_insert(item, depth[i]);
    }
    mark(rndrstream_2d, 15);
    NuRndrEndScene();
    mark(&m[1]->rs, 16);
    NuRndrEndSceneEx(0);
    mark(rndrstream_2d, 17);
    NuRndrEndScene();
    CHECK(packet_is((const uintptr_t[]){ 14, 12, 11, 10, 13, 22, 21, 24, 23, 20, 15, 16, 17 }, 13));
    CHECK(nw_rndr_packet()->cmds[0].material == NULL && nw_rndr_packet()->cmds[1].material == m[2]);
    CHECK(nw_rndr_packet()->cmds[1].mpg == m[2]->rs.mpg);
    NuRndrSwapStreamBuffers();
    CHECK(nw_rndr_packet()->ncmds == 0);
}

/* A record keeps the view and local matrix it was made under; the packet holds each view once. */
static void test_state_capture(void)
{
    struct numtx_s a, b;
    memset(&a, 0, sizeof(a));
    a._00 = 1.0f;
    b = a;
    b._30 = 7.0f;
    NuRndrBeginScene(-1);
    NuRndrSetViewMtx(&a, &a, &a);
    NuRndrSetLocalMtx(&b, 1);
    mark(rndrstream_3d_l, 1);
    b._30 = 8.0f;
    NuRndrSetLocalMtx(&b, 1);
    mark(rndrstream_3d_l, 2);
    a._00 = 2.0f;
    NuRndrSetViewMtx(&a, &a, &a);
    mark(rndrstream_3d_l, 3);
    NuRndrEndScene();
    const NwPacket *p = nw_rndr_packet();
    CHECK(p->ncmds == 3 && p->nviews == 2);
    CHECK(p->cmds[0].local[12] == 7.0f && p->cmds[1].local[12] == 8.0f && p->cmds[2].local[12] == 8.0f);
    CHECK(p->cmds[0].view == p->cmds[1].view && p->views[p->cmds[2].view].vs[0] == 2.0f);
    const NwView *v = &p->views[p->cmds[0].view];
    CHECK(v->xyoffset[0] == (0x800 - 320) << 4 && v->xyoffset[1] == (0x800 - 256) << 4);
    CHECK(v->scissor[2] == 639 && v->scissor[3] == 511 && v->vs[0] == 1.0f);
    NuRndrSwapStreamBuffers();
}

/* Packets the compiled decomp writes into a stream itself are reported, not drawn. */
static void test_raw(void)
{
    CHECK(nw_rndr_raw_streams() == 0);
    NuRndrStreamLink(rndrstream_2d);
    rndrstream_2d->ptr.u8 += 16;
    rndrstream_free = rndrstream_2d->ptr.u8;
    NuRndrStreamFlush();
    CHECK(nw_rndr_raw_streams() == 1 && nw_rndr_packet()->ncmds == 0);
    NuRndrSwapStreamBuffers();
}

/* The decomp's NuMtlAnimate writes the UV offset into the state block (VU 0x14, the STROW row x4096), whose snapshot
   the published packet holds: per axis none, constant, wrapped scroll, sine and cosine of the shared clock. */
static void test_animate(void)
{
    struct numtl_s *m[3];
    static const int umode[3] = { 2, 3, 0 }, vmode[3] = { 1, 4, 0 };
    for (int i = 0; i < 3; i++) {
        m[i] = NuMtlCreate(1);
        NuMtlUpdate(m[i]);
        m[i]->uanmmode = umode[i];
        m[i]->vanmmode = vmode[i];
        m[i]->du = 0.9f;
        m[i]->dv = 0.5f;
        m[i]->su = 1.0f;
        m[i]->sv = 1.0f;
    }
    m[2]->vuattr.f32[0] = 0.125f;
    NuMtlAnimate(0.25f); /* NuTrigTable[16384] = sin(pi / 2), [32768] = sin(pi) */
    CHECK(SDL_fabsf(m[0]->vuattr.f32[0] - 0.15f) < 1e-6f && m[0]->vuattr.s32[9] == (int32_t)(m[0]->du * 4096.0f));
    CHECK(m[0]->vuattr.f32[1] == 0.5f && m[0]->vuattr.s32[10] == 2048);
    CHECK(SDL_fabsf(m[1]->vuattr.f32[0] - 0.9f) < 1e-6f && SDL_fabsf(m[1]->vuattr.f32[1]) < 1e-4f);
    CHECK(m[2]->vuattr.f32[0] == 0.125f && m[2]->vuattr.s32[9] == 0);

    for (int i = 0; i < 3; i++) {
        mark(&m[i]->rs, 30 + i);
    }
    NuRndrEndScene();
    NuRndrVu1Draw();
    const NwPacket *p = nw_packet_take();
    CHECK(p && p->ncmds == 3);
    for (uint32_t i = 0; p && i < p->ncmds; i++) {
        const NwCmd *c = &p->cmds[i];
        int k = (int)(uintptr_t)c->mesh - 30;
        CHECK(k >= 0 && k < 3 && c->mtl < p->nmtls);
        if (k >= 0 && k < 3 && c->mtl < p->nmtls) {
            const NwMtlSnap *s = &p->mtls[c->mtl];
            CHECK(s->uvoff[0] == m[k]->vuattr.f32[0] && s->uvoff[1] == m[k]->vuattr.f32[1]);
            CHECK(s->row[0] == m[k]->vuattr.s32[9] && s->row[1] == m[k]->vuattr.s32[10]);
        }
    }
    NuRndrSwapStreamBuffers();
}

/* NuRndrClear flushes what came before (flags & 4: the streams alone, so material streams wait) and clears from
   the system stream, which sorts first in the next group; the sprites go to the 2D stream at screen or target
   coordinates under numtl_defaultmtl2d unless given a material. */
static void test_2d(void)
{
    struct numtl_s *m = NuMtlCreate(1);
    NuMtlUpdate(m);
    NuRndrBeginScene(-1);
    mark(&m->rs, 50);
    NuRndrClear(6, (int)0x80402010, 0.25f);
    mark(rndrstream_2d, 51);
    NuRndrClear(3, 0, 1.0f);
    NuRndrRect2di(16, 32, 160, 80, (int)0x80FFFFFF, NULL);
    NuRndrRectUV2di(0, 0, 640 << 4, 256 << 4, 0.25f, 0.0f, 0.75f, 1.0f, 0x10203040, m);
    NuRndrClear(5, 0, 0.0f);
    const NwPacket *p = nw_rndr_packet();
    static const uint8_t ops[6] = { NW_CMD_CLEAR, NW_CMD_DRAW, NW_CMD_DRAW, NW_CMD_CLEAR, NW_CMD_RECT, NW_CMD_RECT };
    bool same = p->ncmds == 6;
    for (uint32_t i = 0; i < 6 && same; i++) {
        same = p->cmds[i].op == ops[i];
    }
    CHECK(same);
    if (same) {
        const NwCmd *c = p->cmds;
        CHECK(c[0].clear.mask == 2 && c[0].clear.z == (uint32_t)(int)(0.75f * 8388607.0f));
        CHECK(memcmp(c[0].clear.rgba, (uint8_t[]){ 0x10, 0x20, 0x40, 0x80 }, 4) == 0 && c[0].material == NULL);
        CHECK((uintptr_t)c[1].mesh == 50 && (uintptr_t)c[2].mesh == 51);
        CHECK(c[3].clear.mask == 3 && c[3].clear.z == 0);
        CHECK(c[4].rect.xy[0] == 16 + (0x800 - 320) * 16 && c[4].rect.xy[1] == 32 + (0x800 - 256) * 16);
        CHECK(c[4].rect.xy[2] == c[4].rect.xy[0] + 160 && c[4].rect.xy[3] == c[4].rect.xy[1] + 80);
        CHECK(c[4].rect.st[2] == 1.0f && c[4].rect.rgba[3] == 0x80 && c[4].material == numtl_defaultmtl2d);
        CHECK(c[5].material == m && c[5].rect.st[0] == 0.25f && c[5].rect.st[2] == 0.75f);
        CHECK(c[5].rect.xy[3] == (0x800 - 256) * 16 + (256 << 4) && c[5].rect.rgba[0] == 0x40);
    }
    NuRndrClear(2, 0, 1.0f);
    NuRndrEndScene();
    CHECK(p->ncmds == 8 && p->cmds[6].clear.mask == 1 && p->cmds[7].clear.mask == 2);
    NuRndrSwapStreamBuffers();
}

/* The command of the packet whose mesh field marks id. */
static const NwCmd *marked(const NwPacket *p, uintptr_t id)
{
    for (uint32_t i = 0; i < p->ncmds; i++) {
        if (p->cmds[i].op == NW_CMD_DRAW && (uintptr_t)p->cmds[i].mesh == id) {
            return &p->cmds[i];
        }
    }
    return NULL;
}

/* A draw into a lit material's stream latches the lights set at that point; one into an unlit material's does not;
   the packet holds each set of lights once. */
static void test_lights(void)
{
    struct numtl_s *lit = NuMtlCreate(1), *unlit = NuMtlCreate(1);
    unlit->attrib.lighting = 2;
    NuMtlUpdate(lit);
    NuMtlUpdate(unlit);
    struct nuvec_s d[3] = { { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } };
    struct nucolour3_s c[3] = { { 0.5f, 0.5f, 0.5f }, { 0.25f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } };
    struct nucolour3_s ambient = { 0.1f, 0.2f, 0.3f };
    NuRndrSetDirectionalLights(&d[0], &c[0], &d[1], &c[1], &d[2], &c[2]);
    NuRndrSetAmbientLight(&ambient);
    mark(&lit->rs, 60);
    mark(&unlit->rs, 61);
    mark(&lit->rs, 62);
    c[0].r = 1.0f;
    NuRndrSetDirectionalLights(&d[0], &c[0], &d[1], &c[1], &d[2], &c[2]);
    mark(&lit->rs, 63);
    NuRndrEndScene();
    const NwPacket *p = nw_rndr_packet();
    const NwCmd *a = marked(p, 60), *u = marked(p, 61), *b = marked(p, 62), *e = marked(p, 63);
    CHECK(a && u && b && e && p->nlights == 2);
    if (a && u && b && e && p->nlights == 2) {
        CHECK(u->light == NW_LIGHT_NONE && a->light == b->light && e->light != a->light);
        const NwLights *la = &p->lights[a->light], *le = &p->lights[e->light];
        CHECK(la->col[0][0] == 0.5f && le->col[0][0] == 1.0f && la->dir[1][1] == 1.0f && la->col[1][0] == 0.25f);
        CHECK(la->ambient[0] == 0.1f && le->ambient[2] == 0.3f);
    }
    NuRndrSwapStreamBuffers();
}

/* An ordering-table batch: its triangles bucket by view z and draw far to near, in batch order within a bucket;
   the batch goes into the ordering table at its nearest triangle. */
static void test_ot_batch(void)
{
    static const float z[4] = { 1.0f, 0.0f, 1.0f, 100.0f };
    static uint8_t batch[16 + 4 * 0xA0];
    memset(batch, 0, sizeof(batch));
    batch[0] = 4;
    for (int t = 0; t < 4; t++) {
        for (int k = 0; k < 3; k++) {
            const float pos[4] = { (float)k, 0.0f, z[t], 0.0f };
            memcpy(batch + 16 + t * 0xA0 + 16 + k * 0x30, pos, 16);
        }
    }
    NwGpuMesh mesh = { 0 };
    CHECK(nw_vif_otbatch(&mesh.mesh, batch, sizeof(batch)) && mesh.mesh.ntris == 4);
    struct numtx_s id;
    memset(&id, 0, sizeof(id));
    id._00 = id._11 = id._22 = id._33 = 1.0f;
    global_camera.mtx = id;
    struct numtl_s *m = NuMtlCreate(1);
    NuMtlUpdate(m);
    nw_rndr_ot_batch(&id, m, &mesh, (struct nutribatch_s *)batch);
    mark(rndrstream_3d_l, 70);
    NuRndrEndScene();
    const NwPacket *p = nw_rndr_packet();
    static const uint32_t order[12] = { 9, 10, 11, 0, 1, 2, 6, 7, 8, 3, 4, 5 };
    CHECK(p->ncmds == 2 && p->cmds[1].op == NW_CMD_DRAW_OT && p->cmds[1].count == 12 && p->nidx == 12);
    CHECK(p->ncmds == 2 && p->cmds[1].mesh == &mesh && p->cmds[1].light != NW_LIGHT_NONE);
    CHECK(p->ncmds == 2 && p->cmds[1].mpg == m->rs.mpg);
    CHECK(p->nidx == 12 && memcmp(p->idx + p->cmds[1].first, order, sizeof(order)) == 0);
    NuRndrSwapStreamBuffers();
    nw_vif_free(&mesh.mesh);
}

/* FaceYDirStream's VU 0x16 is part of the view a draw is made under: the yaw's cosine and negated sine, the
   viewport's size scaled for faceon. */
static void test_faceon_dir(void)
{
    NuVpInit();
    FaceYDirStream(0x4000);
    mark(rndrstream_3d_l, 80);
    FaceYDirStream(0);
    mark(rndrstream_3d_l, 81);
    NuRndrEndScene();
    const NwPacket *p = nw_rndr_packet();
    CHECK(p->ncmds == 2 && p->nviews == 2);
    if (p->ncmds == 2 && p->nviews == 2) {
        const float *a = p->views[p->cmds[0].view].faceon, *b = p->views[p->cmds[1].view].faceon;
        CHECK(SDL_fabsf(a[0]) < 1e-6f && a[2] == -1.0f && b[0] == 1.0f && b[2] == 0.0f);
        CHECK(a[1] == 640 * 16 * 9.765625146e-05f && a[3] == 512 * 16 * 0.0002790178696f);
    }
    NuRndrSwapStreamBuffers();
}

int main(void)
{
    PS2_REZ_W = 640;
    PS2_REZ_H = 512;
    PS2_VCNTR_X = 0x800;
    PS2_VCNTR_Y = 0x800;
    no_default_font = 1;
    NuTexInit();
    NuMtlInit();
    NuTrigInit();
    NuRndrInitEx(0x10000);
    test_native();
    test_bitmap();
    test_state();
    test_order();
    test_state_capture();
    test_raw();
    test_animate();
    test_2d();
    test_lights();
    test_ot_batch();
    test_faceon_dir();
    printf("%s\n", failures ? "FAILED" : "rndr: all checks passed");
    return failures ? 1 : 0;
}
