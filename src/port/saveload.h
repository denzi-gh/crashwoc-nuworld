/* The port's gamelib/saveload: the memory card is the saves folder (save/savestore.h). */
#ifndef NUWORLD_PORT_SAVELOAD_H
#define NUWORLD_PORT_SAVELOAD_H

#include "save/savestore.h"

/* dir (ending in a separator, copied) becomes the card; NULL means no card. */
void nw_saveload_init(const char *dir);
/* file (a raw BESLES-50386slot00 or a .ps2 card image) as the game's save in dir (save/savestore.h). */
NwSaveResult nw_saveload_import(const char *dir, const char *file);

#endif
