/* NuWorld's saves in the game: retail's game/loadsave is replaced and its memory card dropped (port/units.toml), and
   the save menus call in here (port/patches/97-native-saves). The game's SaveSlot[2][2] shows a page of 4 saves (save/savestore.h);
   a save is written at once, with no memory card in between. */
#ifndef NUWORLD_PORT_SAVES_H
#define NUWORLD_PORT_SAVES_H

#include "save/savestore.h"

#define NW_SAVES_PAGE 4
#define NW_SAVES_PAGES (NW_SAVE_GAMES / NW_SAVES_PAGE)

struct game_s;

/* dir (ending in a separator, copied) holds the saves; NULL means none. On first use the saves from elsewhere are
   converted (nw_save_convert). */
void nw_saves_init(const char *dir);
/* file's games (a raw PS2 save or a .ps2 card image) into dir's first free saves: the saves made, 0 when it holds no
   save, -1 with an SDL error. */
int nw_saves_import(const char *dir, const char *file);

/* The page SaveSlot shows (saves page * 4 on), loaded at once by nw_saves_show. */
int nw_saves_page(void);
void nw_saves_show(int page);
/* The pages the menus go through: up to the last save's page, and one more (at most NW_SAVES_PAGES). */
int nw_saves_pages(void);
/* The save written last, -1 without saves; as of the last show, write or delete. */
int nw_saves_latest(void);
/* When SaveSlot[i / 2][i % 2]'s game was saved; all zero for an empty slot or an unknown time. */
const NwSaveTime *nw_saves_time(int i);

/* The save menus (game/game). slot is SaveSlot's: written as its save now, or emptied and its save deleted; either
   starts its result screen. */
void nw_saves_write(struct game_s *slot);
void nw_saves_delete(struct game_s *slot);
/* A game loaded: starts the GAME LOADED screen. */
void nw_saves_loaded(void);
/* A frame of the result screen; 1 once it has been shown its time. */
int nw_saves_shown(void);
/* Whether the last write or delete worked, and whether the folder has a save. */
int nw_saves_ok(void);
int nw_saves_any(void);

#endif
