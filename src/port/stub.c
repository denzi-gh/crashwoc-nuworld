#include "port/stub.h"
#include "app.h"

#include <stdlib.h>
#include <string.h>

/* Lock-free: a stub may be hit on any thread while another reports. */
static NwStubSite *nw_stub_hits;
static int nw_stub_mode = -1; /* -1: NUWORLD_STUBS not read yet, else continue (1) or abort (0) */

void nw_stub_set_continue(bool on)
{
    __atomic_store_n(&nw_stub_mode, on, __ATOMIC_RELAXED);
}

static int nw_stub_continue(void)
{
    int mode = __atomic_load_n(&nw_stub_mode, __ATOMIC_RELAXED);
    if (mode < 0) {
        const char *env = getenv("NUWORLD_STUBS");
        int from_env = env != NULL && strcmp(env, "continue") == 0;
        __atomic_compare_exchange_n(&nw_stub_mode, &mode, from_env, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
        mode = __atomic_load_n(&nw_stub_mode, __ATOMIC_RELAXED);
    }
    return mode;
}

void nw_stub_hit(NwStubSite *site)
{
    if (__atomic_fetch_add(&site->hits, 1, __ATOMIC_RELAXED) == 0) {
        NwStubSite *head = __atomic_load_n(&nw_stub_hits, __ATOMIC_RELAXED);
        do {
            site->next = head;
        } while (!__atomic_compare_exchange_n(&nw_stub_hits, &head, site, true, __ATOMIC_RELEASE, __ATOMIC_RELAXED));
        fprintf(stderr, NW_APP_NAME ": stub hit: %s (%s, 0x%08x) has no C yet\n",
                site->name, site->unit, (unsigned)site->address);
    }
    if (!nw_stub_continue()) {
        fprintf(stderr, NW_APP_NAME ": aborting; set NUWORLD_STUBS=continue to log stub hits and go on\n");
        fflush(stderr);
        abort();
    }
}

static int nw_stub_cmp(const void *a, const void *b)
{
    const NwStubSite *x = *(const NwStubSite *const *)a, *y = *(const NwStubSite *const *)b;
    int c = strcmp(x->unit, y->unit);
    return c ? c : strcmp(x->name, y->name);
}

unsigned nw_stub_report(FILE *out)
{
    unsigned n = 0;
    NwStubSite *head = __atomic_load_n(&nw_stub_hits, __ATOMIC_ACQUIRE);
    for (NwStubSite *s = head; s != NULL; s = s->next) {
        n++;
    }
    NwStubSite **sites = malloc((n ? n : 1) * sizeof(*sites));
    if (sites == NULL) {
        return 0;
    }
    unsigned i = 0;
    for (NwStubSite *s = head; s != NULL; s = s->next) {
        sites[i++] = s;
    }
    qsort(sites, n, sizeof(*sites), nw_stub_cmp);
    for (i = 0; i < n; i++) {
        fprintf(out, "%-32s %-40s 0x%08x %u\n", sites[i]->unit, sites[i]->name, (unsigned)sites[i]->address,
                __atomic_load_n(&sites[i]->hits, __ATOMIC_RELAXED));
    }
    free(sites);
    return n;
}
