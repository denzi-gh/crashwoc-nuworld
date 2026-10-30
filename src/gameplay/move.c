/* Wraps of game/move: the movement features' hooks. */
#include "game/creature.h"
#include "game/move.retail.h"
#include "gameplay/air.h"
#include "gameplay/monkeybars.h"

void MoveCRASH(struct creature_s *plr, struct nupad_s *pad)
{
    MoveCRASH__retail(plr, pad);
    nw_air_move(plr);
}

void AnimateCRASH(struct creature_s *plr)
{
    AnimateCRASH__retail(plr);
    nw_monkey_bars_animate(plr);
}
