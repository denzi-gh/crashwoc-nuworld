/* Wraps of game/ai. */
#include "game/creature.h"
#include "nu_port.h"
#include "game/ai.retail.h"
#include "gameplay/submarine.h"

void MoveCreature(struct creature_s *c)
{
    nw_sub_shrink();
    MoveCreature__retail(c);
    nw_sub_restore();
}
