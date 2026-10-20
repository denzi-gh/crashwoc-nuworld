#ifndef NUCORE_FIXTYPES_H
#define NUCORE_FIXTYPES_H

#include "nu_types.h"

typedef unsigned long long fixu64;
typedef void (*fixcb_t)(s32 code);

enum fixmode2_e { FIXM_A = 1, FIXM_B = 4 };

struct fixnode_s {
    struct fixnode_s *next;
    union {
        s32 i;
        f32 f;
        struct {
            short lo;
            short hi;
        };
    };
    struct {
        s32 a;
    } named;
    fixu64 tag : 48;
    fixu64 kind : 16;
    unsigned int x : 3, : 2, y : 4;
    char name[2][3];
    fixcb_t cb;
    long wide;
};

typedef struct {
    f32 x, y;
} FixPair;

#endif
