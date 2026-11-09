/* game/text's tables the port rewrites. Set before the game thread starts; untouched they stay retail. */
#ifndef NUWORLD_PORT_TEXT_H
#define NUWORLD_PORT_TEXT_H

#include <stdbool.h>

/* The save menus' texts for the saves folder instead of the PS2's memory card in slot 1, in all 6 languages: the
   card check and the warning while the folder is written (its power line names the PC, or the Steam Deck with
   deck). The card's other notices (none inserted, unformatted, full, the format menus) cannot show on PC
   (port/saveload.c) and are dropped. */
void nw_text_platform(bool deck);

#endif
