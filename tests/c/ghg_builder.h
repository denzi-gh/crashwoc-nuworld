/* Builds a synthetic .ghg in memory as NuGHGWrite lays it out: every pointer an offset from the file's start. */
#ifndef NUWORLD_TEST_GHG_BUILDER_H
#define NUWORLD_TEST_GHG_BUILDER_H

#include <stdint.h>
#include <string.h>

#include "gsc_builder.h"
#include "skin_builder.h"

/* Where the test file put what the tests check. */
typedef struct GhgFile {
    size_t size;
    size_t tex, mtl[2], strings, T, INV_WT, joint_ixs, poi_ixs, layer;
    size_t gobj[4];      /* joint 0's chain of two, joint 2's, the skin object */
    size_t collide_data[2], texanim_tids, ellipsoids, shadow_vtx[2];
    uint32_t name[7];    /* root, arm, hand, tip, layer, anim, script */
} GhgFile;

static void ghg_align(GscBuild *b)
{
    gsc_zero(b, (16 - b->len % 16) % 16);
}

static size_t ghg_rec(GscBuild *b, size_t size)
{
    size_t at = b->len;
    gsc_zero(b, size);
    return at;
}

static void ghg_mtx(GscBuild *b, float x, float y, float z)
{
    const float m[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x, y, z, 1 };
    gsc_bytes(b, m, sizeof(m));
}

/* A file gobj (nugobj_s up to origin), its geoms right after it as the writer puts them; each geom a rigid chain
   (n vertices), or the skin list when n is 0. */
static size_t ghg_gobj(GscBuild *b, uint32_t mtl, const uint32_t *n, int ngeoms, size_t next_gobj)
{
    size_t at = ghg_rec(b, 0x5C);
    gsc_put32(b, at, 0xDEAD0001);
    gsc_put32(b, at + 4, 0xDEAD0002);
    const float bounds[6] = { -1.0f, -2.0f, -3.0f, 1.0f, 2.0f, 3.0f };
    memcpy(b->buf + at + 0x1C, bounds, sizeof(bounds));
    gsc_put32(b, at + 0x48, 1);
    gsc_put32(b, at + 0x4C, (uint32_t)next_gobj);
    size_t geoms[4];
    for (int i = 0; i < ngeoms; i++) {
        geoms[i] = ghg_rec(b, 0x10);
        gsc_put32(b, geoms[i] + 4, mtl);
    }
    gsc_put32(b, at + 0x0C, (uint32_t)geoms[0]);
    for (int i = 0; i < ngeoms; i++) {
        if (i + 1 < ngeoms) {
            gsc_put32(b, geoms[i], (uint32_t)geoms[i + 1]);
        }
        ghg_align(b);
        size_t blob = b->len;
        VifBuild v;
        if (n[i]) {
            VifVtx vtx[8];
            bool adc[8] = { 1, 1 };
            gsc_vtx(vtx, n[i], (float)i);
            vif_begin(&v);
            vif_batch(&v, vtx, adc, n[i], 0);
            vif_end(&v, 0);
        } else {
            static const uint8_t joints[2] = { 0, 2 }, slots[2] = { 0, 1 };
            static const float w[2] = { 0.25f, 0.75f };
            VifSkinVtx s[3];
            for (int k = 0; k < 3; k++) {
                s[k] = (VifSkinVtx){ .pos = { (float)k, 1.0f, 0.5f }, .nrm = { 0, 1, 0 }, .nw = 2, .adc = k < 2 };
                memcpy(s[k].w, w, sizeof(w));
                memcpy(s[k].slot, slots, sizeof(slots));
            }
            VifSkin sk;
            vif_skin_begin(&sk);
            vif_skin_group(&sk, joints, 2);
            vif_skin_batch(&sk, s, 3, NULL, 0, 0);
            vif_skin_end(&sk, &v);
        }
        gsc_bytes(b, v.buf, v.len);
        gsc_put32(b, geoms[i] + (n[i] ? 8 : 12), (uint32_t)blob);
        vif_free(&v);
    }
    return at;
}

/* Three joints (root, arm, hand), a POI on the hand, one layer: a chain of two gobjs on the root, none on the arm,
   one on the hand and a skin object; a textured and an untextured material, a texanim, collision and a shadow. */
static inline void ghg_build(GscBuild *b, GhgFile *f)
{
    memset(b, 0, sizeof(*b));
    memset(f, 0, sizeof(*f));
    gsc_zero(b, 0x90);
    size_t tids = ghg_rec(b, 4);
    gsc_put32(b, 0x08, 1);
    gsc_put32(b, 0x0C, (uint32_t)tids);
    ghg_align(b);
    f->tex = b->len;
    const uint8_t clut[16][4] = { { 0 } }, packed[32] = { 0 };
    gsc_texture(b, 4, 8, 8, clut, packed, 8, 8, sizeof(packed), 2, 2, 1);
    gsc_put32(b, tids, (uint32_t)f->tex);

    gsc_put32(b, 0x10, 2);
    size_t mtls = ghg_rec(b, 8);
    gsc_put32(b, 0x14, (uint32_t)mtls);
    for (int i = 0; i < 2; i++) {
        ghg_align(b);
        f->mtl[i] = b->len;
        gsc_mtl(b, 2, 0, 0);
        gsc_put32(b, f->mtl[i] + 0x1A4, i == 0 ? 1 : 0);
        gsc_put32(b, mtls + 4 * (size_t)i, (uint32_t)f->mtl[i]);
    }

    static const char strings[] = "root\0arm\0hand\0tip\0layer\0anim\0script";
    f->strings = b->len;
    for (uint32_t i = 0, at = 0; i < 7; i++) {
        f->name[i] = at;
        at += (uint32_t)strlen(strings + at) + 1;
    }
    gsc_bytes(b, strings, sizeof(strings));
    gsc_put32(b, 0x30, (uint32_t)f->strings);
    gsc_put32(b, 0x34, sizeof(strings));

    ghg_align(b);
    gsc_put32(b, 0x18, 3);
    gsc_put32(b, 0x1C, (uint32_t)b->len);
    for (int i = 0; i < 3; i++) {
        size_t j = ghg_rec(b, 0x60);
        const float orient[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, (float)i, 0, 0, 1 };
        memcpy(b->buf + j, orient, sizeof(orient));
        gsc_put32(b, j + 0x4C, (uint32_t)(f->strings + f->name[i]));
        b->buf[j + 0x50] = (uint8_t)(i - 1);
        b->buf[j + 0x51] = (uint8_t)(0x10 + i);
    }
    f->T = b->len;
    for (int i = 0; i < 3; i++) {
        ghg_mtx(b, i ? 1.0f : 0.0f, 0.0f, 0.0f);
    }
    f->INV_WT = b->len;
    for (int i = 0; i < 3; i++) {
        ghg_mtx(b, -(float)i, 0.0f, 0.0f);
    }
    gsc_put32(b, 0x20, (uint32_t)f->T);
    gsc_put32(b, 0x24, (uint32_t)f->INV_WT);
    f->joint_ixs = b->len;
    gsc_bytes(b, (uint8_t[]){ 2, 1, 0 }, 3);
    gsc_put32(b, 0x28, 3);
    gsc_put32(b, 0x2C, (uint32_t)f->joint_ixs);
    f->poi_ixs = b->len;
    gsc_bytes(b, (uint8_t[]){ 0 }, 1);
    gsc_put32(b, 0x38, 1);
    gsc_put32(b, 0x3C, (uint32_t)f->poi_ixs);
    ghg_align(b);
    size_t poi = ghg_rec(b, 0x50);
    const float identity[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    memcpy(b->buf + poi, identity, sizeof(identity));
    gsc_put32(b, poi + 0x40, (uint32_t)(f->strings + f->name[3]));
    b->buf[poi + 0x44] = 2;
    gsc_put32(b, 0x40, 1);
    gsc_put32(b, 0x44, (uint32_t)poi);

    f->layer = ghg_rec(b, 0x14);
    gsc_put32(b, 0x48, 1);
    gsc_put32(b, 0x4C, (uint32_t)f->layer);
    gsc_put32(b, f->layer, (uint32_t)(f->strings + f->name[4]));
    size_t gobjs = ghg_rec(b, 12);
    gsc_put32(b, f->layer + 4, (uint32_t)gobjs);
    static const uint32_t two[2] = { 4, 6 }, one[1] = { 5 }, skin[1] = { 0 };
    f->gobj[1] = ghg_gobj(b, 1, one, 1, 0);
    f->gobj[0] = ghg_gobj(b, 0, two, 2, f->gobj[1]);
    f->gobj[2] = ghg_gobj(b, 1, one, 1, 0);
    f->gobj[3] = ghg_gobj(b, 0, skin, 1, 0);
    gsc_put32(b, gobjs, (uint32_t)f->gobj[0]);
    gsc_put32(b, gobjs + 8, (uint32_t)f->gobj[2]);
    gsc_put32(b, f->layer + 8, (uint32_t)f->gobj[3]);

    size_t collide = ghg_rec(b, 8), entries = ghg_rec(b, 16);
    gsc_put32(b, collide, 2);
    gsc_put32(b, collide + 4, (uint32_t)entries);
    for (int i = 0; i < 2; i++) {
        f->collide_data[i] = ghg_rec(b, 0x14);
        gsc_put32(b, entries + 8 * (size_t)i, (uint32_t)(i + 1));
        gsc_put32(b, entries + 8 * (size_t)i + 4, (uint32_t)f->collide_data[i]);
        gsc_put32(b, f->collide_data[i], 0x3F800000u + (uint32_t)i);
    }
    gsc_put32(b, 0x80, (uint32_t)collide);

    ghg_align(b);
    size_t anim = ghg_rec(b, 0x20);
    f->texanim_tids = ghg_rec(b, 2);
    gsc_put32(b, anim + 8, (uint32_t)f->texanim_tids);
    gsc_put32(b, anim + 0x0C, 1);
    gsc_put32(b, anim + 0x10, 1);
    gsc_put32(b, anim + 0x18, (uint32_t)(f->strings + f->name[5]));
    gsc_put32(b, anim + 0x1C, (uint32_t)(f->strings + f->name[6]));
    gsc_put32(b, 0x88, 1);
    gsc_put32(b, 0x8C, (uint32_t)anim);

    ghg_align(b);
    size_t shadow = ghg_rec(b, 0x20);
    f->ellipsoids = ghg_rec(b, 0x30);
    size_t mesh = ghg_rec(b, 8);
    f->shadow_vtx[0] = ghg_rec(b, 0x10);
    f->shadow_vtx[1] = ghg_rec(b, 0x10);
    gsc_put32(b, shadow, (uint32_t)f->ellipsoids);
    gsc_put32(b, shadow + 4, 0x1234); /* no cylinders: retail leaves the offset */
    gsc_put32(b, shadow + 8, (uint32_t)mesh);
    gsc_put32(b, shadow + 0x0C, 0x02010001);
    gsc_put32(b, shadow + 0x1C, 0xFF000000);
    gsc_put32(b, mesh, (uint32_t)f->shadow_vtx[0]);
    gsc_put32(b, mesh + 4, (uint32_t)f->shadow_vtx[1]);
    gsc_put32(b, 0x50, (uint32_t)shadow);

    const float bounds[11] = { 2.0f, 0.5f, -1, -2, -3, 1, 2, 3, 0.25f, 1.5f, 0.75f };
    memcpy(b->buf + 0x54, bounds, sizeof(bounds));
    const float scale = 1.25f;
    memcpy(b->buf + 0x84, &scale, 4);
    f->size = b->len;
    gsc_put32(b, 0, (uint32_t)b->len);
}

#endif
