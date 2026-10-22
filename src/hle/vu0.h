/* Helpers for the C versions of the VU0 macro-mode units (src/hle). The VU0 accumulator adds its products in
   instruction order and the sums here keep that order; VU0 rounding, and its lack of denormals, infinities and
   NaNs, are not reproduced. */
#ifndef NUWORLD_HLE_VU0_H
#define NUWORLD_HLE_VU0_H

#include <string.h>

#include "numath/numathtypes.h"

/* d = m.row0 * x + m.row1 * y + m.row2 * z + m.row3 * w (vmulax, vmadday, vmaddaz, vmaddw). */
static inline void nw_vu0_xform(float d[4], const struct numtx_s *m, float x, float y, float z, float w)
{
    const float (*r)[4] = (const float (*)[4])m;
    for (int i = 0; i < 4; i++) {
        d[i] = r[0][i] * x + r[1][i] * y + r[2][i] * z + r[3][i] * w;
    }
}

/* The same without row 3 (vmaddz ends the sum). */
static inline void nw_vu0_rotate(float d[4], const struct numtx_s *m, float x, float y, float z)
{
    const float (*r)[4] = (const float (*)[4])m;
    for (int i = 0; i < 4; i++) {
        d[i] = r[0][i] * x + r[1][i] * y + r[2][i] * z;
    }
}

/* dst = a * b: each row of a through b. All of a and b is read before dst is written. */
static inline void nw_vu0_mtx_mul(struct numtx_s *dst, const struct numtx_s *a, const struct numtx_s *b)
{
    const float (*ra)[4] = (const float (*)[4])a;
    float t[4][4];
    for (int i = 0; i < 4; i++) {
        nw_vu0_xform(t[i], b, ra[i][0], ra[i][1], ra[i][2], ra[i][3]);
    }
    memcpy(dst, t, sizeof t);
}

#endif
