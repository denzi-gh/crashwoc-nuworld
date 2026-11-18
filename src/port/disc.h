#ifndef NUWORLD_PORT_DISC_H
#define NUWORLD_PORT_DISC_H

#include <stdbool.h>

/* The US disc */
bool nw_disc_ntsc(void);
/* A sound number of a disc file in PAL's numbering, by the level's tables; -1 for a sound PAL has no row of. */
int nw_disc_sfx(int sfx);

#endif
