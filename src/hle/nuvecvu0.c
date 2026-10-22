#include <float.h>
#include <limits.h>

#include "vu0.h"

static const float nw_ottri_scale = 3.0f;

static int nw_vu0_ftoi0(float f)
{
    if (f >= 2147483648.0f) {
        return INT_MAX;
    }
    if (f <= -2147483648.0f) {
        return INT_MIN;
    }
    return (int)f;
}

void NuVecMtxRotateVU0(struct nuvec_s *dst, struct nuvec_s *src, struct numtx_s *mtx)
{
    float d[4];
    nw_vu0_rotate(d, mtx, src->x, src->y, src->z);
    dst->x = d[0], dst->y = d[1], dst->z = d[2];
}

void NuVecConvertToIntVU0(void *dst, struct nuvec_s *src)
{
    int *d = dst;
    d[0] = nw_vu0_ftoi0(src->x);
    d[1] = nw_vu0_ftoi0(src->y);
    d[2] = nw_vu0_ftoi0(src->z);
}

void NuVec4MtxTransformVU0(struct nuvec4_s *dst, struct nuvec4_s *src, struct numtx_s *mtx)
{
    float d[4];
    nw_vu0_xform(d, mtx, src->x, src->y, src->z, src->w);
    dst->x = d[0], dst->y = d[1], dst->z = d[2], dst->w = d[3];
}

void NuVec4MtxInvTransformVU0(struct nuvec4_s *dst, struct nuvec4_s *src, struct numtx_s *mtx)
{
    float x = src->x - mtx->_30, y = src->y - mtx->_31, z = src->z - mtx->_32;
    dst->x = mtx->_00 * x + mtx->_01 * y + mtx->_02 * z;
    dst->y = mtx->_10 * x + mtx->_11 * y + mtx->_12 * z;
    dst->z = mtx->_20 * x + mtx->_21 * y + mtx->_22 * z;
    dst->w = src->w;
}

void NuVec4ScaleVU0(struct nuvec4_s *dst, struct nuvec4_s *src, float scale)
{
    dst->x = src->x * scale, dst->y = src->y * scale, dst->z = src->z * scale, dst->w = src->w * scale;
}

void NuVec4ScaleXYZVU0(struct nuvec4_s *dst, struct nuvec4_s *src, float scale)
{
    dst->x = src->x * scale, dst->y = src->y * scale, dst->z = src->z * scale, dst->w = src->w;
}

float XFormVuTriZ(float *depth, void *tri, struct numtx_s *mtx, int count)
{
    const unsigned char *t = tri;
    float zmin = FLT_MAX;
    do {
        const float *a = (const float *)(t + 0x10), *b = (const float *)(t + 0x40), *c = (const float *)(t + 0x70);
        float z = mtx->_02 * a[0] + mtx->_12 * a[1] + mtx->_22 * a[2] + mtx->_32
                + mtx->_02 * b[0] + mtx->_12 * b[1] + mtx->_22 * b[2] + mtx->_32
                + mtx->_02 * c[0] + mtx->_12 * c[1] + mtx->_22 * c[2] + mtx->_32;
        z *= nw_ottri_scale;
        if (z < zmin) {
            zmin = z;
        }
        *depth-- = z;
        t -= 0xA0;
    } while (--count != 0);
    return zmin;
}
