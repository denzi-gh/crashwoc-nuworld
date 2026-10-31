/* Wraps of game/panel: Crunch Time's boss bar, which DrawPanel ends with its debris, and the boss heads. */
#include "nu_types.h"
#include "game/panel.retail.h"
#include "gameplay/bosshealth.h"

void DrawPanelDebris(void)
{
    nw_boss_health_crunch_time();
    DrawPanelDebris__retail();
}

s32 DrawPanel3DTempCharacter(u16 xrot, u16 yrot, u16 zrot, s32 rot, f32 x, f32 y, f32 z, f32 scale)
{
    if (nw_boss_health_head(xrot, yrot, rot, x, y, z, scale)) {
        return 1;
    }
    return DrawPanel3DTempCharacter__retail(xrot, yrot, zrot, rot, x, y, z, scale);
}
