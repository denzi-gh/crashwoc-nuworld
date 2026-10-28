/* Debug builds (NW_HIGH_HEAP): every free page below 4 GB is reserved before main, so the heaps, the records and the
   game's buffers all lie above it and a pointer kept in 32 bits cannot work by chance. ASan builds keep their own
   layout (its shadow memory is below 4 GB on Linux). */
#ifdef NW_HIGH_HEAP
#ifndef _WIN32
#define _GNU_SOURCE /* MAP_FIXED_NOREPLACE */
#endif
#include <stdint.h>

#define NW_HIGH_HEAP_TOP 0x100000000ull

#ifdef _WIN32
#include <windows.h>

static void nw_high_heap(void)
{
    MEMORY_BASIC_INFORMATION mbi;
    for (uintptr_t at = 0x10000; at < NW_HIGH_HEAP_TOP; at = (uintptr_t)mbi.BaseAddress + mbi.RegionSize) {
        if (!VirtualQuery((void *)at, &mbi, sizeof(mbi))) {
            break;
        }
        if (mbi.State == MEM_FREE) {
            uintptr_t lo = ((uintptr_t)mbi.BaseAddress + 0xFFFF) & ~(uintptr_t)0xFFFF; /* allocation granularity */
            uintptr_t hi = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
            hi = hi > NW_HIGH_HEAP_TOP ? NW_HIGH_HEAP_TOP : hi;
            if (hi > lo) {
                VirtualAlloc((void *)lo, hi - lo, MEM_RESERVE, PAGE_NOACCESS);
            }
        }
    }
}
#else
#include <stdio.h>
#include <sys/mman.h>

static void nw_high_heap_gap(uintptr_t lo, uintptr_t hi)
{
    hi = hi > NW_HIGH_HEAP_TOP ? NW_HIGH_HEAP_TOP : hi;
    if (hi > lo) {
        mmap((void *)lo, hi - lo, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
    }
}

/* The gaps between the mappings, from the kernel's lowest address (vm.mmap_min_addr). */
static void nw_high_heap(void)
{
    FILE *maps = fopen("/proc/self/maps", "r");
    uintptr_t from = 0x10000;
    unsigned long long lo, hi;
    if (!maps) {
        return;
    }
    while (from < NW_HIGH_HEAP_TOP && fscanf(maps, "%llx-%llx%*[^\n]", &lo, &hi) == 2) {
        nw_high_heap_gap(from, (uintptr_t)lo);
        from = (uintptr_t)hi > from ? (uintptr_t)hi : from;
    }
    fclose(maps);
    nw_high_heap_gap(from, NW_HIGH_HEAP_TOP);
}
#endif

__attribute__((constructor)) static void nw_high_heap_init(void)
{
    nw_high_heap();
}
#endif
