/* [game.movement.crash] jump_fall: Crash in the air, part of N. Hanced Movement. */
#ifndef NUWORLD_GAMEPLAY_AIR_H
#define NUWORLD_GAMEPLAY_AIR_H

struct creature_s;

/* nw_profile's jump data: at boot after the data image and the 60 Hz table, then between frames. */
void nw_air_apply(void);
/* MoveLoopXZ for Crash on foot, before retail: in_speed and the frictions (the caller restores them). */
void nw_air_loop(void);
/* MoveCRASH, after retail. */
void nw_air_move(struct creature_s *plr);

#endif
