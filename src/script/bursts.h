#ifndef NW_SCRIPT_BURSTS_H
#define NW_SCRIPT_BURSTS_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef struct {
    uint32_t frames[2], slot, seed[2], delay[2], flashes[2], on[2], off[2];
} NwBurst;
/* Deterministic integer schedule. Inclusive ranges; budget 0 means 4096 flash operations. */
bool nw_burst_sample(const NwBurst *recipe, int64_t ticks, uint32_t channel, float chance, size_t budget, bool *on);
#endif
