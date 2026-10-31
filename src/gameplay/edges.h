/* Crash on beveled edges: edge_grip. */
#ifndef NUWORLD_GAMEPLAY_EDGES_H
#define NUWORLD_GAMEPLAY_EDGES_H

#include "numath/numathtypes.h"

/* terrain.c's private TerTempInfoType up to uhitnorm; keep in step with the decomp. */
typedef struct NwTerI {
    struct nuvec_s origpos, origvel, curpos, curvel;
    short id, scanmode;
    float stopflag, vellen;
    unsigned char *flags;
    float ax, ay, len, size, sizesq, sizediv, yscale, yscalesq, inyscale, inyscalesq;
    short hitcnt, hitterrno;
    float csx, csy, csz, cex, cey, cez;
    short hittype, plathit;
    short *PlatScanStart;
    void *hitter;
    float hittime, timeadj, impactadj;
    struct nuvec_s hitnorm, uhitnorm;
} NwTerI;

/* NewTerrainScaleY around retail. */
void nw_edges_scan(unsigned char *flags);
void nw_edges_scanned(const unsigned char *flags);
/* TerrainImpact, before retail. */
void nw_edges_impact(const unsigned char *flags);

#endif
