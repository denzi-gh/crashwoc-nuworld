/* Water: the PS2's water (fxid 4: a pass bending the frame copy, then the level's water texture over it) as one pass
   that looks into it: the frame under it bent by drifting ripples and fogged by how deep it is in the level's water
   colour, caustics on the bed, the frame reflected along the ripples (marched against its Z), a Fresnel blend, the
   sun's glint and foam where it meets the shore. Render thread, GL context current. */
#ifndef NUWORLD_GFX_WATER_H
#define NUWORLD_GFX_WATER_H

#include <stdbool.h>

#include "gfx/packet.h"

/* What the water sees this frame. */
typedef struct NwWaterFrame {
    unsigned int color, depth; /* the frame before the water: its pixels and its Z, textures as large as the target */
    int viewport[4];           /* the target's viewport, pixels */
    const float (*waves)[4];   /* the material's wave table (NwWaves), NULL for none */
    float tint[4];             /* the level's water colour, 0-1; [3] how murky */
    const NwGpuTex *layer;     /* the level's water texture, laid on it in the world; NULL for none */
    int layer_blend;           /* how the PS2 laid it: 1 blended, 2 added */
    float layer_opacity;       /* the material's alpha, 0-1 */
    float seconds;
} NwWaterFrame;

/* The mesh under local (row-major, row vectors), screen the view's world to GL clip; false when it cannot be drawn
   (no program, a matrix with no inverse): draw the PS2's passes instead. */
bool nw_water_draw(const NwGpuMesh *mesh, const float local[16], const float screen[16], const NwWaterFrame *f);
/* The program and the ripple map made (once); false when they cannot be. */
bool nw_water_ready(void);
void nw_water_shutdown(void);

#endif
