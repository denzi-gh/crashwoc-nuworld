/* nu3d/nurndr, which the port overrides whole (port/units.toml): the scene bracket and the draws, as records in
   the streams (src/gfx/nustream.c) instead of DMA packets. Every other NuRndr* is a stub, and their hits are the
   renderer's worklist. */
#include <stdio.h>
#include <string.h>

#include "app.h"
#include "gfx/rndr.h"
#include "nu3d/nucamera.h"
#include "nu3d/nugobj.h"
#include "nu3d/nuhgobj.h"
#include "nu3d/numtl.h"
#include "nu3d/nurndr.h"
#include "nu3d/nustream.h"
#include "nu3d/nutex.h"
#include "numath/numtx.h"
#include "numath/nuvec.h"

int NuPs2ScreenSwap(int vsync);
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
extern struct nucamera_s global_camera;
extern struct nutexslot_s *D_0062EBEC;
#define TexList D_0062EBEC

extern struct rndrstream_s *rndrstream_system;
extern struct rndrstream_s *rndrstream_ot_sort;
extern struct rndrstream_s *rndrstream_fx;
extern u8 *rndrstream_free;
extern struct numtx_s numtx_identity;
extern NuRndrShadType NuRndrShadPolDat[];
extern int NuRndrShadowCnt;
extern struct nurndrwaterrip_s NuRndrWaterRipDat[];
extern int NuRndrWaterRipCnt;
extern struct nurndrfoot_s NuRndrFootData[];
void *vpDmaTag_Next(void *packet, uint32_t addr);
void NuRndrStreamLink(struct rndrstream_s *stream);

#define NW_OT_BINS 0x1000
#define NW_OT_ITEMS 0x400
#define NW_OT_TRIS 0x800 /* a batch's most, the per-triangle buckets' slots */

u32 testfootcol; /* a debug colour over every footprint's; 0 in retail */

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

/* NuRndrOTInsertTriBatch with the geom's mesh: each triangle's view z (XFormVuTriZ, from the end of the batch)
   buckets it, the buckets drawn far to near and each from its latest triangle, so a batch is drawn in batch order
   within a bucket; the batch goes into the ordering table at its nearest triangle. Retail drops a batch over 0x800
   triangles or with the item pool used up. */
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

/* Retail also builds the shadow DMA data (#56) and the default blend-deformer weights, which a NULL weights array
   stands for here. */
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
                    if (geom->tristream) {
                        nw_rndr_ot_batch(m, mtl, geom->dmastream, geom->tristream);
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

/* A gobj chain (an hgobj's joint), as NuRndrGScnObj but along next_gobj, never through the ordering table, and
   with the forced material table left out under a forced material. */
int NuRndrGobj(struct nugobj_s *gobj, struct numtx_s *wm)
{
    struct numtx_s mtx;
    struct numtx_s offset;
    int clip = -1;
    int instanced = gobj->next_gobj != 0;
    for (; gobj; gobj = gobj->next_gobj) {
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
        for (struct nugeom_s *geom = gobj->geom; geom; geom = geom->next) {
            struct numtl_s *mtl = geom->mtl;
            if (mtl && nurndr_forced_mtl) {
                mtl = nurndr_forced_mtl;
            } else if (mtl && nurndr_forced_mtl_table && mtl->special_id && nurndr_forced_mtl_table[mtl->special_id]) {
                mtl = nurndr_forced_mtl_table[mtl->special_id];
            }
            for (; mtl && geom->dmastream; mtl = mtl->next) {
                rndrstream_3d = &mtl->rs;
                nw_rndr_cmd(rndrstream_3d, NW_CMD_DRAW)->mesh = geom->dmastream;
            }
        }
        for (struct nufaceongeom_s *fgeom = gobj->faceon_geom; fgeom; fgeom = fgeom->next) {
            rndrstream_3d = &fgeom->mtl->rs;
            if (fgeom->dmastream) {
                nw_rndr_cmd(rndrstream_3d, NW_CMD_SPRITES)->sprites = fgeom->dmastream;
            }
        }
    }
    return clip;
}

/* A skin object's geoms (not its next_gobj, as retail) under wm, each material pass a SKIN record with the palette
   NuHGobjRndrMtxDwa made and the deformer weights (NULL: retail's default, none); retail ignores njoints and joints
   too. skinskipix is -1 in retail and nothing sets it. */
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
    for (struct nugeom_s *geom = gobj->geom; geom; geom = geom->next) {
        struct numtl_s *mtl = nurndr_forced_mtl ? nurndr_forced_mtl : geom->mtl;
        if (mtl && nurndr_forced_mtl_table && mtl->special_id && nurndr_forced_mtl_table[mtl->special_id]) {
            mtl = nurndr_forced_mtl_table[mtl->special_id];
        }
        for (; mtl && geom->skindma; mtl = mtl->next) {
            rndrstream_3d = &mtl->rs;
            nw_rndr_cmd(rndrstream_3d, NW_CMD_SKIN)->skin = (const NwGpuSkin *)geom->skindma;
        }
    }
    return vis;
}

/* Each layer's joint gobjs under their joint's matrix times wm, then its skin objects under wm with the palette
   INV_WT x mtx_array, dwa[ix] the layer's blend-shape weights. hgobj_enabled is 1 in retail and nothing clears it;
   the shadow store comes with the shadows (#56). */
int NuHGobjRndrMtxDwa(struct NUHGOBJ_s *hobj, struct numtx_s *wm, int nlayers, short *layer, struct numtx_s *mtx_array,
                      float **dwa)
{
    struct numtx_s mtx;
    int rv = 0;
    int vis = NuCameraClipTestExtents(&hobj->bbox_min, &hobj->bbox_max, wm);
    if (vis == 0) {
        return 0;
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
                    NuMtxMulVU0(&mtx, &mtx_array[j], wm);
                    rv |= NuRndrGobj(gobjs[j], &mtx);
                }
            }
        }
        if (l->skin_gobj || l->blend_skin_gobj) {
            NuMtxMulArrayVU0(nw_rndr_joints(hobj->num_joints), hobj->INV_WT, mtx_array, hobj->num_joints);
            if (l->skin_gobj) {
                rv |= NuRndrGobjSkin(l->skin_gobj, wm, hobj->num_joints, mtx_array, (u32 *)weights, vis);
            }
            if (l->blend_skin_gobj) {
                rv |= NuRndrGobjSkin(l->blend_skin_gobj, wm, hobj->num_joints, mtx_array, (u32 *)weights, vis);
            }
        }
    }
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
    NwCmd *c = nw_rndr_verts(stream, NW_VERTS_TRIS, n, &v);
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

/* Retail's packet: a RET tag, the S-32 unpack of the count and the weights to VU 0x63, the count at u32 [5]; the
   NuHGobjEvalDwa* write the weights from [6] on. */
u32 *NuRndrCreateBlendShapeDeformerWeightsArray(u32 count)
{
    u32 *base = NuRndrStreamAlloc((int)((count + 10) >> 2));
    base[4] = ((count + 1) << 16) | 0x60000063;
    base[5] = count;
    return base;
}
