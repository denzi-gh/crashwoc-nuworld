/* Builds a synthetic NU20 scene in memory with every block NuGScnLoadFix reads, plus an unknown one. */
#ifndef NUWORLD_TEST_GSC_BUILDER_H
#define NUWORLD_TEST_GSC_BUILDER_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "vif_builder.h"

typedef struct GscBuild {
    uint8_t *buf;
    size_t len, cap;
    size_t blk; /* the open block's header */
} GscBuild;

/* Where the test scene put what the tests check. */
typedef struct GscScene {
    size_t blob[4], blob_size[4];   /* geoms: V4-32 chain, packed, OT batch, face-on */
    size_t geom0_mtl, sub0_type, geom0_size; /* fields to corrupt */
    uint32_t names[5];              /* NTBL offsets: Stencil_A, Special, Anim, Script, Node */
    uint32_t batches[2], verts[2], tris[2]; /* what geoms 0 and 1 decode to */
} GscScene;

static void gsc_bytes(GscBuild *b, const void *p, size_t n)
{
    if (b->len + n > b->cap) {
        b->cap = (b->len + n) * 2 + 1024;
        b->buf = realloc(b->buf, b->cap);
    }
    memcpy(b->buf + b->len, p, n);
    b->len += n;
}

static void gsc_u32(GscBuild *b, uint32_t v)
{
    gsc_bytes(b, &v, 4);
}

static void gsc_f32(GscBuild *b, float f)
{
    gsc_bytes(b, &f, 4);
}

static void gsc_zero(GscBuild *b, size_t n)
{
    while (n--) {
        uint8_t z = 0;
        gsc_bytes(b, &z, 1);
    }
}

static void gsc_put32(GscBuild *b, size_t at, uint32_t v)
{
    memcpy(b->buf + at, &v, 4);
}

static void gsc_open(GscBuild *b, const char id[4])
{
    b->blk = b->len;
    gsc_bytes(b, id, 4);
    gsc_u32(b, 0);
}

static void gsc_close(GscBuild *b)
{
    gsc_put32(b, b->blk + 4, (uint32_t)(b->len - b->blk));
}

/* A self-relative pointer at `field` to `target` (RELOCATE). */
static void gsc_rel(GscBuild *b, size_t field, size_t target)
{
    gsc_put32(b, field, (uint32_t)(int32_t)(target - field));
}

/* numtl_s: next (an index + 1, or -1), attrib, diffuse, alpha, tid -1. */
static void gsc_mtl(GscBuild *b, int32_t next, uint32_t attrib0, uint32_t attrib1)
{
    size_t at = b->len;
    gsc_zero(b, 0x1D0);
    gsc_put32(b, at + 0x168, (uint32_t)next);
    gsc_put32(b, at + 0x16C, attrib0);
    gsc_put32(b, at + 0x170, attrib1);
    float diffuse[3] = { 1.0f, 0.5f, 0.25f }, alpha = 1.0f;
    memcpy(b->buf + at + 0x180, diffuse, 12);
    memcpy(b->buf + at + 0x1A0, &alpha, 4);
    gsc_put32(b, at + 0x1A4, (uint32_t)-1);
}

/* A texture upload as the converter writes it: DMA RET + DIRECT, A+D TRXPOS/TRXREG/TRXDIR, IMAGE of n bytes. */
static inline void gsc_upload(GscBuild *b, uint32_t w, uint32_t h, const void *data, size_t n)
{
    uint32_t qwc = 5 + (uint32_t)(n + 15) / 16;
    const uint32_t head[24] = {
        0x60000000 | qwc, 0, 0, 0x50000000 | qwc, 0x8003, 0x10000000, 0xE, 0, 0, 0, 0x51, 0, w, h, 0x52, 0,
        0, 0, 0x53, 0, 0x8000 | (uint32_t)(n + 15) / 16, 0x08000000, 0, 0,
    };
    gsc_bytes(b, head, sizeof(head));
    gsc_bytes(b, data, n);
    gsc_zero(b, (16 - n % 16) % 16);
}

/* A version 1 native texture of type 4-7 with one level (upload uw x uh); returns where that upload starts. */
static inline size_t gsc_texture(GscBuild *b, uint32_t type, uint32_t w, uint32_t h, const uint8_t clut[][4],
                                 const void *up, uint32_t uw, uint32_t uh, size_t n, uint32_t tbw, uint32_t dbw)
{
    bool psmt4 = type == 4 || type == 6;
    uint16_t dims[4] = { (uint16_t)w, 1, (uint16_t)h, 0 };
    gsc_bytes(b, dims, sizeof(dims));
    gsc_u32(b, 0);
    uint8_t formats[4] = { (uint8_t)type, psmt4 ? 0x14 : 0x13, type >= 6 ? 0x00 : (psmt4 ? 0x14 : 0x13), 0x81 };
    gsc_bytes(b, formats, 4);
    size_t pal = b->len;
    gsc_u32(b, 0);
    gsc_u32(b, 0x2000);
    gsc_u32(b, 1);
    gsc_u32(b, 0);
    gsc_upload(b, psmt4 ? 8 : 16, psmt4 ? 2 : 16, clut, psmt4 ? 64 : 1024);
    gsc_put32(b, pal, (uint32_t)(b->len - pal));
    size_t mip = b->len;
    gsc_u32(b, 0);
    gsc_u32(b, (uint32_t)n);
    gsc_u32(b, tbw);
    gsc_u32(b, dbw);
    gsc_u32(b, 12); /* pad: the upload 16-aligned in the record */
    gsc_zero(b, 12);
    size_t at = b->len;
    gsc_upload(b, uw, uh, up, n);
    gsc_put32(b, mip, (uint32_t)(b->len - mip - 12));
    return at;
}

/* A type 0 geom record: int, skip to a 16-aligned blob, material, the blob. */
static size_t gsc_geom(GscBuild *b, uint32_t mtl, const void *blob, size_t size, size_t *mtl_field)
{
    gsc_u32(b, 0);
    size_t skip = (16 - (b->len + 12) % 16) % 16;
    gsc_u32(b, (uint32_t)skip);
    if (mtl_field) {
        *mtl_field = b->len;
    }
    gsc_u32(b, mtl);
    gsc_zero(b, skip);
    gsc_u32(b, (uint32_t)size);
    size_t at = b->len;
    gsc_bytes(b, blob, size);
    return at;
}

static void gsc_bounds(GscBuild *b)
{
    const float bounds[6] = { -1.0f, -2.0f, -3.0f, 4.0f, 5.0f, 6.0f };
    gsc_bytes(b, bounds, sizeof(bounds));
    gsc_u32(b, 0);
    gsc_u32(b, 0);
}

static void gsc_vtx(VifVtx *v, uint32_t n, float z)
{
    for (uint32_t i = 0; i < n; i++) {
        v[i] = (VifVtx){ .x = (float)(i / 2), .y = (float)(i & 1), .z = z, .nx = 1.0f, .u = 0.5f * (float)i, .v = 0.25f,
                         .ny = 0.5f, .nz = 0.5f, .rgba = { 0x80, 0x40, (uint8_t)i, 0x7F } };
    }
}

/* Materials tricu, OT tric, tric; object 0 a V4-32 chain and a packed geom, object 1 an OT and a face-on geom. */
static void gsc_build_scene(GscBuild *b, GscScene *s)
{
    memset(b, 0, sizeof(*b));
    memset(s, 0, sizeof(*s));
    gsc_bytes(b, "NU20", 4);
    gsc_u32(b, 0);
    gsc_u32(b, 6);
    gsc_u32(b, 0);

    gsc_open(b, "TST0");
    gsc_u32(b, 0);
    gsc_u32(b, 0);
    gsc_close(b);

    gsc_open(b, "MS00");
    gsc_u32(b, 3);
    gsc_u32(b, 0);
    gsc_mtl(b, 3, 2u << 16, 0);                      /* tricu, next -> material 2 */
    gsc_mtl(b, -1, 1u << 18, 1u << 3);               /* tric, OT */
    gsc_mtl(b, -1, 1u << 18, 0);                     /* tric */
    gsc_close(b);

    VifVtx v[6];
    const bool adc[6] = { 1, 1, 0, 0, 0, 0 };
    VifBuild g0, g1, fo;
    gsc_vtx(v, 6, 0.0f);
    vif_begin(&g0);
    vif_batch(&g0, v, adc, 6, 0);
    vif_cnt(&g0);
    vif_batch(&g0, v, adc, 4, VIF_SPLIT_NEXT);
    vif_end(&g0, 48);
    const float span[3] = { 4096.0f, 4096.0f, 4096.0f }, mid[3] = { 0.0f, 0.0f, 0.0f };
    vif_begin(&g1);
    vif_packed(&g1, span, mid);
    vif_batch(&g1, v, adc, 5, VIF_FAST_UV);
    vif_end(&g1, 0);
    vif_begin(&fo);
    vif_faceon(&fo, 2);
    vif_end(&fo, 0);
    s->batches[0] = 2, s->verts[0] = 10, s->tris[0] = 6;
    s->batches[1] = 1, s->verts[1] = 5, s->tris[1] = 3;

    gsc_open(b, "OBJ0");
    gsc_u32(b, 2); /* objects */
    gsc_u32(b, 0); /* no colour references */
    gsc_u32(b, 1); /* object 0: one sub-object */
    gsc_zero(b, 12);
    s->sub0_type = b->len;
    gsc_u32(b, 0);
    gsc_u32(b, 2);
    gsc_f32(b, 1.0f);
    gsc_f32(b, 2.0f);
    gsc_f32(b, 3.0f);
    gsc_u32(b, 7); /* tricount */
    gsc_zero(b, 8);
    s->blob[0] = gsc_geom(b, 0, g0.buf, g0.len, &s->geom0_mtl);
    s->blob_size[0] = g0.len;
    s->geom0_size = s->blob[0] - 4;
    s->blob[1] = gsc_geom(b, 2, g1.buf, g1.len, NULL);
    s->blob_size[1] = g1.len;
    gsc_bounds(b);
    gsc_u32(b, 2); /* object 1: two sub-objects */
    gsc_zero(b, 12);
    gsc_u32(b, 0);
    gsc_u32(b, 1);
    gsc_zero(b, 12 + 12);
    uint8_t ot[16 + 0xA0] = { 1 }; /* one triangle block */
    s->blob[2] = gsc_geom(b, 1, ot, sizeof(ot), NULL);
    s->blob_size[2] = sizeof(ot);
    gsc_bounds(b);
    gsc_u32(b, 1); /* face-on */
    gsc_u32(b, 1);
    gsc_zero(b, 8);
    gsc_u32(b, 0);
    gsc_u32(b, 3);
    gsc_f32(b, 1.5f);
    gsc_u32(b, (uint32_t)fo.len);
    s->blob[3] = b->len;
    s->blob_size[3] = fo.len;
    gsc_bytes(b, fo.buf, fo.len);
    gsc_bounds(b);
    gsc_close(b);
    vif_free(&g0);
    vif_free(&g1);
    vif_free(&fo);

    gsc_open(b, "INST");
    gsc_u32(b, 3);
    gsc_u32(b, 0);
    for (int i = 0; i < 3; i++) {
        float m[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, (float)i * 10.0f, 0, 0, 1 };
        gsc_bytes(b, m, sizeof(m));
        gsc_u32(b, i == 1);  /* objid */
        gsc_u32(b, 1);       /* visible */
        gsc_u32(b, i == 1);  /* animated */
        gsc_u32(b, 0);
    }
    gsc_close(b);

    static const char names[40] = "\0Stencil_A\0Special\0Anim\0Script\0Node";
    const uint32_t name_at[5] = { 1, 11, 19, 24, 31 };
    memcpy(s->names, name_at, sizeof(name_at));
    gsc_open(b, "NTBL");
    gsc_u32(b, sizeof(names));
    gsc_bytes(b, names, sizeof(names));
    gsc_close(b);

    gsc_open(b, "SPEC");
    gsc_u32(b, 1);
    gsc_u32(b, 0);
    gsc_zero(b, 64);
    gsc_u32(b, 2);           /* instance */
    gsc_u32(b, name_at[1]);  /* name */
    gsc_u32(b, 3);           /* flags */
    gsc_u32(b, 0);
    gsc_close(b);

    gsc_open(b, "SST0");
    gsc_u32(b, 1);
    gsc_u32(b, 8 + 3 * 12);
    gsc_u32(b, 3);
    gsc_u32(b, name_at[0]);
    for (int i = 0; i < 9; i++) {
        gsc_f32(b, (float)i);
    }
    gsc_close(b);

    gsc_open(b, "IABL");
    gsc_u32(b, 1);
    gsc_u32(b, 0);
    gsc_zero(b, 0x5C);
    uint8_t ix[4] = { 9 }; /* anim_ix */
    gsc_bytes(b, ix, 4);
    gsc_close(b);

    /* ALIB: anim -> chunk -> curveset -> constants and curves (one null) -> curve -> 2 keys; table entry 1 null. */
    gsc_open(b, "ALIB");
    gsc_u32(b, 2);
    gsc_u32(b, 0);
    size_t table = b->len;
    gsc_zero(b, 8);
    size_t anim = b->len;
    gsc_f32(b, 2.5f);
    gsc_u32(b, 0x1234); /* node_names: a file value retail leaves */
    gsc_u32(b, 1);
    gsc_u32(b, 0);
    size_t chunks = b->len;
    gsc_u32(b, 0);
    size_t chunk = b->len;
    gsc_u32(b, 1);
    gsc_u32(b, 1);
    gsc_u32(b, 0);
    gsc_u32(b, 0x55);
    gsc_u32(b, 0x66);
    size_t sets = b->len;
    gsc_u32(b, 0);
    size_t curveset = b->len;
    gsc_u32(b, 1);
    gsc_zero(b, 8);
    uint8_t ncurves[4] = { 2 };
    gsc_bytes(b, ncurves, 4);
    size_t constants = b->len;
    gsc_f32(b, 0.75f);
    gsc_f32(b, -0.75f);
    size_t curves = b->len;
    gsc_zero(b, 8);
    size_t curve = b->len;
    gsc_u32(b, 0xF0);
    gsc_u32(b, 0);
    gsc_u32(b, 2);
    gsc_u32(b, 0);
    size_t keys = b->len;
    for (int i = 0; i < 8; i++) {
        gsc_f32(b, 0.5f * (float)i);
    }
    gsc_rel(b, table, anim);
    gsc_rel(b, anim + 12, chunks);
    gsc_rel(b, chunks, chunk);
    gsc_rel(b, chunk + 8, sets);
    gsc_rel(b, sets, curveset);
    gsc_rel(b, curveset + 4, constants);
    gsc_rel(b, curveset + 8, curves);
    gsc_rel(b, curves, curve);
    gsc_rel(b, curve + 4, keys);
    gsc_close(b);

    gsc_open(b, "INID");
    gsc_u32(b, 2);
    gsc_u32(b, 0);
    gsc_u32(b, 5 | 7u << 16);
    gsc_close(b);

    gsc_open(b, "TAS0");
    gsc_u32(b, 1);
    gsc_u32(b, 0);
    gsc_zero(b, 16);
    gsc_u32(b, 2);            /* mtl */
    gsc_u32(b, 0);
    gsc_u32(b, name_at[2]);   /* ntaname */
    gsc_u32(b, name_at[3]);   /* scriptname */
    gsc_u32(b, 0);            /* no tids */
    gsc_close(b);

    gsc_open(b, "XXXX");
    gsc_u32(b, 0);
    gsc_close(b);

    gsc_put32(b, 4, (uint32_t)-(int32_t)b->len);
}

static void gsc_free(GscBuild *b)
{
    free(b->buf);
    memset(b, 0, sizeof(*b));
}

#endif
