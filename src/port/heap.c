#include "port/heap.h"

#include <stdint.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "nucore/nuerror.h"

#define NW_HEAP_FIRST 0x400  /* a side's first chunk; each next one doubles */
#define NW_HEAP_CHUNK 0x10000 /* up to this */

typedef struct NwHeapChunk {
    struct NwHeapChunk *next;
    size_t size, used;
    _Alignas(16) char data[];
} NwHeapChunk;

struct NwHeapSide {
    const char *lo, *hi; /* empty until nw_heap_side_own */
    NwHeapChunk *chunks;
    size_t bytes;
};

/* EE threads pass a baton, so no lock. */
static NwHeapSide **nw_heap_live;
static size_t nw_heap_nlive, nw_heap_cap;

static _Noreturn void nw_heap_oom(void)
{
    NuErrorProlog("heap.c", __LINE__)("out of host memory for the loaded records");
    for (;;) {
    }
}

NwHeapSide *nw_heap_side(void)
{
    if (nw_heap_nlive == nw_heap_cap) {
        size_t n = nw_heap_cap ? nw_heap_cap * 2 : 256;
        NwHeapSide **grown = SDL_realloc(nw_heap_live, n * sizeof(*grown));
        if (!grown) {
            nw_heap_oom();
        }
        nw_heap_live = grown;
        nw_heap_cap = n;
    }
    NwHeapSide *side = SDL_calloc(1, sizeof(*side));
    if (!side) {
        nw_heap_oom();
    }
    nw_heap_live[nw_heap_nlive++] = side;
    return side;
}

void *nw_heap_side_new(NwHeapSide *side, size_t size, size_t align)
{
    NwHeapChunk *c = side->chunks;
    uintptr_t at = 0;
    if (c) {
        at = ((uintptr_t)c->data + c->used + align - 1) & ~(uintptr_t)(align - 1);
    }
    if (!c || at - (uintptr_t)c->data > c->size || size > c->size - (at - (uintptr_t)c->data)) {
        size_t cap = c ? c->size * 2 : NW_HEAP_FIRST;
        cap = cap > NW_HEAP_CHUNK ? NW_HEAP_CHUNK : cap;
        cap = size + align > cap ? size + align : cap;
        c = SDL_malloc(sizeof(*c) + cap);
        if (!c) {
            nw_heap_oom();
        }
        c->next = side->chunks;
        c->size = cap;
        c->used = 0;
        side->chunks = c;
        side->bytes += cap;
        at = ((uintptr_t)c->data + align - 1) & ~(uintptr_t)(align - 1);
    }
    c->used = at - (uintptr_t)c->data + size;
    memset((void *)at, 0, size);
    return (void *)at;
}

static void nw_heap_side_free(size_t i)
{
    NwHeapSide *side = nw_heap_live[i];
    for (NwHeapChunk *c = side->chunks, *next; c; c = next) {
        next = c->next;
        SDL_free(c);
    }
    SDL_free(side);
    nw_heap_live[i] = nw_heap_live[--nw_heap_nlive];
}

void nw_heap_side_own(NwHeapSide *side, const void *lo, const void *hi)
{
    side->lo = lo;
    side->hi = hi;
    for (size_t i = 0; i < nw_heap_nlive;) {
        NwHeapSide *other = nw_heap_live[i];
        if (other != side && other->lo < side->hi && side->lo < other->hi) {
            nw_heap_side_free(i);
        } else {
            i++;
        }
    }
}

void *nw_heap_twin(void *at, size_t charge, size_t size, size_t align)
{
    NwHeapSide *side = nw_heap_side();
    void *rec = nw_heap_side_new(side, size, align);
    nw_heap_side_own(side, at, (char *)at + charge);
    return rec;
}

size_t nw_heap_sides(size_t *bytes)
{
    size_t total = 0;
    for (size_t i = 0; i < nw_heap_nlive; i++) {
        total += nw_heap_live[i]->bytes;
    }
    if (bytes) {
        *bytes = total;
    }
    return nw_heap_nlive;
}
