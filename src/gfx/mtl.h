/* The GS state of a material, from numtl_s.attrib as CreateStateBlock and NuTexWriteMaterialSettings set it. */
#ifndef NUWORLD_GFX_MTL_H
#define NUWORLD_GFX_MTL_H

#include <stdbool.h>
#include <stdint.h>

typedef struct NwMtlState {
    bool abe;           /* PRMODE.ABE */
    uint8_t alpha;      /* ALPHA_1: (A - B) * C + D, 2 bits each */
    uint8_t ztst;       /* TEST_1: 0 NEVER, 1 ALWAYS, 2 GEQUAL, 3 GREATER (ZTE is always on) */
    bool zmsk;          /* ZBUF_1: no Z writes */
    bool ate;           /* TEST_1: the alpha test, on with ABE */
    uint8_t atst, aref; /* 0 NEVER, 1 ALWAYS, 2 LESS, 3 LEQUAL, 4 EQUAL, 5 GEQUAL, 6 GREATER, 7 NOTEQUAL */
    uint8_t afail;      /* what a failing pixel writes: 0 KEEP, 1 FB_ONLY, 2 ZB_ONLY, 3 RGB_ONLY */
    uint8_t mmag, mmin; /* TEX1: 0 NEAREST, 1 LINEAR, mmin 5 LINEAR_MIPMAP_LINEAR */
    uint8_t mxl, l;     /* TEX1: the last mip level, LOD = log2(1/Q) << L + K */
    int16_t k;          /* in 1/16 */
    uint8_t wms, wmt;   /* CLAMP: 0 REPEAT, 1 CLAMP */
} NwMtlState;

typedef enum NwBlendOp { NW_BLEND_ADD, NW_BLEND_SUBTRACT, NW_BLEND_REVERSE_SUBTRACT } NwBlendOp;
typedef enum NwBlendFactor {
    NW_BLEND_ZERO,
    NW_BLEND_ONE,
    NW_BLEND_SRC_ALPHA,
    NW_BLEND_ONE_MINUS_SRC_ALPHA,
    NW_BLEND_DST_COLOUR,
    NW_BLEND_DST_ALPHA,
    NW_BLEND_ONE_MINUS_DST_ALPHA
} NwBlendFactor;

/* An ALPHA register as a GL blend: op(Cs * src, Cd * dst); as_colour: the program writes As as Cs. */
typedef struct NwMtlBlend {
    NwBlendOp op;
    NwBlendFactor src, dst;
    bool as_colour;
} NwMtlBlend;

/* One draw of a material's triangles: the pixels it keeps by the alpha test and the buffers it writes. */
typedef struct NwMtlPass {
    bool failed; /* keeps the pixels that fail the test, not the ones that pass */
    bool rgb, alpha, z;
} NwMtlPass;

/* k, l: numtl_s.K and L; levels: the mip levels of the material's texture (0 untextured). */
NwMtlState nw_mtl_state(const uint32_t attrib[2], int16_t k, uint8_t l, uint32_t levels);
/* The passes the alpha test needs: the passing pixels, then (unless AFAIL is KEEP) what AFAIL writes of the rest. */
uint32_t nw_mtl_passes(const NwMtlState *s, NwMtlPass pass[2]);
/* false for an ALPHA whose C is FIX or whose factors have no GL blend. */
bool nw_mtl_blend(uint8_t alpha, NwMtlBlend *b);

#endif
