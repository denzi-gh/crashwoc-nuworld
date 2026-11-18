#include "gameplay/bosscut.h"

#include "gameplay/gameplay.h"

extern s32 Level, new_level;
extern u16 new_lev_flags;

#define NW_BOSS_BEATEN 0x800
#define NW_BOSS_CUT_HUB 0x25

static s32 nw_boss_cut_level = -1;

void nw_boss_cut_finish(u16 flags)
{
    if (nw_profile.first_boss_cut && (flags & NW_BOSS_BEATEN) && !(new_lev_flags & NW_BOSS_BEATEN) &&
        new_level == NW_BOSS_CUT_HUB) {
        nw_boss_cut_level = Level;
    }
}

u16 nw_boss_cut_flags(s32 last, s32 next)
{
    s32 level = nw_boss_cut_level;
    nw_boss_cut_level = -1;
    if (!nw_profile.first_boss_cut || level < 0 || last != level || next != NW_BOSS_CUT_HUB) {
        return 0;
    }
    NW_DEVIATION(first_boss_cut);
    return NW_BOSS_BEATEN;
}
