#ifndef NUWORLD_HLE_VU0_H
#define NUWORLD_HLE_VU0_H

#include <string.h>

#include "numath/numathtypes.h"

static inline void nw_vu0_xform(float d[4], const struct numtx_s *m, float x, float y, float z, float w)
{
    const float (*r)[4] = (const float (*)[4])m;
    for (int i = 0; i < 4; i++) {
        d[i] = r[0][i] * x + r[1][i] * y + r[2][i] * z + r[3][i] * w;
    }
}

static inline void nw_vu0_rotate(float d[4], const struct numtx_s *m, float x, float y, float z)
{
    const float (*r)[4] = (const float (*)[4])m;
    for (int i = 0; i < 4; i++) {
        d[i] = r[0][i] * x + r[1][i] * y + r[2][i] * z;
    }
}

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
