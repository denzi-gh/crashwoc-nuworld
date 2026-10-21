#ifndef NU_TYPES_H
#define NU_TYPES_H

#ifdef NW_HOST
#include <stdint.h>
typedef int32_t s32;
typedef uintptr_t nuintptr;
#else
typedef signed int s32;
typedef unsigned int nuintptr;
#endif
typedef float f32;

#endif
