/* NU20 scene (.gsc) reader without the decomp: blocks and OBJ0 objects, a port of nus_scene.py. */
#ifndef NUWORLD_GFX_GSC_H
#define NUWORLD_GFX_GSC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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
} NwGsc;

typedef struct NwGscMtl {
    uint32_t attrib[2]; /* numtl_s.attrib (0x16C) */
    int32_t tid;        /* an index into the scene's textures, -1 for none */
    float diffuse[3], alpha;
} NwGscMtl;

typedef struct NwGscInst {
    float mtx[16]; /* row-major, translation in the last row */
    int32_t objid;
    uint32_t flags; /* bit 0 visible */
} NwGscInst;

/* numtl_s.attrib fields the reader and the renderer use. */
#define NW_GSC_ATTRIB_FX(a) (((a)[0] >> 6) & 3)
#define NW_GSC_ATTRIB_LIGHTING(a) (((a)[0] >> 16) & 3)
#define NW_GSC_ATTRIB_COLOUR(a) (((a)[0] >> 18) & 1)
#define NW_GSC_ATTRIB_OT(a) (((a)[1] >> 3) & 1)

/* Reads the block list and the objects of an unpacked file; the data must outlive g. */
bool nw_gsc_open(NwGsc *g, const void *data, size_t size);
void nw_gsc_close(NwGsc *g);
NwGscMtl nw_gsc_mtl(const NwGsc *g, uint32_t index);
NwGscInst nw_gsc_inst(const NwGsc *g, uint32_t index);

#endif
