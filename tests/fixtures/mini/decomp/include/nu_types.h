#ifndef NU_TYPES_H
#define NU_TYPES_H

#ifdef NW_HOST
#include <stdint.h>
typedef int32_t s32;
#else
typedef signed int s32;
#endif
typedef float f32;

#endif
