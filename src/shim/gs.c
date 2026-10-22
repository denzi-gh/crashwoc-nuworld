/* GS: the vblank wait cut.c, nusound.c and the screen swap call. */
#include "port/game.h"
#include "shim/shim.h"

/* Waits for the next vblank (a yield point); the field it starts, alternating as an interlaced display's do. */
int sceGsSyncV(int mode)
{
    (void)mode;
    return (int)(nw_game_yield(true) & 1);
}
