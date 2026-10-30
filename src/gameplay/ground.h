/* Crash on the ground: run_speed, grip, long_spin. */
#ifndef NUWORLD_GAMEPLAY_GROUND_H
#define NUWORLD_GAMEPLAY_GROUND_H

struct creature_s;

void nw_ground_apply(void);
/* MoveLoopXZ, before retail; forward is his speed along the stick. */
void nw_ground_loop(const struct creature_s *plr, float forward);
/* AnimateCRASH, after retail. */
void nw_ground_animate(struct creature_s *plr);

#endif
