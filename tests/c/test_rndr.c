/* The port's Nu render layer (src/gfx/nurndr.c, nustream.c, numtl.c, nutex.c, ps2txtr.c) under the decomp code
   that drives it, on synthetic data. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "game/cloudfx.h"
#include "gamelib/debris.h"
#include "gfx/rndr.h"
#include "nu3d/nucamera.h"
#include "nu3d/nugobj.h"
#include "nu3d/nuhgobj.h"
#include "nu3d/numtl.h"
#include "nu3d/nurndr.h"
#include "nu3d/nustream.h"
#include "nu3d/nutex.h"
#include "nu3d/nuvport.h"
#include "numath/numtx.h"
#include "numath/nutrig.h"
#include "nups2/nups2types.h"

#include "gsc_builder.h"

extern char vu_load_tri_mpg[], vu_load_tric_mpg[], vu_load_tricu_mpg[], vu_load_trica_mpg[], vu_load_xformuv_mpg[],
    vu_load_fx2_mpg[];
extern struct nutexslot_s *D_0062EBEC;
extern int no_default_font, PS2_REZ_W, PS2_REZ_H, PS2_VCNTR_X, PS2_VCNTR_Y;
extern struct numtl_s *numtl_defaultmtl2d;
extern struct nucamera_s global_camera;
extern int nurndr_pixel_width, nurndr_pixel_height;
extern struct numtl_s *nurndr_forced_mtl;
extern struct numtl_s **nurndr_forced_mtl_table;
extern struct numtx_s numtx_identity;

/* Declared by no header. */
int NuRndrBeginScene(int target);
void NuRndrEndScene(void);
void NuRndrStreamLink(struct rndrstream_s *stream);
void NuMtlAnimate(float dt);
void FaceYDirStream(int angle);
int NuRndrBeginSceneEx(int target, int zbuffer, int halfoffset);
void NuTexAssignAddr(int tex, int addr);
void NuRndrClear(int flags, int colour, float depth);
void NuRndrRect2di(int x, int y, int width, int height, int colour, struct numtl_s *mtl);
void NuRndrRectUV2di(int x, int y, int width, int height, float u0, float v0, float u1, float v1, int colour,
                     struct numtl_s *mtl);
void NuCameraSet(struct nucamera_s *cam);
void AddQuad3DrotXYZ(struct nuvec_s *pos, struct nuvec_s *shape, struct numtl_s *mtl, s32 *a, f32 *uvs, u32 colour);
int NuBridgeStreamRope(struct rndrstream_s *rs, struct nuvec_s *points, int npoints, int colour);
void *vpDmaTag_Next(void *packet, uint32_t addr);
void NuRndrInitWorld(void);
void NuRndrAddShadow(struct nuvec_s *pos, float size, int shade, int xrot, int yrot, int zrot);
void NuRndrAddWaterRipple(struct nuvec_s *pos, int duration, u32 colour, float startsize, float endsize);
void NuRndrAddFootPrint(int rot, float sizex, float sizez, int brightness, struct nuvec_s *pos, struct nuvec_s *norm,
                        int gfx, u32 colour);
extern int NuRndrShadowCnt, NuRndrFootFree;
extern struct nurndrwaterrip_s NuRndrWaterRipDat[];
extern struct nurndrfoot_s NuRndrFootData[];
void NuRndrShadowDirCol(struct nuvec_s *dir, u32 col);
void NuRndrShadowStencil(int paused, struct nuvec_s *pos);
void NuRndrShadow(int mode);
extern struct nuvec_s lightfan[], NuRndrShadBox[2];
extern struct nurndrshadstore_s shadstore[];
extern int NuRndrShadMaskCount, shadstorecnt, testflipold;
struct DmaTag *CreateDmaParticleSet(struct DmaTag *set, int *size);
struct debdma_s *CreateDmaPartEffectList(struct debdma_s *list, int *size);
void LinkDmaParticalSets(struct DmaTag **sets, int count);
void NuRndrParticleGroup(struct debchunk_s *chunk, struct debdma_s *list, struct numtl_s *mtl, struct numtx_s *mtx,
                         int mode, float time);
extern char vu_load_ptl_mpg[], vu_load_cloud_mpg[];
void cloudRender(struct nuvec_s *pos, struct nuvec_s *off, f32 size, f32 rng, s32 density, s32 maxc,
                 struct cloud_s *cl, struct numtl_s *mtl, struct nuvec_s *col);
int NuRndrGobj(struct nugobj_s *gobj, struct numtx_s *wm);
int NuRndrGobjSkin(struct nugobj_s *gobj, struct numtx_s *wm, int njoints, struct numtx_s *joints, u32 *weights,
                   int vis);
int NuHGobjRndrMtxDwa(struct NUHGOBJ_s *hobj, struct numtx_s *wm, int nlayers, short *layer, struct numtx_s *mtx_array,
                      float **dwa);
u32 *NuRndrCreateBlendShapeDeformerWeightsArray(u32 count);
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
    const NwCmd *c = p->cmds + 1; /* after the TARGET of NuRndrBeginScene, made before its offset and scissor */
    CHECK(p->ncmds == 4 && p->nviews == 3 && p->cmds[0].op == NW_CMD_TARGET);
    CHECK(c[0].local[12] == 7.0f && c[1].local[12] == 8.0f && c[2].local[12] == 8.0f);
    CHECK(c[0].view == c[1].view && p->views[c[2].view].vs[0] == 2.0f);
    const NwView *v = &p->views[c[0].view];
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
    static const uint8_t ops[7] = { NW_CMD_TARGET, NW_CMD_CLEAR, NW_CMD_DRAW, NW_CMD_DRAW,
                                    NW_CMD_CLEAR,  NW_CMD_RECT,  NW_CMD_RECT };
    bool same = p->ncmds == 7;
    for (uint32_t i = 0; i < 7 && same; i++) {
        same = p->cmds[i].op == ops[i];
    }
    CHECK(same);
    if (same) {
        const NwCmd *c = p->cmds + 1;
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
    CHECK(p->ncmds == 9 && p->cmds[7].clear.mask == 1 && p->cmds[8].clear.mask == 2);
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
    nw_rndr_ot_batch(&id, m, &mesh, (struct nutristream_s *)batch);
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

/* A turn about y and a translation. */
static void set_mtx(struct numtx_s *m, float angle, float x, float y, float z)
{
    memset(m, 0, sizeof(*m));
    m->_11 = m->_33 = 1.0f;
    m->_00 = m->_22 = SDL_cosf(angle);
    m->_02 = SDL_sinf(angle);
    m->_20 = -m->_02;
    m->_30 = x;
    m->_31 = y;
    m->_32 = z;
}

static bool same_mtx(const float *a, const struct numtx_s *b)
{
    for (int k = 0; k < 16; k++) {
        if (SDL_fabsf(a[k] - (&b->_00)[k]) > 1e-5f) {
            return false;
        }
    }
    return true;
}

static const NwCmd *drawn(const NwPacket *p, uint8_t op, const void *mesh, const struct numtl_s *mtl)
{
    for (uint32_t i = 0; i < p->ncmds; i++) {
        if (p->cmds[i].op == op && p->cmds[i].mesh == mesh && p->cmds[i].material == mtl) {
            return &p->cmds[i];
        }
    }
    return NULL;
}

/* An hgobj in a pose: each joint's gobjs under the joint's matrix times wm (a chain offset by its origins), each
   material pass a record; the skin object a SKIN record per pass under wm, both passes on one palette of INV_WT x
   the pose. NuRndrGobj leaves the forced material table out under a forced material; the blend-deformer weights
   keep retail's layout. */
static void test_hgobj(void)
{
    set_mtx(&numtx_identity, 0.0f, 0.0f, 0.0f, 0.0f); /* retail data the test has no image of */
    NuVpInit();
    struct nucamera_s *cam = NuCameraCreate();
    set_mtx(&cam->mtx, 0.0f, 0.0f, 0.0f, -20.0f);
    NuCameraSet(cam);
    struct numtl_s *m[3];
    for (int i = 0; i < 3; i++) {
        m[i] = NuMtlCreate(1);
        NuMtlUpdate(m[i]);
    }
    m[0]->next = m[1];
    struct numtx_s inv[3], pose[3], wm, want;
    for (int i = 0; i < 3; i++) {
        set_mtx(&inv[i], 0.0f, -(float)i, 0.0f, 0.0f);
        set_mtx(&pose[i], 0.4f * (float)i, (float)i, 1.0f, 0.0f);
    }
    set_mtx(&wm, 0.25f, 0.5f, 0.0f, 2.0f);
    struct nugeom_s geom[4];
    struct nugobj_s g[4];
    memset(geom, 0, sizeof(geom));
    memset(g, 0, sizeof(g));
    for (int i = 0; i < 4; i++) {
        geom[i].mtl = i == 2 ? m[2] : m[0];
        g[i].geom = &geom[i];
        g[i].min = (struct nuvec_s){ -1.0f, -1.0f, -1.0f };
        g[i].max = (struct nuvec_s){ 1.0f, 1.0f, 1.0f };
        if (i < 3) {
            geom[i].dmastream = (void *)(uintptr_t)(100 + i);
        }
    }
    geom[3].skindma = (struct SkinDmaTag *)(uintptr_t)0x55;
    g[0].next_gobj = &g[1];
    g[1].origin = (struct nuvec_s){ 0.0f, 2.0f, 0.0f };
    struct nugobj_s *gobjs[3] = { &g[0], NULL, &g[2] };
    struct NULAYERDATA_s layer = { NULL, gobjs, &g[3], NULL, NULL };
    struct NUHGOBJ_s h;
    memset(&h, 0, sizeof(h));
    h.type = 1;
    h.num_joints = 3;
    h.num_layers = 1;
    h.layers = &layer;
    h.INV_WT = inv;
    h.bbox_min = (struct nuvec_s){ -5.0f, -5.0f, -5.0f };
    h.bbox_max = (struct nuvec_s){ 5.0f, 5.0f, 5.0f };

    CHECK(NuHGobjRndrMtxDwa(&h, &wm, 1, NULL, pose, NULL) != 0);
    NuRndrEndScene();
    const NwPacket *p = nw_rndr_packet();
    CHECK(p->ncmds == 7 && p->njoints == 3);
    const NwCmd *c = drawn(p, NW_CMD_DRAW, (void *)102, m[2]);
    NuMtxMul(&want, &pose[2], &wm);
    CHECK(c && same_mtx(c->local, &want));
    struct numtx_s offset, jw;
    set_mtx(&offset, 0.0f, 0.0f, 2.0f, 0.0f);
    NuMtxMul(&jw, &pose[0], &wm);
    NuMtxMul(&want, &offset, &jw);
    CHECK((c = drawn(p, NW_CMD_DRAW, (void *)101, m[1])) && same_mtx(c->local, &want));
    CHECK(drawn(p, NW_CMD_DRAW, (void *)100, m[0]) && drawn(p, NW_CMD_DRAW, (void *)101, m[0]));
    const NwCmd *s0 = drawn(p, NW_CMD_SKIN, (void *)0x55, m[0]), *s1 = drawn(p, NW_CMD_SKIN, (void *)0x55, m[1]);
    CHECK(s0 && s1 && s0->first == s1->first && s0->count == 3 && same_mtx(s0->local, &wm));
    for (int j = 0; s0 && j < 3; j++) {
        NuMtxMul(&want, &inv[j], &pose[j]);
        CHECK(same_mtx(p->joints[s0->first + j], &want));
    }
    NuRndrSwapStreamBuffers();

    struct numtl_s *table[2] = { NULL, m[1] };
    m[2]->special_id = 1;
    nurndr_forced_mtl = m[2];
    nurndr_forced_mtl_table = table;
    NuRndrGobj(&g[2], &wm);
    NuRndrEndScene();
    p = nw_rndr_packet();
    CHECK(p->ncmds == 1 && p->cmds[0].material == m[2]);
    nurndr_forced_mtl = NULL;
    nurndr_forced_mtl_table = NULL;
    m[2]->special_id = 0;
    NuRndrSwapStreamBuffers();

    u32 *weights = NuRndrCreateBlendShapeDeformerWeightsArray(5);
    CHECK(weights[4] == 0x60060063 && weights[5] == 5);
    char *ptrs = NuRndrStreamAlloc(1);
    bool zero = true;
    for (int i = 0; i < 32; i++) {
        zero = zero && ptrs[i] == 0;
    }
    CHECK(zero);
    NuRndrSwapStreamBuffers();
}

/* A stream's VU 0x0C block by its attr & 0x43: a specular material (lighting 1) gets the specular UV matrix (row 2 the
   half vector of the light and the view, negated; the offset 0.5), an fx one the fx matrix, both in xformuv. A skin
   draw keeps its deformer weights. */
static void test_xuv(void)
{
    struct numtl_s *spec = NuMtlCreate(1), *fx = NuMtlCreate(1), *tri = NuMtlCreate(1);
    spec->attrib.lighting = 1;
    fx->attrib.fx = 1;
    NuMtlUpdate(spec);
    NuMtlUpdate(fx);
    NuMtlUpdate(tri);
    CHECK(spec->rs.mpg == (unsigned char *)vu_load_xformuv_mpg && fx->rs.mpg == (unsigned char *)vu_load_xformuv_mpg);
    CHECK((spec->rs.attr & 0x43) == 2 && (fx->rs.attr & 0x43) == 0x40);
    set_mtx(&global_camera.mtx, 0.0f, 0.0f, 0.0f, 0.0f);
    struct nuvec_s dir = { 1.0f, 0.0f, 0.0f };
    NuRndrSetSpecularLight(&dir, NULL);
    struct numtx_s fxm, id;
    set_mtx(&fxm, 0.5f, 0.25f, 0.75f, 0.0f);
    NuRndrSetFxMtx(&fxm);
    mark(&spec->rs, 90);
    mark(&fx->rs, 91);

    set_mtx(&id, 0.0f, 0.0f, 0.0f, 0.0f);
    *nw_rndr_joints(1) = id;
    u32 *weights = NuRndrCreateBlendShapeDeformerWeightsArray(2);
    ((float *)weights)[6] = 0.25f;
    ((float *)weights)[7] = 0.5f;
    struct nugeom_s geom;
    struct nugobj_s g;
    memset(&geom, 0, sizeof(geom));
    memset(&g, 0, sizeof(g));
    geom.mtl = tri;
    geom.skindma = (struct SkinDmaTag *)(uintptr_t)0x77;
    g.geom = &geom;
    NuRndrGobjSkin(&g, &id, 1, &id, weights, 1);
    NuRndrEndScene();

    const NwPacket *p = nw_rndr_packet();
    const NwCmd *s = marked(p, 90), *f = marked(p, 91), *k = drawn(p, NW_CMD_SKIN, (void *)0x77, tri);
    CHECK(s && f && s->light != NW_LIGHT_NONE && f->light != NW_LIGHT_NONE && s->light != f->light);
    if (s && f && s->light != NW_LIGHT_NONE && f->light != NW_LIGHT_NONE) {
        const float(*m)[4] = p->lights[s->light].uv;
        CHECK(memcmp(p->lights[f->light].uv, &fxm, sizeof(fxm)) == 0);
        CHECK(SDL_fabsf(m[2][0] + 0.70710678f) < 1e-5f && SDL_fabsf(m[2][2] - 0.70710678f) < 1e-5f && m[2][1] == 0.0f);
        CHECK(m[3][0] == 0.5f && m[3][1] == 0.5f && m[3][2] == 0.0f && m[3][3] == 1.0f && m[0][3] == 0.0f);
    }
    CHECK(k && k->nweights == 2 && p->weights[k->weights] == 0.25f && p->weights[k->weights + 1] == 0.5f);
    NuRndrSwapStreamBuffers();
}

/* The crate explosion's quad: the corners turned, placed and through the camera's VPCS matrix to whole GS units, a
   VERTS strip in the material's stream; a point out of view draws nothing. */
static void test_crate_quad(void)
{
    static struct nuvec_s shape[4] = { { -0.25f, 0.25f, 0.0f }, { 0.25f, 0.25f, 0.0f }, { -0.25f, -0.25f, 0.0f },
                                       { 0.25f, -0.25f, 0.0f } };
    float uvs[8] = { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f };
    s32 a[3] = { 0x1000, 0x2000, 0 };
    struct nuvec_s pos = { 1.0f, 0.5f, 0.0f }, behind = { 0.0f, 0.0f, -40.0f };
    struct numtl_s *mtl = NuMtlCreate(1);
    NuMtlUpdate(mtl);
    AddQuad3DrotXYZ(&pos, shape, mtl, a, uvs, 0x80402010);
    AddQuad3DrotXYZ(&behind, shape, mtl, a, uvs, 0x80402010);
    NuRndrEndScene();
    const NwPacket *p = nw_rndr_packet();
    const NwCmd *c = p->ncmds == 1 ? &p->cmds[0] : NULL;
    CHECK(c && c->op == NW_CMD_VERTS && c->mode == NW_VERTS_GS && c->count == 6 && c->material == mtl);
    if (c && c->count == 6) {
        static const int corner[6] = { 0, 1, 2, 1, 2, 3 };
        struct numtx_s m;
        NuMtxSetRotateXYZ(&m, (struct nuangvec_s *)a);
        NuMtxTranslate(&m, &pos);
        NuMtxMulH(&m, &m, NuCameraGetVPCSMtx());
        for (int k = 0; k < 6; k++) {
            const NwVuSpriteVertex *v = &p->verts[c->first + k];
            const struct nuvec_s *s = &shape[corner[k]];
            float w = s->x * m._03 + s->y * m._13 + s->z * m._23 + m._33;
            float x = (s->x * m._00 + s->y * m._10 + s->z * m._20 + m._30) / w;
            float y = (s->x * m._01 + s->y * m._11 + s->z * m._21 + m._31) / w;
            CHECK(SDL_fabsf(v->pos[0] - x) <= 1.0f && SDL_fabsf(v->pos[1] - y) <= 1.0f);
            CHECK(v->pos[0] == SDL_floorf(v->pos[0]) && v->pos[3] == 1.0f);
            CHECK(v->tex[0] == uvs[corner[k] * 2] * 4096.0f && v->tex[1] == uvs[corner[k] * 2 + 1] * 4096.0f);
            CHECK(v->col[0] == 0x10 && v->col[1] == 0x20 && v->col[2] == 0x40 && v->col[3] == 0x80);
        }
    }
    NuRndrSwapStreamBuffers();
}

/* NuBrdigeDrawRope's sequence: a rope of three points is two segments of four faces, the offsets turning a quarter
   from face to face, one VERTS record in the fx stream under fx2 and the identity; a link of the fx stream after
   another stream's leaves no raw packets. */
static void test_rope(void)
{
    struct nuvec_s pts[3] = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 2.0f, 0.0f, 0.0f } };
    struct numtl_s *mtl = NuMtlCreate(1);
    NuMtlUpdate(mtl);
    unsigned raw = nw_rndr_raw_streams();
    for (int pass = 0; pass < 2; pass++) {
        NuRndrStreamLink(rndrstream_fx);
        NuRndrStreamSetMisc(rndrstream_fx);
        NuRndrStreamAddMtl(rndrstream_fx, mtl);
        NuRndrSetLocalMtx(&numtx_identity, 2);
        NuRndrStreamSetViewMtxScissorClip(rndrstream_fx);
        NuRndrStreamSetLocalMtx(rndrstream_fx);
        NuRndrStreamFxMPG(1);
        NuBridgeStreamRope(rndrstream_fx, pass ? &pts[1] : pts, pass ? 2 : 3, (int)0x80FF8040);
        rndrstream_free = vpDmaTag_Next(rndrstream_fx->ptr.u8, 0);
        NuRndrStreamLink(rndrstream_2d);
        rndrstream_free = vpDmaTag_Next(rndrstream_2d->ptr.u8, 0);
    }
    NuRndrEndScene();
    const NwPacket *p = nw_rndr_packet();
    const NwCmd *c = p->ncmds == 2 ? &p->cmds[0] : NULL;
    CHECK(c && c->op == NW_CMD_VERTS && c->mode == NW_VERTS_TRIS && c->count == 48 && p->cmds[1].count == 24);
    CHECK(c && c->mpg == (void *)vu_load_fx2_mpg && c->material == mtl && same_mtx(c->local, &numtx_identity));
    if (c && c->count == 48) {
        const NwVuSpriteVertex *v = &p->verts[c->first];
        CHECK(v[0].pos[0] == 0.0f && v[0].pos[1] == 0.0f && v[0].pos[2] == -0.05f);
        CHECK(v[5].pos[0] == 1.0f && v[5].pos[1] == 0.05f && v[5].pos[2] == 0.0f);
        CHECK(v[6].pos[1] == -0.05f && v[12].pos[2] == 0.05f && v[24].pos[0] == 1.0f && v[24].pos[2] == -0.05f);
        CHECK(v[5].tex[0] == 0x5F0 && v[5].tex[1] == 0xFF0 && v[0].tex[0] == 0x400 && v[0].tex[1] == 0xC00);
        CHECK(v[3].col[0] == 0x40 && v[3].col[1] == 0x80 && v[3].col[2] == 0xFF && v[3].col[3] == 0x80);
    }
    CHECK(nw_rndr_raw_streams() == raw);
    NuRndrStreamFxMPG(0);
    NuRndrSwapStreamBuffers();
}

/* The hub's first-frame fx: the kept bookkeeping and draws of the shadow polys and ripples and the footprints, each a
   VERTS record in the fx stream under fx2 and the identity. A shadow is black with its shade as alpha and empties the
   list; a ripple takes its current colour; a footprint fades with its timer below 16 and draws over ALPHA_1 0x44. */
static void test_fx_quads(void)
{
    struct numtl_s *mtl = NuMtlCreate(1);
    NuMtlUpdate(mtl);
    NuRndrInitWorld();
    unsigned raw = nw_rndr_raw_streams();
    struct nuvec_s pos = { 1.0f, 2.0f, 3.0f }, at = pos, norm = { 0.0f, 1.0f, 0.0f };
    NuRndrAddShadow(&at, 0.5f, 0x60, 0, 0, 0);
    NuRndrShadPolys(mtl);
    CHECK(NuRndrShadowCnt == 0);
    at = pos;
    NuRndrAddWaterRipple(&at, 10, 0x80204060, 1.0f, 2.0f);
    NuRndrWaterRippleUpdate(1);
    NuRndrWaterRip(mtl);
    float u[8] = { 0 }, v[8] = { 0 };
    for (int k = 0; k < 4; k++) {
        u[4 + k] = 0.25f * (float)k;
        v[4 + k] = 0.5f;
    }
    NuRndrAddFootPrint(0, 0.5f, 0.25f, 0x40, &pos, &norm, 1, 0x00102030);
    NuRndrFootPrints(mtl, u, v);
    NuRndrEndScene();
    const NwPacket *p = nw_rndr_packet();
    const NwCmd *c = p->ncmds == 3 ? p->cmds : NULL;
    CHECK(c && c[0].count == 6 && c[1].count == 6 && c[2].count == 6);
    for (int i = 0; c && i < 3; i++) {
        CHECK(c[i].op == NW_CMD_VERTS && c[i].mode == NW_VERTS_TRIS && c[i].mpg == (void *)vu_load_fx2_mpg);
        CHECK(c[i].material == mtl && same_mtx(c[i].local, &numtx_identity) && c[i].alpha == (i == 2 ? 0x144 : 0));
    }
    if (c && c[0].count == 6 && c[1].count == 6 && c[2].count == 6) {
        const NwVuSpriteVertex *s = &p->verts[c[0].first], *r = &p->verts[c[1].first], *f = &p->verts[c[2].first];
        CHECK(s[0].pos[0] == 0.5f && s[0].pos[1] == 2.01f && s[0].pos[2] == 2.5f && s[5].pos[0] == 1.5f);
        CHECK(s[0].col[0] == 0 && s[0].col[3] == 0x60 && s[5].tex[0] == 4096.0f && s[5].tex[1] == 4096.0f);
        u32 curcolour = NuRndrWaterRipDat[0].curcolour;
        CHECK(r[0].pos[0] == 1.0f - NuRndrWaterRipDat[0].size && memcmp(r[0].col, &curcolour, 4) == 0);
        CHECK(r[0].tex[0] == 0xC00 && r[0].tex[1] == 0x400 && r[5].tex[0] == 0xFF0 && r[5].tex[1] == 0x7F0);
        const struct nurndrfoot_s *foot = &NuRndrFootData[(NuRndrFootFree - 1) & 0x3F];
        CHECK(f[0].pos[0] == foot->pnts[0].x && f[5].pos[2] == foot->pnts[3].z);
        CHECK(f[0].col[3] == 0x40 && f[0].col[0] == 0x30 && f[1].tex[0] == 1024.0f && f[0].tex[1] == 2048.0f);
    }
    CHECK(nw_rndr_raw_streams() == raw);
    NuRndrSwapStreamBuffers();

    struct nurndrfoot_s *foot = &NuRndrFootData[(NuRndrFootFree - 1) & 0x3F];
    foot->timer = 15;
    NuRndrFootPrints(mtl, u, v);
    NuRndrEndScene();
    p = nw_rndr_packet();
    CHECK(foot->timer == 14 && p->ncmds == 1 && p->verts[p->cmds[0].first].col[3] == 0x40 * 14 >> 4);
    NuRndrStreamFxMPG(0);
    NuRndrSwapStreamBuffers();
}

/* The hub's part of the volume shadows: the light's fan of unit directions square to it, the stencil's full-screen
   box without a mask, NuRndrShadow emptying the store (not with testflipold's bit 4) and drawing nothing, and an
   hgobj's joint shadows stored under the joint's matrix times wm. */
static void test_shadows(void)
{
    struct nuvec_s dir = { 1.0f, 0.0f, 0.0f }, pos = { 0.0f, 0.0f, 0.0f };
    NuRndrShadowDirCol(&dir, 0x80202020);
    for (int i = 0; i < 8; i++) {
        float len = lightfan[i].x * lightfan[i].x + lightfan[i].y * lightfan[i].y + lightfan[i].z * lightfan[i].z;
        CHECK(SDL_fabsf(lightfan[i].x) < 1e-3f && SDL_fabsf(len - 1.0f) < 1e-3f);
    }
    NuRndrShadMaskCount = 0;
    shadstorecnt = 2;
    NuRndrShadowStencil(0, &pos);
    CHECK(NuRndrShadBox[0].x == 0.0f && NuRndrShadBox[1].x == nurndr_pixel_width * 16.0f);
    CHECK(NuRndrShadBox[1].y == nurndr_pixel_height * 16.0f && shadstorecnt == 2);
    testflipold = 5;
    NuRndrShadow(0);
    CHECK(shadstorecnt == 2);
    testflipold = 1;
    NuRndrShadow(0);
    NuRndrEndScene();
    CHECK(shadstorecnt == 0 && nw_rndr_packet()->ncmds == 0);
    NuRndrSwapStreamBuffers();

    struct NUCYLINDER_s cyl;
    struct NUSHADOWDATA_s shadow[2];
    memset(shadow, 0, sizeof(shadow));
    shadow[0].cylinders = &cyl;
    shadow[0].ncylinders = 1;
    shadow[0].joint = 1;
    shadow[1].joint = 0xFF;
    struct numtx_s pose[2], wm, want;
    set_mtx(&pose[0], 0.0f, 0.0f, 0.0f, 0.0f);
    set_mtx(&pose[1], 0.5f, 1.0f, 2.0f, 0.0f);
    set_mtx(&wm, 0.25f, 0.5f, 0.0f, 2.0f);
    struct NUHGOBJ_s h;
    memset(&h, 0, sizeof(h));
    h.num_joints = 2;
    h.shadow = shadow;
    h.bbox_min = (struct nuvec_s){ -5.0f, -5.0f, -5.0f };
    h.bbox_max = (struct nuvec_s){ 5.0f, 5.0f, 5.0f };
    NuHGobjRndrMtxDwa(&h, &wm, 0, NULL, pose, NULL);
    NuMtxMul(&want, &pose[1], &wm);
    CHECK(shadstorecnt == 1 && same_mtx(&shadstore[0].mtx._00, &want) && shadstore[0].shadow.cylinders == &cyl);
    CHECK(shadstore[0].shadow.ncylinders == 1 && shadstore[0].shadow.shadow_meshes == NULL);
    shadstorecnt = 0;
}

/* SetupDebris's sets and effect lists as retail lays them out; three sets linked out of order are one PARTICLES record
   of their 96 particles in link order under the matrix, with the list's VU image and the time, which the list keeps;
   the last set's tag ends the chain. */
static void test_particles(void)
{
    static _Alignas(16) uint8_t buf[3 * 0x430 + 0x870];
    struct DmaTag *sets[3];
    int size = 0;
    for (int i = 0; i < 3; i++) {
        sets[i] = CreateDmaParticleSet((struct DmaTag *)(buf + i * 0x430), &size);
        CHECK(size == 0x430 && sets[i]->ctrl == 0x60000000u + 0x42);
    }
    struct debdma_s *list = CreateDmaPartEffectList((struct debdma_s *)(buf + 3 * 0x430), &size);
    CHECK(size == 0x870 && list->u1 == 0xC8 && list->v3 == 0xB8 && list->tri[0].v0x.colour == 0x80);
    CHECK(list->tri[63].v1y.colour == (u8)(0x40 - 63) && *(uint32_t *)&list->tri[64] == 0x14000302);
    struct DmaTag *order[3] = { sets[2], sets[0], sets[1] };
    LinkDmaParticalSets(order, 3);
    CHECK(sets[1]->ctrl >> 28 == 6 && sets[2]->ctrl >> 28 == 2 && (sets[2]->ctrl & 0xFFFF) == 0x42);
    ((struct debchunk_s *)sets[0])->parts[5].pos.x = 1.0f;
    ((struct debchunk_s *)sets[1])->parts[0].rate = 7.0f;
    list->grav = -0.5f;
    struct numtx_s mtx;
    set_mtx(&mtx, 0.5f, 1.0f, 2.0f, 3.0f);
    struct numtl_s *mtl = NuMtlCreate(1);
    NuMtlUpdate(mtl);
    NuRndrParticleGroup((struct debchunk_s *)order[0], list, mtl, &mtx, 0, 5.0f);
    NuRndrEndScene();
    const NwPacket *p = nw_rndr_packet();
    const NwCmd *c = p->ncmds == 1 ? p->cmds : NULL;
    CHECK(c && c->op == NW_CMD_PARTICLES && c->count == 96 && c->material == mtl && same_mtx(c->local, &mtx));
    CHECK(c && c->mpg == (void *)vu_load_ptl_mpg && list->time == 5.0f);
    if (c && c->count == 96) {
        const NwPtl *ptls = &p->ptls[c->first];
        const NwPtlList *l = &p->ptllists[c->ptllist];
        CHECK(ptls[0].birth == -1.0f && ptls[0].rate == 128.0f && ptls[37].pos[0] == 1.0f && ptls[64].rate == 7.0f);
        CHECK(l->head[1] == -0.5f && l->head[3] == 5.0f && l->uv[1][0] == 0xC8 && l->uv[3][1] == 0xB8);
        CHECK(memcmp(l->tri, list->tri, sizeof(l->tri)) == 0);
    }
    NuRndrSwapStreamBuffers();
}

/* cloudRender: each cloud a sprite in the clouds stream under the identity, its point off the wrapped scroll folded
   into +-rng about it (a cloud at rng + 1 lands at 1 - rng), its corners +-size by +-size / 2, in col times density
   with alpha 126. */
static void test_clouds(void)
{
    CloudFxInit();
    struct numtl_s *mtl = NuMtlCreate(1);
    NuMtlUpdate(mtl);
    struct cloud_s cl[2] = { { 16 * 7, 16, -16 * 2 }, { -16 * 6, 0, 0 } };
    struct nuvec_s pos = { 10.0f, 20.0f, 30.0f }, off = { 0.0f, 0.0f, 0.0f }, col = { 0.5f, 0.25f, 1.0f };
    cloudRender(&pos, &off, 8.0f, 4.0f, 64, 2, cl, mtl, &col);
    NuRndrEndScene();
    const NwPacket *p = nw_rndr_packet();
    const NwCmd *c = p->ncmds == 1 ? p->cmds : NULL;
    CHECK(c && c->op == NW_CMD_VERTS && c->mode == NW_VERTS_SPRITE && c->count == 12 && c->material == mtl);
    CHECK(c && c->mpg == (void *)vu_load_cloud_mpg && same_mtx(c->local, &numtx_identity));
    if (c && c->count == 12) {
        const NwVuSpriteVertex *v = &p->verts[c->first];
        float wx = 10.0f - 8.0f * (int)(10.0f / 8.0f), wz = 30.0f - 8.0f * (int)(30.0f / 8.0f);
        CHECK(v[0].pos[0] == 7.0f - wx - 8.0f + 10.0f && v[0].pos[2] == -2.0f - wz + 8.0f + 30.0f);
        CHECK(v[6].pos[0] == -6.0f - wx + 8.0f + 10.0f && v[5].pos[0] == v[0].pos[0]);
        CHECK(v[0].tex[0] == 0.0f && v[5].tex[0] == 1.0f && v[5].tex[1] == 1.0f && v[0].tex[2] == -8.0f);
        CHECK(v[0].tex[3] == -4.0f && v[0].col[0] == 32 && v[0].col[1] == 16 && v[0].col[2] == 64 && v[0].col[3] == 126);
    }
    NuRndrSwapStreamBuffers();
}

/* The TARGET command of the packet, by order among them. */
static const NwCmd *target(const NwPacket *p, int n)
{
    for (uint32_t i = 0; i < p->ncmds; i++) {
        if (p->cmds[i].op == NW_CMD_TARGET && n-- == 0) {
            return &p->cmds[i];
        }
    }
    return NULL;
}

/* Render targets: a scene drawn into one is a TARGET at its GS address with its size; types 11-13 reserve their
   memory, the others take the address NuTexAssignAddr gives them; a material with the target as its texture, or
   with -1 (the back buffer), reads GS memory. The palette and Z render targets are not drawn. */
static void test_render_target(void)
{
    struct nutex_s spec = { 11, 256, 128, 0, NULL, NULL };
    int rt = NuTexCreate(&spec);
    spec = (struct nutex_s){ 15, 640, 512, 0, NULL, NULL };
    int temp = NuTexCreate(&spec);
    spec = (struct nutex_s){ 8, 64, 64, 0, NULL, NULL };
    int pal = NuTexCreate(&spec);
    const struct nups2tex_s *t = &D_0062EBEC[rt - 1].ps2tex;
    CHECK(rt > 0 && t->tbp[0] != 0 && t->psm == 0 && t->tcc && t->tw == 8 && t->th == 7 && t->tbw[0] == 4);
    CHECK(nw_rndr_texture(rt) == NULL && D_0062EBEC[temp - 1].ps2tex.tbp[0] == 0);
    CHECK(D_0062EBEC[temp - 1].ps2tex.psm == 1 && !D_0062EBEC[temp - 1].ps2tex.tcc);
    NuTexAssignAddr(temp, 0x1234);

    struct numtl_s *m = NuMtlCreate(1), *grab = NuMtlCreate(1), *p8 = NuMtlCreate(1);
    m->tid = rt;
    grab->tid = -1;
    p8->tid = pal;
    NuMtlUpdate(m);
    NuMtlUpdate(grab);
    NuMtlUpdate(p8);
    NuRndrBeginSceneEx(rt, -2, 0);
    CHECK(nurndr_pixel_width == 256 && nurndr_pixel_height == 128);
    mark(&grab->rs, 90);
    NuRndrEndScene();
    NuRndrBeginSceneEx(temp, -2, 0);
    NuRndrEndScene();
    NuRndrBeginScene(-1);
    mark(&m->rs, 91);
    mark(&p8->rs, 92);
    NuRndrEndScene();
    NuRndrVu1Draw();
    const NwPacket *p = nw_packet_take();
    const NwCmd *a = target(p, 0), *b = target(p, 1), *c = target(p, 2);
    CHECK(a && b && c && target(p, 3) == NULL);
    if (a && b && c) {
        CHECK(a->target.surf.addr == t->tbp[0] && a->target.surf.tw == 256 && a->target.surf.th == 128);
        CHECK(a->target.width == 256 && a->target.height == 128);
        CHECK(b->target.surf.addr == 0x1234 && b->target.surf.tw == 1024 && !b->target.surf.tcc);
        CHECK(c->target.surf.addr == NW_SURF_BACK && c->target.width == 640 && c->target.surf.th == 512);
    }
    const NwCmd *g = marked(p, 90), *r = marked(p, 91), *q = marked(p, 92);
    CHECK(g && r && q);
    if (g && r && q) {
        CHECK(p->mtls[g->mtl].surf.addr == NW_SURF_BACK && p->mtls[g->mtl].surf.tw == 1024);
        CHECK(p->views[g->view].scissor[2] == 255 && p->views[g->view].scissor[3] == 127);
        CHECK(p->mtls[r->mtl].surf.addr == t->tbp[0] && p->mtls[r->mtl].tex == NULL);
        CHECK(p->mtls[q->mtl].surf.addr == 0 && p->mtls[q->mtl].tex == NULL);
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
    test_render_target();
    test_hgobj();
    test_xuv();
    test_crate_quad();
    test_rope();
    test_fx_quads();
    test_shadows();
    test_particles();
    test_clouds();
    printf("%s\n", failures ? "FAILED" : "rndr: all checks passed");
    return failures ? 1 : 0;
}
