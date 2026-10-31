#include "gameplay/crash.h"

#include "game/creature.h"

/* Declared by no header. */
extern s32 VEHICLECONTROL;

bool nw_crash_on_foot(const struct creature_s *c)
{
    return c->obj.character == 0 && VEHICLECONTROL != 2 && !(VEHICLECONTROL == 1 && c->obj.vehicle != -1);
}
