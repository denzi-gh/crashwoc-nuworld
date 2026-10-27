/* Audio tests: the VAG header and the PS-ADPCM decoder on hand-built blocks, expected values worked out by hand
   from PCSX2's XA_decode_block, the SPU2 voices in time and their mix, and SFX.DAT access on a synthetic archive.
   Usage: test_audio <scratch dir>.
   Local only, with your own disc: `test_audio retail` finds every sample of SFX.DAT in the extracted disc dir
   NUWORLD_TEST_DATA by its game name, decodes it and checks the pinned census; exits 77 without it. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "audio/sfx.h"
#include "audio/spu2.h"
#include "audio/vag.h"
#include "platform/paths.h"
#include "vfs/sha256.h"
#include "vfs/vfs.h"

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

static void spu_samples(unsigned n)
{
    nw_spu2_run_to(nw_spu2_cycle() + 768u * n);
}

/* Core 0 voice v from ssa: the attack in steps of 0x3800 (ADSR1 0x00ff), the fastest release. */
static void spu_voice(unsigned v, uint32_t ssa, uint16_t pitch)
{
    nw_spu2_set_addr((uint16_t)(v << 1 | NW_SD_VA_SSA), ssa);
    nw_spu2_set_param((uint16_t)(v << 1 | NW_SD_VP_ADSR1), 0x00ff);
    nw_spu2_set_param((uint16_t)(v << 1 | NW_SD_VP_ADSR2), 0x1fc0);
    nw_spu2_set_param((uint16_t)(v << 1 | NW_SD_VP_PITCH), pitch);
}

static uint16_t spu_envx(unsigned v)
{
    return nw_spu2_get_param((uint16_t)(v << 1 | NW_SD_VP_ENVX));
}

static uint32_t spu_nax(unsigned v)
{
    return nw_spu2_get_addr((uint16_t)(v << 1 | NW_SD_VA_NAX));
}

static bool spu_endx(unsigned v)
{
    return nw_spu2_get_switch(NW_SD_S_ENDX) >> v & 1;
}

/* SPU2 voices in time, expected values worked out by hand from PCSX2's Mixer.cpp and ADSR.cpp. At pitch 0x1000 the
   decoder takes a word (4 samples) at each of the first 5 samples, then one every 4 (the n-th at sample 4n - 15),
   so the 7 words of a block's data end at its 7th, 14th, 21st word. */
static void test_spu_voice(void)
{
    const uint32_t one = 0x1000, loop = 0x2000;
    uint8_t blocks[3 * NW_ADPCM_BLOCK];

    vag_block_fill(blocks, 0, 0, 0, 1);
    vag_block_fill(blocks + NW_ADPCM_BLOCK, 0, 0, NW_ADPCM_END, 1);
    nw_spu2_voice_trans(one, blocks, 2 * NW_ADPCM_BLOCK);
    vag_block_fill(blocks, 0, 0, NW_ADPCM_START, 1);
    vag_block_fill(blocks + NW_ADPCM_BLOCK, 0, 0, 0, 1);
    vag_block_fill(blocks + 2 * NW_ADPCM_BLOCK, 0, 0, NW_ADPCM_END | NW_ADPCM_REPEAT, 1);
    nw_spu2_voice_trans(loop, blocks, sizeof(blocks));
    nw_spu2_init();

    /* KON waits for the next sample, which starts at SSA past the header and decodes a word. */
    spu_voice(0, one, 0x1000);
    nw_spu2_set_switch(NW_SD_S_KON, 1u << 0);
    CHECK(spu_envx(0) == 0);
    spu_samples(1);
    CHECK(spu_envx(0) == 0x3800 && spu_nax(0) == one + 4);
    spu_samples(2);
    CHECK(spu_envx(0) == 0x7fff);

    /* A one-shot: the 14th word ends the END block at sample 41, which sets ENDX and silences the voice. */
    spu_samples(37);
    CHECK(spu_envx(0) == 0x7fff && !spu_endx(0));
    spu_samples(1);
    CHECK(spu_envx(0) == 0 && spu_endx(0));

    /* KON clears ENDX with its sample. */
    nw_spu2_set_switch(NW_SD_S_KON, 1u << 0);
    CHECK(spu_endx(0));
    spu_samples(1);
    CHECK(!spu_endx(0));

    /* A loop: START latches the loop address, the 21st word ends the END|REPEAT block at sample 69 and the voice
       goes on at the loop's first data word. An LSAX written after the key-on's sample holds; one written before
       it is dropped by the key-on, and START wins. */
    spu_voice(1, loop, 0x1000);
    spu_voice(2, loop, 0x1000);
    spu_voice(3, loop, 0x1000);
    nw_spu2_set_switch(NW_SD_S_KON, 1u << 1 | 1u << 2 | 1u << 3);
    nw_spu2_set_addr(2 << 1 | NW_SD_VA_LSAX, loop + NW_ADPCM_BLOCK);
    spu_samples(1);
    nw_spu2_set_addr(3 << 1 | NW_SD_VA_LSAX, loop + NW_ADPCM_BLOCK);
    spu_samples(67);
    CHECK(!spu_endx(1));
    spu_samples(1);
    CHECK(spu_endx(1) && spu_envx(1) == 0x7fff && spu_nax(1) == loop + 2);
    CHECK(spu_nax(2) == loop + 2 && spu_nax(3) == loop + NW_ADPCM_BLOCK + 2);

    /* The pitch stops at 0x3fff. At pitch 0 the decoder fills up (4 words, NAX = SSA + 5 words) and waits, while the
       envelope rises. */
    spu_voice(4, loop, 0x3fff);
    spu_voice(5, loop, 0x4000);
    spu_voice(6, loop, 0);
    nw_spu2_set_switch(NW_SD_S_KON, 1u << 4 | 1u << 5 | 1u << 6);
    spu_samples(30);
    CHECK(spu_nax(4) == spu_nax(5) && spu_nax(4) != loop + 10);
    CHECK(spu_nax(6) == loop + 10 && spu_envx(6) == 0x7fff);

    /* KOFF with the fastest release (ADSR2 0x1fc0) takes 0x4000 a sample, from its own sample on. */
    nw_spu2_set_switch(NW_SD_S_KOFF, 1u << 1);
    CHECK(spu_envx(1) == 0x7fff);
    spu_samples(1);
    CHECK(spu_envx(1) == 0x3fff);
    spu_samples(1);
    CHECK(spu_envx(1) == 0);

    /* KOFF and KON for the same sample: the release comes first, then the voice starts again. */
    nw_spu2_set_switch(NW_SD_S_KOFF, 1u << 3);
    nw_spu2_set_switch(NW_SD_S_KON, 1u << 3);
    spu_samples(1);
    CHECK(spu_envx(3) == 0x3800);
}

#define MIX_FRAMES 64

static struct {
    int16_t frame[MIX_FRAMES][2];
    unsigned count;
    unsigned tapped[2 * NW_SPU2_VOICES]; /* words per voice */
} mix;

static void mix_out(void *user, const int16_t *frames, unsigned count)
{
    (void)user;
    for (unsigned i = 0; i < count && mix.count < MIX_FRAMES; i++, mix.count++) {
        mix.frame[mix.count][0] = frames[i * 2];
        mix.frame[mix.count][1] = frames[i * 2 + 1];
    }
}

static void mix_tap(void *user, unsigned voice, const int16_t samples[4])
{
    (void)user;
    (void)samples;
    mix.tapped[voice]++;
}

/* Voice v (0-47) from ssa at pitch 0x1000 with volumes l and r, keyed on. */
static void mix_voice(unsigned v, uint32_t ssa, uint16_t l, uint16_t r)
{
    uint16_t id = (uint16_t)((v >= NW_SPU2_VOICES) | (v % NW_SPU2_VOICES) << 1);
    nw_spu2_set_addr(id | NW_SD_VA_SSA, ssa);
    nw_spu2_set_param(id | NW_SD_VP_ADSR1, 0x00ff);
    nw_spu2_set_param(id | NW_SD_VP_ADSR2, 0x1fc0);
    nw_spu2_set_param(id | NW_SD_VP_PITCH, 0x1000);
    nw_spu2_set_param(id | NW_SD_VP_VOLL, l);
    nw_spu2_set_param(id | NW_SD_VP_VOLR, r);
    nw_spu2_set_switch((uint16_t)((v >= NW_SPU2_VOICES) | NW_SD_S_KON), 1u << (v % NW_SPU2_VOICES));
}

/* sceSdInit, then the IRX's master volumes (0x3fff); the next n frames go to mix. */
static void mix_reset(void)
{
    nw_spu2_init();
    for (uint16_t core = 0; core < 2; core++) {
        nw_spu2_set_param(core | NW_SD_P_MVOLL, 0x3fff);
        nw_spu2_set_param(core | NW_SD_P_MVOLR, 0x3fff);
    }
    memset(&mix, 0, sizeof(mix));
}

static bool mix_all(int16_t l, int16_t r)
{
    for (unsigned i = 0; i < mix.count; i++) {
        if (mix.frame[i][0] != l || mix.frame[i][1] != r) {
            fprintf(stderr, "frame %u: %d %d, not %d %d\n", i, mix.frame[i][0], mix.frame[i][1], l, r);
            return false;
        }
    }
    return mix.count > 0;
}

/* The mix, worked out by hand from PCSX2's Mixer.cpp. A looping block of 4096s (nibble 1, shift 0) at pitch 0x1000
   reads phase 0 of the gaussian table: 600 + 2870 + 608 - 1 = 4077, VOL 0x3fff (0x7ffe) makes 4076. Core 0 goes
   through its MVOL (4075) and core 1's AVOL (4074) into core 1, which takes its MVOL (4073). */
static void test_spu_mix(void)
{
    const uint32_t loud = 0x3000, quiet = 0x3010;
    uint8_t block[NW_ADPCM_BLOCK];

    vag_block_fill(block, 0, 0, NW_ADPCM_START | NW_ADPCM_END | NW_ADPCM_REPEAT, 1);
    nw_spu2_voice_trans(loud, block, sizeof(block));
    vag_block_fill(block, 0, 4, NW_ADPCM_START | NW_ADPCM_END | NW_ADPCM_REPEAT, 1); /* 256s: 253, then 252 */
    nw_spu2_voice_trans(quiet, block, sizeof(block));
    nw_spu2_set_output(mix_out, NULL);
    nw_spu2_set_tap(mix_tap, NULL);

    mix_reset();
    mix_voice(0, loud, 0x3fff, 0);
    spu_samples(40);
    CHECK(mix_all(4073, 0));
    CHECK(mix.tapped[0] > 0 && mix.tapped[1] == 0);

    /* Core 1's voices skip core 0's MVOL and core 1's AVOL. */
    mix_reset();
    mix_voice(NW_SPU2_VOICES + 5, loud, 0, 0x3fff);
    spu_samples(40);
    CHECK(mix_all(0, 4075));
    CHECK(mix.tapped[NW_SPU2_VOICES + 5] > 0);

    /* Core 0's dry voices clamp before its MVOL: 32765, 32764, 32762. */
    mix_reset();
    for (unsigned v = 0; v < 16; v++) {
        mix_voice(v, loud, 0x3fff, 0x3fff);
    }
    spu_samples(40);
    CHECK(mix_all(32762, 32762));

    /* All 48 voices: 24 * 252 through core 0 (6047, 6046), plus core 1's 24 * 252 (12093). */
    mix_reset();
    for (unsigned v = 0; v < 2 * NW_SPU2_VOICES; v++) {
        mix_voice(v, quiet, 0x3fff, 0x3fff);
    }
    spu_samples(40);
    CHECK(mix_all(12093, 12093));

    /* The gates: VMIXL off for the voice, MMIX without core 0's dry voices. */
    mix_reset();
    nw_spu2_set_switch(NW_SD_S_VMIXL, 0xfffffe);
    mix_voice(0, loud, 0x3fff, 0x3fff);
    spu_samples(40);
    CHECK(mix_all(0, 4073));
    mix_reset();
    nw_spu2_set_param(NW_SD_P_MMIX, 0x0f0);
    mix_voice(0, loud, 0x3fff, 0x3fff);
    spu_samples(40);
    CHECK(mix_all(0, 0));

    nw_spu2_set_output(NULL, NULL);
    nw_spu2_set_tap(NULL, NULL);
    nw_spu2_init();
}

static void test_sfx(const char *scratch)
{
    static const char text[] = "a text file in SFX.DAT, longer than a VAG header of 48 bytes";
    static uint8_t arc[5 * SFX_DAT_ALIGN + SFX_DAT_DIR];
    uint8_t blocks[4][NW_ADPCM_BLOCK], a[NW_VAG_HEADER + 3 * NW_ADPCM_BLOCK], bl[NW_VAG_HEADER + 4 * NW_ADPCM_BLOCK];
    uint8_t adpcm[4 * NW_ADPCM_BLOCK], pieces[4 * NW_ADPCM_BLOCK];
    int16_t pcm[4 * NW_ADPCM_SAMPLES];
    char dir[1024], path[1100];
    NwVfs vfs;
    NwDat dat;
    NwSfxSample s;

    vag_block_fill(blocks[0], 0, 0, 0, 1);
    vag_block_fill(blocks[1], 1, 12, NW_ADPCM_END, 0);
    vag_block_fill(blocks[2], 0, 0, 7, 0);
    size_t a_size = vag_file(a, 22050, blocks[0], 3);
    vag_block_fill(blocks[1], 2, 3, NW_ADPCM_START | NW_ADPCM_REPEAT, -5);
    vag_block_fill(blocks[2], 3, 5, NW_ADPCM_REPEAT, 3);
    vag_block_fill(blocks[3], 4, 8, NW_ADPCM_END | NW_ADPCM_REPEAT, -2);
    size_t bl_size = vag_file(bl, 44100, blocks[0], 4);
    const uint8_t *const files[SFX_DAT_FILES] = { a, bl, (const uint8_t *)text, NULL };
    const size_t sizes[SFX_DAT_FILES] = { a_size, bl_size, sizeof(text) - 1, 0 };
    size_t size = sfx_dat(arc, files, sizes);

    SDL_zero(vfs);
    SDL_snprintf(dir, sizeof(dir), "%s/disc", scratch);
    SDL_snprintf(path, sizeof(path), "%s/SFX.DAT", dir);
    if (!SDL_CreateDirectory(dir) || !SDL_SaveFile(path, arc, size) || !nw_vfs_mount(&vfs, NW_VFS_DISC, dir) ||
        !nw_dat_open(&dat, &vfs, "SFX.DAT")) {
        CHECK(!"SFX.DAT through the VFS");
        nw_vfs_shutdown(&vfs);
        return;
    }

    CHECK(nw_sfx_find(&dat, "SFX\\GENERIC\\A.VAG", &s) && s.entry == 0 && s.vag.rate == 22050 && s.vag.size == 48);
    CHECK(nw_sfx_read(&dat, &s, 0, adpcm, s.vag.size) && memcmp(adpcm, a + NW_VAG_HEADER, 48) == 0);
    NwVagPass p = nw_vag_decode(adpcm, 3, pcm);
    CHECK(p.samples == 56 && p.loop_start == -1 && pcm[0] == 4096 && pcm[28] == 3840);
    CHECK(nw_sfx_read(&dat, &s, 48, adpcm, 0));
    CHECK(!nw_sfx_read(&dat, &s, 16, adpcm, 48) && SDL_strstr(SDL_GetError(), "runs past"));
    CHECK(!nw_sfx_read(&dat, &s, 49, adpcm, 0) && SDL_strstr(SDL_GetError(), "runs past"));

    /* Stream pieces of any size add up to the whole sample. */
    CHECK(nw_sfx_find(&dat, "sfx\\Music\\BL.vag", &s) && s.entry == 1 && s.vag.rate == 44100 && s.vag.size == 64);
    CHECK(nw_sfx_read(&dat, &s, 0, adpcm, s.vag.size) && memcmp(adpcm, bl + NW_VAG_HEADER, 64) == 0);
    for (uint32_t at = 0; at < s.vag.size; at += 24) {
        CHECK(nw_sfx_read(&dat, &s, at, pieces + at, s.vag.size - at < 24 ? s.vag.size - at : 24));
    }
    CHECK(memcmp(pieces, adpcm, 64) == 0);
    p = nw_vag_decode(adpcm, 4, pcm);
    CHECK(p.samples == 112 && p.loop_start == 28);

    CHECK(!nw_sfx_find(&dat, "SFX\\GENERIC\\B.VAG", &s) && SDL_strstr(SDL_GetError(), "not in SFX.DAT"));
    CHECK(!nw_sfx_find(&dat, "SFX\\GENERIC", &s) && SDL_strstr(SDL_GetError(), "not in SFX.DAT"));
    CHECK(!nw_sfx_find(&dat, "SFX\\GENERIC\\README.TXT", &s) && SDL_strstr(SDL_GetError(), "README.TXT: not a VAG"));
    nw_dat_close(&dat);
    nw_vfs_shutdown(&vfs);
}

/* ---- Retail (local only) ------------------------------------------------------------------------------- */

#define SFX_SAMPLES 782
#define SFX_STREAM_PIECE 0x6b80 /* half of a 0xD700 stream buffer */
static const char sfx_pcm_sha256[] = "1996556c27a4a970a41a24fd457b1552fc5df4ef1eae0e0be88389fcbb59a643";

typedef struct SfxCensus {
    NwDat *dat;
    NwSha256 sha;
    uint8_t *adpcm, *pieces;
    int16_t *pcm;
    uint32_t cap; /* bytes of adpcm */
    unsigned listed, version_20, rates[3], one_shots, loops, music, music_streamed;
    unsigned filter_max, shift_max;
    uint64_t samples;
} SfxCensus;

static bool census_grow(SfxCensus *c, uint32_t size)
{
    if (size <= c->cap) {
        return true;
    }
    free(c->adpcm);
    free(c->pieces);
    free(c->pcm);
    c->adpcm = malloc(size);
    c->pieces = malloc(size);
    c->pcm = malloc((size_t)size / NW_ADPCM_BLOCK * NW_ADPCM_SAMPLES * sizeof(int16_t));
    c->cap = c->adpcm && c->pieces && c->pcm ? size : 0;
    return c->cap != 0;
}

static void retail_sample(void *user, int index, const char *path)
{
    SfxCensus *c = user;
    char name[32]; /* nusound_filename_info_s.Filename */
    size_t len = strlen(path);
    NwSfxSample s;

    c->listed++;
    if (len >= sizeof(name)) {
        fprintf(stderr, "retail: %s does not fit Filename[32]\n", path);
        failures++;
        return;
    }
    for (size_t i = 0; i <= len; i++) {
        name[i] = (char)SDL_toupper((unsigned char)path[i]);
    }
    if (!nw_sfx_find(c->dat, name, &s) || s.entry != index || !census_grow(c, s.vag.size) ||
        !nw_sfx_read(c->dat, &s, 0, c->adpcm, s.vag.size)) {
        fprintf(stderr, "retail: %s (entry %d): %s\n", name, index, SDL_GetError());
        failures++;
        return;
    }

    uint32_t blocks = s.vag.size / NW_ADPCM_BLOCK;
    c->version_20 += s.vag.version == 0x20;
    c->rates[0] += s.vag.rate == 22050;
    c->rates[1] += s.vag.rate == 11025;
    c->rates[2] += s.vag.rate == 44100;
    for (uint32_t b = 0; b < blocks; b++) {
        const uint8_t *h = c->adpcm + (size_t)b * NW_ADPCM_BLOCK;
        c->filter_max = SDL_max(c->filter_max, (unsigned)h[0] >> 4);
        c->shift_max = SDL_max(c->shift_max, (unsigned)h[0] & 15);
    }
    NwVagPass p = nw_vag_decode(c->adpcm, blocks, c->pcm);
    if (p.loop_start < 0 && p.samples == (blocks - 1) * NW_ADPCM_SAMPLES) {
        c->one_shots++; /* the END block, then a flag 7 block */
    } else if (p.loop_start == NW_ADPCM_SAMPLES && p.samples == blocks * NW_ADPCM_SAMPLES) {
        c->loops++;
    } else {
        fprintf(stderr, "retail: %s: pass of %u samples, loop at %d, %u blocks\n", name, p.samples, p.loop_start,
                blocks);
        failures++;
    }
    c->samples += p.samples;
    nw_sha256_update(&c->sha, c->pcm, (size_t)p.samples * sizeof(int16_t)); /* listing order, s16 LE on x86_64 */

    if (SDL_strncmp(name, "SFX\\MUSIC\\", 10) == 0) {
        bool ok = true;
        c->music++;
        for (uint32_t at = 0; ok && at < s.vag.size; at += SFX_STREAM_PIECE) {
            ok = nw_sfx_read(c->dat, &s, at, c->pieces + at, SDL_min(SFX_STREAM_PIECE, s.vag.size - at));
        }
        c->music_streamed += ok && memcmp(c->pieces, c->adpcm, s.vag.size) == 0;
    }
}

static int test_retail(void)
{
    const char *dir = getenv("NUWORLD_TEST_DATA");
    SfxCensus c = { 0 };
    uint8_t digest[NW_SHA256_SIZE];
    char hex[NW_SHA256_HEX];
    NwVfs vfs;
    NwDat dat;

    if (!dir || !*dir) {
        printf("NUWORLD_TEST_DATA is not set: skipped\n");
        return 77;
    }
    SDL_zero(vfs);
    if (!nw_vfs_mount(&vfs, NW_VFS_DISC, dir) || !nw_dat_open(&dat, &vfs, "SFX.DAT")) {
        fprintf(stderr, "NUWORLD_TEST_DATA: %s\n", SDL_GetError());
        nw_vfs_shutdown(&vfs);
        return 1;
    }
    c.dat = &dat;
    nw_sha256_init(&c.sha);
    CHECK(nw_dat_list(&dat, retail_sample, &c));
    nw_sha256_final(&c.sha, digest);
    nw_sha256_hex(digest, hex);
    printf("SFX.DAT: %u samples (%u v0x20), rates 22050/11025/44100 %u/%u/%u, %u one-shots, %u loops, filters <= %u, "
           "shifts <= %u, %llu samples, %u/%u music streamed\nPCM sha256 %s\n",
           c.listed, c.version_20, c.rates[0], c.rates[1], c.rates[2], c.one_shots, c.loops, c.filter_max,
           c.shift_max, (unsigned long long)c.samples, c.music_streamed, c.music, hex);
    CHECK(c.listed == SFX_SAMPLES && c.version_20 == SFX_SAMPLES);
    CHECK(c.rates[0] == 357 && c.rates[1] == 352 && c.rates[2] == 73);
    CHECK(c.one_shots == 731 && c.loops == 51);
    CHECK(c.filter_max == 4 && c.shift_max == 12);
    CHECK(c.samples == 529663960u);
    CHECK(c.music == 84 && c.music_streamed == 84);
    CHECK(strcmp(hex, sfx_pcm_sha256) == 0);
    free(c.adpcm);
    free(c.pieces);
    free(c.pcm);
    nw_dat_close(&dat);
    nw_vfs_shutdown(&vfs);
    return failures ? 1 : 0;
}

int main(int argc, char *argv[])
{
    if (argc == 2 && strcmp(argv[1], "retail") == 0) {
        return test_retail();
    }
    if (argc != 2) {
        fprintf(stderr, "usage: test_audio <scratch dir> | retail\n");
        return 2;
    }
    const char *scratch = argv[1];
    if (!nw_paths_remove_tree(scratch) || !SDL_CreateDirectory(scratch)) {
        fprintf(stderr, "%s: %s\n", scratch, SDL_GetError());
        return 1;
    }

    test_header();
    test_block();
    test_pass();
    test_spu_voice();
    test_spu_mix();
    test_sfx(scratch);

    nw_paths_remove_tree(scratch);
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("audio: all checks passed\n");
    return 0;
}
