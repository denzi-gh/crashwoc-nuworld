/* `harness` needs NUWORLD_STUBS=continue; `harness ghost` must abort. */
#include <stdio.h>
#include <string.h>

#include "port/funcmap.h"
#include "port/stub.h"

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "harness: check failed: %s\n", #cond); \
            return 1; \
        } \
    } while (0)

int main(int argc, char *argv[])
{
    const NwFuncmapEntry *e;

    if (argc > 1 && strcmp(argv[1], "ghost") == 0) {
        e = nw_funcmap_find(0x001001a0u);
        CHECK(e != NULL && e->provenance == NW_PROV_STUB);
        e->fn();
        return 0;
    }

    e = nw_funcmap_find(0x00100100u);
    CHECK(e != NULL && strcmp(e->name, "FixAdd") == 0 && e->provenance == NW_PROV_MATCHING);
    CHECK(((int (*)(int, int))e->fn)(2, 3) == 5);
    CHECK(nw_funcmap_find_host(e->fn) == e);

    e = nw_funcmap_find(0x00100120u);
    CHECK(e != NULL && e->provenance == NW_PROV_NEAR);
    CHECK(((int (*)(int))e->fn)(3) == 6);

    e = nw_funcmap_find(0x00100160u);
    CHECK(e != NULL && strcmp(e->name, "FixLoad") == 0 && e->provenance == NW_PROV_PORT);
    CHECK(((int (*)(const char *))e->fn)("x") == 42);

    e = nw_funcmap_find(0x00100180u);
    CHECK(e != NULL && e->provenance == NW_PROV_MATCHING);
    e = nw_funcmap_find(0x00100220u);
    CHECK(e != NULL && e->provenance == NW_PROV_PORT);
    e = nw_funcmap_find(0x00100300u);
    CHECK(e != NULL && strcmp(e->name, "main") == 0 && e->provenance == NW_PROV_STUB);
    e = nw_funcmap_find(0x00100400u);
    CHECK(e != NULL && strcmp(e->name, "FixDeadA") == 0 && e->provenance == NW_PROV_STUB);

    CHECK(nw_funcmap_find(0x00100008u) == NULL);
    CHECK(nw_funcmap_find(0x00100104u) == NULL);
    CHECK(nw_funcmap_find(0x00100000u) == NULL);
    CHECK(nw_funcmap_find(0xffffffffu) == NULL);

    CHECK(nw_stub_report(stdout) == 1);
    printf("ok\n");
    return 0;
}
