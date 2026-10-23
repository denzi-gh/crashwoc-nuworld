/* Synthetic VAG files and an SFX.DAT-style archive for the tests (no game data). */
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

#define SFX_DAT_ALIGN 2048 /* entries start on sectors, as in SFX.DAT */
#define SFX_DAT_MAX (5 * SFX_DAT_ALIGN)

static inline void vag_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* files[0] as sfx\generic\a.vag, files[1] as sfx\music\bl.vag, files[2] as sfx\generic\readme.txt, each at most
   SFX_DAT_ALIGN bytes, into out[SFX_DAT_MAX]; returns the archive size. */
static inline size_t sfx_dat(uint8_t *out, const uint8_t *const files[3], const size_t sizes[3])
{
    static const struct {
        int16_t child, sibling;
        const char *name;
    } nodes[] = {
        { 0, 0, "" },            /* 0 */
        { 2, 0, "sfx" },         /* 1: root */
        { 4, 3, "generic" },     /* 2 */
        { 6, 0, "music" },       /* 3 */
        { 0, 5, "a.vag" },       /* 4: file 0 */
        { -2, 0, "readme.txt" }, /* 5: file 2 */
        { -1, 0, "bl.vag" },     /* 6: file 1 */
    };
    const uint32_t nnodes = sizeof(nodes) / sizeof(nodes[0]);
    size_t at = 4 * SFX_DAT_ALIGN, name = 0;

    memset(out, 0, SFX_DAT_MAX);
    vag_le32(out, (uint32_t)at);
    vag_le32(out + at, 0xffffffffu);
    vag_le32(out + at + 4, 3);
    at += 8;
    for (uint32_t i = 0; i < 3; i++) {
        memcpy(out + (i + 1) * SFX_DAT_ALIGN, files[i], sizes[i]);
        vag_le32(out + at, (i + 1) * SFX_DAT_ALIGN);
        vag_le32(out + at + 4, (uint32_t)sizes[i]);
        vag_le32(out + at + 8, (uint32_t)sizes[i]);
        at += 16;
    }
    vag_le32(out + at, nnodes);
    at += 4;
    size_t names_at = at + nnodes * 8 + 4;
    for (uint32_t i = 0; i < nnodes; i++) {
        vag_le32(out + at, (uint16_t)nodes[i].child | (uint32_t)(uint16_t)nodes[i].sibling << 16);
        vag_le32(out + at + 4, (uint32_t)name);
        memcpy(out + names_at + name, nodes[i].name, strlen(nodes[i].name) + 1);
        name += strlen(nodes[i].name) + 1;
        at += 8;
    }
    vag_le32(out + at, (uint32_t)name);
    vag_le32(out + 4, (uint32_t)(names_at + name - 4 * SFX_DAT_ALIGN));
    return names_at + name;
}

#endif
