/* The VU1 microcode as GLSL: needs a current GL 3.3 context. */
#ifndef NUWORLD_GFX_VU_H
#define NUWORLD_GFX_VU_H

#include <stdbool.h>
#include <stdint.h>

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
    bool tcc; /* TEX0.TCC */
} NwVuTexture;

/* What a material gives the programs: diffuse VU 0x13 (x128), the texture (NULL or 0: none), the GS state. */
typedef struct NwVuMaterial {
    float diffuse[4];
    float uvoff[4]; /* VU 0x14 u, v; the STROW row u, v (x4096) when the geom unpacks its texcoords under STMOD 1 */
    const NwVuTexture *tex;
    NwMtlState state;
} NwVuMaterial;

bool nw_vu_init(void);
void nw_vu_shutdown(void);
/* The program of a microcode package name; xformuv (no scene material uses it) falls back to tric. */
NwVuProgram nw_vu_program(const char *package);
/* vu1init and clipinit alike, with no lights yet: the GPU clips, so clip and trivclip need no counterpart. */
void nw_vu1init(NwVuObject *o, const float local[16], const float view[16]);
/* Sets the program and the material's state; returns the passes its alpha test draws the triangles in. */
uint32_t nw_vu_use(NwVuProgram program, const NwVuObject *o, const NwVuMaterial *m, NwMtlPass pass[2]);
/* One of those passes: which side of the alpha test it keeps, and the buffers it writes. */
void nw_vu_pass(NwVuProgram program, const NwVuMaterial *m, const NwMtlPass *pass);
/* Uploads t with its mip levels. */
void nw_vu_texture(NwVuTexture *tex, const NwTex *t);
void nw_vu_texture_free(NwVuTexture *tex);
/* The NwVifVertex attributes of the bound VAO and VBO. */
void nw_vu_attribs(void);

#endif
