/* Crash in the air: jump_fall, jump_distance, air_momentum, dash_jump, air_jump. */
#ifndef NUWORLD_GAMEPLAY_AIR_H
#define NUWORLD_GAMEPLAY_AIR_H

struct creature_s;
struct nupad_s;

void nw_air_apply(void);
/* MoveLoopXZ, before retail; forward is his speed along the stick. */
void nw_air_loop(const struct creature_s *plr, float forward);
/* MoveCRASH, around retail. */
void nw_air_before(struct creature_s *plr, const struct nupad_s *pad);
void nw_air_move(struct creature_s *plr, const struct nupad_s *pad);

#endif
