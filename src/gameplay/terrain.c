/* Wraps of gamelib/terrain. */
#include "nu_types.h"
#include "numath/numathtypes.h"
#include "gamelib/terrain.retail.h"
#include "gameplay/edges.h"
#include "gameplay/submarine.h"

void NewTerrainScaleY(struct nuvec_s *vpos, struct nuvec_s *vvel, u8 *flags, s32 terid, f32 stopflag, f32 size,
                      f32 yscale)
{
    nw_sub_accel(vvel);
    nw_edges_scan(flags);
    NewTerrainScaleY__retail(vpos, vvel, flags, terid, stopflag, size, yscale);
    nw_edges_scanned(flags);
}

void TerrainImpact(struct nuvec_s *vpos, struct nuvec_s *vvel, u8 *flags)
{
    nw_edges_impact(flags);
    TerrainImpact__retail(vpos, vvel, flags);
}
