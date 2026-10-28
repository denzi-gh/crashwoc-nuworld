/* The VU1 microcode as GLSL: needs a current GL 3.3 context. */
#ifndef NUWORLD_GFX_VU_H
#define NUWORLD_GFX_VU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx/mtl.h"
#include "gfx/tex.h"

/* The triangle packages' per-vertex programs (tri runs the same code as tric), the sprite programs they jump to for
   face-on geoms, the skin programs of tri, xformuv and glass (MSCAL 0x302), and ptl's particles. water, wateruv,
   triglass and sknglass read the frame copied into a texture at the pixel each vertex lands on. */
typedef enum NwVuProgram {
    NW_VU_TRIC,
    NW_VU_TRICU,
    NW_VU_TRICA,
    NW_VU_TRICDV,
    NW_VU_FACEON,
    NW_VU_FACEY,
    NW_VU_SKIN,
    NW_VU_TRIXUV,
    NW_VU_SKINXUV,
    NW_VU_PTL,
    NW_VU_WATER,
    NW_VU_WATERUV,
    NW_VU_TRIGLASS,
    NW_VU_SKNGLASS,
    NW_VU_PROGRAMS
} NwVuProgram;

/* What vu1init leaves for an object: VF01-04 (local times screen matrix, PS2 row-major), VF23-29 (lights, or for the
   xuv programs the UV matrix: u, v and w dotted with the object's normal, then the offset), and for the sprite
   programs: faceon's corner offset per unit of size in clip space (x, y), facey's facing axis VF15; for ptl the
   clip space per GS unit (x, y) and the texture per texel coordinate unit (z, w). frag maps a pixel to GS coordinates:
   x = gl_FragCoord.x * frag[0] + frag[2], y = gl_FragCoord.y * frag[1] + frag[3]. */
typedef struct NwVuObject {
    float lv[16];
    float ambient[4];
    float lightdir[3][4];
    float lightcol[3][4];
    float uv[4][4];
    float sprite[4];
    float frag[4];
} NwVuObject;

/* A corner of a face-on sprite: the point, s and t of the corner with the sprite's size, the colour. */
typedef struct NwVuSpriteVertex {
    float pos[4];
    float tex[4];
    uint8_t col[4];
} NwVuSpriteVertex;

/* The lights a lit stream uploads to VU 0x0C-0x12, as NuRndrSetDirectionalLights and NuRndrSetAmbientLight set
   them: three directions and colours in world space, the ambient colour; or the UV matrix a specular or fx stream
   uploads to 0x0C-0x0F (NuRndrSetSpecularLight, NuRndrSetFxMtx), row-major with the offset in row 3. */
typedef struct NwLights {
    float dir[3][3];
    float col[3][3];
    float ambient[3];
    float uv[4][4];
} NwLights;

/* A texture on the GPU: CLUT indices (R8UI) and the CLUT (256x1 RGBA8UI), or colours (RGBA8UI) and no CLUT, or a
   surface the renderer draws into (RGBA8: colour / 255, alpha / 128, GS row y at row height - 1 - y). */
typedef struct NwVuTexture {
    unsigned int index, clut;
    bool tcc; /* TEX0.TCC */
    bool surface;
} NwVuTexture;

/* A depth CLUT (NwZClut) the programs read in place of the texture, or of its alpha alone: the target's Z as a
   depth texture, the CLUT (256x1 RGBA8UI), and the lookup; clear: 0 none, 1 NuLightFogClear's, 2 a spot light's,
   then its two Z. */
typedef struct NwVuZClut {
    unsigned int depth, clut;
    uint32_t shift, base, far, near;
    uint32_t clear[3];
    bool alpha;
} NwVuZClut;

/* What a material gives the programs: diffuse VU 0x13 (x128), the texture (NULL or 0: none), the GS state, a depth
   CLUT over the texture (NULL: none), and a copy of the target to blend over in the program as the GS does (NULL:
   GL's blend). */
typedef struct NwVuMaterial {
    float diffuse[4];
    float uvoff[4]; /* VU 0x14 u, v; the STROW row u, v (x4096) when the geom unpacks its texcoords under STMOD 1 */
    float vu[8];    /* VU 0x14-0x15 as the material uploads them (numtl_s.vuattr) */
    const NwVuTexture *tex;
    NwMtlState state;
    const NwVuZClut *zclut;
    const NwVuTexture *dst;
} NwVuMaterial;

bool nw_vu_init(void);
void nw_vu_shutdown(void);
/* The program of a microcode package name. */
NwVuProgram nw_vu_program(const char *package);
/* The program a package's MSCAL 0x302 runs: skin in tri, skinxuv in xformuv, sknglass in glass; NW_VU_PROGRAMS for
   the others, whose entry draws nothing (tric, tricu, trica) or falls into the triangle program (tricdv). */
NwVuProgram nw_vu_skin_program(const char *package);
/* A skin draw's palette and blend-shape weights in the packet's texture buffers (units 3 and 4), its geom's deltas
   on unit 5. */
void nw_vu_skin(NwVuProgram program, uint32_t joints, uint32_t weights, uint32_t nweights);
/* water's and wateruv's VU 0x1B2-0x1D1: the 32 waves a vertex picks by its normal's y, until the next table. */
void nw_vu_waves(const float waves[32][4]);
/* ptl's VU 0x3C-0xBF: the effect list as NwPtlList holds it. */
void nw_vu_ptl(const uint32_t image[132][4]);
/* The particles of the bound VBO from the byte offset on, one per instance: the point and birth, the momentum and
   rate. */
void nw_vu_ptl_attribs(size_t offset);
/* vu1init and clipinit alike (src/gfx/vu1init.c), unlit: the GPU clips, so clip and trivclip need no counterpart. */
void nw_vu1init(NwVuObject *o, const float local[16], const float view[16]);
/* Their lights for a lit stream (mode 1): each direction dotted with the normalised rows of the local matrix, the
   colours with w 0, the ambient colour. */
void nw_vu1init_lights(NwVuObject *o, const float local[16], const NwLights *l);
/* The UV matrix of a specular or fx stream (mode 4) the same way: its first three rows into the object's space, the
   offset as it is. */
void nw_vu1init_uv(NwVuObject *o, const float local[16], const NwLights *l);
/* Sets the program and the material's state; returns the passes its alpha test draws the triangles in. */
uint32_t nw_vu_use(NwVuProgram program, const NwVuObject *o, const NwVuMaterial *m, NwMtlPass pass[2]);
/* Whether the target keeps the alpha the passes write (a PSMCT24 frame does not). */
void nw_vu_alpha_writes(bool on);
/* One of those passes: which side of the alpha test it keeps, and the buffers it writes. */
void nw_vu_pass(NwVuProgram program, const NwVuMaterial *m, const NwMtlPass *pass);
/* Uploads t with its mip levels. */
void nw_vu_texture(NwVuTexture *tex, const NwTex *t);
void nw_vu_texture_free(NwVuTexture *tex);
/* The NwVifVertex attributes of the bound VAO and VBO. */
void nw_vu_attribs(void);
/* The NwVuSpriteVertex attributes of the bound VAO and VBO. */
void nw_vu_sprite_attribs(void);
/* The NwVifSkinVertex attributes of the bound VAO and VBO. */
void nw_vu_skin_attribs(void);

#endif
