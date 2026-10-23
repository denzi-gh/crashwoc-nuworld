/* Audio tests: the VAG header and the PS-ADPCM decoder on hand-built blocks, expected values worked out by hand
   from PCSX2's XA_decode_block. Usage: test_audio. */
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "audio/vag.h"

#include "vag_builder.h"

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: CHECK(%s) failed; last error: %s\n", __FILE__, __LINE__, #cond, SDL_GetError()); \
            failures++; \
        } \
    } while (0)

static void test_header(void)
{
    uint8_t blocks[2 * NW_ADPCM_BLOCK] = { 0 };
    uint8_t file[NW_VAG_HEADER + sizeof(blocks)];
    size_t size = vag_file(file, 22050, blocks, 2);
    NwVagHeader h;

    CHECK(nw_vag_header(&h, file, size));
    CHECK(h.version == 0x20 && h.size == 32 && h.rate == 22050);
    CHECK(!nw_vag_header(&h, file, NW_VAG_HEADER - 1) && SDL_strstr(SDL_GetError(), "no header"));
    CHECK(!nw_vag_header(&h, file, size - 1) && SDL_strstr(SDL_GetError(), "runs past"));
    put_be32(file + 12, 20);
    CHECK(!nw_vag_header(&h, file, size) && SDL_strstr(SDL_GetError(), "whole blocks"));
    put_be32(file + 12, 32);
    file[3] = 'x';
    CHECK(!nw_vag_header(&h, file, size) && SDL_strstr(SDL_GetError(), "magic"));
}

/* The first n samples of one block from the history (prev1, prev2). */
static void decode(int filter, int shift, const int8_t *nibbles, int n, int32_t prev1, int32_t prev2, int16_t *out)
{
    int8_t all[NW_ADPCM_SAMPLES] = { 0 };
    int16_t pcm[NW_ADPCM_SAMPLES];
    uint8_t b[NW_ADPCM_BLOCK];
    NwAdpcm st = { prev1, prev2 };

    memcpy(all, nibbles, (size_t)n);
    vag_block(b, filter, shift, 0, all);
    nw_adpcm_block(&st, b, pcm);
    memcpy(out, pcm, (size_t)n * sizeof(*out));
}

#define DECODES(filter, shift, prev1, prev2, nibbles, ...) \
    do { \
        static const int8_t n_[] = { nibbles }; \
        static const int16_t want_[] = { __VA_ARGS__ }; \
        int16_t got_[sizeof(want_) / sizeof(want_[0])]; \
        decode(filter, shift, n_, (int)(sizeof(got_) / sizeof(got_[0])), prev1, prev2, got_); \
        CHECK(memcmp(got_, want_, sizeof(want_)) == 0); \
    } while (0)

#define N(...) __VA_ARGS__

static void test_block(void)
{
    /* The nibble alone: (n << 12) >> shift, shifts 13-15 included. */
    DECODES(0, 0, 0, 0, N(1, 7, -1, -8), 4096, 28672, -4096, -32768);
    DECODES(0, 4, 0, 0, N(-3), -768);
    DECODES(0, 12, 0, 0, N(7, -1, -8), 7, -1, -8);
    DECODES(0, 13, 0, 0, N(7, -1), 3, -1);
    DECODES(0, 15, 0, 0, N(7, -8), 0, -1);

    /* Prediction: (f0 * prev1 + f1 * prev2 + 32) >> 6, rounding down. */
    DECODES(1, 0, 1000, 0, N(0, 0, 0), 938, 879, 824);
    DECODES(1, 0, -1001, 0, N(0), -938);
    DECODES(2, 0, 2000, 1000, N(0, 0), 2781, 3372);
    DECODES(3, 0, 2000, 1000, N(0), 2203);
    DECODES(4, 0, 2000, 1000, N(0), 2875);
    DECODES(2, 3, 0, 0, N(3, -5, 7, -8, 0, 1), 1536, 200, 2695, 584, -1140, -2011);
    DECODES(7, 0, 2000, 1000, N(1, 0), 4096, 0);

    /* Clamped at both rails, and the history keeps the clamped sample. */
    DECODES(4, 0, 32767, 0, N(7, 0), 32767, 31743);
    DECODES(4, 0, -32768, 0, N(-8, 0), -32768, -31744);

    /* Flags come back as stored; the history runs on into the next block. */
    uint8_t b[2][NW_ADPCM_BLOCK];
    int16_t pcm[2][NW_ADPCM_SAMPLES];
    NwAdpcm st = { 0, 0 };
    vag_block_fill(b[0], 0, 0, NW_ADPCM_START | NW_ADPCM_REPEAT, 1);
    vag_block_fill(b[1], 1, 12, NW_ADPCM_END, 0);
    CHECK(nw_adpcm_block(&st, b[0], pcm[0]) == (NW_ADPCM_START | NW_ADPCM_REPEAT));
    CHECK(pcm[0][0] == 4096 && pcm[0][27] == 4096 && st.prev1 == 4096 && st.prev2 == 4096);
    CHECK(nw_adpcm_block(&st, b[1], pcm[1]) == NW_ADPCM_END);
    CHECK(pcm[1][0] == 3840 && pcm[1][1] == 3600);
}

/* One pass over blocks with these loop flags; pcm past the pass must stay untouched. */
static NwVagPass pass(const unsigned *flags, uint32_t count)
{
    uint8_t blocks[8][NW_ADPCM_BLOCK];
    int16_t pcm[8 * NW_ADPCM_SAMPLES];

    for (uint32_t i = 0; i < count; i++) {
        vag_block_fill(blocks[i], 0, 0, flags[i], 1);
    }
    memset(pcm, 0x55, sizeof(pcm));
    NwVagPass p = nw_vag_decode(blocks[0], count, pcm);
    CHECK(p.samples <= count * NW_ADPCM_SAMPLES && pcm[p.samples - 1] == 4096);
    CHECK(p.samples == count * NW_ADPCM_SAMPLES || pcm[p.samples] == 0x5555);
    return p;
}

static void test_pass(void)
{
    static const unsigned one_shot[] = { 0, 0, NW_ADPCM_END, 7 }; /* the retail tail: END, then a flag 7 block */
    static const unsigned loop[] = { 0, NW_ADPCM_START | NW_ADPCM_REPEAT, NW_ADPCM_REPEAT,
                                     NW_ADPCM_END | NW_ADPCM_REPEAT };
    static const unsigned open[] = { 0, 0 };
    static const unsigned no_start[] = { 0, NW_ADPCM_END | NW_ADPCM_REPEAT };
    static const unsigned restart[] = { NW_ADPCM_START, 0, NW_ADPCM_START, NW_ADPCM_END | NW_ADPCM_REPEAT };
    static const unsigned self[] = { 0, 7 };
    NwVagPass p;

    p = pass(one_shot, 4);
    CHECK(p.samples == 84 && p.loop_start == -1);
    p = pass(loop, 4);
    CHECK(p.samples == 112 && p.loop_start == 28);
    p = pass(open, 2);
    CHECK(p.samples == 56 && p.loop_start == -1);
    p = pass(no_start, 2);
    CHECK(p.samples == 56 && p.loop_start == 0);
    p = pass(restart, 4);
    CHECK(p.samples == 112 && p.loop_start == 56);
    p = pass(self, 2);
    CHECK(p.samples == 56 && p.loop_start == 28);
}

int main(void)
{
    test_header();
    test_block();
    test_pass();
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("audio: all checks passed\n");
    return 0;
}
