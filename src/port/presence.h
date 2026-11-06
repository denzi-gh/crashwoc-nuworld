/* Rich Presence's view of the game (frontend/presence.h), copied at each swap. */
#ifndef NUWORLD_PORT_PRESENCE_H
#define NUWORLD_PORT_PRESENCE_H

#include "frontend/presence.h"

/* The game thread, at each swap. */
void nw_presence_swap(void);
/* Any thread: the newest copy; valid is false before the first swap. */
void nw_presence_poll(NwPresenceSnap *out);

#endif
