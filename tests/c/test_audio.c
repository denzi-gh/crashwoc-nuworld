/* Audio tests: the VAG header and the PS-ADPCM decoder on hand-built blocks, expected values worked out by hand
   from PCSX2's XA_decode_block, and SFX.DAT access on a synthetic archive. Usage: test_audio <scratch dir>.
   Local only, with your own disc: `test_audio retail` finds every sample of SFX.DAT in the extracted disc dir
   NUWORLD_TEST_DATA by its game name, decodes it and checks the pinned census; exits 77 without it. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "audio/sfx.h"
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
    test_sfx(scratch);

    nw_paths_remove_tree(scratch);
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("audio: all checks passed\n");
    return 0;
}
