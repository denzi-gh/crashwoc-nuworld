/* Scene geometry: the VIF decoder and the NU20 reader on synthetic data. Usage: test_scene <scratch dir>. */
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "gfx/gsc.h"
#include "gfx/vif.h"

#include "gsc_builder.h"
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
    /* The hash recipe, pinned in tests/test_gsc_hash.py too. */
    char line[256];
    nw_gsc_obj_line(0, &m, line, sizeof(line));
    CHECK(strcmp(line, "OBJ 0 batches=1 verts=4 tris=2 "
                       "vtx=5ff61ee696e31a115059cd16ee188d6eb4aa25b17e7a40ac6583f6893c700941 "
                       "idx=a55571c9db30026e44ac0bdd7674d9c597d8254732feb18418f3aaf8a5b4f418") == 0);
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

/* The synthetic scene: every block, the objects' blobs, the OT and face-on flags, the materials and instances. */
static void test_gsc(void)
{
    GscBuild b;
    GscScene s;
    gsc_build_scene(&b, &s);
    NwGsc g;
    CHECK(nw_gsc_open(&g, b.buf, b.len));
    CHECK(g.nchunks == 12 && g.unknown == 1 && !g.truncated);
    CHECK(g.nmtls == 3 && g.ninsts == 3 && g.nobjects == 2 && g.nblobs == 4);
    if (g.nobjects == 2 && g.nblobs == 4) {
        CHECK(g.objects[0].first == 0 && g.objects[0].count == 2);
        CHECK(g.objects[1].first == 2 && g.objects[1].count == 2);
        const uint32_t mtl[4] = { 0, 2, 1, 0 };
        for (int i = 0; i < 4; i++) {
            CHECK(g.blobs[i].offset == s.blob[i] && g.blobs[i].size == s.blob_size[i] && g.blobs[i].mtl == mtl[i]);
            CHECK(g.blobs[i].ot == (i == 2) && g.blobs[i].faceon == (i == 3));
        }
        for (int i = 0; i < 2; i++) {
            NwVifMesh m = { 0 };
            CHECK(nw_vif_decode(&m, g.data + g.blobs[i].offset, g.blobs[i].size));
            CHECK(m.nbatches == s.batches[i] && m.nverts == s.verts[i] && m.ntris == s.tris[i]);
            nw_vif_free(&m);
        }
    }
    NwGscMtl m = nw_gsc_mtl(&g, 0);
    CHECK(NW_GSC_ATTRIB_LIGHTING(m.attrib) == 2 && !NW_GSC_ATTRIB_OT(m.attrib) && m.tid == -1);
    CHECK(m.diffuse[1] == 0.5f && m.alpha == 1.0f);
    m = nw_gsc_mtl(&g, 1);
    CHECK(NW_GSC_ATTRIB_COLOUR(m.attrib) == 1 && NW_GSC_ATTRIB_OT(m.attrib));
    NwGscInst inst = nw_gsc_inst(&g, 2);
    CHECK(inst.objid == 0 && inst.flags == 1 && inst.mtx[12] == 20.0f && inst.mtx[15] == 1.0f);
    nw_gsc_close(&g);

    /* The last block cut short: the walk stops there. */
    CHECK(nw_gsc_open(&g, b.buf, b.len - 2));
    CHECK(g.truncated && g.nchunks == 11 && g.unknown == 0 && g.nobjects == 2);
    nw_gsc_close(&g);

    uint8_t *bad = malloc(b.len);
    struct {
        size_t at;
        uint32_t value;
        const char *error;
    } cases[] = {
        { 0, 0x3032554F, "not an NU20 scene" },
        { 8, 5, "scene version 5" },
        { 20, 4, "of 4 bytes" },
        { s.geom0_mtl, 9, "material 9 of 3" },
        { s.sub0_type, 2, "type 2" },
        { s.geom0_size, 0x18, "geom of 24 bytes" },
    };
    for (size_t i = 0; i < SDL_arraysize(cases); i++) {
        memcpy(bad, b.buf, b.len);
        memcpy(bad + cases[i].at, &cases[i].value, 4);
        CHECK_FAILS(nw_gsc_open(&g, bad, b.len), cases[i].error);
    }
    free(bad);
    gsc_free(&b);
}

/* The PSMT4 texture tests/test_gsc_hash.py pins: index (x + 2y) & 15, CLUT entry c = 16c, 255 - 16c, 3c, 8c. */
static void make_psmt4(uint8_t clut[16][4], uint8_t packed[32])
{
    for (int c = 0; c < 16; c++) {
        const uint8_t rgba[4] = { (uint8_t)(c * 16), (uint8_t)(255 - c * 16), (uint8_t)(c * 3), (uint8_t)(c * 8) };
        memcpy(clut[c], rgba, 4);
    }
    memset(packed, 0, 32);
    for (int i = 0; i < 64; i++) {
        packed[i >> 1] |= (uint8_t)((((i & 7) + 2 * (i >> 3)) & 15) << (i & 1) * 4);
    }
}

/* ps2_texture.py's _unswizzle8: where PSMT8 pixel (x, y) of a w-wide texture sits in its PSMCT32 upload. */
static size_t swizzle8(uint32_t x, uint32_t y, uint32_t w)
{
    size_t block = (size_t)(y & ~15u) * w + (x & ~15u) * 2;
    uint32_t swap = (((y + 2) >> 2) & 1) * 4, py = (((y & ~3u) >> 1) + (y & 1)) & 7;
    return block + (size_t)py * w * 2 + ((x + swap) & 7) * 4 + ((y >> 1) & 1) + ((x >> 2) & 2);
}

/* Native records: PSMT4, PSMT8 linear and as PSMCT32 with the CSM1 CLUT, the old header, and bad records. */
static void test_texture(void)
{
    GscBuild b = { 0 };
    uint8_t clut16[16][4], packed[32];
    make_psmt4(clut16, packed);
    size_t up = gsc_texture(&b, 4, 8, 8, clut16, packed, 8, 8, sizeof(packed), 2, 2);
    NwTex t;
    bool ok = nw_tex_decode(&t, b.buf, b.len);
    CHECK(ok && t.version == 1 && t.type == 4 && t.tcc && t.ncolours == 16 && t.nlevels == 1);
    if (ok) {
        bool same = t.levels[0].width == 8 && t.levels[0].height == 8;
        for (int i = 0; i < 64 && same; i++) {
            same = t.levels[0].texels[i] == (((i & 7) + 2 * (i >> 3)) & 15);
        }
        CHECK(same && memcmp(t.clut, clut16, sizeof(clut16)) == 0);
        char line[256];
        nw_gsc_tex_line(0, &t, line, sizeof(line));
        CHECK(strcmp(line, "TEX 0 8x8 type=4 version=1 levels=1 "
                           "rgba=8d5cce5284f06b924fa01c1b8a0b335b49fe6182c3533c2dfb9f08d921632092") == 0);
        nw_tex_free(&t);
    }
    uint8_t *bad = malloc(b.len);
    struct {
        size_t at;
        uint32_t value, bytes;
        const char *error;
    } cases[] = {
        { 2, 2, 2, "header version 2" },
        { 12, 1, 1, "type 1" },
        { 12, 2, 1, "type 2 with psm 0x14" },
        { 13, 0x13, 1, "type 4 with psm 0x13" },
        { 15, 0x80, 1, "8x8 with 0 levels" },
        { 28, 1, 4, "CLUT psm 0x1" },
        { up, 0x30000006, 4, "DMA tag id 3" },
        { up, 0x60000006, 4, "has 16 of 32 bytes" },
        { up + 48, 16, 4, "TRXREG 16x8 for an upload of 8x8" },
    };
    for (size_t i = 0; i < SDL_arraysize(cases); i++) {
        memcpy(bad, b.buf, b.len);
        memcpy(bad + cases[i].at, &cases[i].value, cases[i].bytes);
        CHECK_FAILS(nw_tex_decode(&t, bad, b.len), cases[i].error);
    }
    CHECK_FAILS(nw_tex_decode(&t, b.buf, 10), "record of 10 bytes");
    CHECK_FAILS(nw_tex_decode(&t, b.buf, b.len - 16), "level 0 of");
    free(bad);
    gsc_free(&b);

    /* PSMT8: CLUT entry i stored at CSM1 position i ^ (bits 3 and 4), the texels linear (5) or as PSMCT32 (7). */
    static uint8_t clut[256][4], idx[32 * 32], swz[32 * 32];
    for (int i = 0; i < 256; i++) {
        int csm1 = (i & 0xE7) | ((i & 0x10) >> 1) | ((i & 0x08) << 1);
        const uint8_t rgba[4] = { (uint8_t)i, (uint8_t)(255 - i), (uint8_t)(i ^ 0x55), (uint8_t)(i >> 1) };
        memcpy(clut[csm1], rgba, 4);
    }
    for (uint32_t y = 0; y < 32; y++) {
        for (uint32_t x = 0; x < 32; x++) {
            idx[y * 32 + x] = (uint8_t)(x * 7 + y * 13);
            swz[swizzle8(x, y, 32)] = idx[y * 32 + x];
        }
    }
    for (uint32_t type = 5; type <= 7; type += 2) {
        gsc_texture(&b, type, 32, 32, clut, type == 5 ? idx : swz, type == 5 ? 32 : 16, type == 5 ? 32 : 16,
                    sizeof(idx), 2, 1);
        ok = nw_tex_decode(&t, b.buf, b.len);
        CHECK(ok && t.type == type && t.ncolours == 256);
        if (ok) {
            bool same = memcmp(t.levels[0].texels, idx, sizeof(idx)) == 0;
            for (int i = 0; i < 256 && same; i++) {
                same = t.clut[i][0] == i && t.clut[i][1] == 255 - i && t.clut[i][2] == (i ^ 0x55);
            }
            CHECK(same);
            nw_tex_free(&t);
        }
        gsc_free(&b);
    }
    /* Type 6 (PSMT4 as PSMCT32 at a quarter of the height) only needs its buffer widths. */
    gsc_texture(&b, 6, 32, 32, clut16, idx, 16, 8, 16 * 8 * 4, 2, 1);
    ok = nw_tex_decode(&t, b.buf, b.len);
    CHECK(ok && t.levels[0].width == 32 && t.levels[0].height == 32);
    nw_tex_free(&t);
    gsc_free(&b);
    gsc_texture(&b, 6, 32, 32, clut16, idx, 16, 8, 16 * 8 * 4, 1, 1);
    CHECK_FAILS(nw_tex_decode(&t, b.buf, b.len), "buffer widths 1/1");
    gsc_free(&b);

    /* Direct colours: PSMCT24 gets the alpha 0x80 (TCC is off), PSMCT32 keeps its own. */
    for (uint32_t type = 2; type <= 3; type++) {
        gsc_texture(&b, type, 16, 16, NULL, idx, 16, 16, 16 * 16 * (type == 2 ? 3 : 4), 1, 1);
        ok = nw_tex_decode(&t, b.buf, b.len);
        CHECK(ok && t.type == type && t.ncolours == 0 && t.tcc == (type == 3));
        if (ok) {
            const uint8_t *px = t.levels[0].texels + 5 * 4, *src = idx + 5 * (type == 2 ? 3 : 4);
            uint8_t rgba[16 * 4];
            nw_tex_rgba(&t, 0, rgba);
            CHECK(memcmp(px, src, 3) == 0 && px[3] == (type == 2 ? 0x80 : src[3]));
            CHECK(rgba[5 * 4 + 3] == (type == 2 ? 255 : SDL_min(255, src[3] * 2)));
            nw_tex_free(&t);
        }
        gsc_free(&b);
    }

    /* The old header, its level a CNT tag with the A+D setup and a RET tag with the IMAGE. */
    const uint32_t head[3] = { 16, 16, 0 };
    const uint8_t formats[4] = { 5, 0x13, 1, 1 };
    gsc_bytes(&b, head, sizeof(head));
    gsc_bytes(&b, formats, 4);
    size_t pal = b.len;
    const uint32_t palhead[4] = { 0, 0x2000, 1, 0 };
    gsc_bytes(&b, palhead, sizeof(palhead));
    gsc_upload(&b, 16, 16, clut, sizeof(clut));
    gsc_put32(&b, pal, (uint32_t)(b.len - pal));
    size_t mip = b.len;
    const uint32_t level[24] = {
        0, 256, 2, 0, 0x10000004, 0, 0, 0x50000004, 0x8003, 0x10000000, 0xE, 0,
        0, 0, 0x51, 0, 16, 16, 0x52, 0, 0, 0, 0x53, 0,
    };
    const uint32_t image[8] = { 0x60000011, 0, 0, 0x50000011, 0x8010, 0x08000000, 0, 0 };
    gsc_bytes(&b, level, sizeof(level));
    gsc_bytes(&b, image, sizeof(image));
    gsc_bytes(&b, idx, 256);
    gsc_put32(&b, mip, (uint32_t)(b.len - mip));
    ok = nw_tex_decode(&t, b.buf, b.len);
    CHECK(ok && t.version == 0 && t.type == 5 && t.levels[0].width == 16 && t.tcc);
    if (ok) {
        CHECK(memcmp(t.levels[0].texels, idx, 256) == 0 && t.clut[255][0] == 255);
        nw_tex_free(&t);
    }
    gsc_free(&b);
}

/* ReadTextureSet's walk (a skip of 0 reads two more ints), texture lookups and the TEX lines of the report. */
static void test_texset(const char *scratch)
{
    GscBuild b = { 0 };
    uint8_t clut16[16][4], packed[32];
    make_psmt4(clut16, packed);
    gsc_bytes(&b, "NU20", 4);
    gsc_u32(&b, 0);
    gsc_u32(&b, 6);
    gsc_u32(&b, 0);
    gsc_open(&b, "TST0");
    gsc_u32(&b, 2);
    gsc_u32(&b, 0);
    size_t at[2], size[2], count = b.blk + 8;
    for (int i = 0; i < 2; i++) {
        size[i] = b.len;
        gsc_u32(&b, 0);
        gsc_u32(&b, i ? 24 : 0);
        gsc_zero(&b, i ? 24 : 8);
        at[i] = b.len;
        gsc_texture(&b, 4, 8, 8, clut16, packed, 8, 8, sizeof(packed), 2, 2);
        gsc_put32(&b, size[i], (uint32_t)(b.len - at[i]));
    }
    gsc_close(&b);
    gsc_put32(&b, 4, (uint32_t)-(int32_t)b.len);

    NwGsc g;
    NwTex t;
    CHECK(nw_gsc_open(&g, b.buf, b.len));
    CHECK(g.ntextures == 2 && g.textures[0].offset == at[0] && g.textures[1].offset == at[1]);
    CHECK(nw_gsc_texture(&g, 1, &t) && t.levels[0].width == 8);
    nw_tex_free(&t);
    CHECK_FAILS(nw_gsc_texture(&g, 2, &t), "texture 2 of 2");
    char *path = NULL;
    SDL_asprintf(&path, "%s/texset.txt", scratch);
    FILE *out = fopen(path, "w+");
    NwGscTotal total = { 0 };
    CHECK(out && nw_gsc_report(&g, "TEX.GSC", out, &total));
    if (out) {
        nw_gsc_report_total(&total, out);
        char text[1024];
        rewind(out);
        text[fread(text, 1, sizeof(text) - 1, out)] = 0;
        CHECK(strstr(text, "TEX 1 8x8 type=4 version=1 levels=1 rgba=8d5cce5284f06b92") &&
              strstr(text, "\nGSC TEX.GSC chunks=1 unknown=0 truncated=0 objects=0 batches=0 verts=0 tris=0 "
                           "textures=2\n"
                           "TOTAL files=1 failed=0 unknown=0 truncated=0 objects=0 batches=0 verts=0 tris=0 "
                           "textures=2\n"));
        fclose(out);
    }
    SDL_RemovePath(path);
    SDL_free(path);
    nw_gsc_close(&g);

    gsc_put32(&b, count, 99);
    CHECK_FAILS(nw_gsc_open(&g, b.buf, b.len), "TST0 of");
    gsc_put32(&b, count, 2);
    gsc_put32(&b, size[1], 0x10000);
    CHECK_FAILS(nw_gsc_open(&g, b.buf, b.len), "TST0 runs past its block");
    gsc_free(&b);
}

/* NuMtlUpdateEx's choice of microcode package. */
static void test_package(void)
{
    static const struct {
        unsigned fx, lighting, colour;
        const char *package;
    } cases[] = {
        { 0, 0, 0, "vu_load_tri_mpg" },   { 0, 0, 1, "vu_load_tric_mpg" },  { 0, 1, 1, "vu_load_xformuv_mpg" },
        { 0, 2, 0, "vu_load_tricu_mpg" }, { 0, 3, 1, "vu_load_trica_mpg" }, { 1, 2, 0, "vu_load_xformuv_mpg" },
    };
    for (size_t i = 0; i < SDL_arraysize(cases); i++) {
        NwGscMtl m = { { cases[i].fx << 6 | cases[i].lighting << 16 | cases[i].colour << 18, 1u << 3 }, -1, { 0 }, 0 };
        CHECK(strcmp(nw_gsc_mtl_package(&m), cases[i].package) == 0);
    }
}

/* The --check-scenes lines of the synthetic scene. */
static void test_report(const char *scratch)
{
    GscBuild b;
    GscScene s;
    gsc_build_scene(&b, &s);
    char *path = NULL;
    SDL_asprintf(&path, "%s/report.txt", scratch);
    FILE *out = fopen(path, "w+");
    NwGsc g;
    NwGscTotal total = { 0 };
    CHECK(out && nw_gsc_open(&g, b.buf, b.len));
    if (out && g.data) {
        CHECK(nw_gsc_report(&g, "TEST.GSC", out, &total));
        nw_gsc_report_total(&total, out);
        nw_gsc_close(&g);
        char text[1024];
        rewind(out);
        text[fread(text, 1, sizeof(text) - 1, out)] = 0;
        static const char empty[] = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
        char obj1[200];
        SDL_snprintf(obj1, sizeof(obj1), "\nOBJ 1 batches=0 verts=0 tris=0 vtx=%s idx=%s\n", empty, empty);
        CHECK(strncmp(text, "OBJ 0 batches=3 verts=15 tris=9 vtx=", 36) == 0 && strstr(text, obj1));
        CHECK(strstr(text, "\nGSC TEST.GSC chunks=12 unknown=1 truncated=0 objects=2 batches=3 verts=15 tris=9 "
                           "textures=0\n"
                           "TOTAL files=1 failed=0 unknown=1 truncated=0 objects=2 batches=3 verts=15 tris=9 "
                           "textures=0\n"));
    }
    if (out) {
        fclose(out);
    }
    SDL_RemovePath(path);
    SDL_free(path);
    gsc_free(&b);
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "usage: test_scene <scratch dir>\n");
        return 2;
    }
    SDL_CreateDirectory(argv[1]);
    test_batch();
    test_strips();
    test_long_strip();
    test_packed();
    test_chain();
    test_errors();
    test_gsc();
    test_package();
    test_texture();
    test_texset(argv[1]);
    test_report(argv[1]);
    printf("%s\n", failures ? "FAILED" : "scene: all checks passed");
    return failures ? 1 : 0;
}
