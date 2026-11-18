/* NuWorld's saves back to the PS2: F1's Saves > Export to PS2 card, where a PCSX2 card image is picked and each of
   its 4 PS2 slots gets a save (port/saves.h, nw_save_export), and the save icon the export puts on a card. Main
   thread. */
#ifndef NUWORLD_FRONTEND_SAVEEXPORT_H
#define NUWORLD_FRONTEND_SAVEEXPORT_H

#include <stddef.h>
#include <stdint.h>

#include <SDL3/SDL.h>

#include "vfs/vfs.h"

/* The save icon of the disc (STUFF/CRASH.ICO, as the game loads it), SDL_malloc'd; NULL with an SDL error. */
void *nw_saveexport_icon(const NwVfs *vfs, size_t *len);
char *nw_saveexport_cards_folder(void);
void nw_saveexport_game(const uint8_t *game, char *out, size_t size);

/* Registers the panel: the saves of dir (ending in a separator, copied), the card picked in a dialog over window,
   the icon from vfs. */
void nw_saveexport_start(SDL_Window *window, const char *dir, const NwVfs *vfs);
void nw_saveexport_stop(void);

#endif
