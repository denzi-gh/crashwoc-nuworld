/* The decomp's newlib on the host (libm and rand, compiled per port/units.toml): every name it defines takes
 * an nw_ prefix, so decomp code calls the decomp's libm and rand (the same results on every OS, the PS2's
 * RAND_MAX) while SDL, Dear ImGui and the C runtime keep the host's. SDL3 and MinGW define some of the same
 * fdlibm names. The newlib units' wrappers include it first (tools/tu_filter.py); nw_prelude.h includes it
 * after the host headers, whose math.h pastes these names into its own macros. */
#ifndef NUWORLD_PORT_NW_NEWLIB_H
#define NUWORLD_PORT_NW_NEWLIB_H

#ifndef NW_NEWLIB_UNIT
/* What the host headers declared under the plain names. */
double nw_acos(double x);
double nw_atan(double x);
double nw_atan2(double y, double x);
double nw_copysign(double x, double y);
double nw_cos(double x);
double nw_fabs(double x);
double nw_floor(double x);
double nw_fmod(double x, double y);
double nw_pow(double x, double y);
double nw_rint(double x);
double nw_scalbn(double x, int n);
double nw_sin(double x);
double nw_sqrt(double x);
double nw_tan(double x);
int nw_rand(void);
void nw_srand(unsigned int seed);
#endif

#define acos nw_acos
#define atan nw_atan
#define atan2 nw_atan2
#define copysign nw_copysign
#define cos nw_cos
#define fabs nw_fabs
#define floor nw_floor
#define fmod nw_fmod
#define matherr nw_matherr
#define pow nw_pow
#define rint nw_rint
#define scalbn nw_scalbn
#define sin nw_sin
#define sqrt nw_sqrt
#define tan nw_tan
#define rand nw_rand
#define srand nw_srand

#define __ieee754_acos nw___ieee754_acos
#define __ieee754_atan2 nw___ieee754_atan2
#define __ieee754_fmod nw___ieee754_fmod
#define __ieee754_pow nw___ieee754_pow
#define __ieee754_rem_pio2 nw___ieee754_rem_pio2
#define __ieee754_sqrt nw___ieee754_sqrt
#define __kernel_cos nw___kernel_cos
#define __kernel_rem_pio2 nw___kernel_rem_pio2
#define __kernel_sin nw___kernel_sin
#define __kernel_tan nw___kernel_tan

/* The classification functions only in the newlib units: everywhere else they are the host's math.h macros. */
#ifdef NW_NEWLIB_UNIT
#define finite nw_finite
#define isinf nw_isinf
#define isnan nw_isnan
#endif

#endif
