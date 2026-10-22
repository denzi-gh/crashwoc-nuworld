/* The VU1 microcode as GLSL: needs a current GL 3.3 context. */
#ifndef NUWORLD_GFX_VU_H
#define NUWORLD_GFX_VU_H

#include <stdbool.h>

#include "gfx/mtl.h"
#include "gfx/tex.h"

/* The triangle packages' per-vertex programs (tri runs the same code as tric). */
typedef enum NwVuProgram {
    NW_VU_TRIC,
    NW_VU_TRICU,
    NW_VU_TRICA,
    NW_VU_TRICDV,
    NW_VU_PROGRAMS
} NwVuProgram;

/* What vu1init leaves for an object: VF01-04 (local times screen matrix, PS2 row-major), VF23-29 (lights). */
typedef struct NwVuObject {
    float lv[16];
    float ambient[4];
    float lightdir[3][4];
    float lightcol[3][4];
} NwVuObject;

/* A texture on the GPU: CLUT indices (R8UI) and the CLUT (256x1 RGBA8UI), or colours (RGBA8UI) and no CLUT. */
typedef struct NwVuTexture {
    unsigned int index, clut;
} NwVuTexture;

/* What a material gives the programs: diffuse VU 0x13 (x128), uvoff VU 0x14, the texture (NULL or 0: none), state. */
typedef struct NwVuMaterial {
    float diffuse[4];
    float uvoff[2];
    const NwVuTexture *tex;
    NwMtlState state;
} NwVuMaterial;

bool nw_vu_init(void);
void nw_vu_shutdown(void);
/* The program of a microcode package name; xformuv (no scene material uses it) falls back to tric. */
NwVuProgram nw_vu_program(const char *package);
/* vu1init and clipinit alike, with no lights yet: the GPU clips, so clip and trivclip need no counterpart. */
void nw_vu1init(NwVuObject *o, const float local[16], const float view[16]);
void nw_vu_use(NwVuProgram program, const NwVuObject *o, const NwVuMaterial *m);
/* Uploads level 0 of t. */
void nw_vu_texture(NwVuTexture *tex, const NwTex *t);
void nw_vu_texture_free(NwVuTexture *tex);
/* The NwVifVertex attributes of the bound VAO and VBO. */
void nw_vu_attribs(void);

#endif
