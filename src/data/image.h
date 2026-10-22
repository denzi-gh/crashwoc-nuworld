/* The data image: fills the decomp's data roots (gen/dataimage.c, tools/gen_dataimage.py) from the user's
   retail ELF at startup. Plain roots are the retail bytes in the mirror, copied roots a byte copy; converted
   roots go through the generated converters with every pointer translated to its host object. Objects only
   pointers reach are converted on demand; a pointer that resolves to nothing is fatal. */
#ifndef NUWORLD_DATA_IMAGE_H
#define NUWORLD_DATA_IMAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "data/image_tables.h"

typedef struct NwImageStats {
    unsigned roots[4]; /* by NW_ROOT_* */
    unsigned objects;  /* C definitions at retail addresses */
    unsigned regions;  /* objects only pointers reach, converted on demand */
    uint64_t region_bytes;
    unsigned pointers;                     /* data pointers translated */
    unsigned code_pointers, stub_pointers; /* through the funcmap; to stubs */
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

void nw_image_report(const NwImageStats *s, FILE *out);

#endif
