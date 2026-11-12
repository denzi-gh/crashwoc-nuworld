/* Native directional projection onto a receiver plane; row-major, row vectors. */
#ifndef NUWORLD_GFX_SHADOW_H
#define NUWORLD_GFX_SHADOW_H

#include <stdbool.h>

/* plane's Y axis is the normal, its translation a point on the receiver. light points toward the light.
   Rejects invalid planes; a grazing/upward light falls back to the receiver normal to bound the projection. */
bool nw_shadow_projection(float out[16], const float plane[16], const float light[3]);
/* Retail flattens the receiver's Y axis to zero; recover its normal from Z cross X in that case. */
bool nw_shadow_normal(float out[3], const float plane[16]);

#endif
