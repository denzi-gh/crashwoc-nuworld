/* --dump and --poke: game state in PS2 layout at chosen vblanks, and writes into it, to line a headless run up with
   PCSX2's (tools/lockstep.py); vblanks count from the title cut's start, as the traces do. */
#ifndef NUWORLD_PORT_DUMP_H
#define NUWORLD_PORT_DUMP_H

#include <stdbool.h>
#include <stddef.h>

/* list: a comma list or a file of "frame N" / "vblank N" lines; each goes to dir as <vblank>.nwd, without the
   superbuffer's raw bytes unless heap. */
bool nw_dump_start(const char *list, const char *dir, bool heap);
/* "symbol=value": dump vblanks count from the first vblank on which it holds; pokes still count from the title cut. */
bool nw_dump_after(const char *cond);
/* Comma-separated "vblank:symbol=value" 32-bit writes; a symbol is a data image name, D_<address> or 0x<address>, with
   an optional +offset. */
bool nw_poke_start(const char *list);

/* Game thread, at the yield point before vblank vbcnt's handlers, where PCSX2 saves its state. */
void nw_dump_vblank(int vbcnt);

bool nw_dump_finished(void);
/* Dumps written, and those that failed. */
unsigned nw_dump_written(unsigned *failed);

/* Writes the game's state now; false with the reason in err. */
bool nw_dump_write(const char *path, int vblank, int vbcnt, bool heap, char *err, size_t err_size);

#endif
