/* Scene geometry tests: the VIF decoder on hand-assembled geoms. */
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "gfx/vif.h"

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: CHECK(%s) failed; last error: %s\n", __FILE__, __LINE__, #cond, SDL_GetError()); \
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

int main(void)
{
    test_batch();
    printf("%s\n", failures ? "FAILED" : "scene: all checks passed");
    return failures ? 1 : 0;
}
