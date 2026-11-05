#include "gameplay/steadycam.h"

#include <math.h>
#include <stddef.h>

#include "game/creature.h"
#include "game/gametypes.h"
#include "gameplay/gameplay.h"

/* Declared by no header. */
extern s32 VEHICLECONTROL;

#define NW_STEADYCAM_REACH 3.0f    /* this far over the take-off (a spring, an updraft) the camera follows again */
#define NW_STEADYCAM_TELEPORT 2.0f /* a move this long in one frame is a respawn or a warp */

static NwSteadyCam nw_steadycam;
static struct obj_s *nw_steadycam_obj;
static float nw_steadycam_y;

float nw_steadycam_follow(NwSteadyCam *s, float x, float y, float z, bool standing, int stability)
{
    bool warped = s->valid && (fabsf(x - s->x) > NW_STEADYCAM_TELEPORT || fabsf(y - s->y) > NW_STEADYCAM_TELEPORT ||
                               fabsf(z - s->z) > NW_STEADYCAM_TELEPORT);
    if (!s->valid || standing || warped) {
        s->floor = y;
    } else {
        s->floor = fmaxf(fminf(s->floor, y), y - NW_STEADYCAM_REACH);
    }
    s->valid = true;
    s->x = x;
    s->y = y;
    s->z = z;
    return y + (s->floor - y) * (float)stability / 100.0f;
}

/* Crash or Coco on foot, alive, under the game's own camera (mode -1: the level's first frame). */
static bool nw_steadycam_on(const struct cammtx_s *cam, const struct obj_s *obj)
{
    return nw_profile.camera_stability > 0 && obj == &player->obj && cam->mode != -1 && !obj->dead &&
           VEHICLECONTROL != 2 && !(VEHICLECONTROL == 1 && obj->vehicle != -1);
}

void nw_steadycam_before(const struct cammtx_s *cam, struct obj_s *obj)
{
    nw_steadycam_obj = NULL;
    if (!nw_steadycam_on(cam, obj)) {
        nw_steadycam.valid = false;
        return;
    }
    NW_DEVIATION(camera_stability);
    nw_steadycam_obj = obj;
    nw_steadycam_y = obj->pos.y;
    obj->pos.y = nw_steadycam_follow(&nw_steadycam, obj->pos.x, obj->pos.y, obj->pos.z,
                                     obj->ground != 0 || obj->dangle != 0, nw_profile.camera_stability);
}

void nw_steadycam_after(struct obj_s *obj)
{
    if (obj == nw_steadycam_obj) {
        obj->pos.y = nw_steadycam_y;
        nw_steadycam_obj = NULL;
    }
}
