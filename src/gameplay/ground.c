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
extern f32 in_s_friction;

/* CrashAnim's run, sprint and leaning runs at retail's rate. */
static const struct {
    int action;
    float speed;
} nw_ground_runs[] = { { 0x3A, 1.3f }, { 0x48, 1.0f }, { 0x32, 1.3f }, { 0x39, 1.3f } };

static float nw_ground_run = 1.0f;
static float nw_ground_anims[sizeof(nw_ground_runs) / sizeof(nw_ground_runs[0])] = { 1.0f, 1.0f, 1.0f, 1.0f };
static short nw_ground_spin = 16;

static float nw_ground_scale(void)
{
    if (!nw_profile.movement.crash.run_speed) {
        return 1.0f;
    }
    NW_DEVIATION(movement.crash.run_speed);
    return nw_profile.movement.crash.run_top / 100.0f;
}

void nw_ground_apply(void)
{
    const NwCrashMovement *c = &nw_profile.movement.crash;
    float run = nw_ground_scale(), base_run = NW_HZ(0.072f, 0.060000002f), base_dash = NW_HZ(0.096f, 0.080000006f);
    if (CrashMoveInfo.RUNSPEED == base_run * nw_ground_run) {
        CrashMoveInfo.RUNSPEED = base_run * run;
    }
    if (CrashMoveInfo.SPRINTSPEED == base_dash * nw_ground_run) {
        CrashMoveInfo.SPRINTSPEED = base_dash * run;
    }
    nw_ground_run = run;
    short spin = 16;
    if (c->long_spin) {
        NW_DEVIATION(movement.crash.long_spin);
        spin = (short)((c->spin_ms * NW_HZ(50, 60) + 500) / 1000);
    }
    if (CrashMoveInfo.SPINFRAMES == nw_ground_spin) {
        CrashMoveInfo.SPINFRAMES = spin;
    }
    nw_ground_spin = spin;
}

void nw_ground_loop(const struct creature_s *plr, float forward)
{
    const NwCrashMovement *c = &nw_profile.movement.crash;
    if (!plr->obj.ground || (TerSurface[plr->obj.surface_type].flags & 0x20) || plr->slide || plr->freeze) {
        return;
    }
    if (c->run_speed && in_speed > 0.0f && forward < in_speed) {
        NW_DEVIATION(movement.crash.run_speed);
        in_f_friction *= c->run_accel / 100.0f;
    }
    if (c->grip) {
        NW_DEVIATION(movement.crash.grip);
        in_s_friction *= c->grip_turn / 100.0f;
        if (forward > in_speed || forward < 0.0f) {
            in_f_friction *= c->grip_brake / 100.0f;
        }
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
