/* A frame of draws in the order the Nu render API gives them: what the game thread hands the renderer. GL-free. */
#ifndef NUWORLD_GFX_PACKET_H
#define NUWORLD_GFX_PACKET_H

#include <stdbool.h>
#include <stdint.h>

#include "gfx/vif.h"

struct numtl_s;

typedef enum NwCmdOp {
    NW_CMD_DRAW, /* a geom's mesh under the local matrix */
} NwCmdOp;

/* The screen state a draw is made under: NuRndrSetViewMtx's world-to-GS matrix, XYOFFSET and SCISSOR. */
typedef struct NwView {
    float vs[16];       /* row-major, row vectors (vpsmtx) */
    int32_t xyoffset[2]; /* GS 12.4 */
    int32_t scissor[4];  /* x0, y0, x1, y1 */
} NwView;

typedef struct NwCmd {
    uint8_t op;
    uint32_t view;             /* into NwPacket.views */
    const void *mpg;           /* the microcode package (an NwVuProg) */
    const struct numtl_s *mtl; /* the material whose state block is in effect */
    const NwVifMesh *mesh;
    float local[16];
} NwCmd;

typedef struct NwPacket {
    NwCmd *cmds;
    uint32_t ncmds, cmdcap;
    NwView *views;
    uint32_t nviews, viewcap;
} NwPacket;

/* Appends a command or a view; NULL on out of memory. */
NwCmd *nw_packet_cmd(NwPacket *p);
NwView *nw_packet_view(NwPacket *p, uint32_t *index);
/* Empties p, keeping its memory. */
void nw_packet_reset(NwPacket *p);
void nw_packet_free(NwPacket *p);

#endif
