/* src/port/heap.c: a load's host records, zeroed at their alignment, live until a later load takes a range over
   theirs; loads beside them, such as the loading screen's in the superbuffer's last 1 MB, leave them. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "port/heap.h"

static int failures;
static char heap[4096];

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

static int all_zero(const char *p, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (p[i]) {
            return 0;
        }
    }
    return 1;
}

int main(void)
{
    NwHeapSide *a = nw_heap_side();
    char *rec = nw_heap_side_new(a, 100, 16);
    char *big = nw_heap_side_new(a, 0x30000, 64); /* past a chunk */
    char *next = nw_heap_side_new(a, 8, 8);
    CHECK(((uintptr_t)rec & 15) == 0 && ((uintptr_t)big & 63) == 0 && ((uintptr_t)next & 7) == 0);
    CHECK(all_zero(rec, 100) && all_zero(big, 0x30000) && all_zero(next, 8));
    memset(rec, 1, 100);
    memset(big, 1, 0x30000);
    CHECK(rec[99] == 1 && next[0] == 0);
    nw_heap_side_own(a, heap, heap + 1024);
    size_t bytes = 0;
    CHECK(nw_heap_sides(&bytes) == 1 && bytes >= 0x30000 + 100 + 8);

    NwHeapSide *top = nw_heap_side();
    nw_heap_side_own(top, heap + 3072, heap + 4096);
    NwHeapSide *after = nw_heap_side();
    nw_heap_side_own(after, heap + 1024, heap + 2048);
    CHECK(nw_heap_sides(NULL) == 3);

    /* The heap rewound: a load over the first range frees its records, a load up to the top one the second's. */
    NwHeapSide *again = nw_heap_side();
    nw_heap_side_own(again, heap, heap + 512);
    CHECK(nw_heap_sides(NULL) == 3);
    NwHeapSide *grown = nw_heap_side();
    nw_heap_side_own(grown, heap + 512, heap + 3072);
    CHECK(nw_heap_sides(NULL) == 3);

    /* Owning a wider range again frees what it now covers. */
    nw_heap_side_own(grown, heap + 512, heap + 4096);
    CHECK(nw_heap_sides(NULL) == 2);

    printf("%s\n", failures ? "FAILED" : "heap: all checks passed");
    return failures ? 1 : 0;
}
