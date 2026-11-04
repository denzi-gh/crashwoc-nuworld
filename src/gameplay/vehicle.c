/* Wraps of game/vehicle: the boss health bar's, the atlasphere's and the gliders' hooks. */
#include "game/vehicle.h"
#include "game/vehicle.retail.h"
#include "gameplay/atlasphere.h"
#include "gameplay/bosshealth.h"
#include "gameplay/plane.h"

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

void ControlGlider(struct GLIDERSTRUCT *glider, struct nupad_s *pad)
{
    if (!nw_plane_control(glider, pad)) {
        ControlGlider__retail(glider, pad);
    }
}

void GliderFire(struct GLIDERSTRUCT *glider, struct nupad_s *pad)
{
    if (!nw_plane_fire(glider, pad)) {
        GliderFire__retail(glider, pad);
    }
}
