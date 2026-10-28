#include <float.h>
#include <math.h>

#include "hle/eefloat.h"
#include "numath/nutrig.h"
#include "numath/nuvec.h"
#include "port/nw_newlib.h"

extern f32 NuTrigTable[65536];

/* nutrig.c's with the EE's multiply, which rounds toward zero; sin is the decomp's newlib, as retail's. */
void NuTrigInit(void)
{
    for (s32 i = 0; i < 0x10000; i++) {
        NuTrigTable[i] = (f32)sin(nw_ee_mul(i, 0.0000958738f));
    }
}

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
