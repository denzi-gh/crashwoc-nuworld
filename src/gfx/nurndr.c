/* nu3d/nurndr, which the port overrides whole (port/units.toml): the scene bracket and the draws, as records in
   the streams (src/gfx/nustream.c) instead of DMA packets. Every other NuRndr* is a stub, and their hits are the
   renderer's worklist. */
#include <stdio.h>
#include <string.h>

#include "app.h"
#include "gfx/rndr.h"
#include "nu3d/nucamera.h"
#include "nu3d/nugobj.h"
#include "nu3d/numtl.h"
#include "nu3d/nustream.h"
#include "nu3d/nutex.h"
#include "numath/numtx.h"

int NuPs2ScreenSwap(int vsync);
void NuPs2DisableAutoScreenSwap(void);
void NuFntInit(void);
int Ps2TxValToPowerOfTwo(int value);

struct nugscn_s;
struct nugspline_s;
int NuSplineFindAllSub(struct nugscn_s *scene, char *name, struct nugspline_s **list, int max);
float XFormVuTriZ(float *depth, void *tri, struct numtx_s *mtx, int count);

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
extern struct nucamera_s global_camera;
extern struct nutexslot_s *D_0062EBEC;
#define TexList D_0062EBEC

extern struct rndrstream_s *rndrstream_system;
extern struct rndrstream_s *rndrstream_ot_sort;

#define NW_OT_BINS 0x1000
#define NW_OT_ITEMS 0x400
#define NW_OT_TRIS 0x800 /* a batch's most, the per-triangle buckets' slots */

static const float otscale = 2.0f;
static NwRecord *ot_bins[NW_OT_BINS];
static int ot_batch_free;

/* Shadow masks: scene splines named like "stencil" (D_0062ECA8); only nurndr reads them. */
int NuRndrShadMaskCount;
struct nugspline_s *NuRndrShadMasks[32];
static char nw_stencil[] = "stencil";

void NuRndrSplineSearch(void *spline)
{
    int n = NuRndrShadMaskCount;
    NuRndrShadMaskCount += NuSplineFindAllSub(spline, nw_stencil, &NuRndrShadMasks[n], 32 - n);
}

/* Retail also resets the footprint timers (not drawn yet). */
void NuRndrInitWorld(void)
{
    NuRndrShadMaskCount = 0;
}

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

/* NuRndrOTInsertTriBatch with the geom's mesh: each triangle's view z (XFormVuTriZ, from the end of the batch)
   buckets it, the buckets drawn far to near and each from its latest triangle, so a batch is drawn in batch order
   within a bucket; the batch goes into the ordering table at its nearest triangle. Retail drops a batch over 0x800
   triangles or with the item pool used up. */
void nw_rndr_ot_batch(struct numtx_s *mtx, struct numtl_s *mtl, const NwGpuMesh *mesh, struct nutribatch_s *batch)
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
    NwRecord *item = nw_rndr_record(NW_CMD_DRAW_OT, mtl, mtl->rs.mpg, true);
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

/* Retail also builds the shadow and blend-deformer DMA data, which the shadow and skin work (#56, #46) brings. */
void NuRndrInitEx(int streamsize)
{
    NuRndrStreamInit(streamsize);
    NuFntInit();
    NuRndrOTInit();
}

/* A TARGET into stream: the back buffer (-1), the front buffer (-3), or a render target texture, whose size
   becomes nurndr_pixel_width and height. -2 and -4 put the Z buffer behind the frame, which the port does not draw;
   their draws go nowhere. */
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
    NwCmd *c = nw_rndr_cmd(stream, NW_CMD_TARGET);
    c->target.surf = surf;
    c->target.width = width;
    c->target.height = height;
}

/* Each target the port draws into has a Z buffer of its own; a render target's Z (zbuffer > 0) is not drawn. */
void NuRndrSetZBuffer(struct rndrstream_s *stream, int zbuffer)
{
    static bool reported;
    (void)stream;
    if (zbuffer > 0 && !reported) {
        reported = true;
        fprintf(stderr, NW_APP_NAME ": a render target's Z buffer is not drawn\n");
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

void NuRndrEndScene(void)
{
    NuMtlBuildRenderList();
    NuRndrOTFlush();
    NuRndrStreamFlush();
}

void NuRndrEndSceneEx(int flags)
{
    if (flags & 1) {
        NuMtlBuildRenderList();
    }
    if (flags & 2) {
        NuRndrOTFlush();
    }
    NuRndrStreamFlush();
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

void NuRndrRect2di(int x, int y, int width, int height, int colour, struct numtl_s *mtl)
{
    static const float st[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
    nw_rndr_rect(x, y, width, height, colour, mtl, st, PS2_REZ_W, PS2_REZ_H);
}

/* Centred on the render target, where NuRndrRect2di centres on the screen. */
void NuRndrRectUV2di(int x, int y, int width, int height, float u0, float v0, float u1, float v1, int colour,
                     struct numtl_s *mtl)
{
    const float st[4] = { u0, v0, u1, v1 };
    nw_rndr_rect(x, y, width, height, colour, mtl, st, nurndr_pixel_width, nurndr_pixel_height);
}

/* Target -3 swaps without counting a frame. Retail then kicks VU1, swaps the stream buffers and resets the frame
   counters; only the one main.c reads is kept. */
int NuRndrSwapScreen(int target)
{
    NuPs2DisableAutoScreenSwap();
    int swapped = NuPs2ScreenSwap(target != -3);
    NuRndrVu1Draw();
    NuRndrSwapStreamBuffers();
    nurndr_tritot_this_frame = 0;
    return swapped;
}

/* A scene object: each sub-object in view draws its geoms into their materials' streams (each material of the
   multipass chain mtl->next once) and its face-on geoms into theirs, under its instance's matrix. Returns retail's
   clip state: 0 out of view, 1 in view, 2 partly. */
int NuRndrGScnObj(struct nugobj_s *gobj, struct numtx_s *wm)
{
    struct numtx_s mtx;
    struct numtx_s offset;
    int clip = -1;
    int n = gobj->ngobjs;
    int instanced = n > 1;
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
            for (struct nugeom_s *geom = gobj->geom; geom; geom = geom->next) {
                struct numtl_s *mtl = nurndr_forced_mtl ? nurndr_forced_mtl : geom->mtl;
                if (nurndr_forced_mtl_table && mtl->special_id && nurndr_forced_mtl_table[mtl->special_id]) {
                    mtl = nurndr_forced_mtl_table[mtl->special_id];
                }
                for (; mtl; mtl = mtl->next) {
                    rndrstream_3d = &mtl->rs;
                    if (geom->otbatch) {
                        nw_rndr_ot_batch(m, mtl, geom->dmastream, geom->otbatch);
                    } else if (geom->dmastream) {
                        NwCmd *c = nw_rndr_cmd(rndrstream_3d, NW_CMD_DRAW);
                        c->mesh = geom->dmastream;
                    }
                }
            }
            for (struct nufaceongeom_s *fgeom = gobj->faceon_geom; fgeom; fgeom = fgeom->next) {
                rndrstream_3d = &fgeom->mtl->rs;
                if (fgeom->dmastream) {
                    NwCmd *c = nw_rndr_cmd(rndrstream_3d, NW_CMD_SPRITES);
                    c->sprites = fgeom->dmastream;
                }
            }
        }
        gobj++;
    }
    return clip;
}
