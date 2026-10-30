#include "gameplay/hazards.h"

#include "game/creature.h"
#include "gameplay/crash.h"
#include "gameplay/gameplay.h"

/* Declared by no header. */
extern struct obj_s *pObj[64];
extern s32 GAMEOBJECTCOUNT;

/* hazard_hitboxes: a creature that can hurt Crash while he can't hurt it reaches this share of its size; against fire,
   steam and the like this share of Crash's radius counts. */
#define NW_HAZARDS_CREATURE 0.8f
#define NW_HAZARDS_PARTICLE 0.5f

static struct {
    struct obj_s *po;
    float radius;
    struct nuvec_s min, max;
} nw_hazards_saved[64];
static int nw_hazards_count;

void nw_hazards_shrink(struct obj_s *obj)
{
    nw_hazards_count = 0;
    if (!nw_profile.movement.crash.hazard_hitboxes || obj != &Character[0].obj || !nw_crash_on_foot(&Character[0])) {
        return;
    }
    NW_DEVIATION(movement.crash.hazard_hitboxes);
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
        /* The circle, or for flag 0x8000 the box, both only in XZ as retail tests them. */
        po->RADIUS *= NW_HAZARDS_CREATURE;
        po->min.x *= NW_HAZARDS_CREATURE;
        po->min.z *= NW_HAZARDS_CREATURE;
        po->max.x *= NW_HAZARDS_CREATURE;
        po->max.z *= NW_HAZARDS_CREATURE;
    }
}

void nw_hazards_restore(void)
{
    for (int i = 0; i < nw_hazards_count; i++) {
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
    return radius * NW_HAZARDS_PARTICLE;
}
