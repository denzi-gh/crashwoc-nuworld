#include "gameplay/atlasphere.h"

#include <math.h>
#include <stdbool.h>

#include "game/gametypes.h"
#include "gamelib/terrain.h"
#include "gameplay/gameplay.h"

/* Declared by no header. */
extern struct NEWBUGGY PlayerAtlas;
extern s32 Level;

/* The stick's full push: gravity 10 tilted 53 degrees, 89 in the boss arena. */
#define NW_ATLAS_ARENA 0x15
#define NW_ATLAS_PUSH 7.9863551f
#define NW_ATLAS_ARENA_PUSH 9.9984770f
/* The arena's invisible wall (ProcessAtlas). */
#define NW_ATLAS_ARENA_WALL 15.3f
/* NewShadowMask with no floor. */
#define NW_ATLAS_NO_FLOOR 2000000.0f
/* Kill floor (CheckAtlasGround). */
#define NW_ATLAS_KILL_FLOOR 3
/* Deeper than this below the slope counts as a pit. */
#define NW_ATLAS_PIT_DROP 1.0f
/* Gap the guard leaves at the edge, and how fast it backs off there. */
#define NW_ATLAS_RIM 0.1f
#define NW_ATLAS_BACK 0.05f
#define NW_ATLAS_COS45 0.70710678f

/* A pit at (dx, dz) from the ball: no floor, a deep drop, or a kill floor.  */
static bool nw_atlas_pit(const struct NEWBUGGY *a, float dx, float dz)
{
    struct nuvec_s p = { a->ball_pos.x + dx, a->ball_pos.y, a->ball_pos.z + dz };
    if (Level == NW_ATLAS_ARENA && p.x * p.x + p.z * p.z > NW_ATLAS_ARENA_WALL * NW_ATLAS_ARENA_WALL) {
        return false;
    }
    float y = NewShadowMask(&p, a->ball_radius, -1);
    if (y >= NW_ATLAS_NO_FLOOR) {
        return true;
    }
    const struct nuvec_s *n = &a->lastnormal;
    float ground = a->ball_pos.y - a->ball_radius;
    if (n->y >= 0.5f) {
        ground -= (n->x * dx + n->z * dz) / n->y;
    }
    return ground - y > NW_ATLAS_PIT_DROP || ShadowInfo() == NW_ATLAS_KILL_FLOOR;
}

/* edge_guard: brakes toward a pit ahead unless the stick pushes into it. Braking by speed / look-ahead makes the
   ball ease to a stop just short of the edge. */
static void nw_atlas_guard(const struct NEWBUGGY *a, const NwAtlasMovement *m, float push, float dt, float *ex,
                           float *ez)
{
    float look = m->guard_reach / 1000.0f;
    float wx = a->ball_vel.x + *ex * dt, wz = a->ball_vel.z + *ez * dt, w = sqrtf(wx * wx + wz * wz);
    if (w <= 0.0f || w > m->guard_speed) {
        return;
    }
    float hx = wx / w, hz = wz / w;
    float fx = a->ball_force.x, fz = a->ball_force.z, f = sqrtf(fx * fx + fz * fz);
    if (m->guard_push && a->inputmag >= 0.5f && f > 0.0f && fx * hx + fz * hz >= NW_ATLAS_COS45 * f) {
        return;
    }
    float reach = a->ball_radius + NW_ATLAS_RIM + w * look;
    if (!nw_atlas_pit(a, hx * reach, hz * reach)) {
        return;
    }
    NW_DEVIATION(movement.atlasphere.edge_guard);
    float slow = w * look <= NW_ATLAS_RIM ? (w + NW_ATLAS_BACK) / dt : w / look;
    slow = fminf(slow, push * m->guard_brake / 100.0f);
    *ex -= hx * slow;
    *ez -= hz * slow;
}

void nw_atlas_move(struct NEWBUGGY *a, float dt)
{
    const NwAtlasMovement *m = &nw_profile.movement.atlasphere;
    /* On the ground in the last two steps (onground is already shifted). */
    if ((!m->steer && !m->brake && !m->edge_guard) || a != &PlayerAtlas || a->type != 1 || !(a->onground & 6) ||
        dt <= 0.0f) {
        return;
    }
    float push = Level == NW_ATLAS_ARENA ? NW_ATLAS_ARENA_PUSH : NW_ATLAS_PUSH;
    float vx = a->ball_vel.x, vz = a->ball_vel.z, speed = sqrtf(vx * vx + vz * vz);
    float ex = 0.0f, ez = 0.0f; /* extra force */
    float ux = speed > 0.0f ? vx / speed : 0.0f, uz = speed > 0.0f ? vz / speed : 0.0f;
    float along = a->ball_force.x * ux + a->ball_force.z * uz; /* push along the roll */
    /* steer: more push across the roll, speed kept. */
    if (m->steer && speed > 0.0f) {
        float share = (m->steer_turn - 100) / 100.0f;
        float sx = (a->ball_force.x - ux * along) * share, sz = (a->ball_force.z - uz * along) * share;
        if (sx != 0.0f || sz != 0.0f) {
            NW_DEVIATION(movement.atlasphere.steer);
            float tx = vx + sx * dt, tz = vz + sz * dt, t = sqrtf(tx * tx + tz * tz);
            ex += (tx * speed / t - vx) / dt;
            ez += (tz * speed / t - vz) / dt;
        }
    }
    /* brake: more push against the roll, plus let-go braking; never reverses. */
    if (m->brake && speed > 0.0f) {
        float slow = along < 0.0f ? -along * (m->brake_force - 100) / 100.0f : 0.0f;
        if (a->inputmag == 0.0f) {
            slow += push * m->coast_brake / 100.0f;
        }
        if (slow > 0.0f) {
            NW_DEVIATION(movement.atlasphere.brake);
            slow = fminf(slow, speed / dt);
            ex -= ux * slow;
            ez -= uz * slow;
        }
    }
    if (m->edge_guard) {
        nw_atlas_guard(a, m, push, dt, &ex, &ez);
    }
    /* Applied like retail's force. */
    if (ex != 0.0f || ez != 0.0f) {
        a->ball_vel.x += ex * dt;
        a->ball_vel.z += ez * dt;
        a->ball_target.x += 0.5f * dt * dt * ex;
        a->ball_target.z += 0.5f * dt * dt * ez;
    }
}
