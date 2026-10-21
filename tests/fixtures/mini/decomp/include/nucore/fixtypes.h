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
};

struct fixlong_s {
    long wide;
};

enum fixsign_e { FIXS_NEG = -1, FIXS_POS = 1 };

struct fixopaque_s;

struct fixmisc_s {
    const char *label;
    struct fixnode_s **nodes;
    s32 (*rows)[4];
    fixcb_t *cbs;
    nuintptr addr;
    enum fixmode2_e mode;
    enum fixsign_e sign;
    unsigned char lo : 4, hi : 4;
    struct fixopaque_s *opaque;
};

typedef struct {
    f32 x, y;
} FixPair;

/* Converter cases: unions of pointers only, of identically laid out members, without pointers, mixed
   (port/convert.toml) and hooked, arrays of records and pointers, and a [0] array. */
union fixptr_u {
    void *any;
    struct fixnode_s *node;
    nuintptr addr;
};

struct fixref_s {
    s32 n;
    struct fixnode_s *p;
};

union fixmixed_u {
    void *p;
    s32 i;
};

union fixhook_u {
    f32 f;
    void *p;
};

struct fixunion_s {
    union fixptr_u ptr;
    union {
        struct fixref_s ref;
        struct {
            s32 count;
            void *data;
        };
    };
    union fixmixed_u mixed;
    union {
        short h;
        s32 w;
    } plain;
    struct fixnode_s nodes[2];
    FixPair pairs[2][2];
    struct fixnode_s *grid[2][3];
    s32 n;
    char tail[0];
};

#endif
