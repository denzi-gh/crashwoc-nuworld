/* Game thread. */
#ifndef NUWORLD_GFX_RNDR_H
#define NUWORLD_GFX_RNDR_H

#include <stdint.h>

#include "gfx/mtl.h"
#include "gfx/packet.h"
#include "gfx/tex.h"

struct numtl_s;
struct nups2tex_s;
struct rndrstream_s;

/* What CreateStateBlock keeps in numtl_s.sbdata in place of retail's GS packet and VIF codes. */
typedef struct NwStateBlock {
    NwMtlState gs;
    float diffuse[4];    /* VU 0x13: diffuse r, g, b and alpha, x128 */
    uint32_t vuattr[13]; /* numtl_s.vuattr: VU 0x14-0x15 ([0] [1] the UV offset), [8] retail's STROW code, the row */
} NwStateBlock;

const NwStateBlock *nw_rndr_state(const struct numtl_s *mtl);
/* Values only: no ATS/player ownership in the renderer. A scope overrides one source material.
   Save/restore the returned value; an empty scope restores ordinary drawing. */
typedef struct NwMtlBinding { const struct numtl_s *material; int tid; const NwGpuTex *texture; int layer; NwUV uv; } NwMtlBinding;
NwMtlBinding nw_rndr_mtl_scope(NwMtlBinding binding);
/* One frame-wide fallback; also resolves earlier unbound draws at publication.
   Explicit/scoped snapshots stay fixed. Cleared by NuRndrInitStreams. */
void nw_rndr_mtl_frame_binding(NwMtlBinding binding);
/* Replaces the complete frame collection, copied into the frame arena. Failure leaves it unchanged. */
bool nw_rndr_mtl_frame_bindings(const NwMtlBinding *bindings, size_t count);
/* A frame snapped: effects not set again within two of them go (gfx/numtl.c). */
void nw_rndr_mtl_frame(void);
/* A material drawn by an effect of the port's instead of its texture (port/opening.c): fx[0] the kind (NW_MTL_FX_*),
   the rest its parameters, as the VU programs' u_fx[4]; [12-14] a glow's colour (0: none) and [15] its width, drawn
   as a second pass (NW_MTL_FX_HALO); NULL takes it off. */
enum { NW_MTL_FX_NONE, NW_MTL_FX_VORTEX, NW_MTL_FX_METAL, NW_MTL_FX_GLOSS, NW_MTL_FX_HALO, NW_MTL_FX_FADE };
#define NW_MTL_FX_LEN 16
void nw_rndr_mtl_fx(struct numtl_s *mtl, const float fx[NW_MTL_FX_LEN]);
/* Copies mtl's effect into fx (zeros: none). */
void nw_rndr_mtl_fx_of(const struct numtl_s *mtl, float fx[NW_MTL_FX_LEN]);

typedef struct NwTexture NwTexture;
/* PNG <=8 bits/channel, <=64 MiB compressed and RGBA pixels; requires a published GPU limit. */
NwTexture *nw_texture_png(const void *bytes, size_t size);
/* Success with *output=NULL: valid images incompatible with arrays. Other failures report SDL_GetError.
   Copies native colours/alpha, <=64 MiB; reuse is borrowed, output owns one reference. */
bool nw_texture_array(const short *tids, int count, NwTexture *reuse, NwTexture **output);
bool nw_texture_images(const short *tids, int count, bool mips, NwTexture *reuse, NwTexture **output);
const NwGpuTex *nw_texture_gpu(const NwTexture *texture);
bool nw_texture_retain(NwTexture *texture);
void nw_texture_release(NwTexture *texture);
int nw_texture_tid(const NwTexture *texture);

/* gpu NULL: none; queues its upload and frees the slot's previous texture. */
void nw_ps2tx_set(struct nups2tex_s *tex, NwGpuTex *gpu);
/* tid as NuTexCreate returns it; NULL for 0, a surface (< 0) or a slot with no decoded texture. */
const NwGpuTex *nw_rndr_texture(int tid);
void nw_rndr_texture_free(int tid);
/* -1 and -3 the back and front buffer, else a target's GS address; addr 0 for what the port does not draw. */
NwSurf nw_ps2tx_surf(int tid);
/* Render targets as wide as the frame was (NuLightMatInit's, InitGlass's, NuWaterInit's); their GS memory stays. */
void nw_ps2tx_frame_width(int was, int now);

typedef struct NwViewSnap NwViewSnap;
typedef struct NwLightSnap NwLightSnap;
typedef struct NwJointSnap NwJointSnap;

/* Held by a stream or OT bin until a flush copies it to the frame packet. */
typedef struct NwRecord {
    NwCmd cmd;
    const NwViewSnap *view;
    const NwLightSnap *lights; /* the stream's block at VU 0x0C (lights, specular or fx), NULL for none */
    const uint32_t *idx;       /* DRAW_OT: cmd.count vertex indices */
    const NwJointSnap *joints;
    const uint32_t *blend;     /* SKIN: retail's deformer weights packet, the count at [5] and the weights after it */
    const NwVuSpriteVertex *verts; /* VERTS: cmd.count vertices */
    const NwPtlList *ptllist;
    const NwPtl *ptls;             /* PARTICLES: cmd.count particles */
    const NwZClut *zclut;
    const NwWaves *waves;
    uint8_t misc;              /* the stream's attr & 0x43 */
    struct NwRecord *next;
} NwRecord;

/* Latches the VU 0x0C block by attr & 0x43 as retail: 1 the RT lights, 2 the specular UV matrix, 0x40 the fx matrix. */
NwCmd *nw_rndr_cmd(struct rndrstream_s *stream, NwCmdOp op);
/* Groups of these effect lists (port/opening), NULL none: not drawn when hidden, else drawn through their own transform
   after their matrix when one is set (particles living in a frame of their own). */
extern const void *nw_rndr_particle_own[4];
extern const float *nw_rndr_particle_own_xform[4];
extern bool nw_rndr_particle_own_hidden[4];
/* The same as a VERTS record of n vertices, three a triangle, which the caller writes at *v. */
NwCmd *nw_rndr_verts(struct rndrstream_s *stream, NwVertsMode mode, uint32_t n, NwVuSpriteVertex **v);
NwCmd *nw_rndr_particles(struct rndrstream_s *stream, uint32_t n, NwPtlList **list, NwPtl **ptls);
NwWaves *nw_rndr_waves(struct rndrstream_s *stream);
NwZClut *nw_rndr_zclut(NwCmd *c);
/* (x, y) in 12.4 pixels of the render target, centred as the GS sees it; the port's GS Z stops at 2^23 - 1. */
NwCmd *nw_rndr_rect3d(struct rndrstream_s *stream, int x, int y, int width, int height, const float uv[4],
                      int colour, int z);
/* Its quad at v, for a record of several; returns v + 6. */
NwVuSpriteVertex *nw_rndr_rect3d_quad(NwVuSpriteVertex *v, int x, int y, int width, int height, const float uv[4],
                                      int colour, int z);
/* A quad's corners as the GS strip draws them, the triangles (0, 1, 2) and (1, 2, 3); returns v + 6. */
NwVuSpriteVertex *nw_rndr_quad(NwVuSpriteVertex *v, const NwVuSpriteVertex q[4]);
struct numtx_s;
struct nuvtx_tc1_s;
/* NuRndrStreamTriStrip3dClip's record (src/gfx/nurndr.c). */
NwCmd *nw_rndr_strip(struct nuvtx_tc1_s *verts, int count, struct numtx_s *mtx, struct rndrstream_s *stream);
/* The same record in no stream yet. */
NwRecord *nw_rndr_record(NwCmdOp op, struct numtl_s *mtl, const void *mpg, int misc);
void nw_rndr_model_begin(const struct numtx_s *wm);
void nw_rndr_model_end(void);
uint32_t nw_rndr_silhouette(const struct numtx_s *quad, float *scale);
void nw_rndr_push(struct rndrstream_s *stream, NwRecord *r);
/* The record's view and VU 0x0C block become the current ones, as a stream set up when it is flushed. */
void nw_rndr_latch(NwRecord *r);
/* Memory that lives as long as this frame's records (and the next frame's, as retail's output buffers). */
void *nw_rndr_alloc(size_t size);
struct numtx_s;
/* SKIN records from now on share the n matrices; a flush copies them to the packet once. */
struct numtx_s *nw_rndr_joints(uint32_t n);
/* The deformer weights of the SKIN records from now on (NULL: none). */
void nw_rndr_blend(const uint32_t *weights);
void nw_rndr_splice(struct rndrstream_s *dst, struct rndrstream_s *src);
/* NuRndrOTInsertItem; NuRndrOTFlush empties the bins into the ot_sort stream. */
void nw_rndr_ot_insert(NwRecord *item, float depth);
struct nutristream_s;
/* NuRndrOTInsertTriBatch with the geom's mesh (src/gfx/nurndr.c). */
void nw_rndr_ot_batch(struct numtx_s *mtx, struct numtl_s *mtl, const NwGpuMesh *mesh, struct nutristream_s *batch);
NwPacket *nw_rndr_packet(void);
/* Streams holding packets the compiled decomp wrote, which are not drawn. */
unsigned nw_rndr_raw_streams(void);

#endif
