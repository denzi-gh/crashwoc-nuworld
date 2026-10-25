/* Builds a synthetic .shd in memory: the count, the entry and name offsets, the names, then two entries, each a
   header and a native texture. */
#ifndef NUWORLD_TEST_SHD_BUILDER_H
#define NUWORLD_TEST_SHD_BUILDER_H

#include "gsc_builder.h"

/* Where the test file put what the tests check. */
typedef struct ShdFile {
    size_t entry[2], name[2];
} ShdFile;

static void shd_align(GscBuild *b)
{
    gsc_zero(b, (16 - b->len % 16) % 16);
}

static inline void shd_build(GscBuild *b, ShdFile *f)
{
    static const char *names[2] = { "shadow_a", "SHADOW_B" };
    const uint8_t clut[16][4] = { { 0 } }, packed[32] = { 0 };
    memset(b, 0, sizeof(*b));
    memset(f, 0, sizeof(*f));
    int16_t head[2] = { 2, 0 };
    gsc_bytes(b, head, sizeof(head));
    gsc_zero(b, 16);
    for (int i = 0; i < 2; i++) {
        f->name[i] = b->len;
        gsc_bytes(b, names[i], strlen(names[i]) + 1);
    }
    shd_align(b);
    for (int i = 0; i < 2; i++) {
        f->entry[i] = b->len;
        int16_t frames[2] = { (int16_t)(2 + i), 3 };
        gsc_bytes(b, frames, sizeof(frames));
        gsc_u32(b, 0x55); /* pad04 */
        gsc_u32(b, 0);    /* mtl */
        gsc_u32(b, 0);    /* tid */
        gsc_texture(b, 4, 8, 8, clut, packed, 8, 8, sizeof(packed), 2, 2, 1);
        shd_align(b);
        gsc_put32(b, 4 + 4 * (size_t)i, (uint32_t)f->entry[i]);
        gsc_put32(b, 12 + 4 * (size_t)i, (uint32_t)f->name[i]);
    }
}

#endif
