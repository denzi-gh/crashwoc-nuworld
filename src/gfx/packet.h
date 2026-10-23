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

typedef struct NwGpuSprites {
    NwVifSprites sprites;
    unsigned int vao, vbo, ibo;
} NwGpuSprites;

typedef struct NwGpuSkin {
    NwVifSkin skin;
    unsigned int vao, vbo, ibo;
    unsigned int deltas, delta_tex; /* the blend-shape deltas as a texture buffer */
} NwGpuSkin;

/* A texture the GS reads from its own memory: the back or front buffer, or a render target by its GS address (0:
   none). tw x th is the power-of-two size it is addressed at. */
#define NW_SURF_BACK (-1)
#define NW_SURF_FRONT (-3)

typedef struct NwSurf {
    int32_t addr;
    uint16_t tw, th;
    bool tcc;
} NwSurf;

typedef enum NwCmdOp {
    NW_CMD_DRAW,    /* a geom's mesh under the local matrix */
    NW_CMD_DRAW_OT, /* an ordering-table batch: some of its mesh's triangles, in their order in NwPacket.idx */
    NW_CMD_CLEAR, /* NuRndrClear: the scissor rectangle's colour and Z */
    NW_CMD_RECT,    /* a 2D sprite */
    NW_CMD_SPRITES, /* a face-on geom's sprites under the local matrix */
    NW_CMD_TARGET,  /* what the draws after it go to: the back or front buffer, or a render target */
    NW_CMD_SKIN,    /* a skin geom under the local matrix and its joint palette */
} NwCmdOp;

/* The screen state a draw is made under: NuRndrSetViewMtx's world-to-GS matrix, XYOFFSET and SCISSOR. */
typedef struct NwView {
    float vs[16];        /* row-major, row vectors (vpsmtx) */
    int32_t xyoffset[2]; /* GS 12.4 */
    int32_t scissor[4];  /* x0, y0, x1, y1 */
    float faceon[4];     /* VU 0x16 (FaceYDirStream): cos yaw, viewport width / 10240, -sin yaw, height / 3584 */
} NwView;

/* A material's state block as the frame's DMA reads it. */
typedef struct NwMtlSnap {
    NwMtlState gs;
    float diffuse[4];
    float uvoff[2]; /* VU 0x14: NuMtlAnimate's UV offset */
    int32_t row[2]; /* the STROW row: the offset x4096, for geoms that unpack their texcoords under STMOD 1 */
    const NwGpuTex *tex;
    NwSurf surf; /* a texture in GS memory instead of tex */
} NwMtlSnap;

#define NW_MTL_NONE UINT32_MAX   /* NwCmd.mtl of a command with no material */
#define NW_LIGHT_NONE UINT32_MAX /* NwCmd.light of an unlit draw */

typedef struct NwCmd {
    uint8_t op;
    uint32_t view;                  /* into NwPacket.views */
    uint32_t mtl;                   /* into NwPacket.mtls, from material when the packet is published */
    uint32_t light;                 /* into NwPacket.lights */
    const void *mpg;                /* the microcode package (an NwVuProg) */
    const struct numtl_s *material; /* game thread only */
    union {
        struct {
            union {
                const NwGpuMesh *mesh;
                const NwGpuSprites *sprites; /* SPRITES */
                const NwGpuSkin *skin;       /* SKIN */
            };
            float local[16];
            uint32_t first, count; /* DRAW_OT: the vertex indices in NwPacket.idx; SKIN: the NwPacket.joints */
            uint32_t weights, nweights; /* SKIN: the blend-shape weights in NwPacket.weights */
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
        struct {
            NwSurf surf;          /* addr 0: one the port does not draw, whose draws go nowhere */
            int32_t width, height; /* the target's pixels (nurndr_pixel_width, height) */
        } target;
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
    NwLights *lights;
    uint32_t nlights, lightcap;
    uint32_t *idx; /* the DRAW_OT triangles */
    uint32_t nidx, idxcap;
    float (*joints)[16]; /* the SKIN palettes, INV_WT times the joint matrices (row-major, row vectors) */
    uint32_t njoints, jointcap;
    float *weights;
    uint32_t nweights, weightcap;
} NwPacket;

/* Appends a command, a view, a material or lights; NULL on out of memory. */
NwCmd *nw_packet_cmd(NwPacket *p);
NwView *nw_packet_view(NwPacket *p, uint32_t *index);
NwMtlSnap *nw_packet_mtl(NwPacket *p, uint32_t *index);
NwLights *nw_packet_light(NwPacket *p, uint32_t *index);
/* Appends n vertex indices, joint matrices or weights; NULL on out of memory. */
uint32_t *nw_packet_idx(NwPacket *p, uint32_t n, uint32_t *first);
float (*nw_packet_joints(NwPacket *p, uint32_t n, uint32_t *first))[16];
float *nw_packet_weights(NwPacket *p, uint32_t n, uint32_t *first);
/* Empties p, keeping its memory. */
void nw_packet_reset(NwPacket *p);

/* Game thread: the packet being built; publishing it stamps it and makes it the newest for the renderer. */
NwPacket *nw_packet_building(void);
void nw_packet_publish(int32_t vbcnt);
/* Render thread: the newest published packet, the one it took last if none is newer; NULL before the first. */
const NwPacket *nw_packet_take(void);

/* The frees come last: nw_gpu_take fences every op from NW_GPU_FREE_MESH on. */
typedef enum NwGpuOp {
    NW_GPU_UPLOAD_MESH,
    NW_GPU_UPLOAD_TEX,
    NW_GPU_UPLOAD_SPRITES,
    NW_GPU_UPLOAD_SKIN,
    NW_GPU_FREE_MESH,
    NW_GPU_FREE_TEX,
    NW_GPU_FREE_SPRITES,
    NW_GPU_FREE_SKIN,
    NW_GPU_FREE_HOST, /* SDL_malloc'd memory the game thread's draws read until the fence */
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
