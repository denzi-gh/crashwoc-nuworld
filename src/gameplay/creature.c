/* Wraps of game/creature: the air indicator's, the hitbox view's and the mech's hooks. */
#include "game/creature.h"
#include "game/creature.retail.h"
#include "gameplay/airindicator.h"
#include "gameplay/hitboxes.h"
#include "gameplay/mech.h"
#include "nups2/ps2ctl.h"

void MovePlayer(struct creature_s *c, struct nupad_s *pad)
{
    static struct nupad_s aim;
    nw_mech_aim(c);
    MovePlayer__retail(c, nw_mech_pad(c, pad, &aim));
}

void DrawCreatures(struct creature_s *c, s32 count, s32 render, s32 shadow)
{
    DrawCreatures__retail(c, count, render, shadow);
    if (c == &Character[0] && count == 1 && render && shadow) {
        nw_air_indicator_draw(c);
        nw_hitboxes_draw();
    }
}
