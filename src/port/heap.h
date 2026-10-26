/* The game's heaps (the superbuffer and the buffers carved from it) for the port's loaders (src/port/heap.c): a heap
   holds what retail puts there, so retail's budgets and layout hold; a load's host records live beside it, owned by
   the heap range the load took, until a later load writes over that range. */
#ifndef NUWORLD_PORT_HEAP_H
#define NUWORLD_PORT_HEAP_H

#include <stddef.h>

typedef struct NwHeapSide NwHeapSide;

/* A load's host records, before it owns a range. */
NwHeapSide *nw_heap_side(void);

/* size zeroed bytes at align. */
void *nw_heap_side_new(NwHeapSide *side, size_t size, size_t align);

/* side owns [lo, hi) of a game heap; the records of every other load whose range it overlaps are freed. */
void nw_heap_side_own(NwHeapSide *side, const void *lo, const void *hi);

/* The decomp's NW_HEAP_TWIN (nu_port.h): a zeroed host record of size bytes, owned by the heap's charge bytes at
   at. */
void *nw_heap_twin(void *at, size_t charge, size_t size, size_t align);

/* The loads whose records are alive, and their host bytes. */
size_t nw_heap_sides(size_t *bytes);

#endif
