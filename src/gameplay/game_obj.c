/* Wraps of game/game_obj. */
#include "game/creature.h"
#include "game/game_obj.retail.h"
#include "numath/nuvec.h"
#include "gameplay/air.h"
#include "gameplay/crash.h"
#include "gameplay/ground.h"
#include "gameplay/hazards.h"
#include "gameplay/line2d.h"
#include "gameplay/mech.h"

/* Declared by no header. */
extern f32 in_speed;
extern f32 in_f_friction;
extern f32 in_s_friction;

void MoveLoopXZ(struct obj_s *obj, u16 *hdg)
{
    float speed = in_speed, forward = in_f_friction, side = in_s_friction;
    u16 walk;
    if (nw_mech_loop(obj, hdg, &walk)) {
        MoveLoopXZ__retail(obj, &walk);
        in_speed = speed;
        in_f_friction = forward;
        in_s_friction = side;
        return;
    }
    if (obj != &Character[0].obj || !nw_crash_on_foot(&Character[0])) {
        MoveLoopXZ__retail(obj, hdg);
        return;
    }
    struct nuvec_s v;
    NuVecRotateY(&v, &obj->mom, -(hdg ? *hdg : obj->hdg));
    nw_ground_loop(&Character[0], v.z);
    nw_air_loop(&Character[0], v.z);
    MoveLoopXZ__retail(obj, hdg);
    in_speed = speed;
    in_f_friction = forward;
    in_s_friction = side;
    nw_line2d_hold(&Character[0]);
}

void PlayerCreatureCollisions(struct obj_s *obj)
{
    nw_hazards_shrink(obj);
    PlayerCreatureCollisions__retail(obj);
    nw_hazards_restore();
}
