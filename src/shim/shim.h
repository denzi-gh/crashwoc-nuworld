/* Stand-ins for the SDK the decomp calls: sce* and the EE kernel (the sdk units are "shim" in port/units.toml).
   A function the port does not implement yet goes through the stub telemetry under the unit "sdk". */
#ifndef NUWORLD_SHIM_SHIM_H
#define NUWORLD_SHIM_SHIM_H

#include "port/stub.h"

#define NW_SHIM_STUB(name, address) NW_STUB("sdk", name, address)

struct NwVfs;

/* src/shim/libc.c: the newlib reent block (rand's seed, errno) as retail starts with it. */
void nw_newlib_init(void);

/* src/shim/file.c: the file system the sce file calls use (NULL: none, every open fails); closes what is
   open. */
void nw_shim_file_init(const struct NwVfs *vfs);

#endif
