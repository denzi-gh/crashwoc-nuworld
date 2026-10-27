/* The game's levels (LData) for the debug menu. A level asked for from any thread is entered by the game thread at a
   screen swap once the current level runs, the way retail's debug level select enters it, without the fade out. */
#ifndef NUWORLD_PORT_LEVEL_H
#define NUWORLD_PORT_LEVEL_H

int nw_level_count(void);
/* The English name, NULL for a level the disc lacks. */
const char *nw_level_name(int level);
int nw_level_current(void);
/* The level asked for and not yet entered, or -1. */
int nw_level_pending(void);
void nw_level_load(int level);

/* The game thread, at each swap. */
void nw_level_poll(void);

#endif
