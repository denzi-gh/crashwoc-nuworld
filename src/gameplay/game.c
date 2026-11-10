/* Wraps of game/game: the jeep's steering hook and the WOMBAT name. */
#include <string.h>

#include "nu_types.h"

#include "game/gametypes.h"
#include "game/game.retail.h"
#include "gameplay/gameplay.h"
#include "gameplay/jeep.h"
#include "port/nameentry.h"
#include "port/savemenu.h"

/* Declared by no header. */
extern struct game_s Game;
extern s32 new_level;
void NuStrCpy(char *dst, char *src);
void OpenGame(void);

u16 SeekRot(u16 a, u16 b, s32 shift)
{
    u16 hdg;
    return nw_jeep_steer(a, b, shift, &hdg) ? hdg : SeekRot__retail(a, b, shift);
}

/* WOMBAT name detection; the name typed on the keyboard (port/nameentry.h); the save menus' pages (port/savemenu.h) */
void ProcMenu(struct cursor_s *cursor, struct nupad_s *pad)
{
    bool naming = cursor->menu == 2 && new_level != 0x25;
    int x = cursor->x;

    uint32_t typed = nw_nameentry_proc(cursor, pad);
    ProcMenu__retail(cursor, pad);
    nw_nameentry_done(pad, typed);
    nw_savemenu_proc(cursor, pad, x);
    if (naming && new_level == 0x25 && nw_profile.wombat && strstr(Game.name, "WOMBAT")) {
        NW_DEVIATION(wombat);
        NuStrCpy(Game.name, "CRASH   ");
        OpenGame();
    }
}
