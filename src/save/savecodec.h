/* The game's save records (struct game_s in its PS2 layout, little-endian) in a save document (save/savedoc.h):
     format = 1
     [slot.N]                the record's own fields, N = its index in the game's array
     [slot.N.options]        the options a record carries (loading a game applies them)
     [slot.N.hub.<key>]      one per hub
     [slot.N.level.<key>]    one per level: flags, pad, time = [[name, itime] x3]
   Levels and hubs are keyed by a registry, not by position, so a level keeps its record whatever is added around it.
   Encoding round-trips byte for byte; what the codec does not write stays in the document. */
#ifndef NUWORLD_SAVE_SAVECODEC_H
#define NUWORLD_SAVE_SAVECODEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "save/savedoc.h"

#define NW_SAVE_FORMAT 1 /* documents with a higher format are refused */

/* struct game_s (decomp/include/game/gametypes.h); src/port/saveload.c checks these against it. */
#define NW_SAVE_GAME_SIZE 0x40C
#define NW_SAVE_NAME 0x000
#define NW_SAVE_NAME_SIZE 9
#define NW_SAVE_VIBRATION 0x009
#define NW_SAVE_SURROUND 0x00A
#define NW_SAVE_SFX_VOLUME 0x00B
#define NW_SAVE_MUSIC_VOLUME 0x00C
#define NW_SAVE_SCREEN_X 0x00D
#define NW_SAVE_SCREEN_Y 0x00E
#define NW_SAVE_LANGUAGE 0x00F
#define NW_SAVE_HUB 0x010
#define NW_SAVE_HUBS 6
#define NW_SAVE_LEVEL 0x028
#define NW_SAVE_LEVELS 35
#define NW_SAVE_LIVES 0x3FC
#define NW_SAVE_WUMPAS 0x3FD
#define NW_SAVE_MASK 0x3FE
#define NW_SAVE_PERCENT 0x3FF
#define NW_SAVE_CRYSTALS 0x400
#define NW_SAVE_RELICS 0x401
#define NW_SAVE_CRATE_GEMS 0x402
#define NW_SAVE_BONUS_GEMS 0x403
#define NW_SAVE_GEMS 0x404
#define NW_SAVE_GEMBITS 0x405
#define NW_SAVE_POWERBITS 0x406
#define NW_SAVE_EMPTY 0x407
#define NW_SAVE_CUTBITS 0x408
/* struct hub_s */
#define NW_SAVE_HUB_SIZE 4
#define NW_SAVE_HUB_FLAGS 0x0
#define NW_SAVE_HUB_CRYSTALS 0x1
#define NW_SAVE_HUB_PAD 0x2
/* struct level_s and struct time_s */
#define NW_SAVE_LEVEL_SIZE 0x1C
#define NW_SAVE_LEVEL_FLAGS 0x00
#define NW_SAVE_LEVEL_PAD 0x02
#define NW_SAVE_LEVEL_TIME 0x04
#define NW_SAVE_TIMES 3
#define NW_SAVE_TIME_SIZE 8
#define NW_SAVE_TIME_NAME 0x0
#define NW_SAVE_TIME_NAME_SIZE 4
#define NW_SAVE_TIME_ITIME 0x4

/* The key of each level and hub record, by its index in the record. */
typedef struct NwSaveRegistry {
    char **levels;
    size_t level_count;
    char **hubs;
    size_t hub_count;
} NwSaveRegistry;

/* Level keys from the levels' paths ("a\\cold_a\\cold_a" -> "a/cold_a/cold_a"; a missing, empty or repeated path
   gets "#<index>"), hubs keyed by index; false with an SDL error. */
bool nw_save_registry_init(NwSaveRegistry *reg, const char *const *paths, size_t levels, size_t hubs);
void nw_save_registry_free(NwSaveRegistry *reg);

/* The document's format key; -1 when it has none (or not an integer). */
int64_t nw_save_format(const NwDoc *doc);
/* Writes count records into doc as [slot.0] ..; false with an SDL error (a newer format, a key in the way). */
bool nw_save_merge(NwDoc *doc, const NwSaveRegistry *reg, const uint8_t *games, size_t count);
/* doc's first count slots as records: a missing slot is empty, a missing field zero. false with an SDL error. */
bool nw_save_decode(const NwDoc *doc, const NwSaveRegistry *reg, uint8_t *games, size_t count);

#endif
