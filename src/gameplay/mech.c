#include "gameplay/mech.h"

#include <math.h>

#include "game/creature.h"
#include "game/gametypes.h"
#include "gameplay/gameplay.h"
#include "nu_port.h"
#include "numath/nutrig.h"
#include "numath/nuvec.h"
#include "nups2/ps2ctl.h"

/* Declared by no header. */
extern struct MoveInfo MechMoveInfo;
extern struct gtimer_s GameTimer;
extern struct cursor_s Cursor;
extern struct cammtx_s GameCam[];
extern struct nuvec_s plr_target_firepos;
extern struct obj_s *pObj[64];
extern s32 GAMEOBJECTCOUNT;
extern s32 VEHICLECONTROL;
extern s32 fadeval;
extern f32 vtog_time;
extern f32 vtog_duration;
extern f32 in_speed;
extern f32 in_f_friction;
extern f32 in_s_friction;

#define NW_MECH_VEHICLE 0x44
/* Retail's aim: stick to angle, the limits, easing 1/32 a frame. */
#define NW_MECH_AIM_X 5461.0f
#define NW_MECH_AIM_Y 10923.0f
#define NW_MECH_AIM_X_MAX 0x1555
#define NW_MECH_AIM_Y_MAX 0x2AAB
#define NW_MECH_AIM_RATE (1.0f / 32.0f)
#define NW_MECH_ASSIST_RANGE 20.0f /* the shot's reach */
#define NW_MECH_ASSIST_RATE 0.125f /* a frame at 100 % pull */

static struct {
    bool on;
    u8 x, y; /* the left stick retail is not shown */
} nw_mech_walk;
static bool nw_mech_aimed;      /* plr_target_firepos is this aim's */
static bool nw_mech_firing;     /* MoveMECH starts with a shot to fire */
static u32 nw_mech_shot;        /* GameTimer's frame at the last shot */
static float nw_mech_pad_speed = -1.0f; /* the real pad_speed while stand_jump fakes it, or -1 */

bool nw_mech_driving(const struct creature_s *c)
{
    return c == &Character[0] && VEHICLECONTROL == 1 && c->obj.vehicle == NW_MECH_VEHICLE &&
           vtog_time == vtog_duration;
}

static int nw_mech_frames(int ms)
{
    return ms * NW_HZ(50, 60) / 1000;
}

/* MovePlayer's stick: none at the start, in menus 0x1F and 0x21, in a fade. */
static bool nw_mech_input(const struct creature_s *c)
{
    return GameTimer.frame >= (u32)NW_HZ(0x32, 0x3C) && !c->obj.finished && !c->freeze && Cursor.menu != 0x1F &&
           Cursor.menu != 0x21 && fadeval <= 0;
}

/* Retail's left stick: in a 42.5 circle nothing, otherwise (x, z) and the tilt, 1 at the square's edge. */
static float nw_mech_stick(u8 ax, u8 ay, struct nuvec_s *in)
{
    float x = ax - 127.5f, z = -(ay - 127.5f);
    if (x * x + z * z < 42.5f * 42.5f) {
        x = z = 0.0f;
    }
    *in = (struct nuvec_s){ x, 0.0f, z };
    return fmaxf(fabsf(x), fabsf(z)) / 127.5f;
}

struct nupad_s *nw_mech_pad(const struct creature_s *c, struct nupad_s *pad, struct nupad_s *copy)
{
    nw_mech_walk.on = false;
    if (!nw_profile.movement.mech.aim_walk || !pad || c->target == 0 || !nw_mech_driving(c)) {
        return pad;
    }
    NW_DEVIATION(movement.mech.aim_walk);
    *copy = *pad;
    copy->rdata.l_alg_x = pad->rdata.r_alg_x;
    copy->rdata.l_alg_y = pad->rdata.r_alg_y;
    nw_mech_walk.on = true;
    nw_mech_walk.x = pad->rdata.l_alg_x;
    nw_mech_walk.y = pad->rdata.l_alg_y;
    return copy;
}

static short nw_mech_clamp(int a, int max)
{
    return (short)(a < -max ? -max : a > max ? max : a);
}

/* The nearest shootable creature in the cone round the aim pulls it. */
static void nw_mech_assist(struct creature_s *c, const NwMechMovement *m)
{
    static struct nuvec_s ahead = { 0.0f, 0.0f, 1.0f };
    struct nuvec_s aim, to = { 0 };
    NuVecRotateX(&aim, &ahead, -(u16)c->obj.target_xrot & 0xFFFF);
    NuVecRotateY(&aim, &aim, c->obj.hdg + (u16)c->obj.target_yrot);
    float best = cosf(m->assist_cone * 3.14159265f / 180.0f);
    bool found = false;
    for (int i = 0; i < GAMEOBJECTCOUNT && i < 64; i++) {
        struct obj_s *po = pObj[i];
        if (!po || po == &c->obj || !(po->vulnerable & 0x100) || po->dead) {
            continue;
        }
        struct nuvec_s d = { po->pos.x - plr_target_firepos.x,
                             po->pos.y + (po->bot + po->top) * 0.5f * po->SCALE - plr_target_firepos.y,
                             po->pos.z - plr_target_firepos.z };
        float dist = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
        if (dist < 0.01f || dist > NW_MECH_ASSIST_RANGE) {
            continue;
        }
        float cosine = (d.x * aim.x + d.y * aim.y + d.z * aim.z) / dist;
        if (cosine > best) {
            best = cosine;
            to = d;
            found = true;
        }
    }
    if (!found) {
        return;
    }
    short want_x = nw_mech_clamp((short)NuAtan2D(to.y, sqrtf(to.x * to.x + to.z * to.z)), NW_MECH_AIM_X_MAX);
    short want_y = nw_mech_clamp((short)(NuAtan2D(to.x, to.z) - c->obj.hdg), NW_MECH_AIM_Y_MAX);
    float k = m->assist_pull / 100.0f * NW_MECH_ASSIST_RATE;
    c->obj.target_xrot += (short)((want_x - c->obj.target_xrot) * k);
    c->obj.target_yrot += (short)((want_y - c->obj.target_yrot) * k);
}

void nw_mech_aim(struct creature_s *c)
{
    const NwMechMovement *m = &nw_profile.movement.mech;
    bool aimed = nw_mech_aimed;
    nw_mech_aimed = c->target != 0 && c->fire == 0 && nw_mech_driving(c);
    if (!nw_mech_aimed) {
        return;
    }
    if (m->aim_fast) {
        NW_DEVIATION(movement.mech.aim_fast);
        /* Before retail's own 1/32, toward last frame's stick. */
        float k = (m->aim_speed - 100) / 100.0f * NW_MECH_AIM_RATE;
        bool held = c->obj.pad_speed > 0.0f;
        int ax = held ? (int)(-c->obj.pad_dz * NW_MECH_AIM_X) : c->obj.target_xrot;
        int ay = held ? (int)(c->obj.pad_dx * NW_MECH_AIM_Y) : c->obj.target_yrot;
        c->obj.target_xrot += (short)((ax - c->obj.target_xrot) * k);
        c->obj.target_yrot += (short)((ay - c->obj.target_yrot) * k);
    }
    if (m->aim_assist && aimed) {
        NW_DEVIATION(movement.mech.aim_assist);
        nw_mech_assist(c, m);
    }
}

bool nw_mech_loop(struct obj_s *obj, u16 *hdg, u16 *walk)
{
    struct creature_s *c = &Character[0];
    const NwMechMovement *m = &nw_profile.movement.mech;
    if (obj != &c->obj || !nw_mech_driving(c)) {
        return false;
    }
    *walk = hdg ? *hdg : obj->hdg;
    if (nw_mech_walk.on) {
        struct nuvec_s in;
        in_speed = 0.0f;
        if (nw_mech_input(c) && nw_mech_stick(nw_mech_walk.x, nw_mech_walk.y, &in) >= 0.2f) {
            NuVecRotateY(&in, &in, GameCam[0].yrot);
            *walk = (u16)NuAtan2D(in.x, in.z);
            in_speed = MechMoveInfo.WALKSPEED * m->aim_walk_speed / 100.0f;
        }
    }
    return true;
}

void nw_mech_before(struct creature_s *plr, const struct nupad_s *pad)
{
    const NwMechMovement *m = &nw_profile.movement.mech;
    nw_mech_pad_speed = -1.0f;
    nw_mech_firing = false;
    if (!nw_mech_driving(plr)) {
        return;
    }
    if (m->shot_stop && m->shot_tap && plr->target == 2 && plr->fire == 0 && plr->tap != 0 && !plr->fire_lock) {
        NW_DEVIATION(movement.mech.shot_stop);
        plr->tap = 0;
    }
    nw_mech_firing = plr->target == 2 && plr->fire == 0 && plr->tap == 0 && !plr->fire_lock;
    /* MoveMECH takes the running jump for any stick. */
    bool take_off = (pad->paddata & 0x40) && plr->jump == 0 && (plr->obj.ground || plr->allow_jump) &&
                    plr->target == 0 && !plr->fire && !plr->tap && !plr->obj.transporting && nw_mech_input(plr);
    if (m->stand_jump && take_off && plr->obj.pad_speed == 0.0f) {
        NW_DEVIATION(movement.mech.stand_jump);
        nw_mech_pad_speed = plr->obj.pad_speed;
        plr->obj.pad_speed = MechMoveInfo.WALKSPEED;
    }
}

void nw_mech_after(struct creature_s *plr, const struct nupad_s *pad)
{
    const NwMechMovement *m = &nw_profile.movement.mech;
    if (nw_mech_pad_speed >= 0.0f) {
        plr->obj.pad_speed = nw_mech_pad_speed;
    }
    if (!m->shot_stop || !nw_mech_driving(plr)) {
        return;
    }
    if (nw_mech_firing && plr->fire) {
        nw_mech_shot = GameTimer.frame;
    }
    bool moving = plr->obj.pad_speed > 0.0f || (pad->paddata & 0x40);
    if (plr->target == 0 && (plr->fire || plr->tap) && moving &&
        GameTimer.frame - nw_mech_shot >= (u32)nw_mech_frames(m->shot_stop_ms)) {
        NW_DEVIATION(movement.mech.shot_stop);
        plr->fire = 0;
        plr->tap = 0;
    }
}
