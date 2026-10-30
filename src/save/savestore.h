/* The saves folder: card slot N is BESLES-50386slotNN.toml, written via .tmp with the previous file kept as .bak (an
   unloadable one goes to .corrupt, a deleted one to .bak). A slot with neither file imports a PS2 save once from the
   folder: a raw BESLES-50386slotNN, else the first *.ps2 card image by name that holds it. */
#ifndef NUWORLD_SAVE_SAVESTORE_H
#define NUWORLD_SAVE_SAVESTORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "save/savecodec.h"

#define NW_SAVE_CARD_NAME "BESLES-50386" /* the save's folder on a card, and its file names' prefix */
#define NW_SAVE_CARD_SLOTS 4             /* gamelib/saveload's card slots */
#define NW_SAVE_RECORDS 4                /* records in the game's save (SaveSlot[2][2]) */

typedef enum NwSaveResult {
    NW_SAVE_OK,
    NW_SAVE_NONE,     /* no save */
    NW_SAVE_FAILED,   /* SDL error */
    NW_SAVE_IMPORTED, /* OK, from a PS2 save: the SDL error names its file */
    NW_SAVE_BACKUP,   /* OK, from the .bak: the SDL error says why the file itself did not load */
} NwSaveResult;

/* count records (1..NW_SAVE_RECORDS) of card slot slot from dir (a folder path ending with a separator) into games;
   games is left alone unless the result is an OK one. A file with a newer format fails and is kept. */
NwSaveResult nw_save_load(const char *dir, int slot, const NwSaveRegistry *reg, uint8_t *games, size_t count);
/* Writes count records as card slot slot, merged into its file; false with an SDL error. */
bool nw_save_store(const char *dir, int slot, const NwSaveRegistry *reg, const uint8_t *games, size_t count);
/* Moves card slot slot's file to .bak; NW_SAVE_NONE when it has none. */
NwSaveResult nw_save_remove(const char *dir, int slot);
/* Whether card slot slot has a file. */
bool nw_save_exists(const char *dir, int slot);
/* file (a raw BESLES-50386slotNN or a .ps2 card image holding one) as card slot slot's save, an existing file kept
   as .bak; NW_SAVE_NONE when the card has no such save. */
NwSaveResult nw_save_import(const char *dir, int slot, const char *file, const NwSaveRegistry *reg);

#endif
