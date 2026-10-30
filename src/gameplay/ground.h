/* [game.movement.crash] run_speed, grip: Crash on the ground, part of N. Hanced Movement. */
#ifndef NUWORLD_GAMEPLAY_GROUND_H
#define NUWORLD_GAMEPLAY_GROUND_H

struct creature_s;

/* nw_profile's speeds: at boot after the data image and the 60 Hz table, then between frames. */
void nw_ground_apply(void);
/* MoveLoopXZ for Crash on foot, before retail: the frictions (the caller restores them); forward is his speed along
   the stick. */
void nw_ground_loop(const struct creature_s *plr, float forward);
/* AnimateCRASH, after retail: the run animations' rate. */
void nw_ground_animate(struct creature_s *plr);

#endif
