/* The frame of draws the game thread hands the renderer each tick, and the GPU objects they use; GL-free. */
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

/* A texture in GS memory: the back or front buffer or a target's GS address (0: none), tw x th a power of two. */
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
    NW_CMD_VERTS,   /* triangles of vertices made this frame, in NwPacket.verts */
    NW_CMD_PARTICLES, /* particles under the local matrix, as the ptl program draws them from their effect list */
    NW_CMD_VOLUMES, /* the volume shadows' start on the target: the back buffer's Z, squeezed, and the stencil 0x7F */
    NW_CMD_WAVES,   /* a water material's wave table, which the draws after it read */
} NwCmdOp;

/* nuwater's animateWater table at VU 0x1B2: per wave the camera-space texcoord offset (x, y), 1, the local y lift. */
typedef struct NwWaves {
    float wave[32][4];
} NwWaves;

/* An effect list as unpacked to VU 0x3C-0xBF for the ptl program; tri is 64 entries by age of corner offsets and
   colour. */
typedef struct NwPtlList {
    float head[4]; /* [1] the gravity, [3] the time */
    int32_t uv[4][2];
    uint32_t pad[4];
    uint32_t tri[64][8];
} NwPtlList;

/* A particle as its set holds it; rate is how fast it ages. */
typedef struct NwPtl {
    float pos[3], birth;
    float mom[3], rate;
} NwPtl;

/* A CLUT a VERTS command looks up by the GS Z under each pixel, as retail's fog and spot lights read Z back byte by
   byte: entry (Z >> shift) & 0xFF from Z = base, far below that and near above (or NW_ZCLUT_ZERO, a clear texel, or
   NW_ZCLUT_WRAP, wrapped like the rest). clear_z bounds Seashell's NuLightFogClear alpha fix-up (clear) or a spot
   light's texel clear (spot); with alpha only the texture's alpha is replaced, for the haze. */
#define NW_ZCLUT_ZERO 0x100u
#define NW_ZCLUT_WRAP 0x200u

typedef struct NwZClut {
    uint32_t clut[256];
    uint32_t shift, base;
    uint32_t far, near;
    bool clear, spot;
    uint32_t clear_z[2];
    bool alpha;
} NwZClut;

typedef enum NwVertsMode {
    NW_VERTS_GS,   /* GS coordinates (x, y 12.4, z), s and t x4096, unlit: a DIRECT GIF packet's */
    NW_VERTS_TRIS, /* points under the local matrix, s and t x4096, through the command's program */
    NW_VERTS_SPRITE, /* points under the local matrix, each corner moved by tex.zw GS units before the divide */
} NwVertsMode;

/* The screen state a draw is made under: NuRndrSetViewMtx's world-to-GS matrix, XYOFFSET and SCISSOR. */
typedef struct NwView {
    float vs[16];        /* row-major, row vectors (vpsmtx) */
    int32_t xyoffset[2]; /* GS 12.4 */
    int32_t scissor[4];  /* x0, y0, x1, y1 */
    float faceon[4];     /* VU 0x16 (FaceYDirStream): cos yaw, viewport width / 10240, -sin yaw, height / 3584 */
    float cam[16];       /* the camera vs was made from (global_camera's matrix and scale), for interpolation */
    float camscale[3];
    uint32_t key; /* which SetViewMtx of the frame it is, from 1 */
} NwView;

/* A material's state block as the frame's DMA reads it. */
typedef struct NwMtlSnap {
    NwMtlState gs;
    float diffuse[4];
    float vu[8];    /* VU 0x14-0x15 (numtl_s.vuattr): [0] [1] NuMtlAnimate's UV offset */
    int32_t row[2]; /* the STROW row: the offset x4096, for geoms that unpack their texcoords under STMOD 1 */
    const NwGpuTex *tex;
    NwSurf surf; /* a texture in GS memory instead of tex */
    const void *key; /* the material, for interpolation */
    uint8_t wrap;    /* 1 u, 2 v: the offset scrolls, kept in 0-1 (NuMtlAnimate's mode 2) */
} NwMtlSnap;

#define NW_MTL_NONE UINT32_MAX   /* NwCmd.mtl of a command with no material */
#define NW_LIGHT_NONE UINT32_MAX /* NwCmd.light of an unlit draw */

typedef struct NwCmd {
    uint8_t op;
    uint64_t key;                   /* the same draw in the frame before has it (src/gfx/interp.h); 0 none */
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
            uint32_t first, count; /* DRAW_OT: the vertex indices in NwPacket.idx; SKIN: the NwPacket.joints; VERTS:
                                      the NwPacket.verts; PARTICLES: the NwPacket.ptls; WAVES: first, the table in
                                      NwPacket.waves */
            uint32_t weights, nweights; /* SKIN: the blend-shape weights in NwPacket.weights */
            uint8_t mode;               /* VERTS: an NwVertsMode */
            uint16_t alpha;             /* VERTS: 0x100 | an ALPHA_1 written over the material's; 0 none */
            const NwGpuTex *clut;       /* VERTS: a CLUT over the material texture's, NULL none */
            uint32_t zclut;             /* VERTS: 1 + its depth CLUT in NwPacket.zcluts, 0 none */
            bool exact;                 /* VERTS: the GS blend in the program over a copy of the target, for As past
                                           0x80 or an ALPHA GL has no blend for (triangles that do not overlap) */
            int8_t stencil;             /* VERTS: a shadow volume's +1 or -1, counted into the stencil alone where
                                           the triangles pass the Z test; NW_STENCIL_SHADOWED: drawn only where the
                                           count is 0x80 or more; 0 none */
            uint32_t ptllist;           /* PARTICLES: the effect list in NwPacket.ptllists */
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
            int32_t width; /* the pixels the target's Z takes the back buffer's width in */
        } volumes;
        struct {
            NwSurf surf;          /* addr 0: one the port does not draw, whose draws go nowhere; tcc false: PSMCT24,
                                     whose alpha the draws leave */
            int32_t width, height; /* the target's pixels (nurndr_pixel_width, height) */
            bool zback;            /* the Z buffer is the back buffer's (-2), shared when the sizes match */
        } target;
    };
} NwCmd;

#define NW_STENCIL_SHADOWED 2

typedef struct NwPacket {
    uint64_t seq;  /* 1 for the first published packet */
    int32_t vbcnt; /* nuvideo_global_vbcnt when it was published */
    uint64_t edge; /* the clock's edges then */
    bool snap;     /* a cut: drawn without blending from the packet before */
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
    NwVuSpriteVertex *verts; /* the VERTS triangles */
    uint32_t nverts, vertcap;
    NwPtlList *ptllists;
    uint32_t nptllists, ptllistcap;
    NwPtl *ptls;
    uint32_t nptls, ptlcap;
    NwZClut *zcluts;
    uint32_t nzcluts, zclutcap;
    NwWaves *waves;
    uint32_t nwaves, wavecap;
} NwPacket;

/* Appends a command, a view, a material or lights; NULL on out of memory. */
NwCmd *nw_packet_cmd(NwPacket *p);
NwView *nw_packet_view(NwPacket *p, uint32_t *index);
NwMtlSnap *nw_packet_mtl(NwPacket *p, uint32_t *index);
NwLights *nw_packet_light(NwPacket *p, uint32_t *index);
/* Appends n vertex indices, joint matrices, weights or vertices; NULL on out of memory. */
uint32_t *nw_packet_idx(NwPacket *p, uint32_t n, uint32_t *first);
float (*nw_packet_joints(NwPacket *p, uint32_t n, uint32_t *first))[16];
float *nw_packet_weights(NwPacket *p, uint32_t n, uint32_t *first);
NwVuSpriteVertex *nw_packet_verts(NwPacket *p, uint32_t n, uint32_t *first);
NwPtlList *nw_packet_ptllist(NwPacket *p, uint32_t *index);
NwPtl *nw_packet_ptls(NwPacket *p, uint32_t n, uint32_t *first);
NwZClut *nw_packet_zclut(NwPacket *p, uint32_t *index);
NwWaves *nw_packet_waves(NwPacket *p, uint32_t *index);
/* Empties p, keeping its memory. */
void nw_packet_reset(NwPacket *p);

/* Game thread: the packet being built; publishing it stamps it and makes it the newest for the renderer (returns
   its seq). */
NwPacket *nw_packet_building(void);
uint64_t nw_packet_publish(int32_t vbcnt);
/* Render thread: the newest published packet, the one it took last if none is newer; NULL before the first. */
const NwPacket *nw_packet_take(void);
/* The same, and the packet it took before that one at *prev (NULL: none). */
const NwPacket *nw_packet_take_pair(const NwPacket **prev);

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
