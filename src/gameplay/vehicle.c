/* Wraps of game/vehicle: the boss health bar's and the atlasphere's hooks. */
#include "game/vehicle.h"
#include "game/vehicle.retail.h"
#include "gameplay/atlasphere.h"
#include "gameplay/bosshealth.h"

void BossBar(f32 x, f32 y, f32 z, f32 xs, f32 ys, s32 health, s32 maxhealth)
{
    x = nw_boss_health_bar_x(x);
    BossBar__retail(x, y, z, xs, ys, health, maxhealth);
    nw_boss_health_bar(x, y, z, xs, ys, health, maxhealth);
}

void ProcessMovementAtlas(struct NEWBUGGY *Atlas, f32 DeltaTime)
{
    ProcessMovementAtlas__retail(Atlas, DeltaTime);
    nw_atlas_move(Atlas, DeltaTime);
}
