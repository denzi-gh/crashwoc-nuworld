/* Wraps of game/move: the movement features' hooks. */
#include "game/creature.h"
#include "game/move.retail.h"
#include "gameplay/air.h"
#include "gameplay/ground.h"
#include "gameplay/monkeybars.h"

void MoveCRASH(struct creature_s *plr, struct nupad_s *pad)
{
    nw_air_before(plr, pad);
    MoveCRASH__retail(plr, pad);
    nw_air_move(plr, pad);
}

void AnimateCRASH(struct creature_s *plr)
{
    AnimateCRASH__retail(plr);
    nw_monkey_bars_animate(plr);
    nw_ground_animate(plr);
}
