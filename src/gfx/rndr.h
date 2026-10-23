/* The port's Nu render layer (src/gfx/nurndr.c, nustream.c, numtl.c, nutex.c, ps2txtr.c, which override or replace
   those decomp units): what its parts share. Game thread. */
#ifndef NUWORLD_GFX_RNDR_H
#define NUWORLD_GFX_RNDR_H

#include <stdint.h>

#include "gfx/mtl.h"
#include "gfx/tex.h"

struct numtl_s;
struct nups2tex_s;

/* What CreateStateBlock keeps in numtl_s.sbdata in place of retail's GS packet and VIF codes. */
typedef struct NwStateBlock {
    NwMtlState gs;
    float diffuse[4];    /* VU 0x13: diffuse r, g, b and alpha, x128 */
    uint32_t vuattr[13]; /* numtl_s.vuattr: VU 0x14-0x15 ([0] [1] the UV offset), [8] retail's STROW code, the row */
} NwStateBlock;

const NwStateBlock *nw_rndr_state(const struct numtl_s *mtl);

/* The texture of a TexList slot, by its nups2tex_s: the GS memory the port keeps. Takes t (NULL: none). */
void nw_ps2tx_set(struct nups2tex_s *tex, NwTex *t);
/* tid as NuTexCreate returns it; NULL for 0, a surface (< 0) or a slot with no decoded texture. */
const NwTex *nw_rndr_texture(int tid);

#endif
