/* Crash on the ground: run_speed, grip, long_spin, slide. */
#ifndef NUWORLD_GAMEPLAY_GROUND_H
#define NUWORLD_GAMEPLAY_GROUND_H

struct creature_s;
struct nupad_s;

void nw_ground_apply(void);
/* MoveLoopXZ, before retail; forward is his speed along the stick. */
void nw_ground_loop(const struct creature_s *plr, float forward);
/* MoveCRASH, around retail. */
void nw_ground_before(struct creature_s *plr, const struct nupad_s *pad);
void nw_ground_after(struct creature_s *plr);
/* AnimateCRASH, after retail. */
void nw_ground_animate(struct creature_s *plr);

#endif
