/* Stubs for decomp functions with no C yet (tools/gen_stubs.py). Game thread only. */
#ifndef NUWORLD_PORT_STUB_H
#define NUWORLD_PORT_STUB_H

#include <stdint.h>
#include <stdio.h>

typedef struct NwStubSite {
    const char *unit;
    const char *name;
    uint32_t address;
    unsigned hits;
    struct NwStubSite *next;
} NwStubSite;

/* Aborts unless NUWORLD_STUBS=continue; then logs the first hit and returns. */
void nw_stub_hit(NwStubSite *site);
unsigned nw_stub_report(FILE *out);

#define NW_STUB(unit, name, address) \
    do { \
        static NwStubSite nw_stub_site_ = { unit, name, address, 0, 0 }; \
        nw_stub_hit(&nw_stub_site_); \
    } while (0)

#endif
