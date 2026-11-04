/* Wraps of game/vehicle: the boss health bar's hook. */
#include "nu_types.h"
#include "game/vehicle.retail.h"
#include "gameplay/bosshealth.h"

void BossBar(f32 x, f32 y, f32 z, f32 xs, f32 ys, s32 health, s32 maxhealth)
{
    x = nw_boss_health_bar_x(x);
    BossBar__retail(x, y, z, xs, ys, health, maxhealth);
    nw_boss_health_bar(x, y, z, xs, ys, health, maxhealth);
}
