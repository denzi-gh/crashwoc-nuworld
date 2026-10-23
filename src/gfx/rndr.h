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

typedef struct NwViewSnap NwViewSnap;
typedef struct NwLightSnap NwLightSnap;

/* A command as a stream (or an OT bin) holds it until a flush copies it to the frame packet. */
typedef struct NwRecord {
    NwCmd cmd;
    const NwViewSnap *view;
    const NwLightSnap *lights; /* NULL unlit */
    struct NwRecord *next;
} NwRecord;

/* A record of op under the current view and local matrix, with the stream's material and microcode, appended to
   stream; a stream with retail's RT lights (attr & 0x43 == 1) latches the current lights. */
NwCmd *nw_rndr_cmd(struct rndrstream_s *stream, NwCmdOp op);
/* The same record in no stream yet. */
NwRecord *nw_rndr_record(NwCmdOp op, struct numtl_s *mtl, const void *mpg, bool lit);
void nw_rndr_push(struct rndrstream_s *stream, NwRecord *r);
/* Moves the records of src to the end of dst. */
void nw_rndr_splice(struct rndrstream_s *dst, struct rndrstream_s *src);
/* NuRndrOTInsertItem: the item into its depth's bin of the ordering table, which NuRndrOTFlush empties into the
   ot_sort stream. */
void nw_rndr_ot_insert(NwRecord *item, float depth);
/* The packet this frame's flushes build. */
NwPacket *nw_rndr_packet(void);
/* The streams found holding packets the compiled decomp wrote into them, which are not drawn. */
unsigned nw_rndr_raw_streams(void);

#endif
