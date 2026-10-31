/* Wraps of game/move: the movement features' hooks. */
#include "game/creature.h"
#include "game/move.retail.h"
#include "gameplay/air.h"
#include "gameplay/ground.h"
#include "gameplay/monkeybars.h"
#include "gameplay/submarine.h"

void MoveCRASH(struct creature_s *plr, struct nupad_s *pad)
{
    nw_ground_before(plr, pad);
    nw_air_before(plr, pad);
    MoveCRASH__retail(plr, pad);
    nw_air_move(plr, pad);
    nw_ground_after(plr);
}

void AnimateCRASH(struct creature_s *plr)
{
    AnimateCRASH__retail(plr);
    nw_monkey_bars_animate(plr);
    nw_ground_animate(plr);
}

void MoveSUBMARINE(struct creature_s *plr, struct nupad_s *pad)
{
    nw_sub_turn(plr);
    nw_sub_fire_before(plr);
    MoveSUBMARINE__retail(plr, pad);
    nw_sub_fire_after(plr);
}
