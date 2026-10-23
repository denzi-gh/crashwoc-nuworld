/* Builds skin DMA lists in memory as NuPs2CreateSkinDma wrote them. */
#ifndef NUWORLD_TEST_SKIN_BUILDER_H
#define NUWORLD_TEST_SKIN_BUILDER_H

#include "vif_builder.h"

/* A skin point as AddPointDataToPacket writes it: nw weights w[k] for the group's matrix slot[k], or a dupe of the
   batch's vertex dupe - 1. */
typedef struct VifSkinVtx {
    float pos[3], nrm[3], uv[2];
    float w[8];
    uint8_t slot[8];
    uint8_t nw;
    uint8_t dupe;
    bool adc;
} VifSkinVtx;

typedef struct VifSkinDelta {
    uint32_t shape, vertex; /* the batch's vertex */
    float d[3];
} VifSkinDelta;

/* NuPs2CreateSkinDma's output: the tag list, then the RET blocks its CALLs reach. */
typedef struct VifSkin {
    VifBuild head, body;
    bool first;
} VifSkin;

enum {
    VIF_SKIN_MSCNT = 32,   /* kick the batch with MSCNT */
    VIF_SKIN_COLOUR = 64,  /* STCYCL 5,3 (vertex colours) */
};

static void vif_skin_begin(VifSkin *s)
{
    memset(s, 0, sizeof(*s));
    s->first = true;
}

static void vif_skin_tag(VifBuild *b, uint32_t lo, uint32_t addr, uint32_t vif0, uint32_t vif1)
{
    vif_word(b, lo);
    vif_word(b, addr);
    vif_word(b, vif0);
    vif_word(b, vif1);
}

/* CNT (mark 7), a REF per matrix (mark 8), the CALL of the group's RET block (mark 6), which opens. */
static void vif_skin_group(VifSkin *s, const uint8_t *joints, int n)
{
    if (s->body.len) {
        vif_close_tag(&s->body, 6, 0);
    }
    vif_skin_tag(&s->head, 1u << 28 | 7u << 16, 0, 0x01000108, 0);
    for (int i = 0; i < n; i++) {
        vif_skin_tag(&s->head, 3u << 28 | 8u << 16 | 4, (uint32_t)joints[i] << 6, i ? 0 : 0x10000000,
                     0x6C040018 | (uint32_t)i);
    }
    vif_skin_tag(&s->head, 5u << 28, (uint32_t)s->body.len, 0, 0);
    vif_open_tag(&s->body);
    uint32_t stcycl = 0x01000101;
    memcpy(s->body.buf + s->body.tag + 8, &stcycl, 4);
}

static uint32_t vif_skin_tagged(float f, uint32_t low)
{
    return (vif_fbits(f) & ~0xFFu) | low;
}

/* One packet of n points: their runs of V4-32 pairs at TOP + 3 + 4i (a dupe an S-16 pair), the weights after
   STCYCL 1,1 as WriteWeights writes them, the blend shapes as GenModData, then the kick (0x302 for the list's first
   batch and after skinmod, else MSCNT). */
static void vif_skin_batch(VifSkin *s, const VifSkinVtx *v, uint32_t n, const VifSkinDelta *deltas, uint32_t ndeltas,
                           unsigned flags)
{
    VifBuild *b = &s->body;
    vif_word(b, 0x6C0180D2);
    vif_word(b, (n + (flags & VIF_BAD_NLOOP ? 1 : 0)) | 0x8000);
    vif_word(b, 0x30024000);
    vif_word(b, 0x512);
    vif_word(b, 0);
    vif_word(b, 0x6D018002);
    vif_half(b, (uint16_t)(n * 4));
    vif_half(b, 1);
    vif_half(b, 0);
    vif_half(b, 0);
    vif_word(b, flags & VIF_SKIN_COLOUR ? 0x01000305 : 0x01000204);
    size_t run = 0;
    uint32_t first = 0, count = 0;
    for (uint32_t i = 0; i <= n; i++) {
        if ((i == n || v[i].dupe) && count) {
            uint32_t code = 0x6C008000u | count * 2 << 16 | (3 + 4 * first);
            memcpy(b->buf + run, &code, 4);
            count = 0;
        }
        if (i == n) {
            break;
        }
        if (v[i].dupe) {
            vif_word(b, 0x61028000u | (3 + 4 * i));
            vif_half(b, v[i].dupe);
            vif_half(b, v[i].adc);
            continue;
        }
        if (count == 0) {
            run = b->len;
            first = i;
            vif_word(b, 0);
        }
        count++;
        uint32_t maxix = 0;
        float maxw = 0.0f;
        for (uint32_t k = 0; k < v[i].nw; k++) {
            if (maxw < v[i].w[k]) {
                maxw = v[i].w[k];
                maxix = v[i].slot[k];
            }
        }
        for (int k = 0; k < 3; k++) {
            vif_word(b, vif_fbits(v[i].pos[k]));
        }
        vif_word(b, vif_skin_tagged(v[i].nrm[0], 0));
        vif_word(b, vif_skin_tagged(v[i].uv[0], maxix));
        vif_word(b, vif_skin_tagged(v[i].uv[1], v[i].nw < 5 ? v[i].nw : 4));
        vif_word(b, vif_skin_tagged(v[i].nrm[1], v[i].nw > 4 ? v[i].nw - 4u : 0));
        vif_word(b, vif_skin_tagged(v[i].nrm[2], v[i].adc));
    }
    vif_word(b, 0x01000101);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t cnt = v[i].dupe ? 0 : v[i].nw, addr = 5 + 4 * i, w[8];
        static const uint32_t codes[5] = { 0, 0x60018000, 0x64018000, 0x68018000, 0x6C018000 };
        for (uint32_t k = 0; k < cnt; k++) {
            w[k] = (vif_fbits(v[i].w[k]) & ~7u) | v[i].slot[k];
        }
        if (cnt == 8) {
            vif_word(b, addr | 0x6C028000);
        } else if (cnt) {
            vif_word(b, addr | codes[cnt < 4 ? cnt : 4]);
        }
        for (uint32_t k = 0; k < cnt; k++) {
            if (k == 4 && cnt < 8) {
                vif_word(b, (addr + 1) | codes[cnt - 4]);
            }
            vif_word(b, w[k]);
        }
    }
    bool blended = ndeltas > 0;
    if (blended) {
        vif_word(b, 0x01000101);
        vif_word(b, 0x13000000);
        for (uint32_t k = 0; k < ndeltas;) {
            uint32_t shape = deltas[k].shape, cnt = 0;
            while (k + cnt < ndeltas && deltas[k + cnt].shape == shape) {
                cnt++;
            }
            vif_word(b, 0x6E010038);
            uint8_t hdr[4] = { (uint8_t)shape, (uint8_t)(cnt + 0x3A), 0, 0 };
            vif_bytes(b, hdr, 4);
            vif_word(b, cnt << 16 | 0x6C000039);
            for (uint32_t e = 0; e < cnt; e++, k++) {
                for (int c = 0; c < 3; c++) {
                    vif_word(b, vif_fbits(deltas[k].d[c]));
                }
                vif_word(b, deltas[k].vertex * 4 + 3);
            }
            vif_word(b, 0x1400000C);
            vif_word(b, 0x17000000);
        }
    }
    bool mscal = (s->first || blended) && !(flags & VIF_SKIN_MSCNT);
    vif_word(b, mscal ? 0x14000302 : 0x17000000);
    s->first = false;
}

/* Closes the last RET block, appends the list's RET and makes the CALL addresses relative to the list start. */
static void vif_skin_end(VifSkin *s, VifBuild *out)
{
    vif_close_tag(&s->body, 6, 0);
    vif_skin_tag(&s->head, 6u << 28, 0, 0, 0);
    for (size_t off = 0; off < s->head.len; off += 16) {
        uint32_t lo, addr;
        memcpy(&lo, s->head.buf + off, 4);
        memcpy(&addr, s->head.buf + off + 4, 4);
        if (lo >> 28 == 5) {
            addr += (uint32_t)s->head.len;
            memcpy(s->head.buf + off + 4, &addr, 4);
        }
    }
    memset(out, 0, sizeof(*out));
    vif_bytes(out, s->head.buf, s->head.len);
    vif_bytes(out, s->body.buf, s->body.len);
    vif_free(&s->head);
    vif_free(&s->body);
}

#endif
