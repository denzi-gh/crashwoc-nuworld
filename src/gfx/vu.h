/* Needs a current GL 3.3 context. */
#ifndef NUWORLD_GFX_VU_H
#define NUWORLD_GFX_VU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx/mtl.h"
#include "gfx/tex.h"

/* How game textures are filtered; render targets and depth CLUTs keep the GS's. */
typedef enum NwTexFilter {
    NW_FILTER_RETAIL, /* the GS's, per material: TEX1 MMAG and MMIN */
    NW_FILTER_NEAREST,
    NW_FILTER_BILINEAR,
    NW_FILTERS
} NwTexFilter;

/* tri runs tric's code; water, wateruv, triglass and sknglass read the frame copy at each vertex's pixel. */
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
    NW_VU_PTL_GENERAL, /* general particle filtering; PTL specializes power-of-two bilinear textures without mipmaps */
    NW_VU_GEM,         /* [video] gems (gfx/gem.h): a tric geom in world space, shaded as cut stone or metal */
    NW_VU_SKNGEM,      /* the same for a skin geom */
    NW_VU_PROGRAMS
} NwVuProgram;

/* lv: VF01-04 (local x screen); sprite: faceon/facey/ptl corner scale; GS xy = gl_FragCoord.xy * frag.xy + frag.zw */
typedef struct NwVuObject {
    float lv[16];
    float ambient[4];
    float lightdir[3][4];
    float lightcol[3][4];
    float uv[4][4];
    float sprite[4];
    float frag[4];
} NwVuObject;

typedef struct NwVuSpriteVertex {
    float pos[4];
    float tex[4];
    uint8_t col[4];
} NwVuSpriteVertex;

/* Lit streams: VU 0x0C-0x12 lights in world space; specular/fx streams: a UV matrix at 0x0C-0x0F, offset in row 3. */
typedef struct NwLights {
    float dir[3][3];
    float col[3][3];
    float ambient[3];
    float uv[4][4];
} NwLights;

/* R8UI indices + 256x1 RGBA8UI CLUT, RGBA8UI colours, or a surface (RGBA8: alpha / 128, GS row y at height - 1 - y). */
typedef struct NwVuTexture {
    unsigned int index, clut;
    bool tcc; /* TEX0.TCC */
    bool rgba8;
    uint32_t layers;
    uint32_t levels;
    bool surface;
    bool filtered;
    unsigned int rgba; /* particle-only RGBA8UI cache: GS bytes with the palette already applied */
    uint8_t alpha_min, alpha_max; /* conservative bounds across the particle texture's mip levels */
    bool power_of_two;
} NwVuTexture;

/* Read in place of the texture or its alpha; clear: 0 none, 1 NuLightFogClear's, 2 a spot light's, then its two Z. */
typedef struct NwVuZClut {
    unsigned int depth, clut;
    uint32_t shift, base, far, near;
    uint32_t clear[3];
    bool alpha;
    uint8_t ad;
} NwVuZClut;

/* diffuse is VU 0x13 (x128); tex NULL or 0 and zclut NULL mean none; dst NULL: GL's blend. */
typedef struct NwUV {
    float value[7]; /* offset xy, scale xy, clockwise degrees, pivot xy */
    uint32_t binding, linear;
    int segment[3], address; /* -1 material, 0 repeat, 1 clamp, 2 mirror */
    uint32_t mips;
    uint32_t owner[3]; /* Stable curve owners, independent of the composed material binding. */
    int64_t cycle[3]; /* Loop/half-cycle: equal key segments can still cross a discontinuity. */
} NwUV;

typedef struct NwVuMaterial {
    float diffuse[4];
    float fx[16];  /* nw_rndr_mtl_fx's: u_fx */
    float uvoff[4]; /* VU 0x14 u, v; the STROW row u, v (x4096) when the geom unpacks its texcoords under STMOD 1 */
    float vu[8];    /* VU 0x14-0x15 as the material uploads them (numtl_s.vuattr) */
    const NwVuTexture *tex;
    int layer;
    NwUV uv;
    NwMtlState state;
    const NwVuZClut *zclut;
    const NwVuTexture *dst;
} NwVuMaterial;

bool nw_vu_init(void);
void nw_vu_shutdown(void);
NwVuProgram nw_vu_program(const char *package);
/* MSCAL 0x302: skin in tri, skinxuv in xformuv, sknglass in glass; NW_VU_PROGRAMS for the other packages. */
NwVuProgram nw_vu_skin_program(const char *package);
/* Palette and weights on texture units 3 and 4, the geom's deltas on unit 5. */
void nw_vu_skin(NwVuProgram program, uint32_t joints, uint32_t weights, uint32_t nweights);
void nw_vu_silhouette(bool skin, const float lv[16], uint32_t joints);
/* water's and wateruv's VU 0x1B2-0x1D1: the 32 waves a vertex picks by its normal's y, until the next table. */
void nw_vu_waves(const float waves[32][4]);
/* ptl's VU 0x3C-0xBF: the effect list as NwPtlList holds it. */
void nw_vu_ptl(NwVuProgram program, const uint32_t image[132][4]);
NwVuProgram nw_vu_particle_program(const NwVuTexture *texture, const NwMtlState *state);
/* One particle per instance, from the byte offset on. */
void nw_vu_ptl_attribs(size_t offset);
/* vu1init and clipinit alike (src/gfx/vu1init.c), unlit: the GPU clips, so clip and trivclip need no counterpart. */
void nw_vu1init(NwVuObject *o, const float local[16], const float view[16]);
/* Lit streams (mode 1): directions dotted with the local matrix's normalised rows, colours with w 0. */
void nw_vu1init_lights(NwVuObject *o, const float local[16], const NwLights *l);
/* Specular or fx streams (mode 4): the first three rows into the object's space, the offset as it is. */
void nw_vu1init_uv(NwVuObject *o, const float local[16], const NwLights *l);
/* Row-major as lv; cam: right, up, forward; kind and role as gfx/gem.h; panel: drawn by a menu or the HUD. */
typedef struct NwVuGem {
    float world[16], vp[16];
    float eye[3], cam[9], tint[3];
    int kind, role, panel;
    int viewport[4];
} NwVuGem;
void nw_vu_gem(NwVuProgram program, const NwVuGem *g);
/* Returns the passes its alpha test draws the triangles in. */
uint32_t nw_vu_use(NwVuProgram program, const NwVuObject *o, const NwVuMaterial *m, NwMtlPass pass[2]);
/* Whether the target keeps the alpha the passes write (a PSMCT24 frame does not). */
void nw_vu_alpha_writes(bool on);
void nw_vu_filter(NwTexFilter filter);
void nw_vu_pass(NwVuProgram program, const NwVuMaterial *m, const NwMtlPass *pass);
/* After nw_vu_use: the draw is a silhouette, black where it covers (a blended material as far as its As does).
   Reset by nw_vu_use. */
void nw_vu_black(NwVuProgram program, bool blended);
void nw_vu_texture(NwVuTexture *tex, const NwTex *t);
void nw_vu_texture_free(NwVuTexture *tex);
/* Lazily expands a particle texture's palette; keeps GS alpha (0x80 = 1), including values above 0x80. */
bool nw_vu_texture_rgba(NwVuTexture *tex, const NwTex *t);
void nw_vu_attribs(void);
void nw_vu_sprite_attribs(void);
void nw_vu_skin_attribs(void);

#endif
