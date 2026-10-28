/* The decomp's NW_EE_DIV, NW_EE_LERP and NW_EE_S32 (nu_port.h): EE float where retail leaves IEEE's range. */
#include <stdint.h>

#include "hle/eefloat.h"

float nw_ee_lerp(float a, float dt, float range, float b)
{
    double ea = nw_ee_in(a);
    double step = nw_ee_mul(nw_ee_div(nw_ee_in(dt), nw_ee_in(range)), nw_ee_sub(nw_ee_in(b), ea));
    return nw_ee_out(nw_ee_add(ea, step));
}

float nw_ee_fdiv(float a, float b)
{
    return nw_ee_out(nw_ee_div(nw_ee_in(a), nw_ee_in(b)));
}

/* cvt.w.s: toward zero, saturating. */
int nw_ee_s32(float x)
{
    double v = nw_ee_in(x);
    return v >= 2147483648.0 ? INT32_MAX : v <= -2147483648.0 ? INT32_MIN : (int)v;
}
