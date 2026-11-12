/* Wraps of game/creature: the air indicator's, the hitbox view's, the vehicles', the slide's, the hub Crunch's and the
   gems' hooks. */
#include "game/creature.h"
#include "game/creature.retail.h"
#include "gameplay/airindicator.h"
#include "gameplay/gems.h"
#include "gameplay/ground.h"
#include "gameplay/hubcrunch.h"
#include "gameplay/hitboxes.h"
#include "gameplay/mech.h"
#include "gameplay/submarine.h"
#include "nups2/ps2ctl.h"

void MovePlayer(struct creature_s *c, struct nupad_s *pad)
{
    static struct nupad_s aim;
    nw_mech_aim(c);
    nw_sub_move(c);
    pad = nw_crunch_pad(c, pad);
    MovePlayer__retail(c, nw_mech_pad(c, pad, &aim));
    nw_crunch_push(c);
}

void CloseCreatures(void)
{
    nw_crunch_close();
    CloseCreatures__retail();
}

void LoadCharacterModels(void)
{
    LoadCharacterModels__retail();
    nw_ground_loaded();
    nw_gems_models();
}

void DrawCreatures(struct creature_s *c, s32 count, s32 render, s32 shadow)
{
    DrawCreatures__retail(c, count, render, shadow);
    if (c == &Character[0] && count == 1 && render && shadow) {
        nw_air_indicator_draw(c);
        nw_crunch_draw();
        nw_hitboxes_draw();
    }
}
