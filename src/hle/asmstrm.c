#include <stdint.h>

#include "nu3d/nustream.h"
#include "nups2/nups2types.h"

extern u8 *rndrstream_free;

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
