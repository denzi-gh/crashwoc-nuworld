#include "audio/nusound.h"

#include <stdint.h>
#include <string.h>

#include "audio/irx.h"
#include "audio/spu2.h"

#define NW_IRX_DT 256     /* AdpcmSetParam's dt[] */
#define NW_IRX_REPORTED 4 /* streams in the reply */
#define NW_IRX_NO_REQUEST 0xffff

static NwIrx irx;

/* A string command's data, NUL-terminated within its words (the IRX strcpys it whole). */
static void nw_irx_string(char *dst, size_t size, const uint16_t *p, int length)
{
    size_t n = (size_t)length * sizeof(*p);
    const char *s = (const char *)p;
    size_t len = strnlen(s, n < size - 1 ? n : size - 1);
    memcpy(dst, s, len);
    dst[len] = '\0';
}

void nw_nusound_init(void)
{
    memset(&irx, 0, sizeof(irx));
    irx.IOPRequestLoad = -1;
    irx.LoadFromEE = NW_IRX_NO_REQUEST;
    nw_spu2_init();
}

const NwIrx *nw_nusound_irx(void)
{
    return &irx;
}

/* 2 (a stream started, not keyed on yet) stays; otherwise whether the envelope is up. The IRX reads ENDX too, but
   it decides nothing. */
static void GetChannelStatus(void)
{
    for (int i = 0; i < NW_IRX_CHANNELS; i++) {
        int core = i >= NW_SPU2_VOICES, voice = i - NW_SPU2_VOICES * core;
        if (irx.SPU_KeyStatus[i] != 2) {
            irx.SPU_KeyStatus[i] = nw_spu2_get_param((uint16_t)(core | voice << 1 | NW_SD_VP_ENVX)) != 0;
        }
    }
}

static void AdpcmGetStatus(void)
{
    int32_t *ret = irx.ret;

    ret[0] = 0;
    for (int a = 0; a < NW_IRX_REPORTED; a++) {
        if (irx.cd_stream == a && (irx.cd_stage == 1 || irx.cd_stage == 2)) {
            ret[0] += irx.cd_stage << 2 * a;
        }
    }
    ret[1] = 0;
    for (int a = 0; a < NW_IRX_REPORTED; a++) {
        const STREAMDATA *s = &irx.StreamBuffer[a];
        ret[1] += s->Status << a;
        ret[2 + a] = s->Channel;
        ret[6 + a] = s->ID;
        ret[10 + a] = s->SPUCurrent;
        ret[14 + a] = s->PlayingHalf;
        ret[18 + a] = s->KeyStat;
    }
    ret[22] = irx.ResStatus;
    ret[23] = irx.StreamBusy;
    ret[24] = irx.IOPRequestLoad;
    ret[25] = irx.LoadEECounter;
    ret[26] = irx.CD_SPOT_TRANS;
    ret[27] = irx.StreamShutDown;
    ret[28] = irx.StereoStarted;
    for (int a = 0; a < 3; a++) {
        uint32_t c = 0;
        for (int b = 0; b < 16; b++) {
            c = c << 2 | irx.SPU_KeyStatus[16 * a + b];
        }
        ret[29 + a] = (int32_t)c;
    }
}

static void AdpcmSetMasterVolume(int core, uint16_t vol)
{
    nw_spu2_set_param((uint16_t)(core | NW_SD_P_MVOLL), vol);
    nw_spu2_set_param((uint16_t)(core | NW_SD_P_MVOLR), vol);
}

/* The EE's current path (NuFileGetCurrentPath), where the DATs open; each call resets the SPU. */
static void AdpcmSdInit(const uint16_t *p, int length)
{
    nw_irx_string(irx.nu_filepath, sizeof(irx.nu_filepath), p, length);
    strcpy(irx.nu_fileterm, strncmp(irx.nu_filepath, "cdrom0:", 7) == 0 ? ";1" : "");
    nw_spu2_init();
    nw_spu2_set_core_attr(NW_SD_C_SPDIF_MODE, 0x800);
    AdpcmSetMasterVolume(0, 0x3fff);
    AdpcmSetMasterVolume(1, 0x3fff);
    irx.g_ReverbChans[0] = irx.g_ReverbChans[1] = 0;
}

/* count, then per command {cmd, nwords, data[nwords]}, all s16. The commands with a string or a struct take the
   data in place, the others a copy in dt[] read zero-extended. The IRX trusts the counts; here a batch that runs
   past the buffer ends. */
static void AdpcmSetParam(const uint16_t *jdata, size_t words)
{
    uint16_t dt[NW_IRX_DT] = { 0 };
    size_t at = 1;
    int count = words ? (int16_t)jdata[0] : 0;

    for (int i = 0; i < count && at + 2 <= words; i++) {
        int f = (int16_t)jdata[at];
        int length = (int16_t)jdata[at + 1];
        at += 2;
        if (length < 0 || (size_t)length > words - at) {
            return;
        }
        const uint16_t *p = jdata + at;
        switch (f) {
        case 7:
            AdpcmSdInit(p, length);
            break;
        case 0x1e: /* opened when the files are looked up */
            nw_irx_string(irx.new_global_dat_name, sizeof(irx.new_global_dat_name), p, length);
            break;
        case 0x1f:
            nw_irx_string(irx.new_local_dat_name, sizeof(irx.new_local_dat_name), p, length);
            break;
        default:
            memcpy(dt, p, (size_t)(length < NW_IRX_DT ? length : NW_IRX_DT) * sizeof(*dt));
            break;
        }
        at += (size_t)length;
        switch (f) {
        case 9:
            irx.LoadFromEE = dt[0];
            GetChannelStatus();
            AdpcmGetStatus();
            break;
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
