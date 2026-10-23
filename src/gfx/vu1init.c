#include "gfx/vu.h"

#include <math.h>
#include <string.h>

void nw_vu1init(NwVuObject *o, const float local[16], const float view[16])
{
    memset(o, 0, sizeof(*o));
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            o->lv[r * 4 + c] = local[r * 4] * view[c] + local[r * 4 + 1] * view[4 + c] +
                               local[r * 4 + 2] * view[8 + c] + local[r * 4 + 3] * view[12 + c];
        }
    }
}

static void nw_vu1init_rows(const float local[16], float row[3][3])
{
    for (int r = 0; r < 3; r++) {
        const float *m = &local[r * 4];
        float len = sqrtf(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
        float inv = len > 0.0f ? 1.0f / len : 0.0f; /* the VU's 1/0 is MAX and 0 x MAX is 0, not NaN */
        for (int k = 0; k < 3; k++) {
            row[r][k] = m[k] * inv;
        }
    }
}

void nw_vu1init_lights(NwVuObject *o, const float local[16], const NwLights *l)
{
    float row[3][3];
    nw_vu1init_rows(local, row);
    for (int i = 0; i < 3; i++) {
        for (int r = 0; r < 3; r++) {
            o->lightdir[i][r] = l->dir[i][0] * row[r][0] + l->dir[i][1] * row[r][1] + l->dir[i][2] * row[r][2];
        }
        o->lightdir[i][3] = 0.0f;
        memcpy(o->lightcol[i], l->col[i], sizeof(l->col[i]));
        o->lightcol[i][3] = 0.0f;
    }
    memcpy(o->ambient, l->ambient, sizeof(l->ambient));
    o->ambient[3] = 0.0f;
}

void nw_vu1init_uv(NwVuObject *o, const float local[16], const NwLights *l)
{
    float row[3][3];
    nw_vu1init_rows(local, row);
    for (int i = 0; i < 3; i++) {
        for (int r = 0; r < 3; r++) {
            o->uv[i][r] = l->uv[0][i] * row[r][0] + l->uv[1][i] * row[r][1] + l->uv[2][i] * row[r][2];
        }
        o->uv[i][3] = 0.0f;
    }
    memcpy(o->uv[3], l->uv[3], sizeof(o->uv[3]));
}
