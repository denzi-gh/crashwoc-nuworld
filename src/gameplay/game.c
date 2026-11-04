/* Wraps of game/game: the jeep's steering hook. */
#include "nu_types.h"

#include "game/gametypes.h"
#include "game/game.retail.h"
#include "gameplay/jeep.h"

u16 SeekRot(u16 a, u16 b, s32 shift)
{
    u16 hdg;
    return nw_jeep_steer(a, b, shift, &hdg) ? hdg : SeekRot__retail(a, b, shift);
}
