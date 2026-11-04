/* The mech: stand_jump, shot_stop, aim_walk, aim_fast, aim_assist. */
#ifndef NUWORLD_GAMEPLAY_MECH_H
#define NUWORLD_GAMEPLAY_MECH_H

#include <stdbool.h>

#include "nu_types.h"

struct creature_s;
struct nupad_s;
struct obj_s;

/* Crash in the mech, not climbing in or out. */
bool nw_mech_driving(const struct creature_s *c);
/* MovePlayer, before retail: the pad retail reads (copy, left stick = right stick, while walking and aiming)
   and the aim it eases. */
struct nupad_s *nw_mech_pad(const struct creature_s *c, struct nupad_s *pad, struct nupad_s *copy);
void nw_mech_aim(struct creature_s *c);
/* MoveLoopXZ, before retail: true for the mech, which retail then moves along walk at in_speed. */
bool nw_mech_loop(struct obj_s *obj, u16 *hdg, u16 *walk);
/* MoveMECH, around retail. */
void nw_mech_before(struct creature_s *plr, const struct nupad_s *pad);
void nw_mech_after(struct creature_s *plr, const struct nupad_s *pad);

#endif
