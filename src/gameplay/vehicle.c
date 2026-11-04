/* Wraps of game/vehicle: the atlasphere's hook. */
#include "game/vehicle.h"
#include "game/vehicle.retail.h"
#include "gameplay/atlasphere.h"

void ProcessMovementAtlas(struct NEWBUGGY *Atlas, f32 DeltaTime)
{
    ProcessMovementAtlas__retail(Atlas, DeltaTime);
    nw_atlas_move(Atlas, DeltaTime);
}
