/* The saves folder: NuWorld's own saves, a game each. Save N is saveNNN.toml (NNN = N + 1) with the time it was saved,
   written and verified via .tmp, atomically replaced, with two previous versions (.bak, .bak2) and an integrity
   envelope. An unloadable main is copied to .corrupt; deletion leaves a tombstone so backups are not resurrected.
   autosaveNNN.toml uses the same protection in an independent bank.
   Saves from elsewhere are converted: the PS2's (a raw BESLES-50386slot00, or a PCSX2 .ps2 card image that holds it)
   and the file older NuWorld builds kept them in (BESLES-50386slot00.toml, its 4 games). Games go back to the PS2
   only by an export into a PCSX2 card. */
#ifndef NUWORLD_SAVE_SAVESTORE_H
#define NUWORLD_SAVE_SAVESTORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "save/savecodec.h"

#define NW_SAVE_GAMES 100                /* NuWorld's own saves */
#define NW_SAVE_AUTOS 100                /* independent autosaves, never manual destinations */
#define NW_SAVE_TOTAL (NW_SAVE_GAMES + NW_SAVE_AUTOS)
#define NW_SAVE_PS2_NAME "BESLES-50386" /* a PS2 save's folder and file name prefix */
#define NW_SAVE_PS2_GAMES 4              /* the games a PS2 save holds */

typedef enum NwSaveResult {
    NW_SAVE_OK,
    NW_SAVE_NONE,   /* no save */
    NW_SAVE_FAILED, /* SDL error */
    NW_SAVE_BACKUP, /* OK, from the .bak: the SDL error says why the file itself did not load */
} NwSaveResult;

/* When a save was written, local time; all zero when unknown. */
typedef struct NwSaveTime {
    int year, month, day, hour, minute, second;
} NwSaveTime;

/* Native metadata; absent in old/imported saves. campaign identifies a playthrough independently of its name. */
typedef struct NwSaveMeta {
    char campaign[33];
    bool autosave;
    int64_t revision;
} NwSaveMeta;

/* Extended native API. n >= NW_SAVE_GAMES addresses autosaveNNN.toml. generation: 0 current, 1 .bak, 2 .bak2.
   Explicit older generations do not fall forward to the current save. */
NwSaveResult nw_save_load_meta(const char *dir, int n, int generation, const NwSaveRegistry *reg, uint8_t *game,
                              NwSaveNames *names, NwSaveTime *saved, NwSaveMeta *meta);
bool nw_save_store_meta(const char *dir, int n, const NwSaveRegistry *reg, const uint8_t *game,
                        const NwSaveNames *names, const NwSaveTime *saved, const NwSaveMeta *meta);
int nw_save_scan_auto(const char *dir, int *last);

/* Save n (0 .. NW_SAVE_GAMES - 1) of dir (a folder path ending with a separator) into game (one record), its longer
   names (may be NULL) and when it was saved (may be NULL); they are left alone unless the result is an OK one.
   NW_SAVE_NONE without a file, or with one whose game is empty. A file with a newer format fails and is kept. */
NwSaveResult nw_save_load(const char *dir, int n, const NwSaveRegistry *reg, uint8_t *game, NwSaveNames *names,
                          NwSaveTime *saved);
/* game (not empty) as save n with its longer names (NULL: none), saved then (NULL: now), merged into its file; false
   with an SDL error. */
bool nw_save_store(const char *dir, int n, const NwSaveRegistry *reg, const uint8_t *game, const NwSaveNames *names,
                   const NwSaveTime *saved);
/* Moves save n's file to .bak; NW_SAVE_NONE when it has none. */
NwSaveResult nw_save_remove(const char *dir, int n);
bool nw_save_exists(const char *dir, int n);
/* Whether dir has had saves: a save's file, or the .bak a deleted one leaves. */
bool nw_save_started(const char *dir);
/* The save written last by its file's time (of the same time, the higher), -1 without saves; *last (may be NULL):
   the highest save there, -1. */
int nw_save_scan(const char *dir, int *last);

/* Before dir's first save, once: an older NuWorld build's file, else the folder's PS2 save (its raw file, else the
   first .ps2 card image by name that holds it), as saves 0 to 3 by their place, an empty game skipped. The older
   file is renamed .converted. The saves made, 0 with none to convert, -1 with an SDL error. */
int nw_save_convert(const char *dir, const NwSaveRegistry *reg);
/* The games of file (a raw PS2 save or a .ps2 card image) into dir's first free saves, in their order, saved now.
   The saves made, 0 when it holds no save, -1 with an SDL error (also with fewer free saves than games). */
int nw_save_import(const char *dir, const char *file, const NwSaveRegistry *reg);

/* What a PS2 slot gets in an export, beside a save's number. */
#define NW_SAVE_PS2_KEEP -1  /* the card's game stays (an empty slot when it has none) */
#define NW_SAVE_PS2_EMPTY -2 /* emptied, as the game deletes one */

typedef enum NwSaveCard {
    NW_SAVE_CARD_FAILED, /* an SDL error: no such file, not a card, a save there that does not read */
    NW_SAVE_CARD_BLANK,  /* a card no PS2 has formatted yet (the SDL error says to format it in PCSX2) */
    NW_SAVE_CARD_NONE,   /* formatted, without the game's save */
    NW_SAVE_CARD_SAVE,   /* the game's save: its games are read */
} NwSaveCard;

/* What card (a PCSX2 .ps2 image) holds; with the game's save, its games into games (NW_SAVE_PS2_GAMES records). */
NwSaveCard nw_save_card(const char *card, const NwSaveRegistry *reg, uint8_t *games);
/* Back to the PS2: dir's saves into card's save of the game, picks (NW_SAVE_PS2_GAMES) giving each PS2 slot a save's
   number or NW_SAVE_PS2_KEEP / NW_SAVE_PS2_EMPTY. A record goes as it is: NuWorld's longer names stay behind, the
   record holding the game's own 8 and 3 letters. The save is made as the game makes it (its folder with the icon,
   icon.sys and its files; icon (the disc's .ico) may be NULL when the card has the save's already); other saves on
   the card stay. The card is written via .tmp with the one before copied to .bak; false with an SDL error and the
   card unchanged (also when it is not formatted, or a slot keeps a game of a save there that does not read). */
bool nw_save_export(const char *dir, const NwSaveRegistry *reg, const char *card, const int *picks, const void *icon,
                    size_t icon_len);

/* An SDL_Time as local time; all zero when it does not convert. */
void nw_save_time_from(int64_t time, NwSaveTime *t);

#endif
