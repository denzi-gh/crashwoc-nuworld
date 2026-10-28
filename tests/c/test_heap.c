/* src/port/heap.c: a load's host records, zeroed at their alignment, live until a later load takes a range over
   theirs; loads beside them, such as the loading screen's in the superbuffer's last 1 MB, leave them. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

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

static void count_hole(const void *at, size_t size, const char *why, void *user)
{
    (void)at, (void)size, (void)why;
    ++*(unsigned *)user;
}

/* The frees queued so far, as the render thread takes them once every fence has passed. */
static uint32_t take_frees(const void *objs[4])
{
    NwGpuJob *jobs = NULL;
    uint32_t cap = 0, n = nw_gpu_take(UINT64_MAX, &jobs, &cap);
    for (uint32_t i = 0; objs && i < n && i < 4; i++) {
        objs[i] = jobs[i].obj;
    }
    SDL_free(jobs);
    return n;
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

    /* Records are found by any host address inside them, their end included unless the next one starts there, and
       keep where retail has them; holes live and die with their load. */
    NwHeapSide *load = nw_heap_side();
    char *x = nw_heap_side_at(load, 24, 8, heap + 64);
    char *y = nw_heap_side_at(load, 8, 8, heap + 96);
    char *z = nw_heap_side_new(load, 4, 4);
    nw_heap_side_hole(load, heap + 128, 32, "DMA");
    nw_heap_side_own(load, heap, heap + 256);
    const NwHeapRec *r = nw_heap_find(x + 10);
    CHECK(r && r->host == x && r->size == 24 && r->at == heap + 64);
    r = nw_heap_find(y);
    CHECK(r && r->host == y && r->at == heap + 96);
    r = nw_heap_find(z + 4);
    CHECK(r && r->host == z && r->at == NULL);
    CHECK(nw_heap_find(heap) == NULL);
    unsigned holes = 0;
    nw_heap_each_hole(count_hole, &holes);
    CHECK(holes == 1);
    nw_heap_hole(heap + 1024, 16, "packet");
    nw_heap_each_hole(count_hole, &holes);
    CHECK(holes == 3);
    NwHeapSide *over = nw_heap_side();
    nw_heap_side_own(over, heap, heap + 2048);
    CHECK(nw_heap_find(x) == NULL);
    holes = 0;
    nw_heap_each_hole(count_hole, &holes);
    CHECK(holes == 0);

    /* A load's GPU objects are freed with its records, once: by a load over its range, or by its release. */
    NwHeapSide *scene = nw_heap_side();
    nw_heap_side_gpu(scene, NW_GPU_FREE_MESH, heap + 1);
    nw_heap_side_gpu(scene, NW_GPU_FREE_SPRITES, heap + 2);
    nw_heap_side_own(scene, heap, heap + 64);
    CHECK(take_frees(NULL) == 0);
    nw_heap_side_own(nw_heap_side(), heap, heap + 32);
    const void *freed[4];
    CHECK(take_frees(freed) == 2 && freed[0] == heap + 1 && freed[1] == heap + 2);
    NwHeapSide *viewed = nw_heap_side();
    nw_heap_side_gpu(viewed, NW_GPU_FREE_MESH, heap + 3);
    char *sc = nw_heap_side_new(viewed, 16, 8);
    nw_heap_side_own(viewed, heap + 2048, heap + 2112);
    nw_heap_release(sc + 4);
    CHECK(take_frees(freed) == 1 && freed[0] == heap + 3 && nw_heap_find(sc) == NULL);
    nw_heap_side_own(nw_heap_side(), heap + 2048, heap + 2112);
    CHECK(take_frees(NULL) == 0);

    printf("%s\n", failures ? "FAILED" : "heap: all checks passed");
    return failures ? 1 : 0;
}
