#include "gameplay/firefly.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "game/creature.h"
#include "game/gametypes.h"
#include "game/vehicle.h"
#include "game/vehicle.retail.h"
#include "gameplay/gameplay.h"
#include "gfx/interp.h"
#include "gfx/rndr.h"
#include "nu3d/nugobj.h"
#include "nu3d/numtl.h"
#include "nu3d/nustream.h"
#include "nu3d/nutex.h"
#include "nu_port.h"
#include "numath/numtx.h"
#include "nups2/ps2ctl.h"

/* Declared by no header. */
extern struct GLIDERSTRUCT PlayerGlider;
extern struct cammtx_s GameCam[];
extern s32 Level;
extern s32 FlyingLevelExtro;
extern s32 FireFlyIntroOn;
extern s32 GliderFrameCounter;
extern f32 Level_TargetTime;
extern f32 TorpedoTime;
extern f32 SeekTiltZZero;
extern f32 SeekTiltZNonZero;
extern void SeekAngHalfLife360f(f32 *dest, f32 target, f32 halflife, f32 dt);
extern s32 PickGliderTarget(struct nuvec_s **pos, struct nuvec_s **vel, s32 *type);

#define NW_FIREFLY_LEVEL 0x12
#define NW_FIREFLY_VEHICLE 0x8B
#define NW_FIREFLY_FIRE 0x40
#define NW_FIREFLY_DT NW_HZ(0.02f, 0.016666668f)
#define NW_FIREFLY_LOCK_TIME 1.3f /* FireFlyReset's */
#define NW_FIREFLY_PITCH 0.2f     /* ControlGlider's half-life */
#define NW_FIREFLY_FADE 0.4f
/* The marker: this far from the camera toward the target as retail's, so always the same size. */
#define NW_FIREFLY_AHEAD 10.0f
#define NW_FIREFLY_RADIUS 0.6f
#define NW_FIREFLY_THIN 0.04f
#define NW_FIREFLY_THICK 0.1f
#define NW_FIREFLY_SEGMENTS 32
#define NW_FIREFLY_PULSE NW_HZ(25, 30)
#define NW_FIREFLY_WHITE 0xF0, 0xF0, 0xF0
#define NW_FIREFLY_GREY 0x90, 0x90, 0x90
#define NW_FIREFLY_YELLOW 0xF0, 0xD0, 0x30
#define NW_FIREFLY_RED 0xF0, 0x30, 0x30

static float nw_firefly_time; /* Level_TargetTime as last written, 0 for none */
static float nw_firefly_fade; /* seconds left of the lost lock's fade */
static struct nuvec_s nw_firefly_lost;
static struct numtl_s *nw_firefly_mtl;
static struct numtx_s nw_firefly_mtx;

static bool nw_firefly_flying(const struct GLIDERSTRUCT *g)
{
    return Level == NW_FIREFLY_LEVEL && g == &PlayerGlider && g->Cre && g->Cre->obj.vehicle == NW_FIREFLY_VEHICLE;
}

bool nw_firefly_control(struct GLIDERSTRUCT *glider, struct nupad_s *pad)
{
    const NwFireflyMovement *m = &nw_profile.movement.firefly;
    if (!m->steering || !nw_firefly_flying(glider) || glider->AutoPilot || glider->TerminalDive || glider->BarrelRoll ||
        glider->OverideTiltZ != 0.0f || glider->InTornadoTime != 0.0f || FlyingLevelExtro || FireFlyIntroOn) {
        return false;
    }
    NW_DEVIATION(movement.firefly.steering);
    float tiltx = glider->TiltX, tiltz = glider->TiltZ, dt = NW_FIREFLY_DT;
    ControlGlider__retail(glider, pad);
    float bank = glider->DestTiltZ == 0.0f ? SeekTiltZZero : SeekTiltZNonZero;
    glider->TiltZ = tiltz;
    SeekAngHalfLife360f(&glider->TiltZ, glider->DestTiltZ, bank / (m->steer_response / 100.0f), dt);
    glider->TiltX = tiltx;
    SeekAngHalfLife360f(&glider->TiltX, glider->DestTiltX, NW_FIREFLY_PITCH / (m->pitch_response / 100.0f), dt);
    if (m->steer_turn != 100) {
        /* ProcessGliderMovement, next, turns it TiltZ * 2 degrees a second. */
        glider->AngleY += glider->TiltZ * 2.0f * dt * ((m->steer_turn - 100) / 100.0f);
    }
    return true;
}

bool nw_firefly_fire(struct GLIDERSTRUCT *glider, struct nupad_s *pad)
{
    const NwFireflyMovement *m = &nw_profile.movement.firefly;
    if (Level != NW_FIREFLY_LEVEL || glider != &PlayerGlider) {
        return false;
    }
    if (!m->lock_on) {
        if (nw_firefly_time != 0.0f && Level_TargetTime == nw_firefly_time) {
            Level_TargetTime = NW_FIREFLY_LOCK_TIME;
        }
        nw_firefly_time = 0.0f;
        nw_firefly_fade = 0.0f;
        return false;
    }
    NW_DEVIATION(movement.firefly.lock_on);
    Level_TargetTime = nw_firefly_time = m->lock_ms / 1000.0f;
    struct nupad_s copy;
    if (m->lock_tap && pad && glider->TargetOn && glider->TargetTimer > 0.0f && !(pad->paddata & NW_FIREFLY_FIRE)) {
        copy = *pad;
        copy.paddata |= NW_FIREFLY_FIRE;
        pad = &copy;
    }
    bool on = glider->TargetOn != 0;
    GliderFire__retail(glider, pad);
    nw_firefly_fade = fmaxf(nw_firefly_fade - NW_FIREFLY_DT, 0.0f);
    if (on && !glider->TargetOn && glider->FireTimer == 0.0f && glider->MovingTargetPoint) {
        nw_firefly_fade = NW_FIREFLY_FADE;
        nw_firefly_lost = *glider->MovingTargetPoint;
    }
    return true;
}

NwFireflyLock nw_firefly_lock(struct nuvec_s *at, float *progress)
{
    const struct GLIDERSTRUCT *g = &PlayerGlider;
    struct nuvec_s *pos, *vel;
    s32 type;
    *progress = 0.0f;
    if (g->TerminalDive || g->AutoPilot || FlyingLevelExtro) {
        return NW_FIREFLY_NONE;
    }
    if (g->TargetOn && g->MovingTargetPoint) {
        *at = *g->MovingTargetPoint;
        if (g->TargetTimer > 0.0f && Level_TargetTime > 0.0f) {
            *progress = fmaxf(1.0f - g->TargetTimer / Level_TargetTime, 0.0f);
            return NW_FIREFLY_LOCKING;
        }
        *progress = 1.0f;
        return NW_FIREFLY_LOCKED;
    }
    if (nw_firefly_fade > 0.0f) {
        *at = nw_firefly_lost;
        *progress = nw_firefly_fade / NW_FIREFLY_FADE;
        return NW_FIREFLY_LOST;
    }
    if (!PickGliderTarget(&pos, &vel, &type)) {
        return NW_FIREFLY_NONE;
    }
    *at = *pos;
    if (g->FireTimer > 0.0f && TorpedoTime > 0.0f) {
        *progress = fmaxf(1.0f - g->FireTimer / TorpedoTime, 0.0f);
        return NW_FIREFLY_RELOAD;
    }
    return NW_FIREFLY_TARGET;
}

static struct numtl_s *nw_firefly_material(void)
{
    static uint8_t abgr[2 * 2 * 4];
    memset(abgr, 0xFF, sizeof(abgr));
    struct nutex_s tex = { 3, 2, 2, 1, abgr, NULL };
    struct numtl_s *mtl = NuMtlCreate(1);
    mtl->diffuse.r = 1.0f;
    mtl->diffuse.g = 1.0f;
    mtl->diffuse.b = 1.0f;
    mtl->alpha = 0.999f;
    mtl->attrib.cull = 2;
    mtl->attrib.zmode = 3;
    mtl->attrib.alpha = 1;
    mtl->sort_pri = 102;
    mtl->tid = NuTexCreate(&tex);
    NuMtlUpdate(mtl);
    return mtl;
}

/* share of the ring from the top round, radius r, width w. */
static void nw_firefly_arc(float share, float r, float w, struct nucolour_s c)
{
    struct nuvtx_tc1_s v[2 * (NW_FIREFLY_SEGMENTS + 1)];
    int n = (int)ceilf(share * NW_FIREFLY_SEGMENTS);
    if (n <= 0) {
        return;
    }
    memset(v, 0, sizeof(v));
    for (int k = 0; k <= n; k++) {
        float a = fminf((float)k / NW_FIREFLY_SEGMENTS, share) * 6.2831853f, s = sinf(a), co = cosf(a);
        for (int side = 0; side < 2; side++) {
            struct nuvtx_tc1_s *p = &v[2 * k + side];
            float rr = side ? r + w * 0.5f : r - w * 0.5f;
            p->pnt = (struct nuvec_s){ s * rr, co * rr, 0.0f };
            p->rgba = c;
            p->tc[0] = p->tc[1] = 0.5f;
        }
    }
    nw_rndr_strip(v, 2 * (n + 1), &nw_firefly_mtx, &nw_firefly_mtl->rs);
}

bool nw_firefly_marker(void)
{
    if (Level != NW_FIREFLY_LEVEL || !nw_profile.movement.firefly.lock_on) {
        return false;
    }
    NW_DEVIATION(movement.firefly.lock_on);
    struct nuvec_s at, dir;
    float progress;
    NwFireflyLock lock = nw_firefly_lock(&at, &progress);
    dir = (struct nuvec_s){ at.x - GameCam[0].pos.x, at.y - GameCam[0].pos.y, at.z - GameCam[0].pos.z };
    float d = sqrtf(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (lock == NW_FIREFLY_NONE || d < 0.01f) {
        return true;
    }
    if (!nw_firefly_mtl) {
        nw_firefly_mtl = nw_firefly_material();
    }
    nw_firefly_mtx = GameCam[0].m;
    nw_firefly_mtx._30 += dir.x * NW_FIREFLY_AHEAD / d;
    nw_firefly_mtx._31 += dir.y * NW_FIREFLY_AHEAD / d;
    nw_firefly_mtx._32 += dir.z * NW_FIREFLY_AHEAD / d;
    NuRndrStreamAddMtl(&nw_firefly_mtl->rs, nw_firefly_mtl);
    nw_interp_key(&nw_firefly_mtx);
    float r = NW_FIREFLY_RADIUS;
    switch (lock) {
    case NW_FIREFLY_TARGET:
        nw_firefly_arc(1.0f, r, NW_FIREFLY_THIN, (struct nucolour_s){ NW_FIREFLY_WHITE, 0xC0 });
        break;
    case NW_FIREFLY_RELOAD:
        nw_firefly_arc(1.0f, r, NW_FIREFLY_THIN, (struct nucolour_s){ NW_FIREFLY_GREY, 0x60 });
        nw_firefly_arc(progress, r, NW_FIREFLY_THIN, (struct nucolour_s){ NW_FIREFLY_GREY, 0xC0 });
        break;
    case NW_FIREFLY_LOCKING:
        nw_firefly_arc(1.0f, r, NW_FIREFLY_THIN, (struct nucolour_s){ NW_FIREFLY_YELLOW, 0x80 });
        nw_firefly_arc(progress, r, NW_FIREFLY_THICK, (struct nucolour_s){ NW_FIREFLY_YELLOW, 0xE0 });
        break;
    case NW_FIREFLY_LOCKED: {
        float pulse = sinf((float)(GliderFrameCounter % NW_FIREFLY_PULSE) / NW_FIREFLY_PULSE * 6.2831853f);
        nw_firefly_arc(1.0f, r * (1.0f + 0.08f * pulse), NW_FIREFLY_THICK, (struct nucolour_s){ NW_FIREFLY_RED, 0xE0 });
        break;
    }
    default:
        nw_firefly_arc(1.0f, r * (1.3f - 0.3f * progress), NW_FIREFLY_THICK,
                       (struct nucolour_s){ NW_FIREFLY_RED, (unsigned)(0xE0 * progress) });
        break;
    }
    nw_interp_key(NULL);
    return true;
}

void nw_firefly_release(void)
{
    nw_firefly_mtl = NULL;
}
