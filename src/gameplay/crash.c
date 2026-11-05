#include "gameplay/crash.h"

#include "game/creature.h"

/* Declared by no header. */
extern s32 VEHICLECONTROL;

static bool nw_on_foot(const struct creature_s *c, int character)
{
    return c->obj.character == character && VEHICLECONTROL != 2 && !(VEHICLECONTROL == 1 && c->obj.vehicle != -1);
}

bool nw_crash_on_foot(const struct creature_s *c)
{
    return nw_on_foot(c, 0);
}

bool nw_coco_on_foot(const struct creature_s *c)
{
    return nw_on_foot(c, 1);
}

const NwCrashMovement *nw_foot(const struct creature_s *c)
{
    static const NwCrashMovement none;
    static NwCrashMovement coco;
    if (nw_crash_on_foot(c)) {
        return &nw_profile.movement.crash;
    }
    if (!nw_coco_on_foot(c)) {
        return &none;
    }
#define NW_FOOT_COPY(f, ...) coco.f = nw_profile.movement.coco.f;
    NW_COCO_MOVEMENT(NW_FOOT_COPY, NW_FOOT_COPY, NW_FOOT_COPY)
#undef NW_FOOT_COPY
    return &coco;
}
