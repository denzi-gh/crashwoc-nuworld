#include "port/stub.h"
#include "app.h"

#include <stdlib.h>
#include <string.h>

static NwStubSite *nw_stub_hits;

static int nw_stub_continue(void)
{
    static int mode = -1;
    if (mode < 0) {
        const char *env = getenv("NUWORLD_STUBS");
        mode = env != NULL && strcmp(env, "continue") == 0;
    }
    return mode;
}

void nw_stub_hit(NwStubSite *site)
{
    if (site->hits++ == 0) {
        site->next = nw_stub_hits;
        nw_stub_hits = site;
        fprintf(stderr, NW_APP_NAME ": stub hit: %s (%s, 0x%08x) has no C yet\n",
                site->name, site->unit, (unsigned)site->address);
    }
    if (!nw_stub_continue()) {
        fprintf(stderr, NW_APP_NAME ": aborting; set NUWORLD_STUBS=continue to log stub hits and go on\n");
        fflush(stderr);
        abort();
    }
}

unsigned nw_stub_report(FILE *out)
{
    unsigned n = 0;
    for (NwStubSite *s = nw_stub_hits; s != NULL; s = s->next, n++) {
        fprintf(out, "%-32s %-40s 0x%08x %u\n", s->unit, s->name, (unsigned)s->address, s->hits);
    }
    return n;
}
