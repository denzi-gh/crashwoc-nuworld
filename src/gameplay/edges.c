#include "gameplay/edges.h"

#include <math.h>

#include "game/creature.h"
#include "game/gametypes.h"
#include "gameplay/crash.h"
#include "gameplay/gameplay.h"

/* Declared by no header. */
extern struct gtimer_s GameTimer;
extern NwTerI *TerI;

static bool nw_edges_rim; /* his last contact was a rim held as floor */
static unsigned char nw_edges_flags[2];
static u32 nw_edges_frame;

static bool nw_edges_crash(const unsigned char *flags)
{
    return flags == Character[0].obj.gndflags.chrs && nw_crash_on_foot(&Character[0]);
}

/* An edge's normal y with his centre reach (share of his radius) past it, his shape stretched by yscale. */
static float nw_edges_normal(float reach, float yscale)
{
    float up = sqrtf(1.0f - reach * reach) / yscale;
    return up / sqrtf(reach * reach + up * up);
}

void nw_edges_scan(unsigned char *flags)
{
    /* Standing still over a drop, MovePlayer clears gndflags and retail slides him off. */
    if (nw_profile.movement.crash.edge_grip && nw_edges_crash(flags) && nw_edges_rim && Character[0].obj.ground == 1 &&
        flags[0] == 0 && flags[1] == 0 && nw_edges_flags[1] != 0 && GameTimer.frame - nw_edges_frame == 1) {
        NW_DEVIATION(movement.crash.edge_grip);
        flags[0] = nw_edges_flags[0];
        flags[1] = nw_edges_flags[1];
    }
}

void nw_edges_scanned(const unsigned char *flags)
{
    if (nw_edges_crash(flags)) {
        nw_edges_flags[0] = flags[0];
        nw_edges_flags[1] = flags[1];
        nw_edges_frame = GameTimer.frame;
    }
}

void nw_edges_impact(const unsigned char *flags)
{
    short type = TerI->hittype;
    if (type == 0 || !nw_edges_crash(flags)) {
        return;
    }
    const NwCrashMovement *c = &nw_profile.movement.crash;
    nw_edges_rim = c->edge_grip && (type == 2 || type == 3 || type == 0x12 || type == 0x13) && TerI->hitnorm.y > 0.0f &&
                   TerI->uhitnorm.y < 0.707f &&
                   TerI->uhitnorm.y >= nw_edges_normal(c->edge_reach / 100.0f, TerI->yscale);
    if (nw_edges_rim) {
        NW_DEVIATION(movement.crash.edge_grip);
        TerI->hitnorm = TerI->uhitnorm = (struct nuvec_s){ 0.0f, 1.0f, 0.0f };
    }
}
