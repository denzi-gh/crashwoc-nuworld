/* The save menus, the Xbox version's way: NuWorld's saves a page of 4 at a time (port/saves.h), paged with left and
   right past the grid's edge, arrows where there is another page, each game's time and date, and the platform under
   the title. */
#ifndef NUWORLD_PORT_SAVEMENU_H
#define NUWORLD_PORT_SAVEMENU_H

#include <stdbool.h>

struct cursor_s;
struct nupad_s;

/* Whose name goes under the title: the Steam Deck's, else the PC's. */
void nw_savemenu_platform(bool deck);
/* After retail's ProcMenu (game/game wrap), with the cursor's column before it: a save grid opens on the page of the
   save written last with the cursor on it, and a step past the grid's edge turns the page. */
void nw_savemenu_proc(struct cursor_s *cursor, struct nupad_s *pad, int old_x);
/* Before retail's DrawMenu: a save grid opened outside ProcMenu (the title's LOAD GAME) gets its page too. */
void nw_savemenu_prepare(struct cursor_s *cursor);
/* After it: the platform under the save menus' titles, the grid's page arrows. */
void nw_savemenu_draw(struct cursor_s *cursor);

#endif
