/* Volumetric clouds: a geom marked NwGpuCloud drawn as a volume of tiling 3D noise in its box, lit by the scene's sun
   with self-shadowing, over what is there without writing Z or the frame's alpha; and a living sky over a sky dome,
   its clouds gathering and closing over the sun as the intro's storm goes on. Render thread, GL context current. */
#ifndef NUWORLD_GFX_CLOUDVOL_H
#define NUWORLD_GFX_CLOUDVOL_H

#include <stdbool.h>

#include "gfx/packet.h"

/* The sky over a sky dome this frame. */
typedef struct NwCloudSky {
    float storm;      /* how stormy: 0 a fair sky to 1 overcast */
    bool sun_on;      /* the sun's card is in the frame, left out: the sky draws the sun there */
    float sun_at[3];  /* the card's centre, world */
    float sun_radius; /* its half size, world */
} NwCloudSky;

/* The programs and the noise made (once); false when they cannot be. */
bool nw_cloudvol_ready(void);
/* The tiling 3D noise (RGBA: Perlin-Worley, Worley at 4, 8 and 16 cells a tile), made if need be; 0 for none. */
unsigned int nw_cloudvol_noise(void);
/* m's inverse (row-major), false when it has none. */
bool nw_cloudvol_invert(const float m[16], float out[16]);
/* The cloud under local (row-major, row vectors) with lv local times the screen into GL clip space, seconds into its
   drift and billowing, storm how far the storm is (its light fades). False when it cannot be drawn (no program, a
   matrix with no inverse): draw the geom instead. */
bool nw_cloudvol_draw(const NwGpuCloud *cloud, const float local[16], const float lv[16], float seconds, float storm);
/* A sky of its own over a sky dome just drawn under local and lv, as stormy as sky says; false when it cannot be
   drawn. */
bool nw_cloudvol_sky(const NwGpuMesh *dome, const float local[16], const float lv[16], float seconds,
                     const NwCloudSky *sky);
void nw_cloudvol_shutdown(void);

#endif
