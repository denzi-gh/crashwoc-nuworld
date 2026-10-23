#include "audio/nusound.h"

#include <stdint.h>
#include <string.h>

#define NW_IRX_DT 256 /* AdpcmSetParam's dt[] */

/* The module's globals, under their IRX names. */
static struct NwIrx {
    int32_t ret[NW_NUSOUND_REPLY / 4];
} irx;

void nw_nusound_init(void)
{
    memset(&irx, 0, sizeof(irx));
}

/* count, then per command {cmd, nwords, data[nwords]}, all s16. The commands with a string or a struct take the
   data in place, the others a copy in dt[] read zero-extended. The IRX trusts the counts; here a batch that runs
   past the buffer ends. */
static void AdpcmSetParam(const uint16_t *jdata, size_t words)
{
    uint16_t dt[NW_IRX_DT];
    size_t at = 1;
    int count = words ? (int16_t)jdata[0] : 0;

    for (int i = 0; i < count && at + 2 <= words; i++) {
        int f = (int16_t)jdata[at];
        int length = (int16_t)jdata[at + 1];
        at += 2;
        if (length < 0 || (size_t)length > words - at) {
            return;
        }
        switch (f) {
        default:
            memcpy(dt, jdata + at, (size_t)(length < NW_IRX_DT ? length : NW_IRX_DT) * sizeof(*dt));
            break;
        }
        at += (size_t)length;
        switch (f) {
        default: /* 8, >= 0x22: no handler */
            break;
        }
    }
}

void nw_nusound_rpc(unsigned fno, const void *send, size_t ssize, void *recv, size_t rsize)
{
    if (fno == 0 && send) {
        AdpcmSetParam(send, ssize / sizeof(uint16_t));
    }
    if (recv) {
        memcpy(recv, irx.ret, rsize < sizeof(irx.ret) ? rsize : sizeof(irx.ret));
    }
}
