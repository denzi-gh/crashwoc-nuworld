/* The data image: fills the decomp's data roots (gen/dataimage.c) from the user's retail ELF at startup, every
   pointer translated to its host object; a pointer that resolves to nothing is fatal. */
#ifndef NUWORLD_DATA_IMAGE_H
#define NUWORLD_DATA_IMAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "data/image_tables.h"

typedef struct NwImageStats {
    unsigned roots[5]; /* by NW_ROOT_* */
    unsigned objects;  /* C definitions at retail addresses */
    unsigned regions;  /* objects only pointers reach, converted on demand */
    uint64_t region_bytes;
    unsigned pointers;                     /* data pointers translated */
    unsigned code_pointers, stub_pointers; /* through the funcmap (function pointers and nuintptr); to stubs */
    unsigned vu_tokens;                    /* .vutext addresses in data, made microcode package tokens */
    unsigned kept_ints;                    /* small values or -1 in pointer slots, kept as integers */
    unsigned unclaimed;                    /* void pointers to retail bytes no object claims (the mirror) */
} NwImageStats;

/* Fills the roots from the ELF bytes (verified by the caller). False with the message, field path included,
   in err on a fatal problem. */
bool nw_image_load(const NwImageTables *t, const void *elf, size_t size, NwImageStats *stats, char *err,
                   size_t err_size);

/* Frees the objects created on demand; the roots keep their bytes. */
void nw_image_free(void);

/* The host address of a PS2 data address after nw_image_load; NULL when no object or mirror byte maps it. */
void *nw_image_host(uint32_t ps2);
/* The way back: the PS2 address of a host address in the image (one past an object's end included). */
bool nw_image_ps2(const void *host, uint32_t *ps2);

/* One object of the image with storage of its own. */
typedef struct NwImageObj {
    const char *name;
    uint32_t addr, size; /* PS2 extent */
    const void *host;
    const struct NwType *type; /* element type of a converted object; NULL: plain bytes */
    uint32_t count;
} NwImageObj;

/* Calls fn for every sized object in address order with its current host bytes, except objects with no type to
   convert them back. */
void nw_image_each(void (*fn)(const NwImageObj *o, void *user), void *user);

/* Whether a host pointer slot holds what the image kept as an integer: a small value or -1. */
bool nw_image_kept_int(const void *p);

/* The microcode package mpg is the token of (rndrstream_s.mpg, the address a DMA call tag calls), else NULL. */
const NwVuProg *nw_vu_prog(const void *mpg);

void nw_image_report(const NwImageStats *s, FILE *out);

/* NwImageTrip: [0] what the image made from the ELF (copied and converted roots, objects only pointers reach),
   [1] the C definitions at retail addresses. */
typedef struct NwImageTrip {
    unsigned objects[2]; /* compared */
    unsigned differ[2];  /* with a difference, each listed */
    uint64_t bytes[2];
    unsigned strings;    /* pointers to string literals of C definitions, matched by their text */
    unsigned untyped;    /* C definitions with no type to convert them back, not compared */
} NwImageTrip;

/* Converts everything back and lists its differences from the ELF on out; call it right after nw_image_load, while
   the ELF is still in the mirror. False with the message in err when the way back itself fails. */
bool nw_image_roundtrip(NwImageTrip *trip, FILE *out, char *err, size_t err_size);
void nw_image_trip_report(const NwImageTrip *trip, FILE *out);

#endif
