/* NEW GAME's name typed on the keyboard (game/game's menu 2). While the game asks for it, the main thread hands over
   what the keyboard types; then the letter table hides until the controller is used again. Any thread unless said. */
#ifndef NUWORLD_PORT_NAMEENTRY_H
#define NUWORLD_PORT_NAMEENTRY_H

#include <stdbool.h>
#include <stdint.h>

/* Keys beside the characters. */
#define NW_NAMEENTRY_BACK '\b' /* the last letter off */
#define NW_NAMEENTRY_DONE '\r' /* the name as it is: retail's DONE */
#define NW_NAMEENTRY_QUIT 0x1B /* back to the title: retail's TRIANGLE */

struct cursor_s;
struct nupad_s;

/* Whether the name entry is open, so the keyboard types instead of being the pad. */
bool nw_nameentry_wanted(void);
/* Main thread: a character (A to Z, 0 to 9, space; others are dropped) or a key above. */
void nw_nameentry_type(int c);
/* Main thread: whether the keyboard is the device used last, which hides the letter table. */
void nw_nameentry_set_keyboard(bool keyboard);
bool nw_nameentry_keyboard(void);

/* Game thread, before retail's ProcMenu: opens or closes the entry with the menu and types what came; DONE and
   QUIT press retail's CROSS on DONE and TRIANGLE for this frame. Returns the buttons it pressed. */
uint32_t nw_nameentry_proc(struct cursor_s *cursor, struct nupad_s *pad);
/* Game thread, after retail's ProcMenu: the buttons nw_nameentry_proc pressed, released. */
void nw_nameentry_done(struct nupad_s *pad, uint32_t pressed);
/* Game thread, DrawMenu: the name alone while the keyboard types; false when retail draws the menu. */
bool nw_nameentry_draw(struct cursor_s *cursor);

#endif
