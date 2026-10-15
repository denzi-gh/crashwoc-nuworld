#include "port/funcmap.h"

#include <stddef.h>

const NwFuncmapEntry *nw_funcmap_find(uint32_t address)
{
    unsigned lo = 0, hi = nw_funcmap_unit_count;
    while (lo < hi) {
        unsigned mid = lo + (hi - lo) / 2;
        if (nw_funcmap_units[mid]->entries[0].address <= address) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo == 0) {
        return NULL;
    }
    const NwFuncmapUnit *unit = nw_funcmap_units[lo - 1];
    lo = 0;
    hi = unit->count;
    while (lo < hi) {
        unsigned mid = lo + (hi - lo) / 2;
        uint32_t at = unit->entries[mid].address;
        if (at == address) {
            return &unit->entries[mid];
        }
        if (at < address) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return NULL;
}

const NwFuncmapEntry *nw_funcmap_find_host(NwFuncPtr fn)
{
    for (unsigned u = 0; u < nw_funcmap_unit_count; u++) {
        const NwFuncmapUnit *unit = nw_funcmap_units[u];
        for (unsigned i = 0; i < unit->count; i++) {
            if (unit->entries[i].fn == fn) {
                return &unit->entries[i];
            }
        }
    }
    return NULL;
}
