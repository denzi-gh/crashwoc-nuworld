#include <stdint.h>

#include "nu3d/nustream.h"
#include "nups2/nups2types.h"

extern u8 *rndrstream_free;

/* A stream holding only the NEXT tag that ended its last link starts over at free, so that the port's records leave no
   raw packets behind. */
void NuRndrStreamLink(struct rndrstream_s *stream)
{
    u8 *free = rndrstream_free;
    struct DmaTag *tail = stream->ptr.voidptr;
    if (!stream->base.voidptr || (stream->ptr.u8 == stream->base.u8 && tail->ctrl == 0x20000000)) {
        stream->base.u8 = stream->ptr.u8 = free;
        return;
    }
    tail->addr = (u32)(uintptr_t)free;
    if (free - 16 != stream->ptr.u8) {
        stream->ptr.u8 = free;
    }
}
