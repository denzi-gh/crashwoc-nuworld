/* Wraps of game/panel: Crunch Time's boss bar and the hub Crunch's prompt, which DrawPanel ends with its debris, the
   boss heads, and the loading minigame without its LOADING. */
#include "nu_types.h"
#include "game/gametypes.h"
#include "game/panel.retail.h"
#include "gameplay/bosshealth.h"
#include "gameplay/hubcrunch.h"
#include "gameplay/minigame.h"

/* Declared by no header. */
extern struct game_s Game;
extern char *tLOADING[];

void DrawGameMessage(char *txt, s32 message_frame, float ys)
{
    if (txt == tLOADING[Game.language] && nw_minigame_playing()) {
        return;
    }
    DrawGameMessage__retail(txt, message_frame, ys);
}

void DrawPanelDebris(void)
{
    nw_boss_health_crunch_time();
    nw_crunch_prompt();
    DrawPanelDebris__retail();
}

s32 DrawPanel3DTempCharacter(u16 xrot, u16 yrot, u16 zrot, s32 rot, f32 x, f32 y, f32 z, f32 scale)
{
    if (nw_boss_health_head(xrot, yrot, rot, x, y, z, scale)) {
        return 1;
    }
    return DrawPanel3DTempCharacter__retail(xrot, yrot, zrot, rot, x, y, z, scale);
}
