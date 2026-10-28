/* The game's heaps (the superbuffer and the buffers carved from it) for the port's loaders (src/port/heap.c): a heap
   holds what retail puts there, so retail's budgets and layout hold; a load's host records and GPU objects live
   beside it, owned by the heap range the load took, until a later load writes over that range. */
#ifndef NUWORLD_PORT_HEAP_H
#define NUWORLD_PORT_HEAP_H

#include <stddef.h>

#include "gfx/packet.h"

typedef struct NwHeapSide NwHeapSide;

/* One host record; at is where retail keeps it (a heap address), NULL when the loader does not know. */
typedef struct NwHeapRec {
    void *host;
    size_t size;
    const void *at;
} NwHeapRec;

/* A load's host records, before it owns a range. */
NwHeapSide *nw_heap_side(void);

/* size zeroed bytes at align. */
void *nw_heap_side_new(NwHeapSide *side, size_t size, size_t align);
/* The same for the host record of retail's record at the heap address at. */
void *nw_heap_side_at(NwHeapSide *side, size_t size, size_t align, const void *at);

/* A GPU object of side's load, freed with its records by the job free (after the frames that draw it). */
void nw_heap_side_gpu(NwHeapSide *side, NwGpuOp free, void *obj);

/* side owns [lo, hi) of a game heap; the records of every other load whose range it overlaps are freed. */
void nw_heap_side_own(NwHeapSide *side, const void *lo, const void *hi);
/* Frees the load whose record holds host now, as a later load over its range would. */
void nw_heap_release(const void *host);

/* The decomp's NW_SUPERBUFFER_ALLOC (nu_port.h): the superbuffer at retail's address modulo 0x1000. */
void *nw_superbuffer_alloc(int size, char *file, int line);

/* The decomp's NW_HEAP_TWIN (nu_port.h): a zeroed host record of size bytes, owned by the heap's charge bytes at
   at. */
void *nw_heap_twin(void *at, size_t charge, size_t size, size_t align);

/* size bytes at at that retail rewrites and the port does not (a DMA chain fixed in place): a hole in a PS2 view of
   the heap (src/port/dump.c), with side's records. */
void nw_heap_side_hole(NwHeapSide *side, const void *at, size_t size, const char *why);
/* The same for bytes no load owns: a side of their own over them. */
void nw_heap_hole(const void *at, size_t size, const char *why);
/* fn for every live hole. */
void nw_heap_each_hole(void (*fn)(const void *at, size_t size, const char *why, void *user), void *user);

/* The live record holding p, its end included unless another record starts there; else NULL. */
const NwHeapRec *nw_heap_find(const void *p);

/* The loads whose records are alive, and their host bytes. */
size_t nw_heap_sides(size_t *bytes);
/* The GPU objects of the live loads. */
size_t nw_heap_gpu(void);

#endif
