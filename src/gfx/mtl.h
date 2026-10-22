/* The GS state of a material, from numtl_s.attrib as CreateStateBlock and NuTexWriteMaterialSettings set it. */
#ifndef NUWORLD_GFX_MTL_H
#define NUWORLD_GFX_MTL_H

#include <stdbool.h>
#include <stdint.h>

typedef struct NwMtlState {
    bool abe;           /* PRMODE.ABE */
    uint8_t alpha;      /* ALPHA_1: (A - B) * C + D, 2 bits each */
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
    NW_BLEND_DST_COLOUR
} NwBlendFactor;

/* An ALPHA register as a GL blend: op(Cs * src, Cd * dst); as_colour: the program writes As as Cs. */
typedef struct NwMtlBlend {
    NwBlendOp op;
    NwBlendFactor src, dst;
    bool as_colour;
} NwMtlBlend;

/* k, l: numtl_s.K and L; levels: the mip levels of the material's texture (0 untextured). */
NwMtlState nw_mtl_state(const uint32_t attrib[2], int16_t k, uint8_t l, uint32_t levels);
/* false for an ALPHA whose C is not As or whose factors have no GL blend. */
bool nw_mtl_blend(uint8_t alpha, NwMtlBlend *b);

#endif
