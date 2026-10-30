/* Wraps of game/creature: the air indicator's hook. */
#include "game/creature.h"
#include "game/creature.retail.h"
#include "gameplay/airindicator.h"

void DrawCreatures(struct creature_s *c, s32 count, s32 render, s32 shadow)
{
    DrawCreatures__retail(c, count, render, shadow);
    if (c == &Character[0] && count == 1 && render && shadow) {
        nw_air_indicator_draw(c);
    }
}
