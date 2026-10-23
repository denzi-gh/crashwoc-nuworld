/* Stand-ins for the SDK the decomp calls: sce* and the EE kernel (the sdk units are "shim" in port/units.toml).
   A function the port does not implement yet goes through the stub telemetry under the unit "sdk". */
#ifndef NUWORLD_SHIM_SHIM_H
#define NUWORLD_SHIM_SHIM_H

#include <stdint.h>

#include "port/stub.h"

#define NW_SHIM_STUB(name, address) NW_STUB("sdk", name, address)

struct NwVfs;

/* src/shim/libc.c: the newlib reent block (rand's seed, errno) as retail starts with it. */
void nw_newlib_init(void);

/* src/shim/file.c: the file system the sce file calls use (NULL: none, every open fails); closes what is
   open. */
void nw_shim_file_init(const struct NwVfs *vfs);

/* src/shim/ee.c. The return from an interrupt: the best ready EE thread runs. */
void nw_ee_schedule(void);
/* A yield point of the threads other than main (file reads): delivers the vblanks due. */
void nw_ee_yield(void);
/* NW_HOST_IDLE (decomp nu_port.h): an EE thread's final spin waits for the next vblank. */
void nw_ee_idle(void);
/* The game parks at its next yield point once `n` threads were terminated (0: never). */
void nw_ee_park_after(unsigned n);
/* The threads terminated so far, and the vblanks of up to `max` of them. */
unsigned nw_ee_terminated(uint64_t *at, unsigned max);

#endif
