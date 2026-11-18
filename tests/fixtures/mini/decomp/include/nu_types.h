#ifndef NU_TYPES_H
#define NU_TYPES_H

#ifdef NW_HOST
#include <stdint.h>
typedef int32_t s32;
typedef uintptr_t nuintptr;
#define NW_HOST_BITS(ee, host) host
#else
typedef signed int s32;
typedef unsigned int nuintptr;
#define NW_HOST_BITS(ee, host) ee
#endif
typedef float f32;

#endif
