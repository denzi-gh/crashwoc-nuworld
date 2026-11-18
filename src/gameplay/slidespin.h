#ifndef NUWORLD_GAMEPLAY_SLIDESPIN_H
#define NUWORLD_GAMEPLAY_SLIDESPIN_H

#include "game/gametypes.h"

struct creature_s;
struct nupad_s;

void nw_slide_spin_before(struct creature_s *plr, const struct nupad_s *pad);
void nw_slide_spin_after(struct creature_s *plr);
void nw_slide_spin_loop(const struct creature_s *plr, u16 *hdg);
void nw_slide_spin_lean(struct creature_s *c, s32 count);
void nw_slide_spin_unlean(void);

#endif
