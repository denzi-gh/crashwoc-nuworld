#include "port/heap.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "gfx/packet.h"
#include "nucore/nuerror.h"

#define NW_HEAP_FIRST 0x400  /* a side's first chunk; each next one doubles */
#define NW_HEAP_CHUNK 0x10000 /* up to this */
#define NW_SUPERBUFFER_PS2 0x7069C0u /* retail's superbuffer (PCSX2, over PINE) */
#define NW_SUPERBUFFER_ALIGN 0x1000u

typedef struct NwHeapChunk {
    struct NwHeapChunk *next;
    size_t size, used;
    _Alignas(16) char data[];
} NwHeapChunk;

struct NwHeapSide {
    const char *lo, *hi; /* empty until nw_heap_side_own */
    NwHeapChunk *chunks;
    size_t bytes;
    NwHeapRec *recs;
    size_t nrecs, reccap;
    struct NwHeapHole {
        const void *at;
        size_t size;
        const char *why;
    } *holes;
    size_t nholes, holecap;
    struct NwHeapGpu {
        NwGpuOp free;
        void *obj;
    } *gpu;
    size_t ngpu, gpucap;
};

/* EE threads pass a baton, so no lock. */
static NwHeapSide **nw_heap_live;
static size_t nw_heap_nlive, nw_heap_cap;
static const NwHeapRec **nw_heap_index; /* every live record in host address order, rebuilt when stale */
static size_t nw_heap_nindex;
static bool nw_heap_stale = true;
static size_t nw_heap_ngpu;

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
    if (side->nrecs == side->reccap) {
        size_t n = side->reccap ? side->reccap * 2 : 16;
        NwHeapRec *grown = SDL_realloc(side->recs, n * sizeof(*grown));
        if (!grown) {
            nw_heap_oom();
        }
        side->recs = grown;
        side->reccap = n;
    }
    side->recs[side->nrecs++] = (NwHeapRec){ (void *)at, size, NULL };
    nw_heap_stale = true;
    return (void *)at;
}

void *nw_heap_side_at(NwHeapSide *side, size_t size, size_t align, const void *at)
{
    void *rec = nw_heap_side_new(side, size, align);
    side->recs[side->nrecs - 1].at = at;
    return rec;
}

void nw_heap_side_gpu(NwHeapSide *side, NwGpuOp free, void *obj)
{
    if (side->ngpu == side->gpucap) {
        size_t n = side->gpucap ? side->gpucap * 2 : 64;
        struct NwHeapGpu *grown = SDL_realloc(side->gpu, n * sizeof(*grown));
        if (!grown) {
            nw_heap_oom();
        }
        side->gpu = grown;
        side->gpucap = n;
    }
    side->gpu[side->ngpu++] = (struct NwHeapGpu){ free, obj };
    nw_heap_ngpu++;
}

static void nw_heap_side_free(size_t i)
{
    NwHeapSide *side = nw_heap_live[i];
    for (size_t g = 0; g < side->ngpu; g++) {
        nw_gpu_queue(side->gpu[g].free, side->gpu[g].obj);
    }
    for (NwHeapChunk *c = side->chunks, *next; c; c = next) {
        next = c->next;
        SDL_free(c);
    }
    SDL_free(side->recs);
    SDL_free(side->holes);
    SDL_free(side->gpu);
    nw_heap_ngpu -= side->ngpu;
    SDL_free(side);
    nw_heap_live[i] = nw_heap_live[--nw_heap_nlive];
    nw_heap_stale = true;
}

void nw_heap_release(const void *host)
{
    for (size_t i = 0; i < nw_heap_nlive; i++) {
        const NwHeapSide *side = nw_heap_live[i];
        for (size_t r = 0; r < side->nrecs; r++) {
            if ((uintptr_t)host - (uintptr_t)side->recs[r].host < side->recs[r].size) {
                nw_heap_side_free(i);
                return;
            }
        }
    }
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

void nw_heap_side_hole(NwHeapSide *side, const void *at, size_t size, const char *why)
{
    if (side->nholes == side->holecap) {
        size_t n = side->holecap ? side->holecap * 2 : 16;
        struct NwHeapHole *grown = SDL_realloc(side->holes, n * sizeof(*grown));
        if (!grown) {
            nw_heap_oom();
        }
        side->holes = grown;
        side->holecap = n;
    }
    side->holes[side->nholes++] = (struct NwHeapHole){ at, size, why };
}

void nw_heap_hole(const void *at, size_t size, const char *why)
{
    if (at && size) {
        NwHeapSide *side = nw_heap_side();
        nw_heap_side_hole(side, at, size, why);
        nw_heap_side_own(side, at, (const char *)at + size);
    }
}

void nw_heap_each_hole(void (*fn)(const void *at, size_t size, const char *why, void *user), void *user)
{
    for (size_t i = 0; i < nw_heap_nlive; i++) {
        for (size_t h = 0; h < nw_heap_live[i]->nholes; h++) {
            const struct NwHeapHole *x = &nw_heap_live[i]->holes[h];
            fn(x->at, x->size, x->why, user);
        }
    }
}

void *NuMemAllocFn(int size, char *file, int line);

/* Never freed, as retail's. */
void *nw_superbuffer_alloc(int size, char *file, int line)
{
    char *p = NuMemAllocFn(size + (int)NW_SUPERBUFFER_ALIGN, file, line);
    if (!p) {
        return NULL;
    }
    uintptr_t want = NW_SUPERBUFFER_PS2 % NW_SUPERBUFFER_ALIGN, have = (uintptr_t)p % NW_SUPERBUFFER_ALIGN;
    return p + (want - have + NW_SUPERBUFFER_ALIGN) % NW_SUPERBUFFER_ALIGN;
}

void *nw_heap_twin(void *at, size_t charge, size_t size, size_t align)
{
    NwHeapSide *side = nw_heap_side();
    void *rec = nw_heap_side_at(side, size, align, at);
    nw_heap_side_own(side, at, (char *)at + charge);
    return rec;
}

static int nw_heap_cmp(const void *a, const void *b)
{
    uintptr_t x = (uintptr_t)(*(const NwHeapRec *const *)a)->host;
    uintptr_t y = (uintptr_t)(*(const NwHeapRec *const *)b)->host;
    return x < y ? -1 : x > y;
}

const NwHeapRec *nw_heap_find(const void *p)
{
    if (nw_heap_stale) {
        size_t n = 0;
        for (size_t i = 0; i < nw_heap_nlive; i++) {
            n += nw_heap_live[i]->nrecs;
        }
        const NwHeapRec **index = SDL_realloc(nw_heap_index, (n ? n : 1) * sizeof(*index));
        if (!index) {
            nw_heap_oom();
        }
        nw_heap_index = index;
        nw_heap_nindex = 0;
        for (size_t i = 0; i < nw_heap_nlive; i++) {
            for (size_t r = 0; r < nw_heap_live[i]->nrecs; r++) {
                index[nw_heap_nindex++] = &nw_heap_live[i]->recs[r];
            }
        }
        qsort(index, nw_heap_nindex, sizeof(*index), nw_heap_cmp);
        nw_heap_stale = false;
    }
    size_t lo = 0, hi = nw_heap_nindex;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if ((const void *)nw_heap_index[mid]->host <= p) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo == 0) {
        return NULL;
    }
    const NwHeapRec *rec = nw_heap_index[lo - 1];
    return (uintptr_t)p - (uintptr_t)rec->host <= rec->size ? rec : NULL;
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

size_t nw_heap_gpu(void)
{
    return nw_heap_ngpu;
}
