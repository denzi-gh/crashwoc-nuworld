#include "bursts.h"
#include <math.h>

static uint32_t mix32(uint32_t x)
{
    x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16; return x;
}
static uint32_t pick(uint32_t bits, const uint32_t range[2]) { return range[0]+bits%(range[1]-range[0]+1); }
bool nw_burst_sample(const NwBurst *b, int64_t ticks, uint32_t channel, float chance, size_t budget, bool *on)
{
    if (!b || !on || ticks < 0 || !b->slot || !isfinite(chance) || chance < 0 || chance > 100) return false;
    const uint32_t *ranges[] = {b->delay,b->flashes,b->on,b->off};
    for (size_t i=0; i<4; i++) if (ranges[i][1] < ranges[i][0] || ranges[i][1]-ranges[i][0] == UINT32_MAX) return false;
    if (!b->flashes[0] || !b->on[0]) return false;
    *on=false;
    uint32_t h=mix32((uint32_t)((uint64_t)ticks/b->slot)*b->seed[0] ^ channel*b->seed[1]);
    if (h%100 >= (uint32_t)chance) return true;
    int64_t at=(int64_t)((uint64_t)ticks%b->slot)-(int64_t)pick(h>>8,b->delay);
    uint32_t flashes=pick(h>>16,b->flashes); if (!budget) budget=4096;
    for (uint32_t i=0; i<flashes && at>=0; i++) {
        if (!budget--) return false;
        uint32_t f=mix32(h+i), duration=pick(f,b->on), gap=pick(f>>4,b->off);
        if ((uint64_t)at < duration) { *on=true; return true; }
        at -= (int64_t)duration+gap;
    }
    return true;
}
