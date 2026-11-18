/* NuWorld's longer names beside the game's own (save/savecodec.h's NwSaveNames): a game's up to 16 letters beside
   Game.name's 8, a time trial record's up to 8 beside its 3. One set for the game in play, one for each SaveSlot.
   A longer name stands while the game's own name is its start (its first 8 or 3 letters, spaces after it), so a name
   the game sets itself (a new game's, a placeholder's) wins; the game's copies carry them through port/saves.c, the
   name entry (port/nameentry.c) and the NewLevelTime and DefaultTimeTrialNames wraps. Game thread. */
#ifndef NUWORLD_PORT_NAMES_H
#define NUWORLD_PORT_NAMES_H

#include <stdbool.h>
#include <stddef.h>

#include "save/savecodec.h"

#define NW_NAMES_GAME NW_SAVE_LONG_NAME /* a game's name */
#define NW_NAMES_OWN_GAME 8             /* Game.name's letters */
#define NW_NAMES_TIME NW_SAVE_LONG_TIME /* a time trial record's, with nw_profile.long_time_names */
#define NW_NAMES_OWN_TIME 3             /* the record's own letters */

struct game_s;

/* The game in play's names, and SaveSlot[i / 2][i % 2]'s. */
NwSaveNames *nw_names_game(void);
NwSaveNames *nw_names_slot(int i);
/* The game in play's names to slot i (it saved there), or slot i's to the game in play (it loaded from there). */
void nw_names_save(int i);
void nw_names_load(int i);

/* The longest name a time trial record takes now: NW_NAMES_TIME with nw_profile.long_time_names, else 3. */
int nw_names_time_max(void);

/* What to show for g's name (Game or a SaveSlot): its longer name when it stands, else its own. */
char *nw_names_of(const struct game_s *g);
/* The same for a time trial record of Game or a SaveSlot (struct time_s; NW_TIME_NAME in nu_port.h). */
char *nw_names_time_text(const void *time);
/* scale for text that has more than fit letters, so it keeps the width fit letters have at scale. */
float nw_names_scale(float scale, const char *text, size_t fit);

#endif
