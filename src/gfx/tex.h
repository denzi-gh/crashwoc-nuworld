/* PS2 native textures (NuTexCreateNative's records) decoded to CLUT indices, a port of ps2_texture.py. */
#ifndef NUWORLD_GFX_TEX_H
#define NUWORLD_GFX_TEX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NW_TEX_LEVELS 7 /* nups2tex_s.mipdata */

typedef struct NwTexLevel {
    uint32_t width, height;
    uint8_t *texels; /* a CLUT index per texel */
} NwTexLevel;

typedef struct NwTex {
    uint32_t version; /* of the header: 0 old, 1 cur */
    uint32_t type;    /* nutex_s.type: 4 PSMT4, 5 PSMT8, 6 and 7 the same uploaded as PSMCT32 */
    bool tcc;         /* TEX0.TCC: the texel's alpha takes part */
    uint32_t ncolours;
    uint8_t clut[256][4]; /* r, g, b, a in index order (CSM1 undone); alpha 0x80 = 1.0 */
    uint32_t nlevels;
    NwTexLevel levels[NW_TEX_LEVELS];
} NwTex;

/* Decodes one record of size bytes; false (t empty) on a malformed one or a type it does not handle. */
bool nw_tex_decode(NwTex *t, const void *data, size_t size);
void nw_tex_free(NwTex *t);

/* Row y of level 0 as ps2_texture.py's decode_rgba returns it (alpha doubled, at most 255): width * 4 bytes. */
void nw_tex_rgba(const NwTex *t, uint32_t y, uint8_t *out);

#endif
