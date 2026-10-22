/* The GS state of a material, from numtl_s.attrib as CreateStateBlock and NuTexWriteMaterialSettings set it. */
#ifndef NUWORLD_GFX_MTL_H
#define NUWORLD_GFX_MTL_H

#include <stdint.h>

typedef struct NwMtlState {
    uint8_t mmag, mmin; /* TEX1: 0 NEAREST, 1 LINEAR, mmin 5 LINEAR_MIPMAP_LINEAR */
    uint8_t wms, wmt;   /* CLAMP: 0 REPEAT, 1 CLAMP */
} NwMtlState;

NwMtlState nw_mtl_state(const uint32_t attrib[2]);

#endif
