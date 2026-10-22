#include "vu0.h"

extern float NuTrigTable[];

void NuMtxScaleVU0(struct numtx_s *m, struct nuvec_s *scale)
{
    float (*r)[4] = (float (*)[4])m;
    for (int i = 0; i < 4; i++) {
        r[i][0] *= scale->x;
        r[i][1] *= scale->y;
        r[i][2] *= scale->z;
    }
}

void NuMtxPreScaleVU0(struct numtx_s *m, struct nuvec_s *scale)
{
    float (*r)[4] = (float (*)[4])m;
    for (int i = 0; i < 3; i++) {
        r[0][i] *= scale->x;
        r[1][i] *= scale->y;
        r[2][i] *= scale->z;
    }
}

void NuMtxInvVU0(struct numtx_s *dst, struct numtx_s *src)
{
    float tx = -src->_30, ty = -src->_31, tz = -src->_32;
    struct numtx_s t;

    t._00 = src->_00, t._01 = src->_10, t._02 = src->_20, t._03 = src->_03;
    t._10 = src->_01, t._11 = src->_11, t._12 = src->_21, t._13 = src->_13;
    t._20 = src->_02, t._21 = src->_12, t._22 = src->_22, t._23 = src->_23;
    t._30 = src->_00 * tx + src->_01 * ty + src->_02 * tz;
    t._31 = src->_11 * ty + src->_10 * tx + src->_12 * tz;
    t._32 = src->_22 * tz + src->_20 * tx + src->_21 * ty;
    t._33 = 1.0f;
    *dst = t;
}

void NuMtxSetRotateXYZVU0(struct numtx_s *m, struct nuangvec_s *a)
{
    float sx = NuTrigTable[a->x & 0xFFFF], cx = NuTrigTable[(a->x + 0x4000) & 0xFFFF];
    float sy = NuTrigTable[a->y & 0xFFFF], cy = NuTrigTable[(a->y + 0x4000) & 0xFFFF];
    float sz = NuTrigTable[a->z & 0xFFFF], cz = NuTrigTable[(a->z + 0x4000) & 0xFFFF];

    m->_00 = cz * cy, m->_01 = sz * cy, m->_02 = -sy, m->_03 = 0.0f;
    m->_10 = cz * sy * sx - sz * cx, m->_11 = sz * sy * sx + cz * cx, m->_12 = cy * sx, m->_13 = 0.0f;
    m->_20 = cz * sy * cx + sz * sx, m->_21 = sz * sy * cx - cz * sx, m->_22 = cy * cx, m->_23 = 0.0f;
    m->_30 = 0.0f, m->_31 = 0.0f, m->_32 = 0.0f, m->_33 = 1.0f;
}

void NuMtxMulVU0(struct numtx_s *dst, struct numtx_s *a, struct numtx_s *b)
{
    nw_vu0_mtx_mul(dst, a, b);
}

void NuMtxMulArrayVU0(struct numtx_s *dst, struct numtx_s *a, struct numtx_s *b, int count)
{
    for (int i = 0; i < count; i++) {
        nw_vu0_mtx_mul(&dst[i], &a[i], &b[i]);
    }
}

void NuMtxMulRVU0(struct numtx_s *dst, struct numtx_s *a, struct numtx_s *b)
{
    const float (*ra)[4] = (const float (*)[4])a;
    float t[4][4];
    for (int i = 0; i < 3; i++) {
        nw_vu0_rotate(t[i], b, ra[i][0], ra[i][1], ra[i][2]);
        t[i][3] = 0.0f;
    }
    t[3][0] = 0.0f, t[3][1] = 0.0f, t[3][2] = 0.0f, t[3][3] = 1.0f;
    memcpy(dst, t, sizeof t);
}
