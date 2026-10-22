/* Scene geometry tests: the VIF decoder on hand-assembled and synthetic geoms (vif_builder.h). */
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "gfx/vif.h"

#include "vif_builder.h"

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: CHECK(%s) failed; last error: %s\n", __FILE__, __LINE__, #cond, SDL_GetError()); \
            failures++; \
        } \
    } while (0)

/* call must fail with an error containing text. */
#define CHECK_FAILS(call, text) \
    do { \
        if (call) { \
            fprintf(stderr, "%s:%d: %s succeeded, expected \"%s\"\n", __FILE__, __LINE__, #call, text); \
            failures++; \
        } else if (!SDL_strstr(SDL_GetError(), text)) { \
            fprintf(stderr, "%s:%d: %s: error \"%s\", expected \"%s\"\n", __FILE__, __LINE__, #call, SDL_GetError(), \
                    text); \
            failures++; \
        } \
    } while (0)

static uint32_t fbits(float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

/* One RET-tagged batch of four V4-32 vertices, the first two flagged ADC: the strip 0-1-2-3. */
static void test_batch(void)
{
    uint32_t g[40] = {
        0x60000009, 0, 0, 0,                    /* RET, 9 quadwords, no VIF codes */
        0x6C0180D2, 0x8004, 0x30024000, 0x512, 0, /* the strip GIF tag, NLOOP 4 */
        0x01000103,                             /* STCYCL 3,1 */
        0x6C048003,                             /* UNPACK V4-32 x4 to VU 3 */
    };
    float xyz[4][3] = { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 1, 1, -0.0f } };
    uint32_t *p = g + 11;
    for (int i = 0; i < 4; i++) {
        *p++ = fbits(xyz[i][0]);
        *p++ = fbits(xyz[i][1]);
        *p++ = fbits(xyz[i][2]);
        *p++ = i < 2 ? 0x3F808001u : 0x3F800000u; /* w = the normal's x, ADC in the low half */
    }
    *p++ = 0x65048004; /* UNPACK V2-16 x4 to VU 4 */
    for (int i = 0; i < 4; i++) {
        *p++ = (uint32_t)(uint16_t)(i * 4096) | (uint32_t)(uint16_t)(-i * 2048) << 16;
    }
    *p++ = 0x6E04C005; /* UNPACK V4-8 unsigned x4 to VU 5 */
    for (int i = 0; i < 4; i++) {
        *p++ = 0x80404000u + (uint32_t)i;
    }
    *p++ = 0x01000101; /* STCYCL 1,1 */
    *p++ = 0x14000300; /* MSCAL 0x300 */
    *p++ = 0;          /* NOP to the quadword */
    CHECK(p == g + 40);

    NwVifMesh m = { 0 };
    CHECK(nw_vif_decode(&m, g, sizeof(g)));
    CHECK(m.nverts == 4 && m.ntris == 2 && m.nbatches == 1);
    if (m.nverts == 4 && m.ntris == 2) {
        static const uint32_t tris[6] = { 0, 1, 2, 1, 3, 2 };
        CHECK(memcmp(m.tris, tris, sizeof(tris)) == 0);
        CHECK(m.verts[1].pos[0] == 1.0f && fbits(m.verts[3].pos[2]) == 0x80000000u);
        CHECK(fbits(m.verts[0].pos[3]) == 0x3F808001u);
        CHECK(m.verts[2].tex[0] == 8192 && m.verts[2].tex[1] == -4096 && m.verts[2].tex[2] == 0);
        CHECK(m.verts[3].col[0] == 3 && m.verts[3].col[1] == 0x40 && m.verts[3].col[3] == 0x80);
    }
    /* A second geom appends, indices continue. */
    CHECK(nw_vif_decode(&m, g, sizeof(g)));
    CHECK(m.nverts == 8 && m.ntris == 4 && m.tris[6] == 4);
    nw_vif_free(&m);
}

/* Vertices on a 1/8 grid, exact as V4-32 floats and as V4-16 offsets against a 4096 span. */
static void make_strip(VifVtx *v, uint32_t n, int seed)
{
    for (uint32_t i = 0; i < n; i++) {
        v[i] = (VifVtx){
            .x = (float)(i / 2) * 0.25f - 3.0f, .y = (float)(i & 1) * 0.5f + 1.0f, .z = (float)seed * 0.125f,
            .nx = 1.0f, .u = (float)i * 0.25f, .v = (float)(i & 1) - 0.5f, .ny = 0.5f, .nz = -0.25f,
            .rgba = { (uint8_t)i, (uint8_t)(0x80 - i), 0x40, 0x7F },
        };
    }
}

/* The decoded vertices from `first` match v; packed positions carry w 0, V4-32 ones the normal's x and ADC. */
static bool same_vertices(const NwVifMesh *m, uint32_t first, const VifVtx *v, const bool *adc, uint32_t n,
                          bool packed, bool fast)
{
    for (uint32_t i = 0; i < n; i++) {
        const NwVifVertex *d = &m->verts[first + i];
        uint32_t w = packed ? 0 : (vif_fbits(v[i].nx) & 0xFFFF0000u) | (adc[i] ? 0x8001u : 0);
        int16_t tex[4] = { (int16_t)(v[i].u * 4096.0f), (int16_t)(v[i].v * 4096.0f),
                           fast ? 0 : (int16_t)(v[i].ny * 4096.0f), fast ? 0 : (int16_t)(v[i].nz * 4096.0f) };
        if (d->pos[0] != v[i].x || d->pos[1] != v[i].y || d->pos[2] != v[i].z || vif_fbits(d->pos[3]) != w ||
            memcmp(d->tex, tex, sizeof(tex)) != 0 || memcmp(d->col, v[i].rgba, 4) != 0) {
            fprintf(stderr, "vertex %u differs\n", first + i);
            return false;
        }
    }
    return true;
}

static bool decode(NwVifMesh *m, VifBuild *b)
{
    return nw_vif_decode(m, b->buf, b->len);
}

/* Two strips in one batch: ADC restarts the second; winding alternates on the vertex's index in the batch. */
static void test_strips(void)
{
    VifVtx v[7];
    const bool adc[7] = { 1, 1, 0, 0, 1, 1, 0 };
    make_strip(v, 7, 0);
    VifBuild b;
    vif_begin(&b);
    vif_batch(&b, v, adc, 7, 0);
    vif_end(&b, 0);
    NwVifMesh m = { 0 };
    CHECK(decode(&m, &b));
    static const uint32_t tris[9] = { 0, 1, 2, 1, 3, 2, 4, 5, 6 };
    CHECK(m.nbatches == 1 && m.nverts == 7 && m.ntris == 3);
    CHECK(m.ntris == 3 && memcmp(m.tris, tris, sizeof(tris)) == 0);
    CHECK(same_vertices(&m, 0, v, adc, 7, false, false));
    nw_vif_free(&m);
    vif_free(&b);
}

/* A strip longer than 42 goes out in chunks of 40 that repeat the last two vertices, ADC on both (CNT tags). */
static void test_long_strip(void)
{
    VifVtx v[60];
    bool adc[60] = { 1, 1 };
    const bool restart[22] = { 1, 1 };
    make_strip(v, 60, 1);
    VifBuild b;
    vif_begin(&b);
    vif_batch(&b, v, adc, 40, 0);
    vif_cnt(&b);
    vif_batch(&b, v + 38, restart, 22, 0);
    vif_end(&b, 0);
    NwVifMesh m = { 0 };
    CHECK(decode(&m, &b));
    CHECK(m.nbatches == 2 && m.nverts == 62 && m.ntris == 58);
    bool same = m.ntris == 58;
    for (uint32_t t = 0; same && t < 58; t++) {
        uint32_t i = t + 2, want[3] = { i - 2, i & 1 ? i : i - 1, i & 1 ? i - 1 : i };
        for (int k = 0; k < 3; k++) {
            uint32_t got = m.tris[3 * t + k];
            same = same && (got < 40 ? got : got - 40 + 38) == want[k];
        }
    }
    CHECK(same);
    CHECK(same_vertices(&m, 40, v + 38, restart, 22, false, false));
    nw_vif_free(&m);
    vif_free(&b);
}

/* Compressed V4-16 positions against the STROW span and VU 2 header; a UV row under STMOD 1 is ignored. */
static void test_packed(void)
{
    const float span[3] = { 4096.0f, 4096.0f, 4096.0f }, mid[3] = { 1.0f, 2.0f, 3.0f };
    VifVtx v[5];
    const bool adc[5] = { 1, 1, 0, 0, 0 };
    make_strip(v, 5, 2);
    for (unsigned flags = 0; flags <= VIF_UV_ROW; flags += VIF_UV_ROW) {
        VifBuild b;
        vif_begin(&b);
        vif_packed(&b, span, mid);
        vif_batch(&b, v, adc, 5, VIF_FAST_UV | flags);
        vif_batch(&b, v, adc, 5, flags);
        vif_end(&b, 0);
        NwVifMesh m = { 0 };
        CHECK(decode(&m, &b));
        CHECK(m.nbatches == 2 && m.nverts == 10 && m.ntris == 6);
        CHECK(same_vertices(&m, 0, v, adc, 5, true, true));
        CHECK(same_vertices(&m, 5, v, adc, 5, true, false));
        nw_vif_free(&m);
        vif_free(&b);
    }
}

/* The chain: a batch split by a NEXT tag over a gap, and the converter's stale bytes after the RET. */
static void test_chain(void)
{
    VifVtx v[6];
    const bool adc[6] = { 1, 1, 0, 0, 0, 0 };
    make_strip(v, 6, 3);
    VifBuild b;
    vif_begin(&b);
    vif_batch(&b, v, adc, 6, 0);
    vif_batch(&b, v, adc, 6, VIF_SPLIT_NEXT);
    vif_batch(&b, v, adc, 6, VIF_FAST_UV);
    vif_end(&b, 400);
    NwVifMesh m = { 0 };
    CHECK(decode(&m, &b));
    CHECK(m.nbatches == 3 && m.nverts == 18 && m.ntris == 12);
    CHECK(same_vertices(&m, 6, v, adc, 6, false, false));
    CHECK(same_vertices(&m, 12, v, adc, 6, false, true));
    nw_vif_free(&m);
    vif_free(&b);

    /* Face-on sprites kick 0x304 and are no triangle batch. */
    vif_begin(&b);
    vif_faceon(&b, 5);
    vif_end(&b, 0);
    CHECK(decode(&m, &b));
    CHECK(m.nbatches == 0 && m.nverts == 0);
    nw_vif_free(&m);
    vif_free(&b);
}

static void test_errors(void)
{
    VifVtx v[4];
    const bool adc[4] = { 1, 1, 0, 0 };
    make_strip(v, 4, 4);
    NwVifMesh m = { 0 };
    VifBuild b;

    vif_begin(&b);
    vif_batch(&b, v, adc, 4, 0);
    vif_end(&b, 0);
    CHECK(decode(&m, &b));
    CHECK_FAILS(nw_vif_decode(&m, b.buf, b.len - 16), "runs past the geom");
    CHECK_FAILS(nw_vif_decode(&m, b.buf, 8), "outside the geom");
    b.buf[3] = 0x30; /* REF */
    CHECK_FAILS(decode(&m, &b), "DMA tag id 3");
    CHECK(m.nverts == 4 && m.ntris == 2); /* failures leave the mesh as it was */
    vif_free(&b);

    vif_begin(&b);
    vif_batch(&b, v, adc, 4, 0);
    vif_next(&b, 0x30);
    vif_end(&b, 0);
    uint32_t addr = (uint32_t)b.len + 0x100;
    memcpy(b.buf + 4, &addr, 4);
    CHECK_FAILS(decode(&m, &b), "outside the geom");
    addr = 0;
    memcpy(b.buf + 4, &addr, 4);
    CHECK_FAILS(decode(&m, &b), "sends more than the geom holds");
    vif_free(&b);

    vif_begin(&b);
    vif_batch(&b, v, adc, 4, VIF_BAD_NLOOP);
    vif_end(&b, 0);
    CHECK_FAILS(decode(&m, &b), "NLOOP 5");
    vif_free(&b);

    vif_begin(&b);
    vif_batch(&b, v, adc, 4, VIF_NO_COLOUR);
    vif_end(&b, 0);
    CHECK_FAILS(decode(&m, &b), "without colours");
    vif_free(&b);

    vif_begin(&b);
    vif_word(&b, 0x50000001); /* DIRECT */
    vif_end(&b, 0);
    CHECK_FAILS(decode(&m, &b), "VIF command 0x50");
    vif_free(&b);

    vif_begin(&b);
    vif_word(&b, 0x6C048003); /* 4 positions, no data */
    vif_end(&b, 0);
    CHECK_FAILS(decode(&m, &b), "runs past the end");
    vif_free(&b);

    vif_begin(&b);
    vif_word(&b, 0x01000103);
    vif_word(&b, 0x05000001);
    vif_word(&b, 0x6D018003);
    vif_word(&b, 0);
    vif_word(&b, 0x80010000);
    vif_end(&b, 0);
    CHECK_FAILS(decode(&m, &b), "V4-16 positions without");
    vif_free(&b);

    vif_begin(&b);
    vif_word(&b, 0x01000103);
    vif_word(&b, 0x6C018003);
    for (int i = 0; i < 4; i++) {
        vif_word(&b, 0);
    }
    vif_end(&b, 0);
    CHECK_FAILS(decode(&m, &b), "not kicked");
    vif_free(&b);

    CHECK(m.nverts == 4 && m.nbatches == 1);
    nw_vif_free(&m);
}

int main(void)
{
    test_batch();
    test_strips();
    test_long_strip();
    test_packed();
    test_chain();
    test_errors();
    printf("%s\n", failures ? "FAILED" : "scene: all checks passed");
    return failures ? 1 : 0;
}
