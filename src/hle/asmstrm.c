/* nu3d/asmstrm.s (port/units.toml: hle). */
#include <stdint.h>

#include "nu3d/nustream.h"
#include "nups2/nups2types.h"

/* The stream buffer's next free quadword (a data image root). */
extern u8 *rndrstream_free;

/* Continues the stream at rndrstream_free. A stream with packets links its last tag, the open next tag at ptr, to
   it (the 32-bit ADDR keeps the low half of a host address); an empty one starts there. ptr moves to it unless it
   directly follows that tag. */
void NuRndrStreamLink(struct rndrstream_s *stream)
{
    u8 *free = rndrstream_free;
    if (!stream->base.voidptr) {
        stream->base.u8 = free;
    } else {
        ((struct DmaTag *)stream->ptr.voidptr)->addr = (u32)(uintptr_t)free;
    }
    if (free - 16 != stream->ptr.u8) {
        stream->ptr.u8 = free;
    }
}
