#include "gameplay/title.h"

#include "gameplay/gameplay.h"
#include "nu_types.h"
#include "port/level.h"

extern s32 cutmovie;

/* Every title cut (boot, quit, demo end, game over) ends at its first swap, still black, into TITLES' own level; a
   route's NEW GAME and an asked level keep the cut. */
void nw_title_poll(void)
{
    if (nw_profile.unused_title && cutmovie == 0 && !nw_level_routing()) {
        NW_DEVIATION(unused_title);
        nw_level_titles();
    }
}
