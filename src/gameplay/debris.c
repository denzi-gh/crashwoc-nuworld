/* Wraps of gamelib/debris: the particle hazards' reach. */
#include "nu_types.h"
#include "numath/numathtypes.h"
#include "gamelib/debris.retail.h"
#include "gameplay/hazards.h"

s32 DebrisCollisionCheck(struct nuvec_s *pos, f32 radius)
{
    return DebrisCollisionCheck__retail(pos, nw_hazards_particle(radius));
}
