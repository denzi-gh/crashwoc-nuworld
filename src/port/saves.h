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
/* Without the game running: dir's saves into the PCSX2 card image card, picks giving the 4 PS2 slots their games
   (nw_save_export). false with an SDL error. */
bool nw_saves_export(const char *dir, const char *card, const int *picks, const void *icon, size_t icon_len);
/* Any thread: the level and hub keys of the saves (the game's level table); NULL with an SDL error. */
const NwSaveRegistry *nw_saves_levels(void);

/* The page SaveSlot shows (saves page * 4 on), loaded at once by nw_saves_show. */
int nw_saves_page(void);
void nw_saves_show(int page);
/* The pages the menus go through: up to the last save's page, and one more (at most NW_SAVES_PAGES). */
int nw_saves_pages(void);
/* Separate banks, with independent files and pagination. Manual writes always refuse the automatic bank. */
void nw_saves_autos(bool automatic);
bool nw_saves_automatic(void);
bool nw_saves_has_autos(void);
/* Worker completion updates this cached presence flag; draw functions never enumerate the disk. */
void nw_saves_auto_changed(void);
bool nw_saves_pending(void);
void nw_saves_finish(void);
/* Replace a displayed slot with an explicit older generation, leaving disk untouched. */
bool nw_saves_previous(struct game_s *slot, int generation);
bool nw_saves_recovered(int i);
/* The save written last, -1 without saves; as of the last show, write or delete. */
int nw_saves_latest(void);
/* When SaveSlot[i / 2][i % 2]'s game was saved; all zero for an empty slot or an unknown time. */
const NwSaveTime *nw_saves_time(int i);

/* The save menus (game/game). Queue an immutable slot snapshot or a deletion; result screens poll completion.
   No disk write, flush, backup rotation, enumeration or worker wait occurs in these submission calls. */
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
