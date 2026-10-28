/* Builds .gsc geom packets in memory as NuCvtDmaTriStripCFromIdx / NuCvtDmaFaceOn wrote them. */
#ifndef NUWORLD_TEST_VIF_BUILDER_H
#define NUWORLD_TEST_VIF_BUILDER_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct VifVtx {
    float x, y, z, nx;
    float u, v, ny, nz;
    uint8_t rgba[4];
} VifVtx;

typedef struct VifBuild {
    uint8_t *buf;
    size_t len, cap;
    size_t tag;      /* the open DMA tag */
    bool packed;     /* positions as V4-16 against span - mid */
    float span[3], mid[3];
} VifBuild;

enum {
    VIF_FAST_UV = 1,     /* V2-16 texcoords, no normal */
    VIF_UV_ROW = 2,      /* texcoords under STMOD 1 (the material's UV scroll row) */
    VIF_SPLIT_NEXT = 4,  /* close the chain's tag as NEXT after the positions */
    VIF_NO_COLOUR = 8,
    VIF_BAD_NLOOP = 16,  /* the strip GIF tag counts one vertex more */
};

static uint32_t vif_fbits(float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

static void vif_bytes(VifBuild *b, const void *p, size_t n)
{
    if (b->len + n > b->cap) {
        b->cap = (b->len + n) * 2 + 256;
        b->buf = realloc(b->buf, b->cap);
    }
    memcpy(b->buf + b->len, p, n);
    b->len += n;
}

static void vif_word(VifBuild *b, uint32_t w)
{
    vif_bytes(b, &w, 4);
}

static void vif_half(VifBuild *b, uint16_t h)
{
    vif_bytes(b, &h, 2);
}

static void vif_open_tag(VifBuild *b)
{
    b->tag = b->len;
    for (int i = 0; i < 4; i++) {
        vif_word(b, 0);
    }
}

/* Pads the tag's data with NOPs to a quadword and writes its id, QWC and address. */
static void vif_close_tag(VifBuild *b, uint32_t id, uint32_t addr)
{
    while (b->len & 15) {
        vif_word(b, 0);
    }
    uint32_t lo = id << 28 | (uint32_t)((b->len - b->tag - 16) >> 4);
    memcpy(b->buf + b->tag, &lo, 4);
    memcpy(b->buf + b->tag + 4, &addr, 4);
}

/* Opens the chain and writes the fan GIF tag every packet starts with. */
static void vif_begin(VifBuild *b)
{
    memset(b, 0, sizeof(*b));
    vif_open_tag(b);
    vif_word(b, 0x6C010056);
    vif_word(b, 0x00008000);
    vif_word(b, 0x3002C000);
    vif_word(b, 0x512);
    vif_word(b, 0);
}

/* Compressed positions: STROW with the span's bits; batches then write V4-16 against span - mid. */
static void vif_packed(VifBuild *b, const float span[3], const float mid[3])
{
    b->packed = true;
    memcpy(b->span, span, sizeof(b->span));
    memcpy(b->mid, mid, sizeof(b->mid));
    vif_word(b, 0x30000000);
    for (int k = 0; k < 3; k++) {
        vif_word(b, vif_fbits(span[k]));
    }
    vif_word(b, 0);
}

/* Closes the open tag as CNT, or as NEXT over `gap` bytes (a multiple of 16) of 0xCD, and opens the next one. */
static void vif_cnt(VifBuild *b)
{
    vif_close_tag(b, 1, 0);
    vif_open_tag(b);
}

static void vif_next(VifBuild *b, size_t gap)
{
    while (b->len & 15) {
        vif_word(b, 0);
    }
    size_t next = b->len + gap;
    vif_close_tag(b, 2, (uint32_t)next);
    for (size_t i = 0; i < gap; i++) {
        uint8_t cd = 0xCD;
        vif_bytes(b, &cd, 1);
    }
    vif_open_tag(b);
}

/* One batch of n vertices (n <= 42 as the converter writes them); adc[i] flags the strip starts. */
static void vif_batch(VifBuild *b, const VifVtx *v, const bool *adc, uint32_t n, unsigned flags)
{
    vif_word(b, 0x6C0180D2);
    vif_word(b, (n + (flags & VIF_BAD_NLOOP ? 1 : 0)) | 0x8000);
    vif_word(b, 0x30024000);
    vif_word(b, 0x512);
    vif_word(b, 0);
    if (b->packed) {
        vif_word(b, 0x6C018002);
        vif_word(b, n * 3);
        for (int k = 0; k < 3; k++) {
            vif_word(b, vif_fbits(b->span[k] - b->mid[k]));
        }
    } else {
        vif_word(b, 0x6D018002);
        vif_half(b, (uint16_t)(n * 3));
        vif_half(b, 0);
        vif_half(b, 0);
        vif_half(b, 0);
    }
    vif_word(b, 0x01000103);
    if (b->packed) {
        vif_word(b, 0x05000001);
        vif_word(b, n << 16 | 0x6D008003);
        for (uint32_t i = 0; i < n; i++) {
            const float p[3] = { v[i].x, v[i].y, v[i].z };
            for (int k = 0; k < 3; k++) {
                float packed = b->span[k] + (p[k] - b->mid[k]);
                vif_half(b, (uint16_t)(vif_fbits(packed) - vif_fbits(b->span[k])));
            }
            vif_half(b, adc[i] ? 0x8001 : 0);
        }
        vif_word(b, 0x05000000);
    } else {
        vif_word(b, n << 16 | 0x6C008003);
        for (uint32_t i = 0; i < n; i++) {
            vif_word(b, vif_fbits(v[i].x));
            vif_word(b, vif_fbits(v[i].y));
            vif_word(b, vif_fbits(v[i].z));
            vif_word(b, (vif_fbits(v[i].nx) & 0xFFFF0000u) | (adc[i] ? 0x8001u : 0));
        }
    }
    if (flags & VIF_SPLIT_NEXT) {
        vif_next(b, 0x30);
    }
    if (flags & VIF_UV_ROW) {
        vif_word(b, 0x05000001);
    }
    vif_word(b, n << 16 | (flags & VIF_FAST_UV ? 0x65008004 : 0x6D008004));
    for (uint32_t i = 0; i < n; i++) {
        vif_half(b, (uint16_t)(int32_t)(v[i].u * 4096.0f));
        vif_half(b, (uint16_t)(int32_t)(v[i].v * 4096.0f));
        if (!(flags & VIF_FAST_UV)) {
            vif_half(b, (uint16_t)(int32_t)(v[i].ny * 4096.0f));
            vif_half(b, (uint16_t)(int32_t)(v[i].nz * 4096.0f));
        }
    }
    if (flags & VIF_UV_ROW) {
        vif_word(b, 0x05000000);
    }
    if (!(flags & VIF_NO_COLOUR)) {
        vif_word(b, n << 16 | 0x6E00C005);
        for (uint32_t i = 0; i < n; i++) {
            vif_bytes(b, v[i].rgba, 4);
        }
    }
    vif_word(b, 0x01000101);
    vif_word(b, 0x14000300);
}

/* A face-on packet (NuCvtDmaFaceOn): sprites at VU 4-6, kicked at 0x304; no triangle batch. */
static void vif_faceon(VifBuild *b, uint32_t n)
{
    vif_word(b, 0x6C018000);
    vif_word(b, n | 0x8000);
    vif_word(b, 0x50034000);
    vif_word(b, 0x52521);
    vif_word(b, 0);
    vif_word(b, 0x6D018001);
    vif_half(b, (uint16_t)(n * 3));
    vif_half(b, 0);
    vif_half(b, 0);
    vif_half(b, 0);
    vif_word(b, 0x01000103);
    vif_word(b, n << 16 | 0x68008004);
    for (uint32_t i = 0; i < n * 3; i++) {
        vif_word(b, vif_fbits((float)i));
    }
    vif_word(b, n << 16 | 0x64008005);
    for (uint32_t i = 0; i < n * 2; i++) {
        vif_word(b, vif_fbits(-1.0f));
    }
    vif_word(b, n << 16 | 0x6E00C006);
    for (uint32_t i = 0; i < n; i++) {
        vif_word(b, 0x40808080);
    }
    vif_word(b, 0x01000101);
    vif_word(b, 0x14000304);
}

/* A facey batch as NuCvtDmaFaceY writes it: the fan GIF tag at VU 0x56, then sprite i at (i, 2i, 3i), half size
   (0.5, 1.5) and colour 0x40102030, kicked at 0x306. */
static void vif_facey(VifBuild *b, uint32_t n)
{
    vif_word(b, 0x6C010056);
    vif_word(b, 0x8000);
    vif_word(b, 0x3002C000);
    vif_word(b, 0x512);
    vif_word(b, 0);
    vif_word(b, 0x6C018000);
    vif_word(b, n | 0x8000);
    vif_word(b, 0xC0024000);
    vif_word(b, 0x12512512);
    vif_word(b, 0x5125);
    vif_word(b, 0x6D018001);
    vif_half(b, (uint16_t)(n * 3));
    vif_half(b, 0);
    vif_half(b, 0);
    vif_half(b, 0);
    vif_word(b, 0x01000103);
    vif_word(b, n << 16 | 0x68008004);
    for (uint32_t i = 0; i < n; i++) {
        vif_word(b, vif_fbits((float)i));
        vif_word(b, vif_fbits(2.0f * i));
        vif_word(b, vif_fbits(3.0f * i));
    }
    vif_word(b, n << 16 | 0x64008005);
    for (uint32_t i = 0; i < n; i++) {
        vif_word(b, vif_fbits(0.5f));
        vif_word(b, vif_fbits(1.5f));
    }
    vif_word(b, n << 16 | 0x6E00C006);
    for (uint32_t i = 0; i < n; i++) {
        vif_word(b, 0x40102030);
    }
    vif_word(b, 0x01000101);
    vif_word(b, 0x14000306);
}

/* Closes the chain with RET, then `stale` bytes of the packet again (the converter's leftovers). */
static void vif_end(VifBuild *b, size_t stale)
{
    vif_close_tag(b, 6, 0);
    size_t from = 16;
    for (size_t i = 0; i < stale; i++) {
        uint8_t c = b->buf[from + i % (b->len - from)];
        vif_bytes(b, &c, 1);
    }
}

static void vif_free(VifBuild *b)
{
    free(b->buf);
    memset(b, 0, sizeof(*b));
}

#endif
