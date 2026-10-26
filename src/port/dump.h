/* --dump and --poke (src/port/dump.c): the game's state in PS2 layout at chosen vblanks, and writes into it at
   others, so a headless run lines up with PCSX2's (tools/lockstep.py). Vblanks count from the title cut's start, as
   the traces do (port/trace.h). */
#ifndef NUWORLD_PORT_DUMP_H
#define NUWORLD_PORT_DUMP_H

#include <stdbool.h>
#include <stddef.h>

/* The vblanks (a comma list, or a file of "frame N" / "vblank N" lines) whose state goes to dir as <vblank>.nwd;
   heap false leaves the superbuffer's raw bytes out. */
bool nw_dump_start(const char *list, const char *dir, bool heap);
/* "symbol=value": the dump vblanks count from the first vblank on which it holds (the pokes' still from the title
   cut). */
bool nw_dump_after(const char *cond);
/* "vblank:symbol=value" items, comma separated: a 32-bit write the game thread makes at that vblank. A symbol is
   a data image name, D_<address> or 0x<address>, with an optional +offset. */
bool nw_poke_start(const char *list);

/* The game thread, at a yield point before the vblank handlers of the edge that brings vblank vbcnt: PCSX2 saves a
   state there. */
void nw_dump_vblank(int vbcnt);

bool nw_dump_finished(void);
/* Dumps written, and those that failed. */
unsigned nw_dump_written(unsigned *failed);

/* The game's state now. False with the reason in err. */
bool nw_dump_write(const char *path, int vblank, int vbcnt, bool heap, char *err, size_t err_size);

#endif
