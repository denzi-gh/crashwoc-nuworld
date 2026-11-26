/* The memory of one definition: blocks taken from the system as needed and given back together when the definition
   is freed, so that a definition is one allocation to think about, not hundreds. */
#include <stdlib.h>

#include "port/gcutpc/internal.h"

#define ARENA_BLOCK (64 * 1024)
#define ARENA_ALIGN 16

typedef struct ArenaBlock {
    struct ArenaBlock *next;
    size_t used, size;
    _Alignas(ARENA_ALIGN) unsigned char data[];
} ArenaBlock;

struct NUGCUTPCARENA_s {
    ArenaBlock *blocks; /* the newest first */
};

NUGCUTPCARENA_s *NuGCutPCArenaCreate(void)
{
    return calloc(1, sizeof(NUGCUTPCARENA_s));
}

void NuGCutPCArenaDestroy(NUGCUTPCARENA_s *arena)
{
    if (!arena) {
        return;
    }
    for (ArenaBlock *block = arena->blocks, *next; block; block = next) {
        next = block->next;
        free(block);
    }
    free(arena);
}

static ArenaBlock *arena_block(NUGCUTPCARENA_s *arena, size_t bytes)
{
    size_t size = bytes > ARENA_BLOCK ? bytes : ARENA_BLOCK;
    ArenaBlock *block = calloc(1, sizeof(ArenaBlock) + size);
    if (!block) {
        return NULL;
    }
    block->size = size;
    block->next = arena->blocks;
    arena->blocks = block;
    return block;
}

void *NuGCutPCArenaAlloc(NUGCUTPCARENA_s *arena, size_t count, size_t size)
{
    if (size && count > (SIZE_MAX - ARENA_ALIGN) / size) {
        return NULL;
    }
    size_t bytes = (count * size + ARENA_ALIGN - 1) & ~(size_t)(ARENA_ALIGN - 1);
    if (bytes == 0) {
        bytes = ARENA_ALIGN;
    }
    ArenaBlock *block = arena->blocks;
    if (!block || block->size - block->used < bytes) {
        block = arena_block(arena, bytes);
        if (!block) {
            return NULL;
        }
    }
    void *out = block->data + block->used;
    block->used += bytes;
    return out;
}
