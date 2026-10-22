/* numath/numath.s, the EE FPU wrappers (port/units.toml: hle). The others, which retail never reaches, stay
   generated stubs. */
#include <math.h>

/* sqrt.s: the EE FPU takes the root of |x| (and flags the negative input). */
float NuFsqrt(float x)
{
    return sqrtf(fabsf(x));
}
