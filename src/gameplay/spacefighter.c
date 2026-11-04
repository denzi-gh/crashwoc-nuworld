#include "gameplay/spacefighter.h"

#include <math.h>

#include "game/vehicle.h"
#include "game/vehicle.retail.h"
#include "gameplay/gameplay.h"
#include "nu_port.h"
#include "numath/numtx.h"
#include "numath/nuvec.h"
#include "nups2/ps2ctl.h"

/* vehicle.c's own struct gliderbullet_s and struct asteroid_s; vel is a frame's. */
typedef struct NwBolt {
    struct numtx_s mtx;
    struct nuvec_s vel;
    s16 life, lastlife;
} NwBolt;
_Static_assert(sizeof(NwBolt) == 0x50, "vehicle.c's struct gliderbullet_s");

typedef struct NwAsteroid {
    s32 active;
    struct nuvec_s pos, vel;
    f32 anglex, angley, spinx, spiny;
    struct nuvec_s scale;
    s32 inview, hitpoints;
    char pad[4];
    s32 stealth, explode;
} NwAsteroid;
_Static_assert(sizeof(NwAsteroid) == 0x4C, "vehicle.c's struct asteroid_s");

/* Declared by no header. */
extern struct GLIDERSTRUCT PlayerGlider;
extern s32 Level;
extern s32 FlyingLevelExtro;
extern struct nuvec_s GliderTargetPos;
extern NwBolt *UsedGliderBulletList[64];
extern s32 UsedGliderBulletNum;
extern NwAsteroid AsteroidList[100];
extern struct zoffa_s EnemyZoffa[4];
extern struct satellite_s SatelliteList[9];
extern f32 CRASHTEROIDSSeekTiltXTimeZero;
extern f32 CRASHTEROIDSSeekTiltXTimeNonZero;
extern f32 SeekTiltZZero;
extern f32 SeekTiltZNonZero;
extern f32 Level_GliderSpeed;

#define NW_SPACE_LEVEL 0x1A
#define NW_SPACE_DEG 182.04445f
/* AddGliderBullet's life of a shot here: one that has not moved yet. */
#define NW_SPACE_BOLT_LIFE NW_HZ(37, 45)
/* ControlGlider's stick: the push off centre that starts to steer and the one that steers fully. */
#define NW_SPACE_STICK_DEAD 32.0f
#define NW_SPACE_STICK_FULL (32.0f + 96.0f / 1.02f)

static bool nw_space_flying(const struct GLIDERSTRUCT *g)
{
    return Level == NW_SPACE_LEVEL && g == &PlayerGlider;
}

static bool nw_space_steering(const struct GLIDERSTRUCT *g)
{
    return nw_profile.movement.spacefighter.steering && nw_space_flying(g) && !g->TerminalDive && !g->AutoPilot &&
           !FlyingLevelExtro && g->InTornadoTime == 0.0f;
}

static float nw_space_dot(const struct nuvec_s *a, const struct nuvec_s *b)
{
    return a->x * b->x + a->y * b->y + a->z * b->z;
}

static u8 nw_space_axis(float v)
{
    long a = lroundf(127.0f + v);
    return (u8)(a < 0 ? 0 : a > 255 ? 255 : a);
}

/* deadzone: the stick past it stretched over retail's steering range. */
static void nw_space_stick(struct nupad_s *pad, int deadzone)
{
    float x = pad->rdata.l_alg_x - 127.0f, y = pad->rdata.l_alg_y - 127.0f, r = sqrtf(x * x + y * y);
    float dead = deadzone * 127.0f / 100.0f, k = 0.0f;
    if (r > dead) {
        float push = NW_SPACE_STICK_DEAD +
                     (r - dead) * (NW_SPACE_STICK_FULL - NW_SPACE_STICK_DEAD) / (NW_SPACE_STICK_FULL - dead);
        k = push / r;
    }
    pad->rdata.l_alg_x = nw_space_axis(x * k);
    pad->rdata.l_alg_y = nw_space_axis(y * k);
}

bool nw_space_control(struct GLIDERSTRUCT *g, struct nupad_s *pad)
{
    const NwSpaceMovement *m = &nw_profile.movement.spacefighter;
    if (!nw_space_steering(g)) {
        return false;
    }
    NW_DEVIATION(movement.spacefighter.steering);
    f32 pitch0 = CRASHTEROIDSSeekTiltXTimeZero, pitch1 = CRASHTEROIDSSeekTiltXTimeNonZero;
    f32 bank0 = SeekTiltZZero, bank1 = SeekTiltZNonZero;
    float pitch = 100.0f / m->pitch_speed, bank = 100.0f / m->bank_speed;
    CRASHTEROIDSSeekTiltXTimeZero = pitch0 * pitch;
    CRASHTEROIDSSeekTiltXTimeNonZero = pitch1 * pitch;
    SeekTiltZZero = bank0 * bank;
    SeekTiltZNonZero = bank1 * bank;
    struct nupad_s stick = *pad;
    nw_space_stick(&stick, m->deadzone);
    ControlGlider__retail(g, &stick);
    CRASHTEROIDSSeekTiltXTimeZero = pitch0;
    CRASHTEROIDSSeekTiltXTimeNonZero = pitch1;
    SeekTiltZZero = bank0;
    SeekTiltZNonZero = bank1;
    return true;
}

bool nw_space_movement(struct GLIDERSTRUCT *g, float dt)
{
    const NwSpaceMovement *m = &nw_profile.movement.spacefighter;
    bool turn = nw_space_steering(g);
    bool roll = m->roll && nw_space_flying(g) && g->BarrelRoll != 0;
    if (!turn && !roll) {
        return false;
    }
    f32 speed = Level_GliderSpeed;
    if (roll) {
        NW_DEVIATION(movement.spacefighter.roll);
        /* a roll aims for an eighth of Level_GliderSpeed */
        Level_GliderSpeed = speed * (1.0f + 7.0f * m->roll_kept / 100.0f);
    }
    ProcessGliderMovement__retail(g, dt);
    Level_GliderSpeed = speed;
    /* retail's own turn ran: AngleY += TiltZ * 2 * dt */
    if (turn && g->InTornadoTime == 0.0f && g->BarrelRoll == 0) {
        NW_DEVIATION(movement.spacefighter.steering);
        g->AngleY += g->TiltZ * 2.0f * dt * (m->turn_rate - 100) / 100.0f;
    }
    return true;
}

/* A target at pos moving vel a frame, led for a shot from from at speed a frame; *to its direction when nearer the
   shot's than *best. */
static void nw_space_target(const struct nuvec_s *from, const struct nuvec_s *dir, float speed, float reach,
                            const struct nuvec_s *pos, const struct nuvec_s *vel, float *best, struct nuvec_s *to)
{
    struct nuvec_s d = { pos->x - from->x, pos->y - from->y, pos->z - from->z };
    float frames = sqrtf(nw_space_dot(&d, &d)) / speed;
    d.x += vel->x * frames;
    d.y += vel->y * frames;
    d.z += vel->z * frames;
    float dist = sqrtf(nw_space_dot(&d, &d));
    if (dist < 0.01f || dist > reach) {
        return;
    }
    float c = nw_space_dot(&d, dir) / dist;
    if (c > *best) {
        *best = c;
        *to = (struct nuvec_s){ d.x / dist, d.y / dist, d.z / dist };
    }
}

/* v turned round the unit axis k by the angle of cosine c, sine s. */
static void nw_space_turn(struct nuvec_s *v, const struct nuvec_s *k, float c, float s)
{
    float d = nw_space_dot(k, v) * (1.0f - c);
    struct nuvec_s x = { k->y * v->z - k->z * v->y, k->z * v->x - k->x * v->z, k->x * v->y - k->y * v->x };
    *v = (struct nuvec_s){ v->x * c + x.x * s + k->x * d, v->y * c + x.y * s + k->y * d,
                           v->z * c + x.z * s + k->z * d };
}

/* aim_assist: a new shot bent toward the target nearest its path, the bolt turned with it. */
static void nw_space_assist(NwBolt *b, const NwSpaceMovement *m)
{
    float speed = sqrtf(nw_space_dot(&b->vel, &b->vel));
    if (speed <= 0.0f) {
        return;
    }
    struct nuvec_s from = { b->mtx._30, b->mtx._31, b->mtx._32 };
    struct nuvec_s dir = { b->vel.x / speed, b->vel.y / speed, b->vel.z / speed }, to = dir;
    float reach = speed * b->life, cone = cosf(m->assist_cone * 3.14159265f / 180.0f), best = cone;
    float dt = NW_HZ(0.02f, 0.016666668f);
    for (int i = 0; i < 4; i++) {
        const struct zoffa_s *u = &EnemyZoffa[i];
        if (u->active && !u->explode && !u->terminaldive) {
            struct nuvec_s v = { u->vel.x * dt, u->vel.y * dt, u->vel.z * dt };
            nw_space_target(&from, &dir, speed, reach, &u->pos, &v, &best, &to);
        }
    }
    for (int i = 0; i < 100; i++) {
        const NwAsteroid *a = &AsteroidList[i];
        if (a->active && !a->stealth) {
            nw_space_target(&from, &dir, speed, reach, &a->pos, &a->vel, &best, &to);
        }
    }
    static const struct nuvec_s still = { 0.0f, 0.0f, 0.0f };
    for (int i = 0; i < 9; i++) {
        const struct satellite_s *s = &SatelliteList[i];
        if (s->active && !s->destroyed) {
            nw_space_target(&from, &dir, speed, reach, &s->pos, &still, &best, &to);
        }
    }
    if (best == cone) {
        return;
    }
    float pull = m->assist_pull / 100.0f;
    struct nuvec_s want = { dir.x + (to.x - dir.x) * pull, dir.y + (to.y - dir.y) * pull,
                            dir.z + (to.z - dir.z) * pull };
    float len = sqrtf(nw_space_dot(&want, &want));
    struct nuvec_s k = { dir.y * want.z - dir.z * want.y, dir.z * want.x - dir.x * want.z,
                         dir.x * want.y - dir.y * want.x };
    float s = sqrtf(nw_space_dot(&k, &k)) / len;
    if (s <= 0.0f) {
        return;
    }
    NW_DEVIATION(movement.spacefighter.aim_assist);
    float c = nw_space_dot(&dir, &want) / len, ks = s * len;
    k = (struct nuvec_s){ k.x / ks, k.y / ks, k.z / ks };
    nw_space_turn((struct nuvec_s *)&b->mtx._00, &k, c, s);
    nw_space_turn((struct nuvec_s *)&b->mtx._10, &k, c, s);
    nw_space_turn((struct nuvec_s *)&b->mtx._20, &k, c, s);
    b->vel = (struct nuvec_s){ want.x / len * speed, want.y / len * speed, want.z / len * speed };
}

bool nw_space_fire(struct GLIDERSTRUCT *g, struct nupad_s *pad)
{
    const NwSpaceMovement *m = &nw_profile.movement.spacefighter;
    if ((!m->aim && !m->aim_assist) || !nw_space_flying(g)) {
        return false;
    }
    s32 first = UsedGliderBulletNum;
    GliderFire__retail(g, pad);
    if (m->aim) {
        NW_DEVIATION(movement.spacefighter.aim);
        /* GliderFire's shot matrix; the two guns' shots meet on its line */
        struct numtx_s mat;
        struct nuvec_s ahead = { 0.0f, -0.1f, -(float)m->aim_range };
        NuMtxSetRotationZ(&mat, (s32)(g->TiltZ * NW_SPACE_DEG));
        NuMtxRotateX(&mat, (s32)((g->TiltX * 2.0f - 10.0f) * NW_SPACE_DEG));
        NuMtxRotateY(&mat, (s32)(g->AngleY * NW_SPACE_DEG));
        NuVecMtxRotate(&GliderTargetPos, &ahead, &mat);
        NuVecAdd(&GliderTargetPos, &GliderTargetPos, &g->Position);
    }
    if (m->aim_assist) {
        for (s32 i = first; i < UsedGliderBulletNum; i++) {
            nw_space_assist(UsedGliderBulletList[i], m);
        }
    }
    return true;
}

/* aim_sweep: a shot whose last step passed through the sphere is put there while retail tests it. */
bool nw_space_collide(struct nuvec_s *pos, float radius, int32_t enemy, float yscale, int32_t debris, int32_t reflect,
                      int32_t *hit)
{
    const NwSpaceMovement *m = &nw_profile.movement.spacefighter;
    if (!m->aim || !m->aim_sweep || Level != NW_SPACE_LEVEL || enemy || reflect) {
        return false;
    }
    struct {
        NwBolt *bolt;
        struct nuvec_s at;
    } moved[64];
    int n = 0;
    float r2 = radius * radius;
    for (s32 i = 0; i < UsedGliderBulletNum && n < 64; i++) {
        NwBolt *b = UsedGliderBulletList[i];
        if (b->life >= b->lastlife || b->life >= NW_SPACE_BOLT_LIFE) {
            continue;
        }
        struct nuvec_s at = { b->mtx._30, b->mtx._31, b->mtx._32 };
        struct nuvec_s d = { at.x - pos->x, (at.y - pos->y) * yscale, at.z - pos->z };
        struct nuvec_s v = { b->vel.x, b->vel.y * yscale, b->vel.z };
        float vv = nw_space_dot(&v, &v), t = vv > 0.0f ? nw_space_dot(&d, &v) / vv : 0.0f;
        if (nw_space_dot(&d, &d) <= r2 || t <= 0.0f) {
            continue;
        }
        t = fminf(t, 1.0f);
        struct nuvec_s c = { d.x - v.x * t, d.y - v.y * t, d.z - v.z * t };
        if (nw_space_dot(&c, &c) >= r2) {
            continue;
        }
        moved[n].bolt = b;
        moved[n++].at = at;
        b->mtx._30 = at.x - b->vel.x * t;
        b->mtx._31 = at.y - b->vel.y * t;
        b->mtx._32 = at.z - b->vel.z * t;
    }
    if (n > 0) {
        NW_DEVIATION(movement.spacefighter.aim);
    }
    *hit = CollideGliderBullets__retail(pos, radius, enemy, yscale, debris, reflect);
    for (int i = 0; i < n; i++) {
        moved[i].bolt->mtx._30 = moved[i].at.x;
        moved[i].bolt->mtx._31 = moved[i].at.y;
        moved[i].bolt->mtx._32 = moved[i].at.z;
    }
    return true;
}
