/* Wraps of game/chase. */
#include "game/creature.h"
#include "game/gametypes.h"
#include "game/chase.retail.h"
#include "gameplay/hazards.h"
#include "gameplay/hitboxes.h"

void UpdateChase(struct chase_s *chase, struct obj_s *obj)
{
    float radius = nw_hazards_chase(obj);
    UpdateChase__retail(chase, obj);
    obj->RADIUS = radius;
    nw_hitboxes_chase(chase);
}
