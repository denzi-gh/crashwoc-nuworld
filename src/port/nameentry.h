/* The names typed in game/game's name entries: NEW GAME's (up to 16 letters, port/names.h) and a time trial record's
   (3, or up to 8 with nw_profile.long_time_names), from the letter table or the keyboard. While an entry is open the
   main thread hands over what the keyboard types; then the letter table hides until the controller is used again.
   Any thread unless said. */
#ifndef NUWORLD_PORT_NAMEENTRY_H
#define NUWORLD_PORT_NAMEENTRY_H

#include <stdbool.h>
#include <stdint.h>

/* Keys beside the characters. */
#define NW_NAMEENTRY_BACK '\b' /* the last letter off */
#define NW_NAMEENTRY_DONE '\r' /* the name as it is: retail's DONE */
#define NW_NAMEENTRY_QUIT 0x1B /* back to the title: retail's TRIANGLE (NEW GAME's only) */

struct cursor_s;
struct nupad_s;

/* Whether a name entry is open, so the keyboard types instead of being the pad. */
bool nw_nameentry_wanted(void);
/* Main thread: a character (A to Z, 0 to 9, space; others are dropped) or a key above. */
void nw_nameentry_type(int c);
/* Main thread: whether the keyboard is the device used last, which hides the letter table. */
void nw_nameentry_set_keyboard(bool keyboard);
bool nw_nameentry_keyboard(void);

/* Game thread, before retail's ProcMenu: opens or closes the entry with the menu; a table letter (retail's CROSS on
   it, which it takes: *taken) and what was typed go into the longer name, the game's own taking its first letters;
   DONE and QUIT press retail's CROSS on DONE and TRIANGLE for this frame. Returns the buttons it pressed. */
uint32_t nw_nameentry_proc(struct cursor_s *cursor, struct nupad_s *pad, uint32_t *taken);
/* Game thread, after retail's ProcMenu: the buttons nw_nameentry_proc pressed released, those it took back. */
void nw_nameentry_done(struct nupad_s *pad, uint32_t pressed, uint32_t taken);
/* Game thread: the longer name being typed ('_' for the letters still to come), NULL with no entry open. */
const char *nw_nameentry_name(void);
/* Preserve the typed name across the autosave question. */
void nw_nameentry_finish(void);
void nw_nameentry_resume_game(const char *name, int position);
/* Game thread, DrawMenu: NEW GAME's entry with its longer name (the letters only without the keyboard), a record's
   entry with nothing while the keyboard types; false when retail draws the menu. */
bool nw_nameentry_draw(struct cursor_s *cursor);

#endif
