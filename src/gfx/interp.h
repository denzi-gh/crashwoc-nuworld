/* Display-rate interpolation: the game thread keys the draws of a frame packet by the object they belong to and their
   place in it; the render thread pairs a packet's draws with the packet before's by key and blends what they are
   drawn under toward the newer one. GL-free. */
#ifndef NUWORLD_GFX_INTERP_H
#define NUWORLD_GFX_INTERP_H

#include <stdbool.h>
#include <stdint.h>

#include "gfx/packet.h"

/* Game thread. */
typedef struct NwInterpScope {
    uint64_t obj;
    uint32_t at, n;
} NwInterpScope;

/* The draws until nw_interp_end belong to obj drawn under the matrix at src: inside another object by the place
   it is at there, else by the NW_INTERP_KEY tag, src, obj and how often those came before in the frame. */
NwInterpScope nw_interp_begin(const void *src, const void *obj);
void nw_interp_end(NwInterpScope was);
/* The place in the object of the draws after it. */
void nw_interp_at(uint32_t a, uint32_t b, uint32_t c);
/* A record's key: 0 outside an object and for ops not drawn under the local matrix. */
uint64_t nw_interp_cmd(NwCmdOp op);
/* NuRndrSetViewMtx's key: which call of the frame it is. */
uint32_t nw_interp_view(void);
/* The frame is published as p: its snap, and the counts start again. */
void nw_interp_publish(NwPacket *p);
/* The decomp's NW_INTERP_KEY and NW_INTERP_SNAP (nu_port.h). */
void nw_interp_key(const void *key);
void nw_interp_snap(void);

/* Render thread. */
typedef struct NwInterpStats {
    uint32_t paired, unpaired; /* b's keyed draws with and without one in a */
    uint32_t views;            /* b's views blended */
    uint32_t snapped;          /* b's draws and views that jumped from a's, drawn as b */
} NwInterpStats;

/* How far from a toward b the frame at now (clock edges) is: 0 a, 1 b; always 1 when b is a cut or comes more than
   NW_INTERP_GAP edges after a (a load, a stall). */
#define NW_INTERP_GAP 4
float nw_interp_t(const NwPacket *a, const NwPacket *b, double now);
/* p draws over nothing it left before: it clears the back buffer before it draws there, so it can run again. */
bool nw_interp_redraws(const NwPacket *p);
/* b, or a copy of it blended from a by t, which lives until the next call. */
const NwPacket *nw_interp_blend(const NwPacket *a, const NwPacket *b, float t);
NwInterpStats nw_interp_stats(void);

/* a to b by t: the axes' rotation slerped, their lengths and the position lerped; affine row-vector matrices
   (row-major, rows 0-2 the axes, 3 the position), any other lerped by element. */
void nw_interp_mtx(float out[16], const float a[16], const float b[16], float t);
/* b is not where a went in a frame: its position more than NW_INTERP_JUMP away or its axes turned more than 90
   degrees. */
#define NW_INTERP_JUMP 16.0f
bool nw_interp_jump(const float a[16], const float b[16]);

#endif
