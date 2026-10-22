/* Included first by the wrapper of every engine and game unit (tools/tu_filter.py):
 * the host libc, so a unit that calls libc without including nu_libc.h still
 * sees the real prototypes, then the renames of SDK names the host has or
 * lacks under another meaning. */
#ifndef NUWORLD_PORT_NW_PRELUDE_H
#define NUWORLD_PORT_NW_PRELUDE_H

#include "nu_libc.h"

#include "port/nw_ee.h"
#include "port/nw_newlib.h"

/* newlib and libgcc names: UCRT exports its own strlwr, x86_64 libgcc has no
 * __divdi3 (src/shim/libc.c). After nu_libc.h, so the host headers keep theirs. */
#define strlwr nw_strlwr
#define __divdi3 nw_divdi3

#endif
