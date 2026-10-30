/* The game's levels (LData) for the debug menu. A level asked for from any thread is entered by the game thread at a
   screen swap once the current level runs, the way retail's debug level select enters it, without the fade out. */
#ifndef NUWORLD_PORT_LEVEL_H
#define NUWORLD_PORT_LEVEL_H

#include <stdbool.h>
#include <stddef.h>

int nw_level_count(void);
/* The English name, NULL for a level the disc lacks. */
const char *nw_level_name(int level);
int nw_level_current(void);
/* The level asked for and not yet entered, or -1. */
int nw_level_pending(void);
void nw_level_load(int level);

/* --level: LData indices or file names ("farm"), comma-separated as a route ("intro" first is NEW GAME), each asked for
   once the one before runs; false when the disc lacks one. The trace numbers vblanks from the last. */
bool nw_level_start(const char *names);
/* The route's last level, -1 for none. */
int nw_level_final(void);
/* The level the run is on its way to, -1 for none. */
int nw_level_arrival(void);
/* The run's last level has been entered and runs: loaded, faded in, its arrival cut skipped. */
bool nw_level_arrived(void);

typedef struct NwLevelArrival {
    int level;
    size_t gpu; /* the scene GPU objects alive (src/port/heap.h) when it ran */
} NwLevelArrival;
/* The route's levels that ran so far, in order. */
int nw_level_arrivals(const NwLevelArrival **out);

/* A route's NEW GAME or an asked level is on its way. */
bool nw_level_routing(void);
/* Game thread: ends the running title cut, still black, into TITLES' own level (gameplay/title.c). */
void nw_level_titles(void);

/* The game thread, at each swap. */
void nw_level_poll(void);

#endif
