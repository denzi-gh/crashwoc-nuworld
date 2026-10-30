/* Wraps of game/move: the movement features' hooks. */
#include "game/creature.h"
#include "game/move.retail.h"
#include "gameplay/monkeybars.h"

void AnimateCRASH(struct creature_s *plr)
{
    AnimateCRASH__retail(plr);
    nw_monkey_bars_animate(plr);
}
