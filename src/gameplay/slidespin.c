#include "gameplay/slidespin.h"

#include <math.h>
#include <stddef.h>

#include "game/creature.h"
#include "game/gametypes.h"
#include "gameplay/crash.h"
#include "gameplay/gameplay.h"
#include "nu_port.h"
#include "numath/nutrig.h"
#include "nups2/ps2ctl.h"

extern struct MoveInfo CrashMoveInfo;
extern struct gtimer_s GameTimer;
extern s32 plr_rebound;
extern f32 in_speed;
extern f32 in_f_friction;

static u32 nw_ss_frame;
static int nw_ss_late = -1;
static float nw_ss_slide_v;
static bool nw_ss_wanted, nw_ss_faked;
static short nw_ss_old_slide;
static signed char nw_ss_old_crawl;
static bool nw_ss_on;
static float nw_ss_hold, nw_ss_jump;
static u16 nw_ss_heading;
static int nw_ss_ticks;
static float nw_ss_ramp, nw_ss_bank;
static bool nw_ss_tilted;
static u16 nw_ss_xrot, nw_ss_zrot;

static int nw_ss_frames(int ms)
{
    return (ms * NW_HZ(50, 60) + 500) / 1000;
}

static bool nw_ss_continued(void)
{
    return GameTimer.frame - nw_ss_frame <= 1;
}

static bool nw_ss_attack(const struct creature_s *plr)
{
    return plr->spin != 0 && plr->spin_frame < plr->spin_frames - CrashMoveInfo.SPINRESETFRAMES;
}

static void nw_ss_reset(void)
{
    nw_ss_late = -1;
    nw_ss_on = false;
    nw_ss_hold = nw_ss_jump = nw_ss_ramp = nw_ss_bank = 0.0f;
}

void nw_slide_spin_before(struct creature_s *plr, const struct nupad_s *pad)
{
    const NwCrashMovement *c = &nw_profile.movement.crash;
    nw_ss_wanted = nw_ss_faked = false;
    if (!c->slide_spin || !nw_crash_on_foot(plr) || !nw_ss_continued()) {
        nw_ss_reset();
    }
    if (!c->slide_spin || !nw_crash_on_foot(plr)) {
        return;
    }
    if (plr->slide) {
        nw_ss_slide_v = sqrtf(plr->obj.mom.x * plr->obj.mom.x + plr->obj.mom.z * plr->obj.mom.z);
        nw_ss_late = 0;
    } else if (nw_ss_late >= 0 && ++nw_ss_late > nw_ss_frames(c->slide_spin_late_ms)) {
        nw_ss_late = -1;
    }
    if (nw_ss_late < 0 || !(pad->paddata & 0x80) || plr->spin != 0 || ((pad->paddata & 0x40) && plr->jump == 0) ||
        plr->obj.wade || plr->target || plr->freeze || plr_rebound || (plr->slam != 0 && plr->slam < 3)) {
        return;
    }
    NW_DEVIATION(movement.crash.slide_spin);
    nw_ss_wanted = true;
    nw_ss_old_slide = plr->slide;
    nw_ss_old_crawl = plr->crawl;
    nw_ss_faked = plr->slide != 0 || plr->crawl != 0;
    plr->slide = 0;
    plr->crawl = 0;
}

static float nw_ss_lean_at(const struct creature_s *plr)
{
    float ramp = NW_HZ(4.0f, 5.0f);
    float in = (nw_ss_ticks + 1) / ramp;
    float out = (plr->spin_frames - CrashMoveInfo.SPINRESETFRAMES - plr->spin_frame - 1) / ramp;
    float t = in < out ? in : out;
    return t <= 0.0f ? 0.0f : t >= 1.0f ? 1.0f : t;
}

void nw_slide_spin_after(struct creature_s *plr)
{
    const NwCrashMovement *c = &nw_profile.movement.crash;
    bool started = nw_ss_wanted && plr->spin == 1 && plr->spin_frame == 0;
    if (nw_ss_wanted && !started && nw_ss_faked) {
        plr->slide = nw_ss_old_slide;
        plr->crawl = nw_ss_old_crawl;
    }
    nw_ss_frame = GameTimer.frame;
    if (!c->slide_spin) {
        nw_ss_reset();
        return;
    }
    if (started) {
        float speed = sqrtf(plr->obj.mom.x * plr->obj.mom.x + plr->obj.mom.z * plr->obj.mom.z);
        nw_ss_on = true;
        nw_ss_hold = nw_ss_slide_v * c->slide_spin_kept / 100.0f;
        nw_ss_heading = speed > 0.001f ? (u16)NuAtan2D(plr->obj.mom.x, plr->obj.mom.z) : plr->obj.hdg;
        nw_ss_ticks = 0;
        nw_ss_bank = 0.0f;
        nw_ss_late = -1;
        nw_ss_jump = 0.0f;
    } else if (nw_ss_on) {
        nw_ss_ticks++;
    }
    bool took_off = plr->jump == 1 && plr->jump_frame == 0;
    if (nw_ss_hold > 0.0f && took_off) {
        float run = CrashMoveInfo.RUNSPEED;
        nw_ss_jump = nw_ss_hold > run ? run + (nw_ss_hold - run) * c->slide_spin_jump / 100.0f : 0.0f;
        nw_ss_hold = 0.0f;
    }
    bool off = plr->obj.dead || plr->freeze || plr->obj.dangle || plr->obj.transporting || !nw_crash_on_foot(plr);
    if (nw_ss_on && (off || plr->slide || !nw_ss_attack(plr))) {
        nw_ss_on = false;
        nw_ss_hold = 0.0f;
    }
    if (nw_ss_jump > 0.0f && !took_off &&
        (off || plr->obj.ground || plr->jump == 0 || (plr->slam != 0 && plr->slam < 3))) {
        nw_ss_jump = 0.0f;
    }
    nw_ss_ramp = nw_ss_on ? nw_ss_lean_at(plr) : 0.0f;
}

void nw_slide_spin_loop(const struct creature_s *plr, u16 *hdg)
{
    const NwCrashMovement *c = &nw_profile.movement.crash;
    if (!c->slide_spin || !nw_ss_continued()) {
        return;
    }
    float bank = 0.0f;
    if (nw_ss_hold > 0.0f) {
        NW_DEVIATION(movement.crash.slide_spin);
        if (hdg) {
            short off = plr->obj.pad_speed > 0.0f ? (short)(u16)(*hdg - nw_ss_heading) : 0;
            float share = 1.0f - powf(0.75f, c->slide_spin_steer / 100.0f * NW_HZ(1.0f, 50.0f / 60.0f));
            nw_ss_heading = (u16)(nw_ss_heading + (int)(off * share));
            *hdg = nw_ss_heading;
            bank = c->slide_spin_steer > 0 ? sinf(off * (6.2831855f / 65536.0f)) : 0.0f;
        }
        float a = (hdg ? *hdg : plr->obj.hdg) * (6.2831855f / 65536.0f);
        float forward = plr->obj.mom.x * sinf(a) + plr->obj.mom.z * cosf(a);
        in_speed = in_speed < nw_ss_hold ? nw_ss_hold : in_speed;
        float accel = NW_HZ(0.0216f, 0.015000001f);
        if (forward < nw_ss_hold && in_f_friction < accel) {
            in_f_friction = accel;
        }
    } else if (nw_ss_jump > 0.0f && !plr->obj.ground && plr_rebound == 0 && plr->obj.pad_speed > 0.0f &&
               in_speed > 0.0f) {
        NW_DEVIATION(movement.crash.slide_spin);
        in_speed = in_speed < nw_ss_jump ? nw_ss_jump : in_speed;
    }
    nw_ss_bank += (bank - nw_ss_bank) * (1.0f - powf(0.65f, NW_HZ(1.0f, 50.0f / 60.0f)));
}

static void nw_ss_flat(float v[3], const float up[3], const float *other)
{
    float d = v[0] * up[0] + v[1] * up[1] + v[2] * up[2];
    float e = other ? v[0] * other[0] + v[1] * other[1] + v[2] * other[2] : 0.0f;
    for (int i = 0; i < 3; i++) {
        v[i] -= up[i] * d + (other ? other[i] * e : 0.0f);
    }
    float n = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    for (int i = 0; i < 3; i++) {
        v[i] = n > 0.001f ? v[i] / n : 0.0f;
    }
}

void nw_slide_spin_lean(struct creature_s *c, s32 count)
{
    const NwCrashMovement *m = &nw_profile.movement.crash;
    struct creature_s *plr = &Character[0];
    nw_ss_tilted = false;
    if (!m->slide_spin || nw_ss_ramp <= 0.0f || plr < c || plr >= c + count || plr->obj.dead ||
        !nw_ss_attack(plr)) {
        return;
    }
    NW_DEVIATION(movement.crash.slide_spin);
    const float rad = 6.2831855f / 65536.0f, deg = 3.1415927f / 180.0f;
    float x = plr->obj.xrot * rad, z = plr->obj.zrot * rad, h = nw_ss_heading * rad;
    float up[3] = { -sinf(z), cosf(z) * cosf(x), cosf(z) * sinf(x) };
    float fw[3] = { sinf(h), 0.0f, cosf(h) };
    float side[3] = { cosf(h), 0.0f, -sinf(h) };
    nw_ss_flat(fw, up, NULL);
    nw_ss_flat(side, up, fw);
    float tf = nw_ss_ramp * m->slide_spin_lean * deg, ts = nw_ss_ramp * nw_ss_bank * m->slide_spin_bank * deg;
    float u[3];
    for (int i = 0; i < 3; i++) {
        u[i] = (up[i] * cosf(tf) + fw[i] * sinf(tf)) * cosf(ts) + side[i] * sinf(ts);
    }
    float ux = u[0] < -1.0f ? -1.0f : u[0] > 1.0f ? 1.0f : u[0];
    nw_ss_xrot = plr->obj.xrot;
    nw_ss_zrot = plr->obj.zrot;
    nw_ss_tilted = true;
    plr->obj.xrot = (u16)(int)lroundf(atan2f(u[2], u[1]) / rad);
    plr->obj.zrot = (u16)(int)lroundf(atan2f(-ux, sqrtf(1.0f - ux * ux)) / rad);
}

void nw_slide_spin_unlean(void)
{
    if (nw_ss_tilted) {
        Character[0].obj.xrot = nw_ss_xrot;
        Character[0].obj.zrot = nw_ss_zrot;
        nw_ss_tilted = false;
    }
}
