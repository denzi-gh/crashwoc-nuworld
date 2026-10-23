/* A frame of draws in the order the Nu render API gives them, handed from the game thread to the renderer once a
   tick, and the GPU objects the draws use. GL-free. */
#ifndef NUWORLD_GFX_PACKET_H
#define NUWORLD_GFX_PACKET_H

#include <stdbool.h>
#include <stdint.h>

#include "gfx/mtl.h"
#include "gfx/tex.h"
#include "gfx/vif.h"
#include "gfx/vu.h"

struct numtl_s;

/* A geom's mesh and a texture: made on the game thread, uploaded and freed on the render thread. */
typedef struct NwGpuMesh {
    NwVifMesh mesh;
    unsigned int vao, vbo, ibo;
} NwGpuMesh;

typedef struct NwGpuTex {
    NwTex tex;
    NwVuTexture gl;
} NwGpuTex;

typedef enum NwCmdOp {
    NW_CMD_DRAW,  /* a geom's mesh under the local matrix */
    NW_CMD_CLEAR, /* NuRndrClear: the scissor rectangle's colour and Z */
    NW_CMD_RECT,  /* a 2D sprite */
} NwCmdOp;

/* The screen state a draw is made under: NuRndrSetViewMtx's world-to-GS matrix, XYOFFSET and SCISSOR. */
typedef struct NwView {
    float vs[16];        /* row-major, row vectors (vpsmtx) */
    int32_t xyoffset[2]; /* GS 12.4 */
    int32_t scissor[4];  /* x0, y0, x1, y1 */
} NwView;

/* A material's state block as the frame's DMA reads it. */
typedef struct NwMtlSnap {
    NwMtlState gs;
    float diffuse[4];
    float uvoff[2]; /* VU 0x14: NuMtlAnimate's UV offset */
    int32_t row[2]; /* the STROW row: the offset x4096, for geoms that unpack their texcoords under STMOD 1 */
    const NwGpuTex *tex;
} NwMtlSnap;

#define NW_MTL_NONE UINT32_MAX /* NwCmd.mtl of a command with no material */

typedef struct NwCmd {
    uint8_t op;
    uint32_t view;                  /* into NwPacket.views */
    uint32_t mtl;                   /* into NwPacket.mtls, from material when the packet is published */
    const void *mpg;                /* the microcode package (an NwVuProg) */
    const struct numtl_s *material; /* game thread only */
    union {
        struct {
            const NwGpuMesh *mesh;
            float local[16];
        };
        struct {
            int32_t xy[4]; /* the corners in GS 12.4 */
            float st[4];   /* s, t at each corner (Q 1) */
            uint8_t rgba[4];
        } rect;
        struct {
            uint8_t rgba[4];
            uint32_t z;   /* GS Z */
            uint8_t mask; /* 1 colour, 2 Z */
        } clear;
    };
} NwCmd;

typedef struct NwPacket {
    uint64_t seq;  /* 1 for the first published packet */
    int32_t vbcnt; /* nuvideo_global_vbcnt when it was published */
    NwCmd *cmds;
    uint32_t ncmds, cmdcap;
    NwView *views;
    uint32_t nviews, viewcap;
    NwMtlSnap *mtls;
    uint32_t nmtls, mtlcap;
} NwPacket;

/* Appends a command, a view or a material; NULL on out of memory. */
NwCmd *nw_packet_cmd(NwPacket *p);
NwView *nw_packet_view(NwPacket *p, uint32_t *index);
NwMtlSnap *nw_packet_mtl(NwPacket *p, uint32_t *index);
/* Empties p, keeping its memory. */
void nw_packet_reset(NwPacket *p);

/* Game thread: the packet being built; publishing it stamps it and makes it the newest for the renderer. */
NwPacket *nw_packet_building(void);
void nw_packet_publish(int32_t vbcnt);
/* Render thread: the newest published packet, the one it took last if none is newer; NULL before the first. */
const NwPacket *nw_packet_take(void);

typedef enum NwGpuOp {
    NW_GPU_UPLOAD_MESH,
    NW_GPU_UPLOAD_TEX,
    NW_GPU_FREE_MESH,
    NW_GPU_FREE_TEX,
} NwGpuOp;

typedef struct NwGpuJob {
    NwGpuOp op;
    void *obj;
    uint64_t fence; /* a free waits until a packet after this one is drawn */
} NwGpuJob;

/* Game thread: queues a job for the render thread, which owns the object after a free. */
void nw_gpu_queue(NwGpuOp op, void *obj);
/* Render thread, before drawing packet seq: moves the jobs due to *out (grown as needed) in queue order. */
uint32_t nw_gpu_take(uint64_t seq, NwGpuJob **out, uint32_t *cap);

#endif
