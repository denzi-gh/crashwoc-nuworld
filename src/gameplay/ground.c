#include "gameplay/ground.h"

#include <stddef.h>

#include "game/creature.h"
#include "game/gametypes.h"
#include "gameplay/gameplay.h"
#include "nu_port.h"

/* Declared by no header. */
extern struct MoveInfo CrashMoveInfo;
extern struct tersurface_s TerSurface[];
extern f32 in_speed;
extern f32 in_f_friction;

/* run_speed: run and dash this much faster, their animations with them, and get up to speed this much sooner. */
#define NW_GROUND_RUN 1.2f
#define NW_GROUND_ACCEL 1.5f

/* CrashAnim's run, sprint and the two leaning runs at retail's rate. */
static const struct {
    int action;
    float speed;
} nw_ground_runs[] = { { 0x3A, 1.3f }, { 0x48, 1.0f }, { 0x32, 1.3f }, { 0x39, 1.3f } };

static float nw_ground_run = 1.0f; /* the speeds' scale set last */
static float nw_ground_anims[sizeof(nw_ground_runs) / sizeof(nw_ground_runs[0])] = { 1.0f, 1.0f, 1.0f, 1.0f };

static float nw_ground_scale(void)
{
    if (!nw_profile.movement.crash.run_speed) {
        return 1.0f;
    }
    NW_DEVIATION(movement.crash.run_speed);
    return NW_GROUND_RUN;
}

void nw_ground_apply(void)
{
    float run = nw_ground_scale(), base_run = NW_HZ(0.072f, 0.060000002f), base_dash = NW_HZ(0.096f, 0.080000006f);
    if (CrashMoveInfo.RUNSPEED == base_run * nw_ground_run) {
        CrashMoveInfo.RUNSPEED = base_run * run;
    }
    if (CrashMoveInfo.SPRINTSPEED == base_dash * nw_ground_run) {
        CrashMoveInfo.SPRINTSPEED = base_dash * run;
    }
    nw_ground_run = run;
}

void nw_ground_loop(const struct creature_s *plr, float forward)
{
    /* Speeding up on the ground; ice keeps retail's slide. */
    if (nw_profile.movement.crash.run_speed && plr->obj.ground && !(TerSurface[plr->obj.surface_type].flags & 0x20) &&
        in_speed > 0.0f && forward < in_speed) {
        NW_DEVIATION(movement.crash.run_speed);
        in_f_friction *= NW_GROUND_ACCEL;
    }
}

void nw_ground_animate(struct creature_s *plr)
{
    struct CharacterModel *model = plr->obj.model;
    float run = nw_ground_scale();
    for (size_t i = 0; model && i < sizeof(nw_ground_runs) / sizeof(nw_ground_runs[0]); i++) {
        struct animlist *anim = model->animlist[nw_ground_runs[i].action];
        if (anim && run != nw_ground_anims[i] && anim->speed == nw_ground_runs[i].speed * nw_ground_anims[i]) {
            anim->speed = nw_ground_runs[i].speed * run;
            nw_ground_anims[i] = run;
        }
    }
}
