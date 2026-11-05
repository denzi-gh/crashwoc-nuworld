/* The space fighter in Crashteroids: steering, roll, aim, aim_assist. */
#ifndef NUWORLD_GAMEPLAY_SPACEFIGHTER_H
#define NUWORLD_GAMEPLAY_SPACEFIGHTER_H

#include <stdbool.h>
#include <stdint.h>

struct GLIDERSTRUCT;
struct nupad_s;
struct nuvec_s;

/* The glider wraps: true when the space fighter took the call, which then ran retail's itself. */
bool nw_space_control(struct GLIDERSTRUCT *glider, struct nupad_s *pad);
bool nw_space_movement(struct GLIDERSTRUCT *glider, float dt);
bool nw_space_fire(struct GLIDERSTRUCT *glider, struct nupad_s *pad);
bool nw_space_collide(struct nuvec_s *pos, float radius, int32_t enemy, float yscale, int32_t debris, int32_t reflect,
                      int32_t *hit);

#endif
