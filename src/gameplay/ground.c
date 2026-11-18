#include "gameplay/ground.h"

#include <stddef.h>

#include "game/creature.h"
#include "game/gametypes.h"
#include "gameplay/crash.h"
#include "gameplay/gameplay.h"
#include "nu_port.h"
#include "nups2/ps2ctl.h"

/* Declared by no header. */
extern struct MoveInfo CrashMoveInfo;
extern struct MoveInfo CocoMoveInfo;
extern struct tersurface_s TerSurface[];
extern f32 in_speed;
extern f32 in_f_friction;
extern f32 in_s_friction;

typedef struct NwGroundRun {
    int action;
    float speed;
    bool dash;
} NwGroundRun;

/* CrashAnim's run, sprint and leaning runs at retail's rate. */
static const NwGroundRun nw_ground_runs[] = {
    { 0x3A, 1.3f, false }, { 0x48, 1.0f, false }, { 0x32, 1.3f, false }, { 0x39, 1.3f, false }
};
/* CocoAnim's run and sprint. */
static const NwGroundRun nw_ground_coco_runs[] = { { 0x3A, 1.2f, false }, { 0x48, 1.2f, true } };

static float nw_ground_run = 1.0f;
static float nw_ground_anims[sizeof(nw_ground_runs) / sizeof(nw_ground_runs[0])] = { 1.0f, 1.0f, 1.0f, 1.0f };
static float nw_ground_coco_anims[sizeof(nw_ground_coco_runs) / sizeof(nw_ground_coco_runs[0])] = { 1.0f, 1.0f };
static int nw_ground_coco_top; /* the run_top Coco runs at, 0 her own */
static short nw_ground_spin;
static float nw_ground_slide_speed = 1.0f;
static short nw_ground_slide = 25;
static short nw_ground_slide_retail = 25; /* LoadCharacterModels' */
static bool nw_ground_was_sliding;
static struct nuvec_s nw_ground_mom;

static short nw_ground_frames(int ms)
{
    return (short)((ms * NW_HZ(50, 60) + 500) / 1000);
}

static float nw_ground_scale(void)
{
    if (!nw_profile.movement.crash.run_speed) {
        return 1.0f;
    }
    NW_DEVIATION(movement.crash.run_speed);
    return nw_profile.movement.crash.run_top / 100.0f;
}

static int nw_ground_coco_run(void)
{
    if (!nw_profile.movement.coco.run_speed) {
        return 0;
    }
    NW_DEVIATION(movement.coco.run_speed);
    return nw_profile.movement.coco.run_top;
}

/* Coco's run or dash at top percent of Crash's PS2 one, 0 her own. */
static float nw_ground_coco_speed(int top, bool dash)
{
    float coco = dash ? NW_HZ(0.089999996f, 0.075f) : NW_HZ(0.06f, 0.050000004f);
    float crash = dash ? NW_HZ(0.096f, 0.080000006f) : NW_HZ(0.072f, 0.060000002f);
    return top ? crash * (top / 100.0f) : coco;
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
    int top = nw_ground_coco_run();
    if (CocoMoveInfo.RUNSPEED == nw_ground_coco_speed(nw_ground_coco_top, false)) {
        CocoMoveInfo.RUNSPEED = nw_ground_coco_speed(top, false);
    }
    if (CocoMoveInfo.SPRINTSPEED == nw_ground_coco_speed(nw_ground_coco_top, true)) {
        CocoMoveInfo.SPRINTSPEED = nw_ground_coco_speed(top, true);
    }
    nw_ground_coco_top = top;
    short spin = NW_HZ(16, 20);
    if (!nw_ground_spin) {
        nw_ground_spin = spin;
    }
    if (c->long_spin) {
        NW_DEVIATION(movement.crash.long_spin);
        spin = nw_ground_frames(c->spin_ms);
    }
    if (CrashMoveInfo.SPINFRAMES == nw_ground_spin) {
        CrashMoveInfo.SPINFRAMES = spin;
    }
    nw_ground_spin = spin;
    float slide_speed = 1.0f;
    short slide = nw_ground_slide_retail;
    if (c->slide) {
        NW_DEVIATION(movement.crash.slide);
        slide_speed = c->slide_speed / 100.0f;
        slide = nw_ground_frames(c->slide_ms);
    }
    if (CrashMoveInfo.SLIDESPEED == base_dash * nw_ground_slide_speed) {
        CrashMoveInfo.SLIDESPEED = base_dash * slide_speed;
    }
    if (CrashMoveInfo.SLIDEFRAMES == nw_ground_slide) {
        CrashMoveInfo.SLIDEFRAMES = slide;
    }
    nw_ground_slide_speed = slide_speed;
    nw_ground_slide = slide;
}

void nw_ground_loaded(void)
{
    nw_ground_slide_retail = nw_ground_slide = CrashMoveInfo.SLIDEFRAMES;
    nw_ground_apply();
}

/* slide: retail lets a slide run its whole time and then stops him dead. */
void nw_ground_before(struct creature_s *plr, const struct nupad_s *pad)
{
    const NwCrashMovement *c = &nw_profile.movement.crash;
    nw_ground_was_sliding = plr->slide != 0;
    nw_ground_mom = plr->obj.mom;
    if (c->slide && c->slide_cancel && plr->slide > 1 && !(pad->paddata & 0x28) &&
        nw_ground_slide - plr->slide >= nw_ground_frames(c->slide_min_ms)) {
        NW_DEVIATION(movement.crash.slide);
        plr->slide = 1;
    }
}

void nw_ground_after(struct creature_s *plr)
{
    const NwCrashMovement *c = &nw_profile.movement.crash;
    if (c->slide && c->slide_keep && nw_ground_was_sliding && plr->slide == 0 && !plr->crawl) {
        NW_DEVIATION(movement.crash.slide);
        plr->obj.mom.x = nw_ground_mom.x;
        plr->obj.mom.z = nw_ground_mom.z;
    }
}

void nw_ground_loop(const struct creature_s *plr, float forward)
{
    const NwCrashMovement *c = nw_foot(plr);
    if (!plr->obj.ground || (TerSurface[plr->obj.surface_type].flags & 0x20) || plr->slide || plr->freeze) {
        return;
    }
    if (c->run_speed && in_speed > 0.0f && forward < in_speed) {
        NW_FOOT_DEVIATION(c, run_speed);
        in_f_friction *= c->run_accel / 100.0f;
    }
    if (c->grip) {
        NW_FOOT_DEVIATION(c, grip);
        in_s_friction *= c->grip_turn / 100.0f;
        if (forward > in_speed || forward < 0.0f) {
            in_f_friction *= c->grip_brake / 100.0f;
        }
    }
}

static void nw_ground_runs_set(struct CharacterModel *model, const NwGroundRun *runs, float *anims, size_t n, float run,
                               float dash)
{
    for (size_t i = 0; model && i < n; i++) {
        struct animlist *anim = model->animlist[runs[i].action];
        float scale = runs[i].dash ? dash : run;
        if (anim && scale != anims[i] && anim->speed == runs[i].speed * anims[i]) {
            anim->speed = runs[i].speed * scale;
            anims[i] = scale;
        }
    }
}

void nw_ground_animate(struct creature_s *plr)
{
    struct CharacterModel *model = plr->obj.model;
    if (plr->obj.character != 1) {
        float run = nw_ground_scale();
        nw_ground_runs_set(model, nw_ground_runs, nw_ground_anims, sizeof(nw_ground_runs) / sizeof(nw_ground_runs[0]),
                           run, run);
        return;
    }
    int top = nw_ground_coco_run();
    float run = nw_ground_coco_speed(top, false) / nw_ground_coco_speed(0, false);
    float dash = nw_ground_coco_speed(top, true) / nw_ground_coco_speed(0, true);
    nw_ground_runs_set(model, nw_ground_coco_runs, nw_ground_coco_anims,
                       sizeof(nw_ground_coco_runs) / sizeof(nw_ground_coco_runs[0]), run, dash);
}
