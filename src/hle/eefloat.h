/* The EE FPU's single precision: exponent 255 is a number (no inf or NaN), results are rounded toward zero, clamp at
   its largest value and flush below the smallest normal to zero. Values travel between operations as doubles. */
#ifndef NUWORLD_HLE_EEFLOAT_H
#define NUWORLD_HLE_EEFLOAT_H

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#define NW_EE_MAX 0x1.fffffep128

static inline double nw_ee_in(float f)
{
    uint32_t bits;
    memcpy(&bits, &f, sizeof(bits));
    uint32_t exp = bits >> 23 & 0xFF;
    double v = exp == 0 ? 0.0 : exp == 0xFF ? ldexp(1.0 + (bits & 0x7FFFFF) * 0x1p-23, 128) : fabs((double)f);
    return bits >> 31 ? -v : v;
}

/* x + err is the exact result, x its nearest double. */
static inline double nw_ee_round(double x, double err)
{
    if (x == 0.0) {
        return x;
    }
    int e;
    double m = frexp(x, &e);
    double t = trunc(ldexp(m, 24));
    if (t == ldexp(m, 24) && err != 0.0 && (err < 0.0) != (x < 0.0)) {
        t -= copysign(fabs(m) == 0.5 ? 0.5 : 1.0, x);
    }
    double r = ldexp(t, e - 24);
    if (fabs(r) > NW_EE_MAX) {
        return copysign(NW_EE_MAX, r);
    }
    return fabs(r) < FLT_MIN ? copysign(0.0, r) : r;
}

static inline double nw_ee_add(double a, double b)
{
    double s = a + b, bb = s - a;
    return nw_ee_round(s, (a - (s - bb)) + (b - bb));
}

static inline double nw_ee_sub(double a, double b)
{
    return nw_ee_add(a, -b);
}

/* Two 24-bit significands multiply exactly in a double. */
static inline double nw_ee_mul(double a, double b)
{
    return nw_ee_round(a * b, 0.0);
}

/* The host float nearest the EE's: past FLT_MAX, FLT_MAX. */
static inline float nw_ee_out(double v)
{
    return fabs(v) > FLT_MAX ? (v < 0.0 ? -FLT_MAX : FLT_MAX) : (float)v;
}

#endif
