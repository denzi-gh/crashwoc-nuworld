/* NU20 scene (.gsc) reader without the decomp: blocks and OBJ0 objects, a port of nus_scene.py. */
#ifndef NUWORLD_GFX_GSC_H
#define NUWORLD_GFX_GSC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "gfx/tex.h"
#include "gfx/vif.h"

#define NW_GSC_ID(a, b, c, d) ((uint32_t)(a) | (uint32_t)(b) << 8 | (uint32_t)(c) << 16 | (uint32_t)(d) << 24)

#define NW_GSC_MTL_SIZE 0x1D0  /* numtl_s */
#define NW_GSC_INST_SIZE 0x50  /* nuinstance_s */

typedef struct NwGscChunk {
    uint32_t id;
    uint32_t offset, size; /* the data after the 8-byte header */
} NwGscChunk;

typedef struct NwGscBlob {
    uint32_t offset, size; /* the geom's DMA chain in the file */
    uint32_t mtl;
    bool faceon; /* a face-on (sprite) geom of a type 1 sub-object */
    bool ot;     /* its material's attrib.ot: triangle blocks for the ordering table, not a DMA chain */
} NwGscBlob;

typedef struct NwGscObject {
    uint32_t first, count; /* into blobs */
} NwGscObject;

typedef struct NwGscTexture {
    uint32_t offset, size; /* a native texture record, what ReadTextureSet hands NuTexCreateNative */
} NwGscTexture;

typedef struct NwGsc {
    const uint8_t *data;
    size_t size;
    NwGscChunk *chunks;
    uint32_t nchunks;
    uint32_t unknown;  /* blocks NuGScnLoadFix has no case for */
    bool truncated;    /* the block list ran past the end of the file */
    uint32_t mtls, nmtls;   /* offset and count of the MS00/MS02 records */
    uint32_t insts, ninsts; /* of the INST records */
    NwGscObject *objects;
    uint32_t nobjects;
    NwGscBlob *blobs;
    uint32_t nblobs;
    NwGscTexture *textures; /* of the last TST0/TST2 */
    uint32_t ntextures;
} NwGsc;

typedef struct NwGscMtl {
    uint32_t attrib[2]; /* numtl_s.attrib (0x16C) */
    int32_t tid;        /* an index into the scene's textures, -1 for none */
    float diffuse[3], alpha;
    int16_t k; /* the mipmap K (0x1AC) and L (0x1AE) */
    uint8_t l;
} NwGscMtl;

typedef struct NwGscInst {
    float mtx[16]; /* row-major, translation in the last row */
    int32_t objid;
    uint32_t flags; /* bit 0 visible */
} NwGscInst;

/* numtl_s.attrib fields the reader and the renderer use. */
#define NW_GSC_ATTRIB_ALPHA(a) ((a)[0] & 15)
#define NW_GSC_ATTRIB_FILTER(a) (((a)[0] >> 4) & 3)
#define NW_GSC_ATTRIB_FX(a) (((a)[0] >> 6) & 3)
#define NW_GSC_ATTRIB_UTC(a) (((a)[0] >> 8) & 3)
#define NW_GSC_ATTRIB_VTC(a) (((a)[0] >> 10) & 3)
#define NW_GSC_ATTRIB_ZMODE(a) (((a)[0] >> 14) & 3)
#define NW_GSC_ATTRIB_LIGHTING(a) (((a)[0] >> 16) & 3)
#define NW_GSC_ATTRIB_COLOUR(a) (((a)[0] >> 18) & 1)
#define NW_GSC_ATTRIB_OT(a) (((a)[1] >> 3) & 1)

typedef struct NwGscTotal {
    uint32_t files, failed, unknown, truncated;
    uint64_t objects, batches, verts, tris, textures;
} NwGscTotal;

/* Reads the block list and the objects of an unpacked file; the data must outlive g. */
bool nw_gsc_open(NwGsc *g, const void *data, size_t size);
void nw_gsc_close(NwGsc *g);
NwGscMtl nw_gsc_mtl(const NwGsc *g, uint32_t index);
NwGscInst nw_gsc_inst(const NwGsc *g, uint32_t index);
/* Decodes texture index (a material's tid); false (t empty) on a failure. */
bool nw_gsc_texture(const NwGsc *g, uint32_t index, NwTex *t);
/* The microcode package NuMtlUpdateEx gives a material ("vu_load_tric_mpg", ...). */
const char *nw_gsc_mtl_package(const NwGscMtl *m);

/* Decodes every plain geom (not OT or face-on) and texture: OBJ, TEX, then the GSC line; false on a failure. */
bool nw_gsc_report(const NwGsc *g, const char *name, FILE *out, NwGscTotal *total);
/* "OBJ <id> ... vtx= idx=": SHA-256 of x,y,z,u,v f32 + rgba and of u32 triangles, as tools/gsc_hash.py. */
void nw_gsc_obj_line(uint32_t id, const NwVifMesh *mesh, char *line, size_t size);
/* "TEX <id> <w>x<h> ... rgba=": SHA-256 of level 0 as nw_tex_rgba gives it, as tools/gsc_hash.py. */
void nw_gsc_tex_line(uint32_t id, const NwTex *t, char *line, size_t size);
void nw_gsc_report_total(const NwGscTotal *total, FILE *out);

#endif
