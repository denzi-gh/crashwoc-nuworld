#include "gameplay/jeep.h"

#include <math.h>
#include <stddef.h>

#include "game/creature.h"
#include "game/gametypes.h"
#include "gameplay/gameplay.h"
#include "nu_port.h"

/* Declared by no header. */
extern s32 VEHICLECONTROL;
extern s32 Level;
extern s32 vtog_blend;
extern RailPosition *best_cRPos;
extern f32 SMOKEYSPEED;
extern f32 SMOKEYBOOSTSPEED;
extern f32 OFFROADERSEEK;
extern f32 OFFROADERSPEEDTIME;
extern struct MoveInfo OffRoaderMoveInfo;

#define NW_JEEP_OFFROADER 0x99
#define NW_JEEP_SMOKEY 3

/* The scales last written. */
static float nw_jeep_accel = 1.0f;
static float nw_jeep_time = 1.0f;
static float nw_jeep_top = 1.0f;
static float nw_jeep_grip = 1.0f;

void nw_jeep_apply(void)
{
    const NwJeepMovement *j = &nw_profile.movement.jeep;
    float top = 1.0f, accel = 1.0f, grip = 1.0f;
    if (j->speed) {
        NW_DEVIATION(movement.jeep.speed);
        top = j->speed_top / 100.0f;
        accel = j->speed_accel / 100.0f;
    }
    if (j->grip) {
        NW_DEVIATION(movement.jeep.grip);
        grip = j->grip_speed / 100.0f;
    }
    /* Smokey's run-up */
    float time = top / accel, base_run = NW_HZ(0.12f, 0.10000001f), base_sprint = NW_HZ(0.17999999f, 0.15f);
    if (SMOKEYSPEED == 8.0f * nw_jeep_accel) {
        SMOKEYSPEED = 8.0f * accel;
    }
    if (SMOKEYBOOSTSPEED == 12.0f * nw_jeep_accel) {
        SMOKEYBOOSTSPEED = 12.0f * accel;
    }
    if (OFFROADERSPEEDTIME == 2.0f * nw_jeep_time) {
        OFFROADERSPEEDTIME = 2.0f * time;
    }
    /* Jungle Rumble */
    if (OffRoaderMoveInfo.RUNSPEED == base_run * nw_jeep_top) {
        OffRoaderMoveInfo.RUNSPEED = base_run * top;
    }
    if (OffRoaderMoveInfo.SPRINTSPEED == base_sprint * nw_jeep_top) {
        OffRoaderMoveInfo.SPRINTSPEED = base_sprint * top;
    }
    /* Smokey's grip at speed */
    if (OFFROADERSEEK == 0.02f * nw_jeep_grip) {
        OFFROADERSEEK = 0.02f * grip;
    }
    nw_jeep_accel = accel;
    nw_jeep_time = time;
    nw_jeep_top = top;
    nw_jeep_grip = grip;
}

/* MovePlayer's hdg = SeekRot(hdg, thdg, 5 in Smokey or 3). */
static bool nw_jeep_steering(const struct creature_s *c, u16 a, u16 b, s32 shift)
{
    return nw_profile.movement.jeep.steering && VEHICLECONTROL == 1 && c->obj.vehicle == NW_JEEP_OFFROADER &&
           best_cRPos != NULL && vtog_blend == 0 && a == c->obj.hdg && b == c->obj.thdg &&
           (shift == 3 || (shift == 5 && Level == NW_JEEP_SMOKEY));
}

bool nw_jeep_steer(uint16_t a, uint16_t b, int32_t shift, uint16_t *out)
{
    struct creature_s *c = &Character[0];
    if (!nw_jeep_steering(c, a, b, shift)) {
        return false;
    }
    NW_DEVIATION(movement.jeep.steering);
    const NwJeepMovement *j = &nw_profile.movement.jeep;
    u16 rail = best_cRPos->angle;
    s32 off = (s16)(u16)(b - rail);
    off = (s32)floorf(off * (j->steer_angle / 100.0f));
    if (off > 0x3FFF) {
        off = 0x3FFF;
    } else if (off < -0x3FFF) {
        off = -0x3FFF;
    }
    u16 target = (u16)(rail + off);
    c->obj.thdg = target;
    /* the same per second at 50 and 60 Hz */
    s32 diff = target - a;
    if (0x8000 < diff) {
        diff -= 0x10000;
    } else if (diff < -0x8000) {
        diff += 0x10000;
    }
    float frames = j->steer_response / 100.0f * NW_HZ(1.0f, 50.0f / 60.0f);
    float share = 1.0f - powf(1.0f - ldexpf(1.0f, -shift), frames);
    *out = (u16)(a + (s32)floorf(diff * share));
    return true;
}
