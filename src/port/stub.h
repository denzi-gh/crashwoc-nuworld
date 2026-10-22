/* Stubs for decomp functions with no C yet (tools/gen_stubs.py). Any thread may hit one. */
#ifndef NUWORLD_PORT_STUB_H
#define NUWORLD_PORT_STUB_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct NwStubSite {
    const char *unit;
    const char *name;
    uint32_t address;
    unsigned hits;
    struct NwStubSite *next;
} NwStubSite;

/* Logs the first hit; then aborts unless NUWORLD_STUBS=continue or nw_stub_set_continue(true). */
void nw_stub_hit(NwStubSite *site);
void nw_stub_set_continue(bool on);
/* One line per stub hit so far, by unit and name; returns the count. */
unsigned nw_stub_report(FILE *out);

#define NW_STUB(unit, name, address) \
    do { \
        static NwStubSite nw_stub_site_ = { unit, name, address, 0, 0 }; \
        nw_stub_hit(&nw_stub_site_); \
    } while (0)

#endif
