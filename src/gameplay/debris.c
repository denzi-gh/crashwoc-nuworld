/* Wraps of gamelib/debris. */
#include "nu_types.h"
#include "numath/numathtypes.h"
#include "gamelib/debris.retail.h"
#include "gameplay/hazards.h"
#include "gameplay/hitboxes.h"

s32 DebrisCollisionCheck(struct nuvec_s *pos, f32 radius)
{
    radius = nw_hazards_particle(radius);
    nw_hitboxes_particle(pos, radius);
    return DebrisCollisionCheck__retail(pos, radius);
}
