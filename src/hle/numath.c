#include <float.h>
#include <math.h>

#include "numath/nuvec.h"

float NuFsqrt(float x)
{
    return sqrtf(fabsf(x));
}

/* nuvec.c's with the EE's divide: 1 / 0 is FLT_MAX, so a zero vector stays zero where x86 makes it NaN (the atlas
   trail normalises one while the ball stands still). */
f32 NuVecNorm(struct nuvec_s *dst, struct nuvec_s *src)
{
    f32 len = NuFsqrt(src->x * src->x + src->y * src->y + src->z * src->z);
    f32 scale = len == 0.0f ? FLT_MAX : 1.0f / len;
    dst->x = src->x * scale;
    dst->y = src->y * scale;
    dst->z = src->z * scale;
    return len;
}
