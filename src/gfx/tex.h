/* A port of ps2_texture.py. */
#ifndef NUWORLD_GFX_TEX_H
#define NUWORLD_GFX_TEX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NW_TEX_LEVELS 14 /* Host chain through 1x1 for an 8192px image. */
#define NW_TEX_NATIVE_LEVELS 7 /* nups2tex_s.mipdata */

typedef struct NwTexLevel {
    uint32_t width, height;
    uint8_t *texels; /* a CLUT index per texel, or r, g, b, a (alpha 0x80 = 1.0) without a CLUT */
} NwTexLevel;

typedef struct NwTex {
    uint32_t version; /* of the header: 0 old, 1 cur */
    uint32_t type;    /* nutex_s.type: 2 PSMCT24, 3 PSMCT32, 4 PSMT4, 5 PSMT8, 6 and 7 those two uploaded as PSMCT32 */
    bool tcc;         /* TEX0.TCC: the texel's alpha takes part */
    bool rgba8;       /* External RGBA: alpha 255 is opaque; no GS alpha conversion on import/export. */
    uint32_t ncolours; /* 0 for the direct colour types 2 and 3 */
    uint8_t clut[256][4]; /* r, g, b, a in index order (CSM1 undone); alpha 0x80 = 1.0 */
    uint32_t nlevels;
    uint32_t layers; /* Zero for an ordinary 2D texture; array levels contain every layer consecutively. */
    NwTexLevel levels[NW_TEX_LEVELS];
} NwTex;

/* Decodes one record of size bytes; false (t empty) on a malformed one. */
bool nw_tex_decode(NwTex *t, const void *data, size_t size);
void nw_tex_free(NwTex *t);
/* Owned RGBA base only: deterministic, alpha-aware per-layer reduction; failure leaves t unchanged. */
bool nw_tex_mipmaps(NwTex *t, size_t budget);

/* Row y of level 0 as ps2_texture.py's decode_rgba returns it (alpha doubled, at most 255): width * 4 bytes. */
void nw_tex_rgba(const NwTex *t, uint32_t y, uint8_t *out);

/* GL init publishes its limit before resource loading. Zero means unavailable. */
void nw_tex_set_max_side(int side);
int nw_tex_max_side(void);
void nw_tex_set_max_layers(int layers);
int nw_tex_max_layers(void);

#endif
