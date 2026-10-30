/* --pad-script: pad port 0's reads, one step each once the run's level runs (at once without --level), from a text
   file, a retail .ppd recording (played once, then idle), "sweep" (every button and stick direction in a loop) or
   "auto" (the level's own .ppd if the disc has one, else "sweep").
   Text: one step per line, looping, `<reads> <buttons> [<lx> <ly> [<rx> <ry>]]`, buttons `-` or names joined by `|`
   (cross, square, ...), sticks 0-255 (128 centred); `#` starts a comment. */
#ifndef NUWORLD_PORT_PADSCRIPT_H
#define NUWORLD_PORT_PADSCRIPT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vfs/vfs.h"

struct nupadframe_s;

/* Whether spec replays a retail recording (a .ppd, or "auto"), one pad state per 50 Hz tick. */
bool nw_pad_script_is_recording(const char *spec);
/* Before the game starts; `level` is the run's last level, -1 for none. False with SDL_GetError(). */
bool nw_pad_script_start(const NwVfs *vfs, const char *spec, int level);
/* A text script (NUL-terminated) or a .ppd image in place of the run's script; false with SDL_GetError(). */
bool nw_pad_script_parse(const char *text);
bool nw_pad_script_recording(const uint8_t *data, size_t size);
void nw_pad_script_stop(void);
/* The pad shim's read of port 0: true when the script gave this read's frame. */
bool nw_pad_script_read(struct nupadframe_s *frame);

#endif
