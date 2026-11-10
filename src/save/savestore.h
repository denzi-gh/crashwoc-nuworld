/* The saves folder: NuWorld's own saves, a game each. Save N is saveNNN.toml (NNN = N + 1) with the time it was saved,
   written via .tmp with the previous file kept as .bak (an unloadable one goes to .corrupt, a deleted one to .bak).
   Saves from elsewhere are converted, never written back: the PS2's (a raw BESLES-50386slot00, or a PCSX2 .ps2 card
   image that holds it) and the file older NuWorld builds kept them in (BESLES-50386slot00.toml, its 4 games). */
#ifndef NUWORLD_SAVE_SAVESTORE_H
#define NUWORLD_SAVE_SAVESTORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "save/savecodec.h"

#define NW_SAVE_GAMES 100                /* NuWorld's own saves */
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

/* Save n (0 .. NW_SAVE_GAMES - 1) of dir (a folder path ending with a separator) into game (one record) and when it
   was saved (saved may be NULL); game is left alone unless the result is an OK one. NW_SAVE_NONE without a file, or
   with one whose game is empty. A file with a newer format fails and is kept. */
NwSaveResult nw_save_load(const char *dir, int n, const NwSaveRegistry *reg, uint8_t *game, NwSaveTime *saved);
/* game (not empty) as save n, saved then (NULL: now), merged into its file; false with an SDL error. */
bool nw_save_store(const char *dir, int n, const NwSaveRegistry *reg, const uint8_t *game, const NwSaveTime *saved);
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

/* An SDL_Time as local time; all zero when it does not convert. */
void nw_save_time_from(int64_t time, NwSaveTime *t);

#endif
