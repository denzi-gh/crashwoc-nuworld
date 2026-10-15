/* Retail PS2 function address -> host function (tools/gen_funcmap.py). */
#ifndef NUWORLD_PORT_FUNCMAP_H
#define NUWORLD_PORT_FUNCMAP_H

#include <stdint.h>

typedef void (*NwFuncPtr)(void);

typedef enum NwProvenance {
    NW_PROV_MATCHING,
    NW_PROV_EQUIVALENT,
    NW_PROV_NEAR,
    NW_PROV_PORT,
    NW_PROV_STUB
} NwProvenance;

typedef struct NwFuncmapEntry {
    uint32_t address;
    NwProvenance provenance;
    const char *name;
    NwFuncPtr fn;
} NwFuncmapEntry;

typedef struct NwFuncmapUnit {
    const char *name;
    const NwFuncmapEntry *entries;
    unsigned count;
} NwFuncmapUnit;

/* Generated, in address order. */
extern const NwFuncmapUnit *const nw_funcmap_units[];
extern const unsigned nw_funcmap_unit_count;

const NwFuncmapEntry *nw_funcmap_find(uint32_t address);
const NwFuncmapEntry *nw_funcmap_find_host(NwFuncPtr fn);

#endif
