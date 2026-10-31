#include "gameplay/submarine.h"

#include <math.h>
#include <stdlib.h>

#include "game/creature.h"
#include "game/gametypes.h"
#include "gameplay/gameplay.h"
#include "nu_port.h"
#include "numath/nuvec.h"

/* Declared by no header. */
extern struct MoveInfo SubmarineMoveInfo;
extern struct gtimer_s GameTimer;
extern struct nuvec_s v001;
extern s32 VEHICLECONTROL;
extern s32 vtog_blend;

/* MovePlayer's share of the stick across that moves the sub sideways. */
#define NW_SUB_SIDE 0.33333334f
/* MoveSUBMARINE's reloads: a torpedo every half second, a bomb every second. */
#define NW_SUB_TORPEDO_FRAMES NW_HZ(0x19, 0x1E)
#define NW_SUB_BOMB_FRAMES NW_HZ(0x32, 0x3C)

static float nw_sub_top = 1.0f;  /* what WALKSPEED and RUNSPEED hold */
static float nw_sub_sink = 1.0f; /* what SPRINTSPEED, the bombs' sink speed (terminal_fall_speed), holds */
static u16 nw_sub_hdg, nw_sub_target, nw_sub_xrot;
static u32 nw_sub_frame;
static bool nw_sub_firing;
static int nw_sub_fire, nw_sub_tap;
static int nw_sub_depth;
static struct {
    bool on;
    float k, radius, bot, top, min, max;
} nw_sub_saved;

static bool nw_sub_driving(const struct obj_s *obj)
{
    return obj == &Character[0].obj && VEHICLECONTROL == 1 && obj->vehicle == 0x20;
}

/* RotDiff's. */
static int nw_sub_diff(u16 a, u16 b)
{
    int d = b - a;
    return d > 0x8000 ? d - 0x10000 : d < -0x8000 ? d + 0x10000 : d;
}

void nw_sub_apply(void)
{
    const NwSubMovement *s = &nw_profile.movement.submarine;
    float top = 1.0f, walk = NW_HZ(0.024999999f, 0.020833334f), run = NW_HZ(0.06f, 0.050000004f);
    if (s->speed) {
        NW_DEVIATION(movement.submarine.speed);
        top = s->speed_top / 100.0f;
    }
    if (SubmarineMoveInfo.WALKSPEED == walk * nw_sub_top) {
        SubmarineMoveInfo.WALKSPEED = walk * top;
    }
    if (SubmarineMoveInfo.RUNSPEED == run * nw_sub_top) {
        SubmarineMoveInfo.RUNSPEED = run * top;
    }
    nw_sub_top = top;
    float sink = 1.0f, fall = NW_HZ(0.08f, 0.06666667f);
    if (s->weapons) {
        NW_DEVIATION(movement.submarine.weapons);
        sink = s->bomb_speed / 100.0f;
    }
    if (SubmarineMoveInfo.SPRINTSPEED == fall * nw_sub_sink) {
        SubmarineMoveInfo.SPRINTSPEED = fall * sink;
    }
    nw_sub_sink = sink;
}

void nw_sub_move(const struct creature_s *c)
{
    if (c == &Character[0]) {
        nw_sub_hdg = c->obj.hdg;
    }
}

/* turning: retail only faces 0 (+z), 0x8000 (-z) or the screen (0x4000), takes a new one only once a turn is done,
   and from the screen only with the stick near level. This keeps MovePlayer's heading from before its turn. */
void nw_sub_turn(struct creature_s *plr)
{
    const NwSubMovement *s = &nw_profile.movement.submarine;
    if (!s->turning || !nw_sub_driving(&plr->obj) || vtog_blend || plr->target) {
        return;
    }
    NW_DEVIATION(movement.submarine.turning);
    if (GameTimer.frame - nw_sub_frame > 1) {
        nw_sub_target = nw_sub_hdg;
    }
    nw_sub_frame = GameTimer.frame;
    if (plr->obj.pad_speed > 0.0f && fabsf(plr->obj.pad_dx) >= NW_SUB_SIDE) {
        nw_sub_target = plr->obj.pad_dx > 0.0f ? 0 : 0x8000;
    }
    int h = nw_sub_hdg, t = nw_sub_target, step = NW_HZ(0x369, 0x2D8) * s->turn_speed / 100;
    h = h < t ? (t - h < step ? t : h + step) : (h - t < step ? t : h - step);
    plr->obj.hdg = (u16)h;
    plr->obj.thdg = (u16)t;
}

void nw_sub_fire_before(const struct creature_s *plr)
{
    nw_sub_firing = nw_profile.movement.submarine.weapons && nw_sub_driving(&plr->obj);
    nw_sub_fire = plr->fire;
    nw_sub_tap = plr->tap;
}

static u8 nw_sub_reload(int frames, int rate)
{
    int n = (frames * 100 + rate / 2) / rate;
    return (u8)(n < 1 ? 1 : n);
}

/* weapons: the reload MoveSUBMARINE just started, shortened. */
void nw_sub_fire_after(struct creature_s *plr)
{
    const NwSubMovement *s = &nw_profile.movement.submarine;
    if (!nw_sub_firing) {
        return;
    }
    nw_sub_firing = false;
    NW_DEVIATION(movement.submarine.weapons);
    if (nw_sub_fire == 0 && plr->fire == NW_SUB_TORPEDO_FRAMES) {
        plr->fire = nw_sub_reload(NW_SUB_TORPEDO_FRAMES, s->torpedo_rate);
    }
    if (nw_sub_tap == 0 && plr->tap == NW_SUB_BOMB_FRAMES) {
        plr->tap = nw_sub_reload(NW_SUB_BOMB_FRAMES, s->bomb_rate);
    }
}

/* The torpedo (type 4) flies at its mom; the bomb (10) drops at the sub's speed and 0.02 a frame more, then eases to
   terminal_fall_speed, which nw_sub_apply scales. */
struct nuvec_s *nw_sub_launch(int type, struct nuvec_s *mom, const struct obj_s *owner, struct nuvec_s *out)
{
    const NwSubMovement *s = &nw_profile.movement.submarine;
    if (!nw_sub_firing || !mom || owner != &Character[0].obj || (type != 4 && type != 10)) {
        return mom;
    }
    NW_DEVIATION(movement.submarine.weapons);
    *out = *mom;
    if (type == 4) {
        NuVecScale(out, out, s->torpedo_speed / 100.0f);
    } else {
        out->y = owner->mom.y + (mom->y - owner->mom.y) * s->bomb_speed / 100.0f;
    }
    return out;
}

void nw_sub_pitch_before(const struct obj_s *obj)
{
    nw_sub_xrot = obj->xrot;
}

/* ObjectRotation's sub pitch seeks a 32nd of the way a frame; this as fast in seconds at both rates. */
void nw_sub_pitch(struct obj_s *obj, int mode)
{
    const NwSubMovement *s = &nw_profile.movement.submarine;
    if (!s->turning || mode != 2 || (obj->flags & 0x10000) || !nw_sub_driving(obj)) {
        return;
    }
    NW_DEVIATION(movement.submarine.turning);
    int pitch = obj->pad_speed > 0.0f ? 0x4000 - abs(nw_sub_diff(0, obj->pad_angle)) : 0;
    int d = nw_sub_diff(nw_sub_xrot, (u16)pitch);
    float share = 1.0f - powf(31.0f / 32.0f, s->pitch_speed / 100.0f * NW_HZ(1.0f, 50.0f / 60.0f));
    int step = (int)lroundf(d * share);
    obj->xrot = (u16)(nw_sub_xrot + (step == 0 && d != 0 ? (d > 0 ? 1 : -1) : step));
}

static float nw_sub_step(float v, float want, float base, const NwSubMovement *s)
{
    bool faster = fabsf(want) > fabsf(v) && want * v >= 0.0f;
    float extra = base * ((faster ? s->speed_accel : s->speed_brake) / 100.0f - 1.0f);
    return v < want ? fminf(v + extra, want) : fmaxf(v - extra, want);
}

/* speed: MovePlayer steps the sub's speed toward the stick's by a fixed amount a frame; this adds to the step. */
void nw_sub_accel(struct nuvec_s *vvel)
{
    const NwSubMovement *s = &nw_profile.movement.submarine;
    const struct obj_s *obj = &Character[0].obj;
    if (!s->speed || vvel != &obj->mom || !nw_sub_driving(obj) || vtog_blend || obj->dead) {
        return;
    }
    NW_DEVIATION(movement.submarine.speed);
    struct nuvec_s want;
    NuVecRotateX(&want, &v001, -obj->xrot);
    if (obj->pad_speed > 0.0f) {
        want.z = fabsf(obj->pad_dx) < NW_SUB_SIDE ? 0.0f : want.z * obj->pad_speed;
        want.z = obj->pad_angle > 0x8000 ? -want.z : want.z;
        want.y *= obj->pad_speed;
    } else {
        want.y = want.z = 0.0f;
    }
    float base = NW_HZ(0.00144f, 0.001f);
    vvel->z = nw_sub_step(vvel->z, want.z, base, s);
    vvel->y = nw_sub_step(vvel->y, want.y, base, s);
}

static void nw_sub_scale(float *lo, float *hi, float k)
{
    float mid = (*lo + *hi) * 0.5f;
    *lo = mid + (*lo - mid) * k;
    *hi = mid + (*hi - mid) * k;
}

/* hitbox: the sub against what kills it, round its middle; walls, crates and fruit keep retail's size. MoveCreature
   and UpdateProjectiles take its height afresh (GetTopBot from CData, NewTopBot), so its CData entry shrinks too. */
void nw_sub_shrink(void)
{
    struct obj_s *obj = &Character[0].obj;
    struct CharacterData *d = &CData[0x20];
    if (nw_sub_depth++ > 0 || !nw_profile.movement.submarine.hitbox || !nw_sub_driving(obj)) {
        return;
    }
    NW_DEVIATION(movement.submarine.hitbox);
    float k = nw_profile.movement.submarine.hitbox_size / 100.0f;
    nw_sub_saved.on = true;
    nw_sub_saved.k = k;
    nw_sub_saved.radius = obj->RADIUS;
    nw_sub_saved.bot = obj->bot;
    nw_sub_saved.top = obj->top;
    nw_sub_saved.min = d->min.y;
    nw_sub_saved.max = d->max.y;
    obj->RADIUS *= k;
    nw_sub_scale(&obj->bot, &obj->top, k);
    nw_sub_scale(&obj->objbot, &obj->objtop, k);
    nw_sub_scale(&d->min.y, &d->max.y, k);
}

/* objbot and objtop grow back round their middle, which keeps a height retail took afresh in between. */
void nw_sub_restore(void)
{
    struct obj_s *obj = &Character[0].obj;
    struct CharacterData *d = &CData[0x20];
    if (--nw_sub_depth > 0 || !nw_sub_saved.on) {
        return;
    }
    obj->RADIUS = nw_sub_saved.radius;
    obj->bot = nw_sub_saved.bot;
    obj->top = nw_sub_saved.top;
    nw_sub_scale(&obj->objbot, &obj->objtop, 1.0f / nw_sub_saved.k);
    d->min.y = nw_sub_saved.min;
    d->max.y = nw_sub_saved.max;
    nw_sub_saved.on = false;
}

float nw_sub_particle(float radius)
{
    if (!nw_profile.movement.submarine.hitbox || !nw_sub_driving(&Character[0].obj)) {
        return radius;
    }
    NW_DEVIATION(movement.submarine.hitbox);
    return radius * nw_profile.movement.submarine.hitbox_size / 100.0f;
}
