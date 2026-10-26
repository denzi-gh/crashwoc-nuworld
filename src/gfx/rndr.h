/* The port's Nu render layer (src/gfx/nurndr.c, nustream.c, numtl.c, nutex.c, ps2txtr.c, which override or replace
   those decomp units): what its parts share. Game thread. */
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

/* The texture of a TexList slot, by its nups2tex_s: the GS memory the port keeps. Takes gpu (NULL: none) and
   queues its upload; the slot's texture before it is freed. */
void nw_ps2tx_set(struct nups2tex_s *tex, NwGpuTex *gpu);
/* tid as NuTexCreate returns it; NULL for 0, a surface (< 0) or a slot with no decoded texture. */
const NwGpuTex *nw_rndr_texture(int tid);
void nw_rndr_texture_free(int tid);
/* A tid in GS memory: -1 and -3 the back and front buffer, a render target at its address as NuTexSetRenderTarget
   finds it; addr 0 for any other, and for the Z buffer and the palette and Z render targets the port does not draw
   (reported once). */
NwSurf nw_ps2tx_surf(int tid);

typedef struct NwViewSnap NwViewSnap;
typedef struct NwLightSnap NwLightSnap;
typedef struct NwJointSnap NwJointSnap;

/* A command as a stream (or an OT bin) holds it until a flush copies it to the frame packet. */
typedef struct NwRecord {
    NwCmd cmd;
    const NwViewSnap *view;
    const NwLightSnap *lights; /* the stream's block at VU 0x0C (lights, specular or fx), NULL for none */
    const uint32_t *idx;       /* DRAW_OT: cmd.count vertex indices */
    const NwJointSnap *joints; /* SKIN: the palette */
    const uint32_t *blend;     /* SKIN: retail's deformer weights packet, the count at [5] and the weights after it */
    const NwVuSpriteVertex *verts; /* VERTS: cmd.count vertices */
    const NwPtlList *ptllist;      /* PARTICLES: the effect list */
    const NwPtl *ptls;             /* PARTICLES: cmd.count particles */
    uint8_t misc;              /* the stream's attr & 0x43 */
    struct NwRecord *next;
} NwRecord;

/* A record of op under the current view and local matrix, with the stream's material and microcode, appended to
   stream; it latches the stream's block at VU 0x0C as retail's attr & 0x43 picks it: 1 the RT lights, 2 the
   specular UV matrix, 0x40 the fx matrix. */
NwCmd *nw_rndr_cmd(struct rndrstream_s *stream, NwCmdOp op);
/* The same as a VERTS record of n vertices, three a triangle, which the caller writes at *v. */
NwCmd *nw_rndr_verts(struct rndrstream_s *stream, NwVertsMode mode, uint32_t n, NwVuSpriteVertex **v);
/* The same as a PARTICLES record of an effect list and n particles, which the caller writes at *list and *ptls. */
NwCmd *nw_rndr_particles(struct rndrstream_s *stream, uint32_t n, NwPtlList **list, NwPtl **ptls);
/* A quad's corners as the GS strip draws them, the triangles (0, 1, 2) and (1, 2, 3); returns v + 6. */
NwVuSpriteVertex *nw_rndr_quad(NwVuSpriteVertex *v, const NwVuSpriteVertex q[4]);
struct numtx_s;
struct nuvtx_tc1_s;
/* NuRndrStreamTriStrip3dClip's record (src/gfx/nurndr.c). */
NwCmd *nw_rndr_strip(struct nuvtx_tc1_s *verts, int count, struct numtx_s *mtx, struct rndrstream_s *stream);
/* The same record in no stream yet. */
NwRecord *nw_rndr_record(NwCmdOp op, struct numtl_s *mtl, const void *mpg, int misc);
void nw_rndr_push(struct rndrstream_s *stream, NwRecord *r);
/* The record's view and VU 0x0C block become the current ones, as a stream set up when it is flushed. */
void nw_rndr_latch(NwRecord *r);
/* Memory that lives as long as this frame's records (and the next frame's, as retail's output buffers). */
void *nw_rndr_alloc(size_t size);
struct numtx_s;
/* The joint palette of the SKIN records from now on, n matrices to fill in that memory; the records share it and a
   flush copies it to the packet once. */
struct numtx_s *nw_rndr_joints(uint32_t n);
/* The deformer weights of the SKIN records from now on (NULL: none). */
void nw_rndr_blend(const uint32_t *weights);
/* Moves the records of src to the end of dst. */
void nw_rndr_splice(struct rndrstream_s *dst, struct rndrstream_s *src);
/* NuRndrOTInsertItem: the item into its depth's bin of the ordering table, which NuRndrOTFlush empties into the
   ot_sort stream. */
void nw_rndr_ot_insert(NwRecord *item, float depth);
struct nutristream_s;
/* NuRndrOTInsertTriBatch with the geom's mesh (src/gfx/nurndr.c). */
void nw_rndr_ot_batch(struct numtx_s *mtx, struct numtl_s *mtl, const NwGpuMesh *mesh, struct nutristream_s *batch);
/* The packet this frame's flushes build. */
NwPacket *nw_rndr_packet(void);
/* The streams found holding packets the compiled decomp wrote into them, which are not drawn. */
unsigned nw_rndr_raw_streams(void);

#endif
