/* Stubs for decomp functions with no C yet (tools/gen_stubs.py) and probes at the top of decomp C that is only a
   near match (tools/tu_filter.py): the telemetry that orders the decomp work. Any thread may hit one. */
#ifndef NUWORLD_PORT_STUB_H
#define NUWORLD_PORT_STUB_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct NwStubSite {
    const char *unit;
    const char *name;
    const char *why; /* why there is no body: an override without a port body, no decomp C, ... */
    uint32_t address;
    bool probe;     /* a near-match probe, not a stub */
    unsigned hits;
    unsigned order; /* 1 for the first site this run hit, 2 for the next, ... */
    struct NwStubSite *next;
} NwStubSite;

/* Logs the first hit; then aborts unless NUWORLD_STUBS=continue or nw_stub_set_continue(true). */
void nw_stub_hit(NwStubSite *site);
/* Counts the call; never logs or aborts. */
void nw_near_hit(NwStubSite *site);
void nw_stub_set_continue(bool on);
/* One line per site hit so far in first-hit order (order, stub|near, unit, name, retail address, hits), read by
   tools/stub_hits.py; returns the number of stubs and stores the number of near-match probes in *probes (if set). */
unsigned nw_stub_report(FILE *out, unsigned *probes);

#define NW_STUB(unit, name, address, why) \
    do { \
        static NwStubSite nw_stub_site_ = { unit, name, why, address, false, 0, 0, 0 }; \
        nw_stub_hit(&nw_stub_site_); \
    } while (0)

#define NW_NEAR(unit, name, address) \
    do { \
        static NwStubSite nw_near_site_ = { unit, name, NULL, address, true, 0, 0, 0 }; \
        nw_near_hit(&nw_near_site_); \
    } while (0)

#endif
