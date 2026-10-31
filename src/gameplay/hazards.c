#include "gameplay/hazards.h"

#include "game/creature.h"
#include "gameplay/crash.h"
#include "gameplay/gameplay.h"

/* Declared by no header. */
extern struct obj_s *pObj[64];
extern s32 GAMEOBJECTCOUNT;

static struct {
    struct obj_s *po;
    float radius;
    struct nuvec_s min, max;
} nw_hazards_saved[64];
static int nw_hazards_count;

/* Only what can hurt Crash while he can't hurt it, so his own reach stays; the circle or the box, in XZ. */
void nw_hazards_shrink(struct obj_s *obj)
{
    nw_hazards_count = 0;
    if (!nw_profile.movement.crash.hazard_hitboxes || obj != &Character[0].obj || !nw_crash_on_foot(&Character[0])) {
        return;
    }
    NW_DEVIATION(movement.crash.hazard_hitboxes);
    float k = nw_profile.movement.crash.hazard_enemy / 100.0f;
    for (int i = 0; i < GAMEOBJECTCOUNT && i < 64; i++) {
        struct obj_s *po = pObj[i];
        if (!po || po == obj || !(po->attack != 0 || (po->flags & 0x100)) || (obj->attack & po->vulnerable)) {
            continue;
        }
        nw_hazards_saved[nw_hazards_count].po = po;
        nw_hazards_saved[nw_hazards_count].radius = po->RADIUS;
        nw_hazards_saved[nw_hazards_count].min = po->min;
        nw_hazards_saved[nw_hazards_count].max = po->max;
        nw_hazards_count++;
        float cx = (po->min.x + po->max.x) * 0.5f, cz = (po->min.z + po->max.z) * 0.5f;
        po->RADIUS *= k;
        po->min.x = cx + (po->min.x - cx) * k;
        po->min.z = cz + (po->min.z - cz) * k;
        po->max.x = cx + (po->max.x - cx) * k;
        po->max.z = cz + (po->max.z - cz) * k;
    }
}

void nw_hazards_restore(void)
{
    for (int i = nw_hazards_count - 1; i >= 0; i--) {
        struct obj_s *po = nw_hazards_saved[i].po;
        po->RADIUS = nw_hazards_saved[i].radius;
        po->min = nw_hazards_saved[i].min;
        po->max = nw_hazards_saved[i].max;
    }
    nw_hazards_count = 0;
}

float nw_hazards_particle(float radius)
{
    if (!nw_profile.movement.crash.hazard_hitboxes || !nw_crash_on_foot(&Character[0])) {
        return radius;
    }
    NW_DEVIATION(movement.crash.hazard_hitboxes);
    return radius * nw_profile.movement.crash.hazard_fire / 100.0f;
}
