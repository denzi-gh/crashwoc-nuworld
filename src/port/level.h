/* The game's levels (LData) for the debug menu. A level asked for from any thread is entered by the game thread at a
   screen swap once the current level runs, the way retail's debug level select enters it, without the fade out. */
#ifndef NUWORLD_PORT_LEVEL_H
#define NUWORLD_PORT_LEVEL_H

#include <stdbool.h>

int nw_level_count(void);
/* The English name, NULL for a level the disc lacks. */
const char *nw_level_name(int level);
int nw_level_current(void);
/* The level asked for and not yet entered, or -1. */
int nw_level_pending(void);
void nw_level_load(int level);

/* --level: the run's level, by LData index or its file's name ("farm"), asked for at once; false when the disc lacks
   it. Once it runs, the trace (port/trace.h) numbers vblanks from there. */
bool nw_level_start(const char *name);
/* The run's level, -1 for none. */
int nw_level_arrival(void);
/* The run's level has been entered and runs: loaded, faded in, its arrival cut skipped. */
bool nw_level_arrived(void);

/* The game thread, at each swap. */
void nw_level_poll(void);

#endif
