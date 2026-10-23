/* Synthetic VAG files for the tests (no game data). */
#ifndef NUWORLD_TESTS_VAG_BUILDER_H
#define NUWORLD_TESTS_VAG_BUILDER_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "audio/vag.h"

static inline void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/* nibbles are -8..7, low nibble first. */
static inline void vag_block(uint8_t *b, int filter, int shift, unsigned flags, const int8_t nibbles[NW_ADPCM_SAMPLES])
{
    b[0] = (uint8_t)(filter << 4 | shift);
    b[1] = (uint8_t)flags;
    for (int i = 0; i < NW_ADPCM_SAMPLES; i += 2) {
        b[2 + i / 2] = (uint8_t)((nibbles[i] & 15) | (nibbles[i + 1] & 15) << 4);
    }
}

static inline void vag_block_fill(uint8_t *b, int filter, int shift, unsigned flags, int nibble)
{
    int8_t n[NW_ADPCM_SAMPLES];
    memset(n, nibble, sizeof(n));
    vag_block(b, filter, shift, flags, n);
}

/* Header and count blocks into out (NW_VAG_HEADER + count * 16 bytes); returns the file size. */
static inline size_t vag_file(uint8_t *out, uint32_t rate, const uint8_t *blocks, uint32_t count)
{
    memset(out, 0, NW_VAG_HEADER);
    memcpy(out, "VAGp", 4);
    put_be32(out + 4, 0x20);
    put_be32(out + 12, count * NW_ADPCM_BLOCK);
    put_be32(out + 16, rate);
    memcpy(out + 32, "test", 4);
    memcpy(out + NW_VAG_HEADER, blocks, (size_t)count * NW_ADPCM_BLOCK);
    return NW_VAG_HEADER + (size_t)count * NW_ADPCM_BLOCK;
}

#endif
