/* Menu input, committed-progress snapshots, and the jeep's steering hook. */
#include "nu_types.h"

#include "game/gametypes.h"
#include "game/creature.h"
#include "game/game.retail.h"
#include "gameplay/jeep.h"
#include "port/nameentry.h"
#include "port/savemenu.h"
#include "port/autosave.h"
#include "port/menu.h"

/* Declared by no header. */
extern s32 new_level;

void NewGame(void)
{
    nw_autosave_leave();
    NewGame__retail();
}

void CheckFinish(struct obj_s *obj)
{
    int before = new_level;
    CheckFinish__retail(obj);
    if (obj->finished == 1 && new_level != -1 && new_level != before) {
        nw_autosave_checkpoint(true);
    }
}

u16 SeekRot(u16 a, u16 b, s32 shift)
{
    u16 hdg;
    return nw_jeep_steer(a, b, shift, &hdg) ? hdg : SeekRot__retail(a, b, shift);
}

/* Native name entry and save menus use retail's navigation and input guards. */
void ProcMenu(struct cursor_s *cursor, struct nupad_s *pad)
{
    int x = cursor->x;
    int menu = cursor->menu;

    uint32_t taken = 0, typed = nw_nameentry_proc(cursor, pad, &taken);
    ProcMenu__retail(cursor, pad);
    nw_nameentry_done(pad, typed, taken);
    nw_savemenu_proc(cursor, pad, x);
    if (menu == 15 && cursor->menu != 15) {
        nw_nameentry_finish();
        nw_autosave_checkpoint(true);
    }
    if (menu == 22 && cursor->menu == -1) {
        nw_menu_autosave_loaded(cursor);
    }
    if (menu == 8 && new_level != -1) {
        nw_autosave_checkpoint(false);
    }
}
