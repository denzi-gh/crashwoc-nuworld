/* .gsc geom VIF decoder (DMA chain to vertices and tri-strip indices), a port of nus_ps2.py. */
#ifndef NUWORLD_GFX_VIF_H
#define NUWORLD_GFX_VIF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* One vertex as the VU reads it, the layout of the VBO (28 bytes). */
typedef struct NwVifVertex {
    float pos[4];   /* VF06: x, y, z, w (V4-32: the normal's x with the ADC flag in its low half; V4-16: 0) */
    int16_t tex[4]; /* VF08: u, v (x4096), and the normal's y, z (x4096) when the unpack is V4-16 */
    uint8_t col[4]; /* VF31: r, g, b, a; 0x80 = 1.0 */
} NwVifVertex;

/* Triangles index verts; both grow as geoms are appended. */
typedef struct NwVifMesh {
    NwVifVertex *verts;
    uint32_t *tris; /* 3 per triangle */
    uint32_t nverts, ntris, nbatches;
    uint32_t vcap, tcap;
    bool uvrow; /* the last geom unpacked its texcoords under STMOD 1: they add the material's STROW row */
} NwVifMesh;

/* A face-on sprite as the faceon and facey programs read it. */
typedef struct NwVifSprite {
    float pos[3];
    float size[2]; /* faceon: -4900 width, -2300 height (GS units before the divide); facey: half width, height */
    uint8_t col[4];
} NwVifSprite;

/* A face-on geom's sprites, all of one program. */
typedef struct NwVifSprites {
    NwVifSprite *sprites;
    uint32_t count, cap;
    bool facey; /* kicked at MSCAL 0x306 (facey), not 0x304 (faceon) */
} NwVifSprites;

/* Appends one geom's batches; false (mesh unchanged) on a malformed chain or stream. */
bool nw_vif_decode(NwVifMesh *mesh, const void *geom, size_t size);
/* Appends an ordering-table geom's triangles, each one its own three vertices, in batch order; false (mesh
   unchanged) on a malformed batch. */
bool nw_vif_otbatch(NwVifMesh *mesh, const void *batch, size_t size);
void nw_vif_free(NwVifMesh *mesh);
/* A face-on geom's sprites; false (s empty) on a malformed chain or stream. */
bool nw_vif_sprites(NwVifSprites *s, const void *geom, size_t size);
void nw_vif_sprites_free(NwVifSprites *s);

#endif
