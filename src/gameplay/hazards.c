#include "gameplay/hazards.h"

#include <math.h>

#include "game/creature.h"
#include "gameplay/crash.h"
#include "gameplay/gameplay.h"
#include "gameplay/mech.h"

/* Declared by no header. */
extern struct obj_s *pObj[64];
extern s32 GAMEOBJECTCOUNT;

static struct {
    struct obj_s *po;
    float radius;
    struct nuvec_s min, max;
} nw_hazards_saved[64];
static int nw_hazards_count;

static bool nw_hazards_mech(const struct obj_s *obj)
{
    return obj == &Character[0].obj && nw_profile.movement.mech.hitbox && nw_mech_driving(&Character[0]);
}

/* Only what can hurt the player while he can't hurt it, so his own reach stays; the circle or the box, in XZ. Crash's
   scale it, the mech's lose what the mech's own radius loses. */
void nw_hazards_shrink(struct obj_s *obj)
{
    nw_hazards_count = 0;
    float k = 1.0f, cut = 0.0f;
    if (obj == &Character[0].obj && nw_profile.movement.crash.hazard_hitboxes && nw_crash_on_foot(&Character[0])) {
        NW_DEVIATION(movement.crash.hazard_hitboxes);
        k = nw_profile.movement.crash.hazard_enemy / 100.0f;
    } else if (nw_hazards_mech(obj)) {
        NW_DEVIATION(movement.mech.hitbox);
        cut = obj->RADIUS * (1.0f - nw_profile.movement.mech.hitbox_size / 100.0f);
    } else {
        return;
    }
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
        float box = po->SCALE > 0.0f ? cut / po->SCALE : 0.0f; /* the box is scaled by SCALE */
        po->RADIUS = fmaxf(po->RADIUS * k - cut, 0.0f);
        po->min.x = fminf(cx + (po->min.x - cx) * k + box, cx);
        po->min.z = fminf(cz + (po->min.z - cz) * k + box, cz);
        po->max.x = fmaxf(cx + (po->max.x - cx) * k - box, cx);
        po->max.z = fmaxf(cz + (po->max.z - cz) * k - box, cz);
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

float nw_hazards_chase(struct obj_s *obj)
{
    float radius = obj->RADIUS;
    if (nw_hazards_mech(obj)) {
        NW_DEVIATION(movement.mech.hitbox);
        obj->RADIUS *= nw_profile.movement.mech.hitbox_size / 100.0f;
    }
    return radius;
}

float nw_hazards_radius(const struct obj_s *obj)
{
    return nw_hazards_mech(obj) ? obj->RADIUS * nw_profile.movement.mech.hitbox_size / 100.0f : obj->RADIUS;
}
