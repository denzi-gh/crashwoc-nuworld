/* Round to nearest for the EE's software doubles and the C runtime; tools/softdouble.py calls these. */
#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#if defined(__x86_64__) || defined(_M_X64)
#include <xmmintrin.h>

#pragma STDC FENV_ACCESS ON

#define NW_NEAREST(type, expr) \
    unsigned csr = _mm_getcsr(); \
    _mm_setcsr(0x1F80); \
    type r = (expr); \
    _mm_setcsr(csr); \
    return r
#else
#define NW_NEAREST(type, expr) return (expr)
#endif

double nw_sd_add(double a, double b)
{
    NW_NEAREST(double, a + b);
}

double nw_sd_sub(double a, double b)
{
    NW_NEAREST(double, a - b);
}

double nw_sd_mul(double a, double b)
{
    NW_NEAREST(double, a * b);
}

double nw_sd_div(double a, double b)
{
    NW_NEAREST(double, a / b);
}

double nw_sd_fma(double a, double b, double c)
{
    NW_NEAREST(double, fma(a, b, c));
}

float nw_sd_to_f32(double a)
{
    NW_NEAREST(float, (float)a);
}

float nw_sd_s64_to_f32(int64_t a)
{
    NW_NEAREST(float, (float)a);
}

float nw_sd_u64_to_f32(uint64_t a)
{
    NW_NEAREST(float, (float)a);
}

float nw_host_sinf(float x)
{
    NW_NEAREST(float, sinf(x));
}

float nw_host_cosf(float x)
{
    NW_NEAREST(float, cosf(x));
}

float nw_host_tanf(float x)
{
    NW_NEAREST(float, tanf(x));
}

float nw_host_atan2f(float y, float x)
{
    NW_NEAREST(float, atan2f(y, x));
}

float nw_host_powf(float x, float y)
{
    NW_NEAREST(float, powf(x, y));
}

float nw_host_fmodf(float x, float y)
{
    NW_NEAREST(float, fmodf(x, y));
}

double nw_host_atof(const char *s)
{
    NW_NEAREST(double, atof(s));
}

double nw_host_strtod(const char *s, char **end)
{
    NW_NEAREST(double, strtod(s, end));
}
