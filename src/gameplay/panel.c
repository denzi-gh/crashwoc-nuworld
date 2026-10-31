/* Wraps of game/panel: Crunch Time's boss bar, which DrawPanel ends with its debris. */
#include "nu_types.h"
#include "game/panel.retail.h"
#include "gameplay/bosshealth.h"

void DrawPanelDebris(void)
{
    nw_boss_health_crunch_time();
    DrawPanelDebris__retail();
}
