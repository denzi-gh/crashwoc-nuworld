#include "gameplay/plane.h"

#include <math.h>

#include "game/gametypes.h"
#include "game/vehicle.h"
#include "game/vehicle.retail.h"
#include "gameplay/gameplay.h"
#include "nu_port.h"
#include "nups2/ps2ctl.h"

/* Declared by no header. */
struct gliderbullet_s {
    struct numtx_s mtx;
    struct nuvec_s vel; /* a frame's */
    s16 life;
    s16 lastlife;
};
extern struct gliderbullet_s *UsedGliderBulletList[];
extern s32 UsedGliderBulletNum;
extern struct nuvec_s GliderTargetPos;
extern struct cammtx_s GameCam[];
extern struct zoffa_s EnemyZoffa[4];
extern struct hovablimp_s HovaBlimp[6];
extern f32 TORNSeekTiltXTimeZero;
extern f32 TORNSeekTiltXTimeNonZero;
extern f32 SeekTiltZZero;
extern f32 SeekTiltZNonZero;
extern s32 FlyingLevelExtro;
extern s32 WBIntroOn;
extern s32 Level;
void SeekAngHalfLife360f(f32 *dest, f32 target, f32 halflife, f32 dt);

#define NW_PLANE_TORNADO 0xD
#define NW_PLANE_BOSS 0x18
#define NW_PLANE_AIRSHIP 0x24
/* ControlGlider's stick: 0x7F is centre, nothing inside 32, full tilt at 32 + 96 / 1.02. */
#define NW_PLANE_CENTRE 0x7F
#define NW_PLANE_DEAD 32.0f
#define NW_PLANE_REACH 128.0f
#define NW_PLANE_SLOPE 1.02f
/* Every level but Tornado Alley eases the pitch at this half-life. */
#define NW_PLANE_PITCH_HALF 0.2f
#define NW_PLANE_PAD_UP 0x1000u
#define NW_PLANE_PAD_DOWN 0x4000u

static bool nw_plane_level(void)
{
    return Level == NW_PLANE_TORNADO || Level == NW_PLANE_BOSS || Level == NW_PLANE_AIRSHIP;
}

/* The player flies it: retail zeroes the stick otherwise. */
static bool nw_plane_flown(const struct GLIDERSTRUCT *g)
{
    return !FlyingLevelExtro && !g->AutoPilot && g->InTornadoTime <= 0.0f && !g->TerminalDive &&
           !(Level == NW_PLANE_BOSS && WBIntroOn);
}

/* The stick as retail reads it: the deadzone, then retail's ramp from it to full tilt. */
static void nw_plane_stick(struct nupad_s *p, const NwPlaneMovement *m)
{
    float x = p->rdata.l_alg_x - NW_PLANE_CENTRE, z = p->rdata.l_alg_y - NW_PLANE_CENTRE;
    if (!m->push_dive) {
        z = -z;
        u32 up = p->paddata & NW_PLANE_PAD_UP, down = p->paddata & NW_PLANE_PAD_DOWN;
        p->paddata &= ~(NW_PLANE_PAD_UP | NW_PLANE_PAD_DOWN);
        p->paddata |= (up ? NW_PLANE_PAD_DOWN : 0) | (down ? NW_PLANE_PAD_UP : 0);
    }
    float mag = sqrtf(x * x + z * z), dead = m->deadzone * (NW_PLANE_REACH / 100.0f), k = 0.0f;
    float full = NW_PLANE_DEAD + (NW_PLANE_REACH - NW_PLANE_DEAD) / NW_PLANE_SLOPE;
    if (mag < dead && mag < NW_PLANE_DEAD) {
        k = 1.0f; /* retail ignores it too, but keeps it beside the d-pad */
    } else if (mag >= dead) {
        float tilt = (mag - dead) * NW_PLANE_SLOPE / (NW_PLANE_REACH - dead);
        if (tilt < 1.0f) {
            k = (NW_PLANE_DEAD + tilt * (full - NW_PLANE_DEAD)) / mag;
        } else {
            k = mag < full ? NW_PLANE_REACH / mag : 1.0f;
        }
    }
    p->rdata.l_alg_x = (u8)fminf(fmaxf(NW_PLANE_CENTRE + lroundf(x * k), 0.0f), 255.0f);
    p->rdata.l_alg_y = (u8)fminf(fmaxf(NW_PLANE_CENTRE + lroundf(z * k), 0.0f), 255.0f);
}

bool nw_plane_control(struct GLIDERSTRUCT *g, struct nupad_s *pad)
{
    const NwPlaneMovement *m = &nw_profile.movement.plane;
    if (!m->steering || !pad || !nw_plane_level()) {
        return false;
    }
    NW_DEVIATION(movement.plane.steering);
    static struct nupad_s copy;
    copy = *pad;
    nw_plane_stick(&copy, m);
    bool flown = nw_plane_flown(g);
    float tilt_x = g->TiltX, tilt_z = g->BarrelRoll ? 0.0f : g->TiltZ;
    ControlGlider__retail(g, &copy);
    if (!flown) {
        return true;
    }
    /* Retail's easing redone at the set response. */
    float dt = NW_HZ(0.02f, 0.016666668f), half = NW_PLANE_PITCH_HALF;
    if (Level == NW_PLANE_TORNADO) {
        half = g->DestTiltX == 0.0f ? TORNSeekTiltXTimeZero : TORNSeekTiltXTimeNonZero;
    }
    g->TiltX = tilt_x;
    SeekAngHalfLife360f(&g->TiltX, g->DestTiltX, half / (m->pitch_response / 100.0f), dt);
    if (g->BarrelRoll == 0 && g->OverideTiltZ == 0.0f) {
        half = g->DestTiltZ == 0.0f ? SeekTiltZZero : SeekTiltZNonZero;
        g->TiltZ = tilt_z;
        SeekAngHalfLife360f(&g->TiltZ, g->DestTiltZ, half / (m->bank_response / 100.0f), dt);
    }
    return true;
}

/* Where a shot at speed from p meets something at pos moving at vel; false if it never does. */
static bool nw_plane_lead(const struct nuvec_s *p, float speed, const struct nuvec_s *pos, const struct nuvec_s *vel,
                          struct nuvec_s *at)
{
    struct nuvec_s d = { pos->x - p->x, pos->y - p->y, pos->z - p->z };
    float a = vel->x * vel->x + vel->y * vel->y + vel->z * vel->z - speed * speed;
    float b = 2.0f * (d.x * vel->x + d.y * vel->y + d.z * vel->z), c = d.x * d.x + d.y * d.y + d.z * d.z;
    float disc = b * b - 4.0f * a * c;
    if (a >= 0.0f || disc < 0.0f) {
        return false;
    }
    float t = (-b - sqrtf(disc)) / (2.0f * a);
    *at = (struct nuvec_s){ pos->x + vel->x * t, pos->y + vel->y * t, pos->z + vel->z * t };
    return true;
}

/* An enemy at pos moving at vel, picked when its lead point is in reach and nearer the camera's ray r (unit) than
   *best's cosine. */
static void nw_plane_pick(const struct nuvec_s *p, float speed, float reach, const struct nuvec_s *r,
                          const struct nuvec_s *pos, const struct nuvec_s *vel, float *best, struct nuvec_s *aim)
{
    const struct nuvec_s *cam = &GameCam[0].pos;
    struct nuvec_s at;
    if (!nw_plane_lead(p, speed, pos, vel, &at)) {
        return;
    }
    struct nuvec_s e = { at.x - cam->x, at.y - cam->y, at.z - cam->z };
    struct nuvec_s f = { at.x - p->x, at.y - p->y, at.z - p->z };
    float el = sqrtf(e.x * e.x + e.y * e.y + e.z * e.z);
    float cosine = el > 0.0f ? (e.x * r->x + e.y * r->y + e.z * r->z) / el : -1.0f;
    if (cosine > *best && f.x * f.x + f.y * f.y + f.z * f.z <= reach * reach) {
        *best = cosine;
        *aim = at;
    }
}

/* aim_assist: the UFO or blimp whose lead point is nearest the reticle, in the cone. */
static void nw_plane_assist(const struct nuvec_s *p, float speed, float reach, struct nuvec_s *aim)
{
    const struct nuvec_s *cam = &GameCam[0].pos;
    struct nuvec_s r = { aim->x - cam->x, aim->y - cam->y, aim->z - cam->z };
    float rl = sqrtf(r.x * r.x + r.y * r.y + r.z * r.z);
    if (rl <= 0.0f) {
        return;
    }
    r = (struct nuvec_s){ r.x / rl, r.y / rl, r.z / rl };
    float best = cosf(nw_profile.movement.plane.aim_assist * 3.14159265f / 180.0f);
    for (int i = 0; i < 4; i++) {
        if (EnemyZoffa[i].active) {
            nw_plane_pick(p, speed, reach, &r, &EnemyZoffa[i].pos, &EnemyZoffa[i].vel, &best, aim);
        }
    }
    for (int i = 0; i < 6; i++) {
        if (HovaBlimp[i].active) {
            nw_plane_pick(p, speed, reach, &r, &HovaBlimp[i].pos, &HovaBlimp[i].vel, &best, aim);
        }
    }
}

/* v turned about the unit axis k by the angle of cosine c, sine s. */
static void nw_plane_turn(float *v, const struct nuvec_s *k, float c, float s)
{
    float d = k->x * v[0] + k->y * v[1] + k->z * v[2];
    float x = v[0] * c + (k->y * v[2] - k->z * v[1]) * s + k->x * d * (1.0f - c);
    float y = v[1] * c + (k->z * v[0] - k->x * v[2]) * s + k->y * d * (1.0f - c);
    float z = v[2] * c + (k->x * v[1] - k->y * v[0]) * s + k->z * d * (1.0f - c);
    v[0] = x;
    v[1] = y;
    v[2] = z;
}

/* The shot flies to the reticle's point or the enemy it picks, its tracer turned with it. */
static void nw_plane_aim(struct gliderbullet_s *b)
{
    float dt = NW_HZ(0.02f, 0.016666668f);
    struct nuvec_s p = { b->mtx._30, b->mtx._31, b->mtx._32 }, v = b->vel, aim = GliderTargetPos;
    float vl = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
    if (vl <= 0.0f) {
        return;
    }
    if (nw_profile.movement.plane.aim_assist > 0) {
        nw_plane_assist(&p, vl / dt, vl * b->life, &aim);
    }
    struct nuvec_s n = { aim.x - p.x, aim.y - p.y, aim.z - p.z };
    float nl = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
    if (nl <= 0.0f) {
        return;
    }
    n = (struct nuvec_s){ n.x / nl, n.y / nl, n.z / nl };
    struct nuvec_s o = { v.x / vl, v.y / vl, v.z / vl };
    struct nuvec_s k = { o.y * n.z - o.z * n.y, o.z * n.x - o.x * n.z, o.x * n.y - o.y * n.x };
    float s = sqrtf(k.x * k.x + k.y * k.y + k.z * k.z), c = o.x * n.x + o.y * n.y + o.z * n.z;
    b->vel = (struct nuvec_s){ n.x * vl, n.y * vl, n.z * vl };
    if (s > 0.0f) {
        k = (struct nuvec_s){ k.x / s, k.y / s, k.z / s };
        nw_plane_turn(&b->mtx._00, &k, c, s);
        nw_plane_turn(&b->mtx._10, &k, c, s);
        nw_plane_turn(&b->mtx._20, &k, c, s);
    }
}

bool nw_plane_fire(struct GLIDERSTRUCT *g, struct nupad_s *pad)
{
    if (!nw_profile.movement.plane.aim || Level != NW_PLANE_TORNADO) {
        return false;
    }
    s32 shots = UsedGliderBulletNum;
    GliderFire__retail(g, pad);
    if (UsedGliderBulletNum == shots + 1) {
        NW_DEVIATION(movement.plane.aim);
        nw_plane_aim(UsedGliderBulletList[shots]);
    }
    return true;
}
