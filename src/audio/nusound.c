#include "audio/nusound.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio/irx.h"
#include "audio/spu2.h"

#define NW_IRX_DT 256     /* AdpcmSetParam's dt[] */
#define NW_IRX_REPORTED 4 /* streams in the reply */
#define NW_IRX_NO_REQUEST 0xffff
#define NW_IRX_TICK_US 16666 /* the stream thread's hard timer */
#define NW_IRX_SECTOR 2048
#define NW_IRX_PATH 600

static NwIrx irx;

/* The port's side: the file system, the clock and how far the stream thread's ticks got. */
static struct {
    const NwVfs *vfs;
    uint64_t (*now_us)(void);
    bool timer;
    uint64_t timer_us;
    uint64_t ticks;
    NwDat global_dat;
    NwDat local_dat;
} iop;

/* A string command's data, NUL-terminated within its words (the IRX strcpys it whole). */
static void nw_irx_string(char *dst, size_t size, const uint16_t *p, int length)
{
    size_t n = (size_t)length * sizeof(*p);
    const char *s = (const char *)p;
    size_t len = strnlen(s, n < size - 1 ? n : size - 1);
    memcpy(dst, s, len);
    dst[len] = '\0';
}

void nw_nusound_init(const NwVfs *vfs, uint64_t (*now_us)(void))
{
    for (int i = 0; i < NW_IRX_CHANNELS; i++) {
        free(irx.StreamBuffer[i].IOPAddress);
    }
    if (irx.global_dat) {
        nw_dat_close(irx.global_dat);
    }
    if (irx.local_dat) {
        nw_dat_close(irx.local_dat);
    }
    memset(&irx, 0, sizeof(irx));
    memset(&iop, 0, sizeof(iop));
    iop.vfs = vfs;
    iop.now_us = now_us;
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

/* A stream's IOP buffer, kept (parameters included) while one is allocated. */
static void ADPCM_AllocIOPMemory(unsigned i, uint32_t spuaddr, int size)
{
    if (i >= NW_IRX_CHANNELS) {
        return;
    }
    STREAMDATA *s = &irx.StreamBuffer[i];
    if (!s->IOPAddress) {
        s->IOPAddress = malloc(size > 0 ? (size_t)size : 1);
        s->AllocSize = (uint32_t)size;
        s->AllocHalfSize = (uint32_t)(size / 2);
        s->AllocSPUAddress = spuaddr;
    }
}

/* The IRX also sprintf's each name over a .rodata string, dropped here. */
static void SoundAddSample(const uint16_t *p, int length)
{
    if (irx.fileinfo_cnt < NW_IRX_FILES) {
        NwIrxFilename *fni = &irx.ADPCM_FileNames[irx.fileinfo_cnt++];
        size_t n = (size_t)length * sizeof(*p);
        memset(fni, 0, sizeof(*fni));
        memcpy(fni, p, n < sizeof(*fni) ? n : sizeof(*fni));
    }
}

/* SetGlobalDat / SetLocalDat: reopened only for another name; a DAT that fails to open is tried again next time. */
static void nw_irx_set_dat(NwDat **dat, NwDat *store, char *dat_name, const char *dfname)
{
    char path[NW_IRX_PATH];

    if (*dat) {
        if (strcmp(dfname, dat_name) == 0) {
            return;
        }
        nw_dat_close(*dat);
        *dat = NULL;
    }
    strcpy(dat_name, dfname);
    if (dat_name[0] && iop.vfs) {
        snprintf(path, sizeof(path), "%s%s%s", irx.nu_filepath, dat_name, irx.nu_fileterm);
        if (nw_dat_open(store, iop.vfs, path)) {
            *dat = store;
        }
    }
}

static void SetGlobalDat(const char *dfname)
{
    nw_irx_set_dat(&irx.global_dat, &iop.global_dat, irx.global_dat_name, dfname);
}

static void SetLocalDat(const char *dfname)
{
    nw_irx_set_dat(&irx.local_dat, &iop.local_dat, irx.local_dat_name, dfname);
}

/* Finds the samples from `start` on in the local DAT (from 0: the global one); a Pitch of 0 marks an unused entry.
   Loose files (the IRX's sceCdSearchFile and host0: open) are not looked up: the disc keeps every sample in
   SFX.DAT. */
static void InitADPCMFiles(int start)
{
    NwDat *current_dat;
    char fn[NW_IRX_PATH];

    if (start) {
        SetLocalDat(irx.new_local_dat_name);
        current_dat = irx.local_dat;
        if (!current_dat) {
            SetGlobalDat(irx.new_global_dat_name);
            current_dat = irx.global_dat;
        }
    } else {
        SetGlobalDat(irx.new_global_dat_name);
        current_dat = irx.global_dat;
    }
    for (int i = start; i < NW_IRX_FILES; i++) {
        const NwIrxFilename *n = &irx.ADPCM_FileNames[i];
        ADPCMFILE *f = &irx.ADPCM_FileInfo[i];
        if (n->ID == -1) {
            break;
        }
        if (n->Pitch == 0) {
            continue;
        }
        snprintf(fn, sizeof(fn), "%s%s%s", irx.nu_filepath, n->Filename, irx.nu_fileterm);
        bool cd = strncmp(fn, "cdrom0:", 7) == 0;
        int ix = current_dat ? nw_dat_find(current_dat, n->Filename) : -1;
        if (ix < 0 || (!cd && strncmp(fn, "host0:", 6) != 0)) {
            f->size = 0;
            continue;
        }
        const NwDatEntry *e = &current_dat->entries[ix];
        irx.FilesOnCD += cd;
        f->lsn = cd ? e->offset / NW_IRX_SECTOR : e->offset;
        f->size = e->size;
        f->ID = (int16_t)n->ID;
        f->Source = cd;
        f->datfile = current_dat;
        irx.AnyFiles = 1;
    }
}

/* The stream table, the files, then the stream thread on its 60 Hz timer. The IRX drops the slots' buffers
   without freeing them, and a second call starts a second timer; the game sends this once, before the buffers. */
static void InitStreamData(void)
{
    irx.FilesOnCD = 0;
    irx.CD_SPOT_TRANS = 0;
    irx.StereoStarted = irx.StereoChan1 = irx.StereoChan2 = irx.StereoPitch = 0;
    for (int i = 0; i < NW_IRX_CHANNELS; i++) {
        STREAMDATA *s = &irx.StreamBuffer[i];
        s->Error = 0x8000;
        s->Status = 0;
        s->FileHandle = 0;
        s->Channel = (uint8_t)i;
        s->VolL = s->VolR = 0;
        free(s->IOPAddress);
        s->IOPAddress = NULL;
        s->PlayingHalf = 0;
        s->iStatus = 3;
    }
    InitADPCMFiles(0);
    if (!iop.timer) {
        iop.timer = true;
        iop.timer_us = iop.now_us ? iop.now_us() : 0;
        iop.ticks = 0;
    }
}

/* The IRX reads NAX until two reads agree, as it moves while the voice plays. */
static int GetNAX(int core, int ch)
{
    return (int)nw_spu2_get_addr((uint16_t)(core | ch << 1 | NW_SD_VA_NAX));
}

/* One tick of the stream thread. Its CD-not-ready handling (sceCdDiskReady 6 pauses the streams) is left out: the
   disc is always ready here. */
static void StreamADPCM(void)
{
    for (int i = 0; i < NW_IRX_CHANNELS; i++) {
        STREAMDATA *s = &irx.StreamBuffer[i];
        if (s->Status == 1) {
            int core = s->Channel >= NW_SPU2_VOICES, ch = s->Channel - NW_SPU2_VOICES * core;
            s->SPUCurrent = GetNAX(core, ch);
            s->KeyStat = nw_spu2_get_param((uint16_t)(core | ch << 1 | NW_SD_VP_ENVX));
        }
    }
    irx.Direction ^= 1;
}

/* The ticks due since the timer started, all before the call they precede. */
static void nw_iop_catch_up(void)
{
    if (!iop.timer || !iop.now_us) {
        return;
    }
    uint64_t now = iop.now_us();
    if (now < iop.timer_us) {
        return;
    }
    uint64_t due = (now - iop.timer_us) / NW_IRX_TICK_US;
    while (iop.ticks < due) {
        iop.ticks++;
        StreamADPCM();
    }
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
        case 0x1a:
            SoundAddSample(p, length);
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
        case 0x10:
            if (dt[0] < NW_IRX_CHANNELS) {
                irx.UserMaxStreams = dt[0];
            }
            break;
        case 0xe:
            irx.StreamMemoryEnd = (uint32_t)dt[0] << 16 | dt[1];
            break;
        case 0xd:
            ADPCM_AllocIOPMemory(dt[0], (uint32_t)dt[1] << 16 | dt[2], (int)((uint32_t)dt[3] << 16 | dt[4]));
            break;
        case 0xc:
            InitStreamData();
            break;
        default: /* 8, >= 0x22: no handler */
            break;
        }
    }
}

void nw_nusound_rpc(unsigned fno, const void *send, size_t ssize, void *recv, size_t rsize)
{
    nw_iop_catch_up();
    if (fno == 0 && send) {
        AdpcmSetParam(send, ssize / sizeof(uint16_t));
    }
    if (recv) {
        memcpy(recv, irx.ret, rsize < sizeof(irx.ret) ? rsize : sizeof(irx.ret));
    }
}
