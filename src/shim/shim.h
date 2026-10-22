/* Stand-ins for the SDK the decomp calls: sce* and the EE kernel (the sdk units are "shim" in port/units.toml).
   A function the port does not implement yet goes through the stub telemetry under the unit "sdk". */
#ifndef NUWORLD_SHIM_SHIM_H
#define NUWORLD_SHIM_SHIM_H

#include "port/stub.h"

#define NW_SHIM_STUB(name, address) NW_STUB("sdk", name, address)

/* src/shim/libc.c: the newlib reent block (rand's seed, errno) as retail starts with it. */
void nw_newlib_init(void);

#endif
