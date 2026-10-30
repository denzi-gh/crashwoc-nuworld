/* nu3d/nurndr overridden whole (port/units.toml): the scene bracket and draws as stream records (src/gfx/nustream.c);
   every other NuRndr* is a stub whose hits are the renderer's worklist. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "app.h"
#include "gfx/interp.h"
#include "gfx/rndr.h"
#include "nu3d/nucamera.h"
#include "nu3d/nugobj.h"
#include "nu3d/nuhgobj.h"
#include "nu3d/numtl.h"
#include "nu3d/nurndr.h"
#include "nu3d/nustream.h"
#include "gamelib/debris.h"
#include "nu3d/nutex.h"
#include "nu3d/nuvport.h"
#include "numath/numtx.h"
#include "numath/nutrig.h"
#include "numath/nuvec.h"
#include "nups2/nups2types.h"
#include "port/wide.h"

int NuPs2ScreenSwap(int vsync);
void NuCameraSet(struct nucamera_s *cam);
void NuRndrSetRTarget(struct rndrstream_s *stream, int target);
void NuRndrSetZBuffer(struct rndrstream_s *stream, int zbuffer);
void NuPs2DisableAutoScreenSwap(void);
void NuFntInit(void);
int Ps2TxValToPowerOfTwo(int value);

float XFormVuTriZ(float *depth, void *tri, struct numtx_s *mtx, int count);
void NuMtxMulVU0(struct numtx_s *dst, struct numtx_s *a, struct numtx_s *b);
void NuMtxMulArrayVU0(struct numtx_s *dst, struct numtx_s *a, struct numtx_s *b, int count);
struct NUJOINTANIM_s;
void NuHGobjEval(struct NUHGOBJ_s *hobj, int njanims, struct NUJOINTANIM_s *janim, struct numtx_s *mtx_array);

/* Roots of the data image. */
extern int nurndr_tritot_this_frame;
extern int nurndr_pixel_width;
extern int nurndr_pixel_height;
extern int nurndr_pixel_p2w;
extern int nurndr_pixel_p2h;
extern int PS2_REZ_W;
extern int PS2_REZ_H;
extern int PS2_VCNTR_X;
extern int PS2_VCNTR_Y;
extern struct numtl_s *numtl_defaultmtl2d;
extern struct rndrstream_s *rndrstream_2d;
extern struct rndrstream_s *rndrstream_3d;
extern struct numtl_s *nurndr_forced_mtl;
extern struct numtl_s **nurndr_forced_mtl_table;
extern struct numtl_s *NuLightAddMat;
extern struct numtl_s *NuLightAddMat2;
extern struct numtl_s *NuLgtGreyNZMat;
extern struct numtl_s *NuLgtSurf[18];
extern int NuLgtHSurf[18];
extern u32 curlightcol;
extern struct nuvec_s NuRndrShadBox[2];
extern int txupload_on;
extern struct nucamera_s global_camera;
extern struct nutexslot_s *D_0062EBEC;
#define TexList D_0062EBEC

extern struct rndrstream_s *rndrstream_system;
extern struct rndrstream_s *rndrstream_ot_sort;
extern struct rndrstream_s *rndrstream_fx;
extern struct rndrstream_s *rndrstream_ptl;
extern u8 *rndrstream_free;
extern struct numtx_s numtx_identity;
extern NuRndrShadType NuRndrShadPolDat[];
extern int NuRndrShadowCnt;
extern struct nurndrwaterrip_s NuRndrWaterRipDat[];
extern int NuRndrWaterRipCnt;
extern struct nurndrfoot_s NuRndrFootData[];
extern int shadstorecnt;
extern struct nuvec_s curlight;
extern float NuTrigTable[];
extern float NuRndrWaterLevel;
extern u32 NuRndrWaterTint;
extern struct numtl_s *NuLgtBackBuff;
extern struct rndrstream_s *rndrstream_3d_u;

void NuVec4MtxTransformVU0(struct nuvec4_s *dst, struct nuvec4_s *src, struct numtx_s *mtx);
void NuVec4ScaleVU0(struct nuvec4_s *dst, struct nuvec4_s *src, float scale);
void *vpDmaTag_Next(void *packet, uint32_t addr);
void NuRndrStreamLink(struct rndrstream_s *stream);
void NuRndrShadowDirCol(struct nuvec_s *dir, u32 col);
void NuRndrShadowStack(struct nuvec_s *light);
struct nurndrshadface_s;
void NuRndrShadowCylinderCap(struct numtx_s *mtx, struct NUCYLINDER_s *cyl, int count, struct nuvec_s *light);
void NuRndrShadowEllipse(struct numtx_s *mtx, struct NUELLIPSOID_s *ellipse, int count, struct nuvec_s *light);
void NuRndrShadowPols(struct numtx_s *mtx, struct nurndrshadface_s *face, struct nurndrshadface_s *vtx,
                      struct nuvec_s *light);
void RotateCyl(struct nuvec_s *v, float xang, float yang);
void DeRotateVectorAngles(struct nuvec_s *dir, float *xang, float *yang);

#define NW_OT_BINS 0x1000
#define NW_OT_ITEMS 0x400
#define NW_OT_TRIS 0x800 /* a batch's most, the per-triangle buckets' slots */

u32 testfootcol; /* a debug colour over every footprint's; 0 in retail */
int testflipold = 1;
struct nuvec_s lightfan[9];
struct nurndrshadstore_s shadstore[256];
static struct nuvec_s default_light_dir = { 0.0f, -0.75f, -0.75f }; /* retail's data */
struct debdma_s *testforcepart; /* an effect list over every particle group's; NULL in retail */
float testmultl = 4.0f;         /* the volume shadows' light's length over NuRndrShadowStencil's */
float waveampx = 64.0f;         /* NuRndrWater's cell corners' sway, 1/16 pixels */
float waveampy = 32.0f;

static const float otscale = 2.0f;
static NwRecord *ot_bins[NW_OT_BINS];
static int ot_batch_free;

void NuRndrOTInit(void)
{
    ot_batch_free = NW_OT_ITEMS;
    memset(ot_bins, 0, sizeof(ot_bins));
}

/* NuRndrOTInsertItem: bin depth * otscale, the latest first in its bin. */
void nw_rndr_ot_insert(NwRecord *item, float depth)
{
    int index = (int)(depth * otscale);
    index = index < 0 ? 0 : index > NW_OT_BINS - 1 ? NW_OT_BINS - 1 : index;
    item->next = ot_bins[index];
    ot_bins[index] = item;
    ot_batch_free--;
}

/* Back to front: bin 0xFFF first, each bin from its latest item, under the view and lights of the flush. */
void NuRndrOTFlush(void)
{
    if (ot_batch_free == NW_OT_ITEMS) {
        return;
    }
    for (int bin = NW_OT_BINS - 1; bin >= 0; bin--) {
        NwRecord *item = ot_bins[bin];
        while (item) {
            NwRecord *next = item->next;
            nw_rndr_latch(item);
            nw_rndr_push(rndrstream_ot_sort, item);
            item = next;
        }
        ot_bins[bin] = NULL;
    }
    ot_batch_free = NW_OT_ITEMS;
}

/* Triangles bucketed by view z (XFormVuTriZ, from the batch's end), far to near, each bucket from its latest; the batch
   enters the ordering table at its nearest triangle. Retail drops a batch over 0x800 triangles or with no item left. */
void nw_rndr_ot_batch(struct numtx_s *mtx, struct numtl_s *mtl, const NwGpuMesh *mesh, struct nutristream_s *batch)
{
    static float depth[NW_OT_TRIS];
    static uint16_t bucket[NW_OT_TRIS];
    static uint16_t first[NW_OT_BINS], next[NW_OT_TRIS];
    int count = *(int *)batch;
    if (count <= 0 || count > NW_OT_TRIS || !ot_batch_free) {
        return;
    }
    struct numtx_s caminv, local;
    NuMtxInv(&caminv, &global_camera.mtx);
    NuMtxMulH(&local, mtx, &caminv);
    uint8_t *tris = (uint8_t *)batch + 16;
    float batchz = XFormVuTriZ(&depth[count - 1], tris + (size_t)(count - 1) * 0xA0, &local, count);
    int highest = 0;
    memset(first, 0xFF, sizeof(first));
    for (int t = count - 1; t >= 0; t--) {
        int z = (int)depth[t];
        z = z < 0 ? 0 : z > NW_OT_BINS - 1 ? NW_OT_BINS - 1 : z;
        bucket[t] = (uint16_t)z;
        next[t] = first[z];
        first[z] = (uint16_t)t;
        highest = z > highest ? z : highest;
    }
    NwRecord *item = nw_rndr_record(NW_CMD_DRAW_OT, mtl, mtl->rs.mpg, 1);
    uint32_t *idx = nw_rndr_alloc((size_t)count * 3 * sizeof(*idx));
    uint32_t n = 0;
    for (int z = highest; z >= 0; z--) {
        for (uint16_t t = first[z]; t != 0xFFFF; t = next[t]) {
            for (int k = 0; k < 3; k++) {
                idx[n++] = mesh->mesh.tris[t * 3 + k];
            }
        }
    }
    item->cmd.mesh = mesh;
    item->cmd.count = n;
    item->idx = idx;
    nw_rndr_ot_insert(item, batchz);
}

/* Retail also builds the volume shadows' cylinder DMA data and the default blend-deformer weights, which a NULL
   weights array stands for here. */
void NuRndrInitEx(int streamsize)
{
    NuRndrStreamInit(streamsize);
    NuFntInit();
    NuRndrOTInit();
    NuRndrShadowDirCol(&default_light_dir, 0x10808080);
    NuRndrShadowCnt = 0;
    for (int i = 0; i < 32; i++) {
        NuRndrWaterRipDat[i].timer = 0;
    }
}

/* The volume shadows' fan of eight directions about the light, which ShadElp reads (src/gfx/fx2.c); retail also
   packs them into its DMA packet. */
u32 *NuRndrShadowLightFan(void)
{
    struct nuvec_s angles;
    DeRotateVectorAngles(&curlight, &angles.x, &angles.y);
    for (int i = 0; i < 8; i++) {
        lightfan[i].x = NuTrigTable[(i * 0x2000 + 0x4000) & 0xFFFF];
        lightfan[i].y = -NuTrigTable[(i * 0x2000) & 0xFFFF];
        lightfan[i].z = 0.0f;
        RotateCyl(&lightfan[i], angles.x, angles.y);
    }
    return NULL;
}

/* Level 0xC's volume shadows (nurndr_shadowson): retail counts them in NuLgtHSurf[2]'s colour from 0x7F and takes
   curlightcol's 0x20 off the frame where the top bit is set (ALPHA 0x42, stretched from 0.4 of the width); here the
   stencil counts (NW_CMD_VOLUMES) and the mask is grey where it is 0x80 or more. */
void NuRndrShadow(int mode)
{
    struct nuvec_s light = curlight;
    NwVuSpriteVertex *v;
    (void)mode;
    if ((testflipold & 4) || shadstorecnt == 0) {
        return;
    }
    NuRndrStreamLink(rndrstream_system);
    NuRndrSetRTarget(rndrstream_system, NuLgtHSurf[2]);
    NuRndrSetZBuffer(rndrstream_system, NuLgtHSurf[1]);
    rndrstream_free = vpDmaTag_Next(rndrstream_system->ptr.u8, 0);
    NuRndrSetXYOffset((0x800 - (nurndr_pixel_width >> 1)) << 4, (0x800 - (nurndr_pixel_height >> 1)) << 4);
    NuRndrSetScissor(0, 0, nurndr_pixel_width - 1, nurndr_pixel_height - 1);
    txupload_on = 0;
    NuRndrStreamLink(rndrstream_fx);
    NuRndrStreamSetMisc(rndrstream_fx);
    NuRndrStreamAddMtl(rndrstream_fx, NuLgtGreyNZMat);
    nw_rndr_cmd(rndrstream_fx, NW_CMD_VOLUMES)->volumes.width = (int)ceilf((float)PS2_REZ_W * 0.4f);
    rndrstream_free = vpDmaTag_Next(rndrstream_fx->ptr.u8, 0);
    light.x *= testmultl;
    light.y *= testmultl;
    light.z *= testmultl;
    NuRndrShadowStack(&light);
    NuRndrStreamLink(rndrstream_fx);
    NuRndrStreamSetMisc(rndrstream_fx);
    NuRndrStreamAddMtl(rndrstream_fx, NuLgtGreyNZMat);
    static const float none[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    for (int shadowed = 0; shadowed < 2; shadowed++) {
        const int cols = ((int)ceilf((float)PS2_REZ_W * 0.4f) + 63) >> 6;
        NwCmd *c = nw_rndr_verts(rndrstream_fx, NW_VERTS_GS, (uint32_t)cols * 6, &v);
        c->stencil = (int8_t)(shadowed ? NW_STENCIL_SHADOWED : 0);
        for (int i = 0; i < cols; i++) {
            v = nw_rndr_rect3d_quad(v, (i << 10) - 8, -8, 0x400, PS2_REZ_H << 4, none,
                                    shadowed ? (int)0x80808080 : 0, 0xFFFFFF);
        }
    }
    rndrstream_free = vpDmaTag_Next(rndrstream_fx->ptr.u8, 0);
    NuRndrStreamFlush();

    NuRndrStreamLink(rndrstream_system);
    NuRndrSetRTarget(rndrstream_system, -1);
    NuRndrSetZBuffer(rndrstream_system, -2);
    rndrstream_free = vpDmaTag_Next(rndrstream_system->ptr.u8, 0);
    NuRndrSetXYOffset((0x800 - (nurndr_pixel_width >> 1)) << 4, (0x800 - (nurndr_pixel_height >> 1)) << 4);
    NuRndrSetScissor(0, 0, nurndr_pixel_width - 1, nurndr_pixel_height - 1);
    NuRndrStreamLink(rndrstream_3d);
    NuRndrStreamSetMisc(rndrstream_3d);
    NuRndrStreamAddMtl(rndrstream_3d, NuLgtSurf[2]);
    for (int i = (int)NuRndrShadBox[0].x >> 10; i < ((int)NuRndrShadBox[1].x + 0x3FF) >> 10; i++) {
        int x[2] = { i << 10, (i + 1) << 10 };
        if ((float)x[0] < NuRndrShadBox[0].x) {
            x[0] = (int)NuRndrShadBox[0].x;
        }
        if (NuRndrShadBox[1].x < (float)x[1]) {
            x[1] = (int)NuRndrShadBox[1].x;
        }
        int y0 = (int)NuRndrShadBox[0].y, y1 = (int)NuRndrShadBox[1].y;
        const float uv[4] = { (float)x[0] * 0.0009765625f * 25.6f / NuPower2(PS2_REZ_W),
                              NuRndrShadBox[0].y / (float)(NuPower2(PS2_REZ_H) << 4),
                              (float)x[1] * 0.0009765625f * 25.6f / NuPower2(PS2_REZ_W),
                              NuRndrShadBox[1].y / (float)(NuPower2(PS2_REZ_H) << 4) };
        nw_rndr_rect3d(rndrstream_3d, x[0] - 8, y0 - 8, x[1] - x[0], y1 - y0, uv, (int)curlightcol, 0);
    }
    rndrstream_free = vpDmaTag_Next(rndrstream_3d->ptr.u8, 0);
    NuRndrStreamFlush();
}

/* A vertex of NuRndrWater's DIRECT packets: GS (x, y) at z (the port's GS Z stops at 2^23 - 1), s, t, colour. */
static void nw_water_vertex(NwVuSpriteVertex *v, int x, int y, int z, float s, float t, u32 colour)
{
    memset(v, 0, sizeof(*v));
    v->pos[0] = (float)x;
    v->pos[1] = (float)y;
    v->pos[2] = (float)(z > 0x7FFFFF ? 0x7FFFFF : z);
    v->pos[3] = 1.0f;
    v->tex[0] = s * 4096.0f;
    v->tex[1] = t * 4096.0f;
    memcpy(v->col, &colour, 4);
}

static void nw_water_stream(struct numtl_s *mtl)
{
    rndrstream_3d = rndrstream_3d_u;
    rndrstream_3d->mtl = 0;
    rndrstream_3d->tid = 0;
    NuRndrStreamLink(rndrstream_3d);
    NuRndrStreamSetMisc(rndrstream_3d);
    NuRndrStreamAddMtl(rndrstream_3d, mtl);
}

static void nw_water_target(int target)
{
    NuRndrStreamLink(rndrstream_system);
    NuRndrSetRTarget(rndrstream_system, target);
    NuRndrSetZBuffer(rndrstream_system, -2);
    rndrstream_free = vpDmaTag_Next(rndrstream_system->ptr.u8, 0);
    NuRndrSetXYOffset((0x800 - (nurndr_pixel_width >> 1)) << 4, (0x800 - (nurndr_pixel_height >> 1)) << 4);
    NuRndrSetScissor(0, 0, nurndr_pixel_width - 1, nurndr_pixel_height - 1);
}

/* The view from under the water. Only the uncalled NuRndrWaterLevelTint sets NuRndrWaterLevel, so this runs at most
   once, from its .sdata 0, if the camera starts below 0.5: an alpha ramp at the water line, then the frame copied into
   NuLgtHSurf[4] in 64 x 16 cells swayed by watertab and drawn over the frame in NuRndrWaterTint. */
void NuRndrWater(void)
{
    struct nuvec4_s nearpnt, farpnt;
    NwVuSpriteVertex q[4], *v;

    if (NuRndrWaterLevel > -10000.0f && NuRndrWaterLevel > global_camera.mtx._31 - 0.5f) {
        struct numtx_s *pcs = NuCameraGetPCSMtx();
        int ang = NuAtan2D(global_camera.mtx._11, global_camera.mtx._21);
        nearpnt.x = 0.0f;
        nearpnt.y = ((NuRndrWaterLevel - global_camera.mtx._31) -
                     global_camera.nearclip * NuTrigTable[(ang + 0x4000) & 0xFFFF]) /
                    NuTrigTable[ang & 0xFFFF];
        nearpnt.z = global_camera.nearclip;
        nearpnt.w = 1.0f;
        NuVec4MtxTransformVU0(&nearpnt, &nearpnt, pcs);
        NuVec4ScaleVU0(&nearpnt, &nearpnt, 1.0f / nearpnt.w);
        if (nearpnt.y < 34944.0f) {
            farpnt.x = 0.0f;
            farpnt.y = ((NuRndrWaterLevel - global_camera.mtx._31) -
                        (global_camera.nearclip + 0.1f) * NuTrigTable[(ang + 0x4000) & 0xFFFF]) /
                       NuTrigTable[ang & 0xFFFF];
            farpnt.z = global_camera.nearclip + 0.1f;
            farpnt.w = 1.0f;
            NuVec4MtxTransformVU0(&farpnt, &farpnt, pcs);
            NuVec4ScaleVU0(&farpnt, &farpnt, 1.0f / farpnt.w);
            if (farpnt.y < nearpnt.y) {
                farpnt.x = nearpnt.y;
                nearpnt.y = farpnt.y;
                farpnt.y = farpnt.x;
            }
            if (farpnt.y > 30720.0f) {
                nw_water_target(-1);
                nw_water_stream(NuLightAddMat);
                bool ramp = farpnt.y > nearpnt.y;
                nw_rndr_verts(rndrstream_3d, NW_VERTS_GS, ramp ? 12 : 6, &v);
                const int ys[3] = { (int)nearpnt.y, (int)farpnt.y, 0x8800 };
                for (int k = ramp ? 0 : 1; k < 2; k++) {
                    for (int c = 0; c < 4; c++) {
                        nw_water_vertex(&q[c], c & 1 ? ((0x800 + (PS2_REZ_W >> 1)) << 4) + 8
                                                     : ((0x800 - (PS2_REZ_W >> 1)) << 4) - 8,
                                        ys[k + (c >> 1)], 0xFFFFFF, 0.0f, 0.0f,
                                        k + (c >> 1) ? 0x80000000u : 0);
                    }
                    v = nw_rndr_quad(v, q);
                }
                rndrstream_free = vpDmaTag_Next(rndrstream_3d->ptr.u8, 0);
                rndrstream_3d = rndrstream_3d_u;
                rndrstream_3d->mtl = 0;
                rndrstream_3d->tid = 0;
                NuRndrStreamFlush();
            }
            nw_water_target(NuLgtHSurf[4]);
            NuRndrStreamLink(rndrstream_3d);
            NuRndrStreamSetMisc(rndrstream_3d);
            NuRndrStreamAddMtl(rndrstream_3d, NuLgtBackBuff);
            const int xoffset = (PS2_VCNTR_X - (nurndr_pixel_width >> 1)) << 4;
            const int yoffset = (PS2_VCNTR_Y - (nurndr_pixel_height >> 1)) << 4;
            const int cols = (PS2_REZ_W + 63) >> 6;
            const float p2w = (float)NuPower2(PS2_REZ_W);
            nw_rndr_verts(rndrstream_3d, NW_VERTS_GS, (uint32_t)cols * 14 * 6, &v);
            for (int i = 0; i < cols; i++) {
                for (int j = 0; j < 14; j++) {
                    for (int k = 0; k < 4; k++) {
                        const int col = (i + (k & 1)) % 40;
                        const struct nurndrscreenvtx_s *w = &watertab[j + (k >> 1)][col <= 20 ? col : 40 - col];
                        int dx = (int)(NuTrigTable[w->x & 0xFFFF] * waveampx) - 8;
                        int dy = (int)(NuTrigTable[(w->y + 0x4000) & 0xFFFF] * waveampy) - 8;
                        nw_water_vertex(&q[k], ((i + (k & 1)) << 10) + dx + xoffset,
                                        ((j + (k >> 1)) << 8) + dy + yoffset, 0xFFFFFF,
                                        (float)(i + (k & 1)) * 64.0f / p2w,
                                        (float)((j + (k >> 1)) << 4) / 256.0f, 0x80808080u);
                    }
                    v = nw_rndr_quad(v, q);
                }
            }
            rndrstream_free = vpDmaTag_Next(rndrstream_3d->ptr.u8, 0);
            NuRndrStreamFlush();
            for (int i = 0; i < 21; i++) {
                for (int j = 0; j < 15; j++) {
                    watertab[j][i].x += watertab[j][i].u;
                    watertab[j][i].y += watertab[j][i].v;
                }
            }
            nw_water_target(-1);
            NuRndrStreamLink(rndrstream_3d);
            NuRndrStreamSetMisc(rndrstream_3d);
            NuRndrStreamAddMtl(rndrstream_3d, farpnt.y > 30720.0f ? NuLgtSurf[12] : NuLgtSurf[15]);
            const float uv[4] = { 4.0f / NuPower2(PS2_REZ_W), 0.015625f,
                                  (float)(PS2_REZ_W - 4) / NuPower2(PS2_REZ_W), 0.859375f };
            nw_rndr_rect3d(rndrstream_3d, -8, -8, PS2_REZ_W << 4, nurndr_pixel_height << 4, uv, (int)NuRndrWaterTint,
                           0x1FFFFFF);
            rndrstream_free = vpDmaTag_Next(rndrstream_3d->ptr.u8, 0);
            NuRndrStreamFlush();
        }
        NuRndrWaterLevel = -10000.0f;
    }
}

/* The store's volumes along the light (fx2's programs, src/gfx/fx2.c) in NuLightAddMat2 under the identity, in a
   viewport 0.4 of the frame's width (4096 / 16 pixels at 640), which squeezes them; then the store is empty. */
void NuRndrShadowStack(struct nuvec_s *light)
{
    NuVpSetSize((float)(PS2_REZ_W << 4) * 0.4f, (float)(PS2_REZ_H << 4));
    NuCameraSet(&global_camera);
    NuRndrStreamLink(rndrstream_fx);
    NuRndrStreamSetMisc(rndrstream_fx);
    NuRndrStreamAddMtl(rndrstream_fx, NuLightAddMat2);
    NuRndrSetLocalMtx(&numtx_identity, 2);
    NuRndrStreamSetViewMtxScissorClip(rndrstream_fx);
    NuRndrStreamSetLocalMtx(rndrstream_fx);
    NuRndrStreamFxMPG(1);
    for (int i = 0; i < shadstorecnt; i++) {
        struct nurndrshadstore_s *store = &shadstore[i];
        if (store->shadow.ncylinders) {
            NuRndrShadowCylinderCap(&store->mtx, store->shadow.cylinders, store->shadow.ncylinders, light);
        }
        if (store->shadow.nellipsoids) {
            NuRndrShadowEllipse(&store->mtx, store->shadow.ellipsoids, store->shadow.nellipsoids, light);
        }
        struct NUSHADOWMESH_s *mesh = store->shadow.shadow_meshes;
        if (mesh) {
            NuRndrShadowPols(&store->mtx, (struct nurndrshadface_s *)mesh->normals,
                             (struct nurndrshadface_s *)mesh->verts, light);
        }
    }
    NuRndrStreamFxMPG(0);
    rndrstream_free = vpDmaTag_Next(rndrstream_fx->ptr.u8, 0);
    NuVpSetSize((float)(PS2_REZ_W << 4), (float)(PS2_REZ_H << 4));
    NuCameraSet(&global_camera);
    shadstorecnt = 0;
}

static NwCmd *nw_target;    /* the TARGET record NuRndrBeginSceneEx's NuRndrSetZBuffer completes */
static int nw_zbuffer = -2; /* the last NuRndrSetZBuffer */

/* The back buffer (-1), front buffer (-3) or a render target texture, which sets nurndr_pixel_width and height; -2 and
   -4 put the Z buffer behind the frame, which the port does not draw, so their draws go nowhere. */
void NuRndrSetRTarget(struct rndrstream_s *stream, int target)
{
    NwSurf surf = { 0, 0, 0, false };
    int width = PS2_REZ_W, height = PS2_REZ_H;
    if (target > 0) {
        const struct nutexslot_s *e = &TexList[target - 1];
        if (!e->valid) {
            return;
        }
        surf = nw_ps2tx_surf(target);
        width = e->width;
        height = e->height;
    } else if (target == -2 || target == -4) {
        static bool reported;
        if (!reported) {
            reported = true;
            fprintf(stderr, NW_APP_NAME ": the Z buffer as a target is not drawn\n");
        }
    } else {
        surf = nw_ps2tx_surf(target == -3 ? NW_SURF_FRONT : NW_SURF_BACK);
    }
    nurndr_pixel_width = width;
    nurndr_pixel_height = height;
    nurndr_pixel_p2w = Ps2TxValToPowerOfTwo(width);
    nurndr_pixel_p2h = Ps2TxValToPowerOfTwo(height);
    NwCmd *c = nw_target = nw_rndr_cmd(stream, NW_CMD_TARGET);
    c->target.surf = surf;
    c->target.width = width;
    c->target.height = height;
    c->target.zback = nw_zbuffer == -2;
}

/* -2 shares the back buffer's Z when the target is its size, else the target has its own; a render target as Z buffer
   (zbuffer > 0) is only ever the volume shadows' NuLgtHSurf[1], which NW_CMD_VOLUMES fills. */
void NuRndrSetZBuffer(struct rndrstream_s *stream, int zbuffer)
{
    (void)stream;
    nw_zbuffer = zbuffer;
    if (nw_target) {
        nw_target->target.zback = zbuffer == -2;
        nw_target = NULL;
    }
}

/* halfoffset adds nurndr_half_offset, which is 0 in retail. */
int NuRndrBeginSceneEx(int target, int zbuffer, int halfoffset)
{
    (void)halfoffset;
    NuRndrSetRTarget(rndrstream_system, target);
    NuRndrSetZBuffer(rndrstream_system, zbuffer);
    NuRndrSetXYOffset((0x800 - (nurndr_pixel_width >> 1)) << 4, (0x800 - (nurndr_pixel_height >> 1)) << 4);
    NuRndrSetScissor(0, 0, nurndr_pixel_width - 1, nurndr_pixel_height - 1);
    return 1;
}

int NuRndrBeginScene(int target)
{
    return NuRndrBeginSceneEx(target, -2, target == -1);
}

void NuRndrFlush(int flags)
{
    NuRndrEndSceneEx(flags);
}

/* The flush, then a CLEAR at the start of the system stream, which sorts first: colour (flags & 3 = 1), Z (2) or
   both over the scissor at z = (1 - depth) * (2^23 - 1); flags & 4 flushes the streams alone. */
void NuRndrClear(int flags, int colour, float depth)
{
    if (flags & 4) {
        NuRndrStreamFlush();
    } else {
        NuMtlBuildRenderList();
        NuRndrOTFlush();
        NuRndrStreamFlush();
    }
    NwCmd *c = nw_rndr_cmd(rndrstream_system, NW_CMD_CLEAR);
    memcpy(c->clear.rgba, &colour, 4);
    c->clear.z = (uint32_t)(int)((1.0f - depth) * 8388607.0f);
    c->clear.mask = (flags & 3) == 1 || (flags & 3) == 2 ? flags & 3 : 3;
}

/* A sprite in the 2D stream under mtl (numtl_defaultmtl2d without one) from (x, y) in 12.4 pixels of a width x
   height screen, centred on PS2_VCNTR as the GS sees it. */
static void nw_rndr_rect(int x, int y, int width, int height, int colour, struct numtl_s *mtl, const float st[4],
                         int screen_w, int screen_h)
{
    x += (PS2_VCNTR_X - (screen_w >> 1)) << 4;
    y += (PS2_VCNTR_Y - (screen_h >> 1)) << 4;
    NuRndrStreamAddMtl(rndrstream_2d, mtl ? mtl : numtl_defaultmtl2d);
    NwCmd *c = nw_rndr_cmd(rndrstream_2d, NW_CMD_RECT);
    c->rect.xy[0] = x;
    c->rect.xy[1] = y;
    c->rect.xy[2] = x + width;
    c->rect.xy[3] = y + height;
    memcpy(c->rect.st, st, sizeof(c->rect.st));
    memcpy(c->rect.rgba, &colour, 4);
}

/* A rect short of the screen's width is laid out on retail's 640-wide screen, at a wider one's centre. */
void NuRndrRect2di(int x, int y, int width, int height, int colour, struct numtl_s *mtl)
{
    static const float st[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
    nw_rndr_rect(x, y, width, height, colour, mtl, st, x <= 0 && x + width >= PS2_REZ_W << 4 ? PS2_REZ_W : 640,
                 PS2_REZ_H);
}

/* Centred on the render target, where NuRndrRect2di centres on the screen. */
void NuRndrRectUV2di(int x, int y, int width, int height, float u0, float v0, float u1, float v1, int colour,
                     struct numtl_s *mtl)
{
    const float st[4] = { u0, v0, u1, v1 };
    nw_rndr_rect(x, y, width, height, colour, mtl, st, nurndr_pixel_width, nurndr_pixel_height);
}

NwVuSpriteVertex *nw_rndr_rect3d_quad(NwVuSpriteVertex *v, int x, int y, int width, int height, const float uv[4],
                                      int colour, int z)
{
    NwVuSpriteVertex q[4];
    x += (PS2_VCNTR_X - (nurndr_pixel_width >> 1)) << 4;
    y += (PS2_VCNTR_Y - (nurndr_pixel_height >> 1)) << 4;
    z = z < 0 ? 0 : z > 0x7FFFFF ? 0x7FFFFF : z;
    memset(q, 0, sizeof(q));
    for (int k = 0; k < 4; k++) {
        q[k].pos[0] = (float)(k & 1 ? x + width : x);
        q[k].pos[1] = (float)(k & 2 ? y + height : y);
        q[k].pos[2] = (float)z;
        q[k].pos[3] = 1.0f;
        q[k].tex[0] = uv[k & 1 ? 2 : 0] * 4096.0f;
        q[k].tex[1] = uv[k & 2 ? 3 : 1] * 4096.0f;
        memcpy(q[k].col, &colour, 4);
    }
    return nw_rndr_quad(v, q);
}

NwCmd *nw_rndr_rect3d(struct rndrstream_s *stream, int x, int y, int width, int height, const float uv[4],
                      int colour, int z)
{
    NwVuSpriteVertex *v;
    NwCmd *c = nw_rndr_verts(stream, NW_VERTS_GS, 6, &v);
    nw_rndr_rect3d_quad(v, x, y, width, height, uv, colour, z);
    return c;
}

void NuRndrRectUV3di(int x, int y, int width, int height, float u0, float v0, float u1, float v1, int colour, int z)
{
    const float uv[4] = { u0, v0, u1, v1 };
    nw_rndr_rect3d(rndrstream_3d, x, y, width, height, uv, colour, z);
}

void NuRndrRectUVStream(struct rndrstream_s *stream, int x, int y, int width, int height, float u0, float v0, float u1,
                        float v1, int colour, int z)
{
    const float uv[4] = { u0, v0, u1, v1 };
    nw_rndr_rect3d(stream, x, y, width, height, uv, colour, z);
}

/* Target -3 swaps without counting a frame; of retail's frame counters only the one main.c reads is kept. A new
   picture aspect starts with the next frame. */
int NuRndrSwapScreen(int target)
{
    NuPs2DisableAutoScreenSwap();
    int swapped = NuPs2ScreenSwap(target != -3);
    NuRndrVu1Draw();
    NuRndrSwapStreamBuffers();
    nurndr_tritot_this_frame = 0;
    if (target != -3) {
        nw_wide_poll();
    }
    return swapped;
}

/* Each sub-object in view draws its geoms (each material of the mtl->next chain once) and face-on geoms; returns
   retail's clip state: 0 out of view, 1 in view, 2 partly. */
int NuRndrGScnObj(struct nugobj_s *gobj, struct numtx_s *wm)
{
    struct numtx_s mtx;
    struct numtx_s offset;
    int clip = -1;
    int n = gobj->ngobjs;
    int instanced = n > 1;
    NwInterpScope scope = nw_interp_begin(wm, gobj);
    while (n-- != 0) {
        struct numtx_s *m = wm;
        if (instanced) {
            NuMtxSetIdentity(&offset);
            offset._30 = gobj->origin.x;
            offset._31 = gobj->origin.y;
            offset._32 = gobj->origin.z;
            NuMtxMul(&mtx, &offset, wm);
            m = &mtx;
        }
        int vis = NuCameraClipTestExtents(&gobj->min, &gobj->max, m);
        if (clip == -1) {
            clip = vis;
        } else if ((clip == 1 && vis != 1) || (clip == 0 && vis != 0)) {
            clip = 2;
        }
        if (vis) {
            nurndr_tritot_this_frame += gobj->tricount;
            NuRndrSetLocalMtx(m, vis);
            uint32_t g = 0;
            for (struct nugeom_s *geom = gobj->geom; geom; geom = geom->next, g++) {
                struct numtl_s *mtl = nurndr_forced_mtl ? nurndr_forced_mtl : geom->mtl;
                if (nurndr_forced_mtl_table && mtl->special_id && nurndr_forced_mtl_table[mtl->special_id]) {
                    mtl = nurndr_forced_mtl_table[mtl->special_id];
                }
                for (uint32_t pass = 0; mtl; mtl = mtl->next, pass++) {
                    nw_interp_at((uint32_t)n, g, pass);
                    rndrstream_3d = &mtl->rs;
                    if (geom->tristream) {
                        nw_rndr_ot_batch(m, mtl, geom->dmastream, geom->tristream);
                    } else if (geom->dmastream) {
                        NwCmd *c = nw_rndr_cmd(rndrstream_3d, NW_CMD_DRAW);
                        c->mesh = geom->dmastream;
                    }
                }
            }
            g = 0x10000;
            for (struct nufaceongeom_s *fgeom = gobj->faceon_geom; fgeom; fgeom = fgeom->next, g++) {
                nw_interp_at((uint32_t)n, g, 0);
                rndrstream_3d = &fgeom->mtl->rs;
                if (fgeom->dmastream) {
                    NwCmd *c = nw_rndr_cmd(rndrstream_3d, NW_CMD_SPRITES);
                    c->sprites = fgeom->dmastream;
                }
            }
        }
        gobj++;
    }
    nw_interp_end(scope);
    return clip;
}

/* A gobj chain (an hgobj's joint), as NuRndrGScnObj but along next_gobj, never through the ordering table, and
   with the forced material table left out under a forced material. */
int NuRndrGobj(struct nugobj_s *gobj, struct numtx_s *wm)
{
    struct numtx_s mtx;
    struct numtx_s offset;
    int clip = -1;
    int instanced = gobj->next_gobj != 0;
    NwInterpScope scope = nw_interp_begin(wm, gobj);
    for (uint32_t link = 0; gobj; gobj = gobj->next_gobj, link++) {
        struct numtx_s *m = wm;
        if (instanced) {
            NuMtxSetIdentity(&offset);
            offset._30 = gobj->origin.x;
            offset._31 = gobj->origin.y;
            offset._32 = gobj->origin.z;
            NuMtxMul(&mtx, &offset, wm);
            m = &mtx;
        }
        int vis = NuCameraClipTestExtents(&gobj->min, &gobj->max, m);
        if (clip == -1) {
            clip = vis;
        } else if ((clip == 1 && vis != 1) || (clip == 0 && vis != 0)) {
            clip = 2;
        }
        if (!vis) {
            continue;
        }
        NuRndrSetLocalMtx(m, vis);
        uint32_t g = 0;
        for (struct nugeom_s *geom = gobj->geom; geom; geom = geom->next, g++) {
            struct numtl_s *mtl = geom->mtl;
            if (mtl && nurndr_forced_mtl) {
                mtl = nurndr_forced_mtl;
            } else if (mtl && nurndr_forced_mtl_table && mtl->special_id && nurndr_forced_mtl_table[mtl->special_id]) {
                mtl = nurndr_forced_mtl_table[mtl->special_id];
            }
            for (uint32_t pass = 0; mtl && geom->dmastream; mtl = mtl->next, pass++) {
                nw_interp_at(link, g, pass);
                rndrstream_3d = &mtl->rs;
                nw_rndr_cmd(rndrstream_3d, NW_CMD_DRAW)->mesh = geom->dmastream;
            }
        }
        g = 0x10000;
        for (struct nufaceongeom_s *fgeom = gobj->faceon_geom; fgeom; fgeom = fgeom->next, g++) {
            nw_interp_at(link, g, 0);
            rndrstream_3d = &fgeom->mtl->rs;
            if (fgeom->dmastream) {
                nw_rndr_cmd(rndrstream_3d, NW_CMD_SPRITES)->sprites = fgeom->dmastream;
            }
        }
    }
    nw_interp_end(scope);
    return clip;
}

/* Each material pass a SKIN record with NuHGobjRndrMtxDwa's palette and weights (NULL: none); like retail it skips
   next_gobj and ignores njoints and joints. skinskipix is -1 in retail and nothing sets it. */
int NuRndrGobjSkin(struct nugobj_s *gobj, struct numtx_s *wm, int njoints, struct numtx_s *joints, u32 *weights,
                   int vis)
{
    (void)njoints;
    (void)joints;
    if (!vis) {
        return vis;
    }
    NuRndrSetLocalMtx(wm, vis);
    nw_rndr_blend(weights);
    NwInterpScope scope = nw_interp_begin(wm, gobj);
    uint32_t g = 0;
    for (struct nugeom_s *geom = gobj->geom; geom; geom = geom->next, g++) {
        struct numtl_s *mtl = nurndr_forced_mtl ? nurndr_forced_mtl : geom->mtl;
        if (mtl && nurndr_forced_mtl_table && mtl->special_id && nurndr_forced_mtl_table[mtl->special_id]) {
            mtl = nurndr_forced_mtl_table[mtl->special_id];
        }
        for (uint32_t pass = 0; mtl && geom->skindma; mtl = mtl->next, pass++) {
            nw_interp_at(g, pass, 0);
            rndrstream_3d = &mtl->rs;
            nw_rndr_cmd(rndrstream_3d, NW_CMD_SKIN)->skin = (const NwGpuSkin *)geom->skindma;
        }
    }
    nw_interp_end(scope);
    return vis;
}

/* Each joint's shadow volumes into the store under the joint's matrix times wm, each layer's joint gobjs under that
   matrix, then its skin objects under wm with the palette INV_WT x mtx_array, dwa[ix] the layer's blend-shape
   weights. hgobj_enabled is 1 in retail and nothing clears it. */
int NuHGobjRndrMtxDwa(struct NUHGOBJ_s *hobj, struct numtx_s *wm, int nlayers, short *layer, struct numtx_s *mtx_array,
                      float **dwa)
{
    struct numtx_s mtx;
    int rv = 0;
    NwInterpScope scope = nw_interp_begin(wm, hobj);
    int vis = NuCameraClipTestExtents(&hobj->bbox_min, &hobj->bbox_max, wm);
    if (vis == 0) {
        nw_interp_end(scope);
        return 0;
    }
    if (hobj->shadow && (u8)hobj->pad81[0] == 0) {
        for (struct NUSHADOWDATA_s *shadow = hobj->shadow; shadow->joint != 0xFF; shadow++) {
            if (shadstorecnt < 256) {
                struct nurndrshadstore_s *s = &shadstore[shadstorecnt++];
                NuMtxMulVU0(&s->mtx, &mtx_array[shadow->joint], wm);
                s->shadow.ellipsoids = shadow->ellipsoids;
                s->shadow.cylinders = shadow->cylinders;
                s->shadow.shadow_meshes = shadow->nshadow_meshes ? shadow->shadow_meshes : NULL;
                s->shadow.nellipsoids = shadow->nellipsoids;
                s->shadow.ncylinders = shadow->ncylinders;
                s->shadow.nshadow_meshes = shadow->nshadow_meshes;
            }
        }
    }
    for (int i = 0; i < nlayers; i++) {
        int ix = 0;
        if (layer) {
            ix = layer[i];
        } else {
            nlayers = 1;
        }
        if (ix < 0 || ix >= hobj->num_layers) {
            continue;
        }
        float *weights = dwa ? dwa[ix] : 0;
        struct NULAYERDATA_s *l = &hobj->layers[ix];
        for (int set = 0; set < 2; set++) {
            struct nugobj_s **gobjs = set ? l->blend_gobjs : l->gobjs;
            for (int j = 0; gobjs && j < hobj->num_joints; j++) {
                if (gobjs[j]) {
                    nw_interp_at((uint32_t)ix, (uint32_t)set, (uint32_t)j);
                    NuMtxMulVU0(&mtx, &mtx_array[j], wm);
                    rv |= NuRndrGobj(gobjs[j], &mtx);
                }
            }
        }
        if (l->skin_gobj || l->blend_skin_gobj) {
            NuMtxMulArrayVU0(nw_rndr_joints(hobj->num_joints), hobj->INV_WT, mtx_array, hobj->num_joints);
            if (l->skin_gobj) {
                nw_interp_at((uint32_t)ix, 2, 0);
                rv |= NuRndrGobjSkin(l->skin_gobj, wm, hobj->num_joints, mtx_array, (u32 *)weights, vis);
            }
            if (l->blend_skin_gobj) {
                nw_interp_at((uint32_t)ix, 3, 0);
                rv |= NuRndrGobjSkin(l->blend_skin_gobj, wm, hobj->num_joints, mtx_array, (u32 *)weights, vis);
            }
        }
    }
    nw_interp_end(scope);
    return rv;
}

void NuHGobjRndr(void *hobj, struct numtx_s *mtx, int nlayers, short *layer)
{
    static struct numtx_s joints[256]; /* hgobj_joint_mtx */
    NuHGobjEval(hobj, 0, 0, joints);
    NuHGobjRndrMtxDwa(hobj, mtx, nlayers, layer, joints, 0);
}

/* A quad of the fx streams as fx2 draws it, s and t x4096 truncated as retail sends them. */
static NwVuSpriteVertex *nw_rndr_tc1_quad(NwVuSpriteVertex *v, const struct nuvtx_tc1_s q[4])
{
    NwVuSpriteVertex c[4];
    memset(c, 0, sizeof(c));
    for (int k = 0; k < 4; k++) {
        c[k].pos[0] = q[k].pnt.x;
        c[k].pos[1] = q[k].pnt.y;
        c[k].pos[2] = q[k].pnt.z;
        c[k].tex[0] = (float)(int)(q[k].tc[0] * 4096.0f);
        c[k].tex[1] = (float)(int)(q[k].tc[1] * 4096.0f);
        memcpy(c[k].col, &q[k].diffuse, 4);
    }
    return nw_rndr_quad(v, c);
}

/* Retail loops forever on a count that is not a multiple of four. */
static NwCmd *nw_rndr_quads(struct rndrstream_s *stream, const struct nuvtx_tc1_s *verts, int count)
{
    NwVuSpriteVertex *v;
    NwCmd *c = nw_rndr_verts(stream, NW_VERTS_TRIS, (uint32_t)(count / 4) * 6, &v);
    for (int i = 0; i + 4 <= count; i += 4) {
        v = nw_rndr_tc1_quad(v, &verts[i]);
    }
    return c;
}

int NuRndrStreamQuad3dClip(struct nuvtx_tc1_s *verts, int count, void *mtx, struct rndrstream_s *stream)
{
    (void)mtx;
    nw_rndr_quads(stream, verts, count);
    return 1;
}

/* A strip under mtx (the identity for NULL) through the stream's program, which retail sends in batches of 42 that
   each start the strip over; the normal goes as the tri programs read it, x in w and y, z x4096 after s and t. */
NwCmd *nw_rndr_strip(struct nuvtx_tc1_s *verts, int count, struct numtx_s *mtx, struct rndrstream_s *stream)
{
    NwVuSpriteVertex *v;
    uint32_t n = 0;
    for (int b = 0; b < count; b += 42) {
        int batch = count - b < 42 ? count - b : 42;
        n += batch >= 3 ? (uint32_t)(batch - 2) * 3 : 0;
    }
    NuRndrSetLocalMtx(mtx ? mtx : &numtx_identity, 2);
    NwInterpScope scope = mtx ? nw_interp_begin(mtx, stream->mtl) : (NwInterpScope){ 0 };
    NwCmd *c = nw_rndr_verts(stream, NW_VERTS_TRIS, n, &v);
    if (mtx) {
        nw_interp_end(scope);
    }
    for (int b = 0; b < count; b += 42) {
        for (int i = b + 2; i < b + 42 && i < count; i++) {
            for (int k = i - 2; k <= i; k++, v++) {
                const struct nuvtx_tc1_s *s = &verts[k];
                memset(v, 0, sizeof(*v));
                v->pos[0] = s->pnt.x;
                v->pos[1] = s->pnt.y;
                v->pos[2] = s->pnt.z;
                v->pos[3] = s->nrm.x;
                v->tex[0] = (float)(int)(s->tc[0] * 4096.0f);
                v->tex[1] = (float)(int)(s->tc[1] * 4096.0f);
                v->tex[2] = (float)(int)(s->nrm.y * 4096.0f);
                v->tex[3] = (float)(int)(s->nrm.z * 4096.0f);
                v->col[0] = (uint8_t)s->rgba.r;
                v->col[1] = (uint8_t)s->rgba.g;
                v->col[2] = (uint8_t)s->rgba.b;
                v->col[3] = (uint8_t)s->rgba.a;
            }
        }
    }
    return c;
}

int NuRndrStreamTriStrip3dClip(struct nuvtx_tc1_s *verts, int count, struct numtx_s *mtx, struct rndrstream_s *stream)
{
    nw_rndr_strip(verts, count, mtx, stream);
    return 1;
}

/* NuRndrShadPolys' quads: each shadow's square turned by y, z then x at its point, black with the shade as alpha. */
int NuRndrStreamQuadShad(struct rndrstream_s *stream)
{
    struct nuvtx_tc1_s q[4];
    struct numtx_s m;
    NwVuSpriteVertex *v;
    memset(q, 0, sizeof(q));
    nw_rndr_verts(stream, NW_VERTS_TRIS, (uint32_t)NuRndrShadowCnt * 6, &v);
    for (int i = 0; i < NuRndrShadowCnt; i++) {
        NuRndrShadType *s = &NuRndrShadPolDat[i];
        NuMtxSetRotationY(&m, s->yrot);
        NuMtxRotateZ(&m, s->zrot);
        NuMtxRotateX(&m, s->xrot);
        for (int k = 0; k < 4; k++) {
            struct nuvec_s offset = { k & 1 ? s->size : -s->size, 0.0f, k & 2 ? s->size : -s->size };
            NuVecMtxRotate(&q[k].pnt, &offset, &m);
            q[k].pnt.x += s->pos.x;
            q[k].pnt.y += s->pos.y;
            q[k].pnt.z += s->pos.z;
            q[k].tc[0] = (float)(k & 1);
            q[k].tc[1] = (float)(k >> 1);
            q[k].diffuse = (int)((u32)s->shade << 24);
        }
        v = nw_rndr_tc1_quad(v, q);
    }
    return 1;
}

/* NuRndrWaterRip's quads: a square of the ripple's size about its point in x and z, in its current colour, over
   part of the texture. */
int NuRndrStreamQuadWRip(struct rndrstream_s *stream, short *visible)
{
    struct nuvtx_tc1_s q[4];
    NwVuSpriteVertex *v;
    memset(q, 0, sizeof(q));
    nw_rndr_verts(stream, NW_VERTS_TRIS, (uint32_t)NuRndrWaterRipCnt * 6, &v);
    for (int i = 0; i < NuRndrWaterRipCnt; i++) {
        const struct nurndrwaterrip_s *r = &NuRndrWaterRipDat[visible[i]];
        for (int k = 0; k < 4; k++) {
            q[k].pnt.x = k & 1 ? r->pos.x + r->size : r->pos.x - r->size;
            q[k].pnt.y = r->pos.y;
            q[k].pnt.z = k & 2 ? r->pos.z + r->size : r->pos.z - r->size;
            q[k].tc[0] = (k & 1 ? 0xFF0 : 0xC00) / 4096.0f;
            q[k].tc[1] = (k & 2 ? 0x7F0 : 0x400) / 4096.0f;
            q[k].diffuse = (int)r->curcolour;
        }
        v = nw_rndr_tc1_quad(v, q);
    }
    return 1;
}

/* Retail writes ALPHA_1 0x44, (Cs - Cd) * As + Cd, into the fx stream over the material's before the quads. */
void NuRndrFootPrints(struct numtl_s *mtl, float *u, float *v)
{
    struct nuvtx_tc1_s vtx[64 * 4];
    struct nuvtx_tc1_s *vp = vtx;
    for (int i = 0; i < 64; i++) {
        struct nurndrfoot_s *foot = &NuRndrFootData[i];
        if (foot->timer <= 0) {
            continue;
        }
        if (testfootcol != 0) {
            foot->colour = testfootcol;
        }
        if (foot->timer < 16) {
            foot->timer--;
        }
        int gfx = foot->gfx;
        u32 colour = ((u32)((foot->brightness * foot->timer) >> 4) << 24) + foot->colour;
        for (int k = 0; k < 4; k++, vp++) {
            vp->pnt = foot->pnts[k];
            vp->tc[0] = u[gfx + k];
            vp->tc[1] = v[gfx + k];
            vp->diffuse = (int)colour;
        }
    }
    if (vp == vtx) {
        return;
    }
    NuRndrStreamLink(rndrstream_fx);
    NuRndrStreamSetMisc(rndrstream_fx);
    NuRndrStreamAddMtl(rndrstream_fx, mtl);
    NuRndrSetLocalMtx(&numtx_identity, 2);
    NuRndrStreamSetViewMtxScissorClip(rndrstream_fx);
    NuRndrStreamSetLocalMtx(rndrstream_fx);
    NuRndrStreamFxMPG(1);
    nw_rndr_quads(rndrstream_fx, vtx, (int)(vp - vtx))->alpha = 0x100 | 0x44;
    rndrstream_free = vpDmaTag_Next(rndrstream_fx->ptr.u8, 0);
}

/* nurndr.c's; vehicle.c's trail_s and jeep.c's jeeptrail_s have its layout. */
struct nurndrtrail_s {
    struct nuvec_s pos1;
    struct nuvec_s pos2;
    int intensity;
    int RealIntensity;
};

/* Retail's trail quads, untextured with ALPHA_1 0x44: two ribbons per segment, from pos1 and pos2 a quarter of the way
   in, white fading inwards (dark for a negative intensity). The 0xFFFFF is retail's. */
void NuRndrTrail(int trailpt, struct nurndrtrail_s *trail, int trailcount)
{
    static struct nuvtx_tc1_s vtx[2048];
    int cnt = 0, last = -1, start = trailpt + (1 - trailcount);
    int col = 0, lastcol, begin = 0;
    memset(vtx, 0, sizeof(vtx));
    for (int loop = trailpt; loop > start; loop--) {
        int pt = loop & (trailcount - 1);
        if (trail[pt].pos1.x == -10000.0f) {
            last = -1;
            continue;
        }
        if (last == -1) {
            last = pt;
            col = 0;
            begin = 1;
            continue;
        }
        int next = (loop - 1) & (trailcount - 1);
        int next2 = (loop - 2) & (trailcount - 1);
        lastcol = col;
        if (trail[next].pos1.x == -10000.0f || loop - 1 <= start) {
            col = 0;
        } else if (trail[next2].pos1.x == -10000.0f || begin) {
            col = ((trail[pt].intensity >> 24) / 2) << 24;
        } else {
            col = trail[pt].intensity;
        }
        begin = 0;
        for (int side = 0; side < 2; side++, cnt += 4) {
            const struct nuvec_s *a0 = side ? &trail[last].pos2 : &trail[last].pos1;
            const struct nuvec_s *a1 = side ? &trail[last].pos1 : &trail[last].pos2;
            const struct nuvec_s *b0 = side ? &trail[pt].pos2 : &trail[pt].pos1;
            const struct nuvec_s *b1 = side ? &trail[pt].pos1 : &trail[pt].pos2;
            vtx[cnt].pnt = *a0;
            vtx[cnt + 1].pnt = *b0;
            vtx[cnt + 2].pnt.x = (a1->x - a0->x) * 0.25f + a0->x;
            vtx[cnt + 2].pnt.y = (a1->y - a0->y) * 0.25f + a0->y;
            vtx[cnt + 2].pnt.z = (a1->z - a0->z) * 0.25f + a0->z;
            vtx[cnt + 3].pnt.x = (b1->x - b0->x) * 0.25f + b0->x;
            vtx[cnt + 3].pnt.y = (b1->y - b0->y) * 0.25f + b0->y;
            vtx[cnt + 3].pnt.z = (b1->z - b0->z) * 0.25f + b0->z;
            if (side == 0) {
                vtx[cnt].diffuse = lastcol < 0 ? -lastcol : lastcol + 0xFFFFFF;
                vtx[cnt + 2].diffuse = lastcol < 0 ? 0 : 0xFFFFFF;
                vtx[cnt + 1].diffuse = col < 0 ? -col : col + 0xFFFFFF;
                vtx[cnt + 3].diffuse = col < 0 ? 0 : 0xFFFFFF;
            } else {
                vtx[cnt].diffuse = lastcol < 0 ? 0xFFFFF - lastcol : lastcol;
                vtx[cnt + 2].diffuse = lastcol < 0 ? 0xFFFFFF : 0;
                vtx[cnt + 1].diffuse = col < 0 ? 0xFFFFFF - col : col;
                vtx[cnt + 3].diffuse = col < 0 ? 0xFFFFFF : 0;
            }
        }
        last = pt;
    }
    if (cnt == 0) {
        return;
    }
    NuRndrStreamLink(rndrstream_fx);
    NuRndrStreamSetMisc(rndrstream_fx);
    NuRndrStreamAddMtl(rndrstream_fx, NuLightAddMat);
    NuRndrSetLocalMtx(&numtx_identity, 2);
    NuRndrStreamSetViewMtxScissorClip(rndrstream_fx);
    NuRndrStreamSetLocalMtx(rndrstream_fx);
    NuRndrStreamFxMPG(1);
    nw_rndr_quads(rndrstream_fx, vtx, cnt)->alpha = 0x100 | 0x44;
    rndrstream_free = vpDmaTag_Next(rndrstream_fx->ptr.u8, 0);
}

_Static_assert(sizeof(NwPtl) == sizeof(struct debpart_s), "debpart_s");
_Static_assert(sizeof(((NwPtlList *)0)->tri) == sizeof(((struct debdma_s *)0)->tri), "debdma_s.tri");

/* chunk's chain of sets is linked by NEXT tags (LinkDmaParticalSets' offset), RET on the last; mode's screen clamp is
   moot under GPU clipping. */
void NuRndrParticleGroup(struct debchunk_s *chunk, struct debdma_s *list, struct numtl_s *mtl, struct numtx_s *mtx,
                         int mode, float time)
{
    NwPtlList *l;
    NwPtl *ptls;
    (void)mode;
    if (testforcepart) {
        list = testforcepart;
    }
    list->time = time;
    struct debchunk_s *sets[256];
    uint32_t n = 0;
    for (struct debchunk_s *set = chunk; set && n < 256;) {
        const struct DmaTag *tag = (const struct DmaTag *)set;
        sets[n++] = set;
        set = (tag->ctrl >> 28 & 7) == 2 ? (struct debchunk_s *)((u8 *)set + (s32)tag->addr) : NULL;
    }
    NuRndrSetLocalMtx(mtx, 1);
    NuRndrStreamAddMtl(rndrstream_ptl, mtl);
    NwInterpScope scope = nw_interp_begin(chunk, list);
    nw_rndr_particles(rndrstream_ptl, n * 32, &l, &ptls);
    nw_interp_end(scope);
    memset(l, 0, sizeof(*l));
    l->head[1] = list->grav;
    l->head[3] = list->time;
    const s32 uv[8] = { list->u0, list->v0, list->u1, list->v1, list->u2, list->v2, list->u3, list->v3 };
    memcpy(l->uv, uv, sizeof(uv));
    memcpy(l->tri, list->tri, sizeof(l->tri));
    for (uint32_t i = 0; i < n; i++) {
        memcpy(&ptls[i * 32], sets[i]->parts, sizeof(sets[i]->parts));
    }
}

/* Retail's packet: a RET tag, the S-32 unpack of the count and the weights to VU 0x63, the count at u32 [5]; the
   NuHGobjEvalDwa* write the weights from [6] on. */
u32 *NuRndrCreateBlendShapeDeformerWeightsArray(u32 count)
{
    u32 *base = NuRndrStreamAlloc((int)((count + 10) >> 2));
    base[4] = ((count + 1) << 16) | 0x60000063;
    base[5] = count;
    return base;
}
