#include "audio/vag.h"

#include <SDL3/SDL.h>

/* tbl_XA_Factor: filters 5-15 predict nothing. */
static const int32_t nw_adpcm_filters[16][2] = { { 0, 0 }, { 60, 0 }, { 115, -52 }, { 98, -55 }, { 122, -60 } };

static uint32_t nw_vag_be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | (uint32_t)p[3];
}

bool nw_vag_header(NwVagHeader *h, const void *head, uint64_t file_size)
{
    const uint8_t *b = head;

    if (file_size < NW_VAG_HEADER) {
        return SDL_SetError("VAG of %llu bytes has no header", (unsigned long long)file_size);
    }
    if (SDL_memcmp(b, "VAGp", 4) != 0) {
        return SDL_SetError("not a VAG (no VAGp magic)");
    }
    *h = (NwVagHeader){ nw_vag_be32(b + 4), nw_vag_be32(b + 12), nw_vag_be32(b + 16) };
    if (h->size > file_size - NW_VAG_HEADER) {
        return SDL_SetError("VAG data (%u bytes) runs past the file (%llu bytes)", h->size,
                            (unsigned long long)file_size);
    }
    if (h->size % NW_ADPCM_BLOCK != 0) {
        return SDL_SetError("VAG data (%u bytes) is not whole blocks", h->size);
    }
    return true;
}

unsigned nw_adpcm_block(NwAdpcm *st, const uint8_t *block, int16_t out[NW_ADPCM_SAMPLES])
{
    const int32_t *f = nw_adpcm_filters[block[0] >> 4];
    int shift = block[0] & 15;
    int32_t prev1 = st->prev1, prev2 = st->prev2;

    for (int i = 0; i < NW_ADPCM_SAMPLES; i++) {
        int32_t nibble = (((block[2 + i / 2] >> (i & 1 ? 4 : 0)) & 15) ^ 8) - 8;
        int32_t pcm = ((nibble * 4096) >> shift) + ((f[0] * prev1 + f[1] * prev2 + 32) >> 6);
        pcm = pcm < -32768 ? -32768 : pcm > 32767 ? 32767 : pcm;
        out[i] = (int16_t)pcm;
        prev2 = prev1;
        prev1 = pcm;
    }
    st->prev1 = prev1;
    st->prev2 = prev2;
    return block[1];
}

NwVagPass nw_vag_decode(const uint8_t *blocks, uint32_t count, int16_t *pcm)
{
    NwAdpcm st = { 0, 0 };
    uint32_t loop = 0; /* the SPU2 keeps the voice's old loop address when no START came */

    for (uint32_t i = 0; i < count; i++) {
        unsigned flags = nw_adpcm_block(&st, blocks + (size_t)i * NW_ADPCM_BLOCK, pcm + (size_t)i * NW_ADPCM_SAMPLES);
        if (flags & NW_ADPCM_START) {
            loop = i * NW_ADPCM_SAMPLES;
        }
        if (flags & NW_ADPCM_END) {
            return (NwVagPass){ (i + 1) * NW_ADPCM_SAMPLES, flags & NW_ADPCM_REPEAT ? (int32_t)loop : -1 };
        }
    }
    return (NwVagPass){ count * NW_ADPCM_SAMPLES, -1 };
}
