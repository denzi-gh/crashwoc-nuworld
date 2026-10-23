/* The port's .ghg loader (src/port/nuhgobj.c): what it keeps outside the caller's buffer. */
#ifndef NUWORLD_PORT_NUHGOBJ_H
#define NUWORLD_PORT_NUHGOBJ_H

#include <stdint.h>

/* The objects NuGHGRead made that NuHGobjDestroy has not freed yet. */
uint32_t nw_ghg_count(void);

#endif
