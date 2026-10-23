/* nu3d/nustream, which the port overrides whole (port/units.toml): the render streams. */
#include "nu3d/nustream.h"

extern int gs_top;

void NuRndrStreamResetStream(struct rndrstream_s *stream, int flag)
{
    stream->base.u8 = 0;
    stream->ptr.u8 = 0;
    stream->t_base.u32 = 0;
    stream->t_ptr.u32 = 0;
    if (flag) {
        stream->gs_ptr = gs_top;
        stream->local_mtx_id = -2;
        stream->view_mtx_id = -2;
        stream->scissor_id = -2;
        stream->misc_addr.voidptr = 0;
        stream->tid = 0;
        stream->xyoffset_id = -2;
    }
}
