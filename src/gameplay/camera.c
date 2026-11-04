/* Wraps of game/camera. */
#include "game/creature.h"
#include "game/gametypes.h"
#include "game/camera.retail.h"
#include "gameplay/steadycam.h"

void MoveGameCamera(struct cammtx_s *GameCamera, struct obj_s *obj)
{
    nw_steadycam_before(GameCamera, obj);
    MoveGameCamera__retail(GameCamera, obj);
    nw_steadycam_after(obj);
}
