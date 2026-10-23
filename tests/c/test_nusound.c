/* The NUSOUND.IRX port (src/audio/nusound.c) behind the SIF shim, driven by the decomp's own nusound.c, on a
   synthetic SFX.DAT and a clock the test sets. Usage: test_nusound <scratch dir>. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "audio/irx.h"
#include "audio/nusound.h"
#include "audio/spu2.h"
#include "nusound/nusound.h"
#include "platform/paths.h"
#include "vfs/vfs.h"

#include "vag_builder.h"

/* decomp nusound.c (no header declares them) */
int SOUND_InitIOP(void);
void SetIOPCommand(short command, short count);
void SetIOPCommandEx(short command, short size, void *data);
int FlushIOPCommand(int mode);
void SOUND_CopyIOPBuffer(void);
void SOUND_GetStatus(int request);
void SOUND_AddSamples(struct nusound_filename_info_s *sfx_info);
void SOUND_SetMaxStreamLimit(int limit);
void SOUND_SetSPUWriteAddress(unsigned int address);
void SOUND_AllocateStreamBuffer(int stream, unsigned int address, unsigned int size);
void SOUND_InitStreamData(void);
void TransferAllSpotFX(int first);
void SOUND_TransferSFX(int sample, int channel);
void SOUND_StoreLocalStart(void);
void NuSoundLocalSet(NUSOUND_FILENAME_INFO *sfx_info, int unused, int first);
int SOUND_PlaySFX(int sample, unsigned int channel, int left, int right, int pitch, int flags, int priority);
int SOUND_SetChannelVolume(unsigned int channel, int left, int right);
int SOUND_SetChannelPitch(unsigned int channel, int pitch);
int SOUND_StopSound(unsigned int channel);
int SOUND_PlayStream(int stream, int bank, unsigned int channel, int left, int right, int pitch, int start, int flags,
                     int priority);
void NuSoundUpdate(void);
extern int nusound_irx_loaded;
extern volatile int sbuff2[128];

static int failures;
static NwVfs vfs; /* the scratch disc with SFX.DAT */
static uint64_t test_us;
static bool test_auto; /* each RPC moves the clock on, for the EE's busy loops */
#define BIG_BLOCKS 4096
static uint8_t a_vag[NW_VAG_HEADER + 3 * NW_ADPCM_BLOCK], bl_vag[NW_VAG_HEADER + 4 * NW_ADPCM_BLOCK];
static uint8_t big_blocks[BIG_BLOCKS * NW_ADPCM_BLOCK], big_vag[NW_VAG_HEADER + sizeof(big_blocks)];
static size_t a_size, bl_size, big_size; /* sfx\generic\a.vag, sfx\music\bl.vag, sfx\music\big.vag */

static uint64_t test_clock(void)
{
    if (test_auto) {
        test_us += 2000;
    }
    return test_us;
}

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

static bool reply_is(int32_t value)
{
    for (int i = 0; i < 128; i++) {
        if (sbuff2[i] != value) {
            return false;
        }
    }
    return true;
}

static void test_transport(void)
{
    nw_nusound_init(NULL, NULL);

    /* Unbound, the call has no server and the reply stays. */
    memset((void *)sbuff2, 0x55, sizeof(sbuff2));
    SetIOPCommand(8, 0);
    CHECK(FlushIOPCommand(0) == 2);
    CHECK(reply_is(0x55555555));

    CHECK(SOUND_InitIOP() == 1);
    SetIOPCommand(8, 0);
    CHECK(FlushIOPCommand(0) == 2);
    CHECK(reply_is(0));
    CHECK(FlushIOPCommand(0) == -2); /* nothing queued */
}

static void test_batch(void)
{
    int32_t reply[4] = { -1, -1, -1, -1 };
    const uint16_t past_end[] = { 2, 8, 100 };     /* the data runs past the batch */
    const uint16_t negative[] = { 1, 8, 0xffff }; /* nwords -1 */

    nw_nusound_init(NULL, NULL);
    nw_nusound_rpc(0, past_end, sizeof(past_end), reply, 8);
    CHECK(reply[0] == 0 && reply[1] == 0 && reply[2] == -1);
    nw_nusound_rpc(0, negative, sizeof(negative), NULL, 0);
    nw_nusound_rpc(1, past_end, sizeof(past_end), reply, sizeof(reply));
    CHECK(reply[3] == 0);
}

/* GetStatus as the EE sends it (-1: no load request). */
static void status(int32_t ret[128])
{
    const uint16_t batch[] = { 1, 9, 1, 0xffff };
    nw_nusound_rpc(0, batch, sizeof(batch), ret, NW_NUSOUND_REPLY);
}

static void test_status(void)
{
    int32_t ret[128];

    nw_nusound_init(NULL, NULL);
    status(ret);
    CHECK(ret[24] == -1);
    for (int i = 0; i < 32; i++) {
        CHECK(i == 24 || ret[i] == 0);
    }

    /* Key status: channel j of a word at bits 30 - 2j; 3 is core 0 voice 3, 24 and 47 core 1 voices 0 and 23. */
    nw_spu2_set_switch(NW_SD_S_KON, 1u << 3);
    nw_spu2_set_switch(NW_SD_S_KON | 1, 1u << 0 | 1u << 23);
    status(ret);
    CHECK(ret[29] == 1 << 24 && ret[30] == 1 << 14 && ret[31] == 1);

    SOUND_GetStatus(-1);
    CHECK(FlushIOPCommand(0) == 3);
    SOUND_CopyIOPBuffer();
    CHECK(NuSoundKeyStatus(3) == 1 && NuSoundKeyStatus(24) == 1 && NuSoundKeyStatus(47) == 1);
    CHECK(NuSoundKeyStatus(4) == 0);

    /* The reply only changes on a GetStatus. */
    nw_spu2_set_switch(NW_SD_S_KOFF, 1u << 3);
    SetIOPCommand(8, 0);
    FlushIOPCommand(0);
    SOUND_CopyIOPBuffer();
    CHECK(NuSoundKeyStatus(3) == 1);
    SOUND_GetStatus(-1);
    FlushIOPCommand(0);
    SOUND_CopyIOPBuffer();
    CHECK(NuSoundKeyStatus(3) == 0 && NuSoundKeyStatus(24) == 1);
}

/* A string command as the EE queues it. */
static void send_string(short command, const char *s)
{
    SetIOPCommandEx(command, (short)(strlen(s) + 1), (void *)s);
    FlushIOPCommand(0);
}

static void test_init_spu(void)
{
    int32_t ret[128];

    nw_nusound_init(NULL, NULL);
    nw_spu2_set_switch(NW_SD_S_KON, 1u << 5);
    send_string(7, "cdrom0:\\");
    status(ret);
    CHECK(ret[29] == 0);
    for (int core = 0; core < 2; core++) {
        CHECK(nw_spu2_get_param((uint16_t)(core | NW_SD_P_MVOLL)) == 0x3fff);
        CHECK(nw_spu2_get_param((uint16_t)(core | NW_SD_P_MVOLR)) == 0x3fff);
    }
    CHECK(nw_spu2_get_core_attr(NW_SD_C_SPDIF_MODE) == 0x800);
    CHECK(strcmp(nw_nusound_irx()->nu_filepath, "cdrom0:\\") == 0 && strcmp(nw_nusound_irx()->nu_fileterm, ";1") == 0);
    send_string(7, "host0:");
    CHECK(nw_nusound_irx()->nu_fileterm[0] == '\0');
}

static void test_dat_path(void)
{
    const NwIrx *irx = nw_nusound_irx();

    nw_nusound_init(NULL, NULL);
    send_string(0x1e, "SFX.DAT");
    send_string(0x1f, "LOCAL.DAT");
    CHECK(strcmp(irx->new_global_dat_name, "SFX.DAT") == 0 && strcmp(irx->new_local_dat_name, "LOCAL.DAT") == 0);
    send_string(0x1f, "");
    CHECK(irx->new_local_dat_name[0] == '\0');
}

static void test_sample_info(void)
{
    const NwIrx *irx = nw_nusound_irx();
    struct nusound_filename_info_s info[14];

    memset(info, 0, sizeof(info));
    for (int i = 0; i < 13; i++) {
        snprintf(info[i].Filename, sizeof(info[i].Filename), "SFX\\GENERIC\\S%02d.VAG", i);
        info[i].Pitch = 22050;
        info[i].LoopInfo = i & 1;
        info[i].ID = 0x1000 + i;
    }
    nw_nusound_init(NULL, NULL);
    SOUND_AddSamples(info); /* 12 per batch */
    CHECK(irx->fileinfo_cnt == 13);
    CHECK(strcmp(irx->ADPCM_FileNames[12].Filename, "SFX\\GENERIC\\S12.VAG") == 0);
    CHECK(irx->ADPCM_FileNames[12].Pitch == 22050 && irx->ADPCM_FileNames[12].LoopInfo == 0);
    CHECK(irx->ADPCM_FileNames[12].ID == 0x100c && irx->ADPCM_FileNames[13].Filename[0] == '\0');

    /* At most 300; the rest are dropped. */
    uint16_t batch[1 + 30 * 24];
    batch[0] = 30;
    for (int i = 0; i < 30; i++) {
        batch[1 + 24 * i] = 0x1a;
        batch[2 + 24 * i] = 22;
        memcpy(&batch[3 + 24 * i], &info[0], sizeof(info[0]));
    }
    for (int i = 0; i < 10; i++) {
        nw_nusound_rpc(0, batch, sizeof(batch), NULL, 0);
    }
    CHECK(irx->fileinfo_cnt == 300 && irx->ADPCM_FileNames[299].ID == 0x1000);
}

static void test_max_streams(void)
{
    nw_nusound_init(NULL, NULL);
    SOUND_SetMaxStreamLimit(5);
    FlushIOPCommand(0);
    CHECK(nw_nusound_irx()->UserMaxStreams == 5);
    SOUND_SetMaxStreamLimit(48);
    FlushIOPCommand(0);
    CHECK(nw_nusound_irx()->UserMaxStreams == 5);
}

static void test_write_address(void)
{
    nw_nusound_init(NULL, NULL);
    SOUND_SetSPUWriteAddress(0x48310);
    FlushIOPCommand(0);
    CHECK(nw_nusound_irx()->StreamMemoryEnd == 0x48310);
}

static void test_stream_buffers(void)
{
    const STREAMDATA *sb = nw_nusound_irx()->StreamBuffer;

    nw_nusound_init(NULL, NULL);
    for (int i = 0; i < 5; i++) { /* init_adpcm's */
        SOUND_AllocateStreamBuffer(i, 0x5010 + i * 0xd700, 0xd700);
    }
    FlushIOPCommand(0);
    for (int i = 0; i < 5; i++) {
        CHECK(sb[i].IOPAddress && sb[i].AllocSize == 0xd700 && sb[i].AllocHalfSize == 0x6b80);
        CHECK(sb[i].AllocSPUAddress == 0x5010u + i * 0xd700u);
    }
    CHECK(!sb[5].IOPAddress);
    /* Kept while allocated, parameters too. */
    SOUND_AllocateStreamBuffer(0, 0x1000, 0x800);
    FlushIOPCommand(0);
    CHECK(sb[0].AllocSize == 0xd700 && sb[0].AllocSPUAddress == 0x5010);
}

static void test_init_stream_data(void)
{
    const NwIrx *irx = nw_nusound_irx();
    int32_t ret[128];
    struct nusound_filename_info_s info[] = {
        { "SFX\\GENERIC\\A.VAG", 22050, 1, 0x1000 },
        { "SFX\\MUSIC\\BL.VAG", 44100, 0, 1 },
        { "SFX\\GENERIC\\MISSING.VAG", 22050, 1, 0x1001 },
        { "SFX\\GENERIC\\README.TXT", 0, 1, 0x1002 }, /* Pitch 0: unused */
        { "", 0, 0, 0 },
    };

    nw_nusound_init(&vfs, test_clock);
    test_us = 1000000;
    send_string(0x1e, "SFX.DAT");
    send_string(7, "cdrom0:\\");
    SOUND_AddSamples(info);
    SOUND_InitStreamData();
    FlushIOPCommand(0);
    CHECK(irx->global_dat && irx->AnyFiles == 1 && irx->FilesOnCD == 2);
    const ADPCMFILE *f = irx->ADPCM_FileInfo;
    CHECK(f[0].size == a_size && f[0].lsn == 1 && f[0].ID == 0x1000 && f[0].Source == 1);
    CHECK(f[0].datfile == irx->global_dat);
    CHECK(f[1].size == bl_size && f[1].lsn == 2 && f[1].ID == 1);
    CHECK(f[2].size == 0 && f[3].size == 0);
    CHECK(irx->StreamBuffer[47].Channel == 47 && irx->StreamBuffer[47].iStatus == 3);
    CHECK(irx->StreamBuffer[47].Error == 0x8000);

    /* The stream thread ticks at 60 Hz from the call on, caught up at the next one. */
    int direction = irx->Direction;
    test_us += 3 * 16666;
    status(ret);
    CHECK(irx->Direction == (direction ^ 1));
    test_us += 16666;
    status(ret);
    CHECK(irx->Direction == direction);

    /* Without the DAT nothing is found. */
    nw_nusound_init(&vfs, test_clock);
    send_string(0x1e, "NONE.DAT");
    send_string(7, "cdrom0:\\");
    SOUND_AddSamples(info);
    SOUND_InitStreamData();
    FlushIOPCommand(0);
    CHECK(!irx->global_dat && irx->AnyFiles == 0 && irx->ADPCM_FileInfo[0].size == 0);
}

/* What init_adpcm sends, on the cdrom0: path, with info as the sample table. */
static void boot_module(struct nusound_filename_info_s *info)
{
    nw_nusound_init(&vfs, test_clock);
    test_us = 0;
    send_string(0x1e, "SFX.DAT");
    send_string(7, "cdrom0:\\");
    SOUND_AddSamples(info);
    SOUND_InitStreamData();
    for (int i = 0; i < 5; i++) {
        SOUND_AllocateStreamBuffer(i, 0x5010 + i * 0xd700, 0xd700);
    }
    SOUND_SetSPUWriteAddress(0x48310);
    SOUND_SetMaxStreamLimit(5);
    FlushIOPCommand(0);
}

static struct nusound_filename_info_s bank[] = {
    { "SFX\\GENERIC\\A.VAG", 22050, 1, 0x1000 },
    { "SFX\\GENERIC\\MISSING.VAG", 22050, 1, 0x1001 },
    { "SFX\\GENERIC\\A.VAG", 11025, 1, 0x1002 },
    { "SFX\\MUSIC\\BL.VAG", 44100, 0, 1 },
    { "", 0, 0, 0 },
};

static void test_transfer_sfx(void)
{
    const NwIrx *irx = nw_nusound_irx();
    const uint8_t *ram = nw_spu2_ram();
    int32_t ret[128];

    /* One transfer: the slot starts it on the next tick, the CD seeks, reads the one sector, then the DMA. */
    boot_module(bank);
    SOUND_TransferSFX(0x1000, 0);
    FlushIOPCommand(0);
    status(ret);
    CHECK(irx->ResStatus == 0 && ret[26] == 1);
    for (int tick = 1; tick <= 3; tick++) {
        test_us += 16666;
        status(ret);
        CHECK(ret[26] == (tick < 3));
    }
    CHECK(memcmp(ram + 0x48310, a_vag + NW_VAG_HEADER, a_size - NW_VAG_HEADER) == 0);
    CHECK(irx->StreamMemoryEnd == 0x48310 + a_size && !irx->SPOT_IOPAddress);

    /* The game's loop: loaded, missing (2), loaded again after the first with its header's size, then the end. */
    boot_module(bank);
    test_auto = true;
    TransferAllSpotFX(0);
    test_auto = false;
    CHECK(irx->ResStatus == 1 && irx->CD_SPOT_TRANS == 0);
    CHECK(irx->ADPCM_SpotFX[0].ID == 0x1000 && irx->ADPCM_SpotFX[0].SPUAddress == 0x48310);
    CHECK(irx->ADPCM_SpotFX[2].ID == 0x1002 && irx->ADPCM_SpotFX[2].SPUAddress == 0x48310 + a_size);
    CHECK(irx->ADPCM_SpotFX[3].ID == -1);
    CHECK(memcmp(ram + 0x48310 + a_size, a_vag + NW_VAG_HEADER, a_size - NW_VAG_HEADER) == 0);
    CHECK(irx->StreamMemoryEnd == 0x48310 + 2 * a_size);
    CHECK(irx->StreamBuffer[4].Status == 0);

    /* Without files the IRX answers 1 at once. */
    nw_nusound_init(&vfs, test_clock);
    SOUND_TransferSFX(0x1000, 0);
    FlushIOPCommand(0);
    status(ret);
    CHECK(ret[22] == 1 && ret[26] == 0);
}

/* NuSoundInit's end, then two levels' banks the way InitLocalSfx sets them. */
static void test_local_bank(void)
{
    const NwIrx *irx = nw_nusound_irx();
    const uint8_t *ram = nw_spu2_ram();
    struct nusound_filename_info_s level1[] = { { "SFX\\GENERIC\\A.VAG", 22050, 1, 0x1004 }, { "", 0, 0, 0 } };
    struct nusound_filename_info_s level2[] = { { "SFX\\MUSIC\\BL.VAG", 22050, 1, 0x1004 }, { "", 0, 0, 0 } };

    boot_module(bank);
    test_auto = true;
    TransferAllSpotFX(0);
    SOUND_StoreLocalStart();
    uint32_t global_end = 0x48310 + 2 * (uint32_t)a_size;
    CHECK(irx->GlobalSfxfileinfo_cnt == 4 && irx->GlobalSfxMemoryEnd == global_end);

    NuSoundLocalSet(level1, 1, 4);
    CHECK(irx->fileinfo_cnt == 5 && irx->ADPCM_FileInfo[4].size == a_size);
    CHECK(irx->ADPCM_SpotFX[4].ID == 0x1004 && irx->ADPCM_SpotFX[4].SPUAddress == global_end);
    CHECK(irx->StreamMemoryEnd == global_end + a_size);

    /* The next level's samples take the same place. */
    NuSoundLocalSet(level2, 1, 4);
    test_auto = false;
    CHECK(irx->fileinfo_cnt == 5 && irx->ADPCM_FileInfo[4].size == bl_size);
    CHECK(irx->StreamMemoryEnd == global_end + bl_size && irx->ADPCM_SpotFX[4].SPUAddress == global_end);
    CHECK(memcmp(ram + global_end, bl_vag + NW_VAG_HEADER, bl_size - NW_VAG_HEADER) == 0);
    CHECK(memcmp(ram + 0x48310, a_vag + NW_VAG_HEADER, a_size - NW_VAG_HEADER) == 0);
}

/* The global bank of `bank` in SPU RAM, as after NuSoundInit. */
static void boot_bank(void)
{
    boot_module(bank);
    test_auto = true;
    TransferAllSpotFX(0);
    test_auto = false;
}

/* Status as the EE sees it after its next flush. */
static void ee_status(void)
{
    SOUND_GetStatus(-1);
    FlushIOPCommand(0);
    SOUND_CopyIOPBuffer();
}

static void test_play_sfx(void)
{
    boot_bank();
    SOUND_PlaySFX(0x1002, 7, 0x3fff, 0x2000, 0x1000, 3, 0x14);
    SOUND_PlaySFX(0x1000, 30, 0x1000, 0x1000, 0x800, 0, 0); /* core 1 voice 6 */
    ee_status();
    CHECK(NuSoundKeyStatus(7) == 1 && NuSoundKeyStatus(30) == 1 && NuSoundKeyStatus(8) == 0);
    CHECK(nw_spu2_get_param(7 << 1 | NW_SD_VP_VOLL) == 0x3fff && nw_spu2_get_param(7 << 1 | NW_SD_VP_VOLR) == 0x2000);
    CHECK(nw_spu2_get_param(7 << 1 | NW_SD_VP_PITCH) == 0x1000 && nw_nusound_irx()->SPU_PausePitch[7] == 0x1000);
    CHECK(nw_spu2_get_param(7 << 1 | NW_SD_VP_ADSR1) == 0x03ff && nw_spu2_get_param(7 << 1 | NW_SD_VP_ADSR2) == 0x1fd4);
    CHECK(nw_spu2_get_addr(7 << 1 | NW_SD_VA_SSA) == 0x48310 + a_size);
    CHECK(nw_spu2_get_addr(7 << 1 | NW_SD_VA_NAX) == 0x48310 + a_size);
    CHECK(nw_spu2_get_addr(1 | 6 << 1 | NW_SD_VA_SSA) == 0x48310);

    /* An ID without a sample still keys on, from -1. */
    SOUND_PlaySFX(0x1100, 9, 0x3fff, 0x3fff, 0x1000, 0, 0);
    ee_status();
    CHECK(NuSoundKeyStatus(9) == 1 && nw_spu2_get_addr(9 << 1 | NW_SD_VA_SSA) == 0xffffffffu);
}

static void test_channel_volume(void)
{
    nw_nusound_init(&vfs, test_clock);
    SOUND_SetChannelVolume(5, 0x1234, 0x0567);
    SOUND_SetChannelVolume(40, 0x7fff, 0); /* core 1 voice 16 */
    FlushIOPCommand(0);
    CHECK(nw_spu2_get_param(5 << 1 | NW_SD_VP_VOLL) == 0x1234 && nw_spu2_get_param(5 << 1 | NW_SD_VP_VOLR) == 0x0567);
    CHECK(nw_spu2_get_param(1 | 16 << 1 | NW_SD_VP_VOLL) == 0x7fff);
    CHECK(nw_spu2_get_param(1 | 16 << 1 | NW_SD_VP_VOLR) == 0);
}

static void test_channel_pitch(void)
{
    nw_nusound_init(&vfs, test_clock);
    SOUND_SetChannelPitch(5, 0x0759);
    SOUND_SetChannelPitch(47, 0x3fff); /* core 1 voice 23 */
    FlushIOPCommand(0);
    CHECK(nw_spu2_get_param(5 << 1 | NW_SD_VP_PITCH) == 0x0759 && nw_nusound_irx()->SPU_PausePitch[5] == 0x0759);
    CHECK(nw_spu2_get_param(1 | 23 << 1 | NW_SD_VP_PITCH) == 0x3fff && nw_nusound_irx()->SPU_PausePitch[47] == 0x3fff);
}

static void test_stop_sound(void)
{
    boot_bank();
    SOUND_PlaySFX(0x1000, 7, 0x3fff, 0x3fff, 0x1000, 0, 0);
    SOUND_PlaySFX(0x1000, 31, 0x3fff, 0x3fff, 0x1000, 0, 0);
    ee_status();
    CHECK(NuSoundKeyStatus(7) == 1 && NuSoundKeyStatus(31) == 1);
    SOUND_StopSound(31);
    ee_status();
    CHECK(NuSoundKeyStatus(7) == 1 && NuSoundKeyStatus(31) == 0);
    CHECK(nw_spu2_get_param(1 | 7 << 1 | NW_SD_VP_ADSR2) == 0x1fc0);
}

static struct nusound_filename_info_s streams[] = {
    { "SFX\\GENERIC\\A.VAG", 22050, 1, 2 }, /* a one-shot */
    { "SFX\\MUSIC\\BL.VAG", 44100, 0, 1 },  /* a loop */
    { "SFX\\MUSIC\\BIG.VAG", 44100, 1, 3 }, /* a one-shot longer than the buffer */
    { "", 0, 0, 0 },
};

/* A game frame: a vblank of time, then NuSoundUpdate, which answers the module's load request. */
static void frame(void)
{
    test_us += 20000;
    NuSoundUpdate();
}

static void test_play_stream(void)
{
    const NwIrx *irx = nw_nusound_irx();
    const STREAMDATA *s = &irx->StreamBuffer[4];
    const uint32_t spu = 0x5010 + 4 * 0xd700;
    int frames = 0;
    bool starting = false;

    nusound_irx_loaded = 1;
    boot_module(streams);
    /* NuSoundPlayChan's stream on channel 4 (a cut's audio): bank 4, the file's rate (pitch 1), its LoopInfo (2). */
    SOUND_PlayStream(2, 4, 4, 0x3fff, 0x2000, 1, 2, 0, 0x10);
    for (; frames < 50 && NuSoundKeyStatus(4) != 1; frames++) {
        frame();
        starting |= NuSoundKeyStatus(4) == 2;
    }
    CHECK(NuSoundKeyStatus(4) == 1 && starting && frames == 8); /* two handshakes, the CD's steps */
    CHECK(s->Status == 1 && s->iStatus == 0 && s->PlayingHalf == 3 && s->LoopInfo == 2);
    CHECK(s->ID == 2 && s->Channel == 4 && s->Pitch == 0x759 && s->SPUAddress == spu);
    CHECK(nw_spu2_get_addr(4 << 1 | NW_SD_VA_SSA) == spu && nw_spu2_get_param(4 << 1 | NW_SD_VP_PITCH) == 0x759);
    CHECK(nw_spu2_get_param(4 << 1 | NW_SD_VP_VOLR) == 0x2000 && nw_spu2_get_param(4 << 1 | NW_SD_VP_ADSR2) == 0x1fd0);
    CHECK(memcmp(nw_spu2_ram() + spu, a_vag + NW_VAG_HEADER, a_size - NW_VAG_HEADER) == 0);

    /* Without time in the SPU it plays on. */
    for (int i = 0; i < 20; i++) {
        frame();
    }
    CHECK(NuSoundKeyStatus(4) == 1 && s->Status == 1);

    /* A stream longer than its buffer, on slot 0: the whole buffer as a ring in SPU RAM, then the rest (less than
       a half) read ahead with the end's flags set, waiting for the voice to leave the first half. */
    const STREAMDATA *s0 = &irx->StreamBuffer[0];
    const uint8_t *ram = nw_spu2_ram() + 0x5010;
    SOUND_PlayStream(3, 0, 0, 0x3fff, 0, 1, 2, 0, 0x10);
    for (frames = 0; frames < 50 && NuSoundKeyStatus(0) != 1; frames++) {
        frame();
    }
    CHECK(frames == 12 && s0->iStatus == 1); /* keyed on, reading ahead */
    for (; frames < 50 && s0->iStatus != 0; frames++) {
        frame();
    }
    CHECK(frames == 14 && NuSoundKeyStatus(0) == 1 && s0->PlayingHalf == 0 && s0->LoopInfo == 2);
    CHECK(s0->Pitch == 0xeb3 && s0->FileSeek == 0x30 + 0xd700);
    CHECK(ram[1] == (NW_ADPCM_START | NW_ADPCM_REPEAT) && ram[0x11] == NW_ADPCM_REPEAT);
    CHECK(ram[0xd700 - 0xf] == (NW_ADPCM_END | NW_ADPCM_REPEAT));
    CHECK(memcmp(ram + 0x20, big_blocks + 0x20, 0xd700 - 0x30) == 0);
    CHECK(memcmp(s0->IOPAddress, big_blocks + 0xd700, sizeof(big_blocks) - 0xd700) == 0);
    nusound_irx_loaded = 0;
}

/* sfx\generic\a.vag: a one-shot of 3 blocks; sfx\music\bl.vag: a loop of 4; a text file; sfx\music\big.vag: a
   one-shot of BIG_BLOCKS, more than a stream buffer and a half, each block numbered in its nibbles. */
static bool setup_disc(const char *scratch)
{
    static const char text[] = "a text file in SFX.DAT, longer than a VAG header of 48 bytes";
    uint8_t blocks[4][NW_ADPCM_BLOCK];
    char dir[1024], path[1100];

    for (uint32_t b = 0; b < BIG_BLOCKS; b++) {
        uint8_t *p = big_blocks + (size_t)b * NW_ADPCM_BLOCK;
        unsigned flags = b == BIG_BLOCKS - 2 ? NW_ADPCM_END : b == BIG_BLOCKS - 1 ? 7 : 0;
        vag_block_fill(p, 0, 0, flags, 0);
        memcpy(p + 2, &b, sizeof(b));
    }
    big_size = vag_file(big_vag, 44100, big_blocks, BIG_BLOCKS);

    vag_block_fill(blocks[0], 0, 0, 0, 1);
    vag_block_fill(blocks[1], 1, 12, NW_ADPCM_END, 0);
    vag_block_fill(blocks[2], 0, 0, 7, 0);
    a_size = vag_file(a_vag, 22050, blocks[0], 3);
    vag_block_fill(blocks[1], 2, 3, NW_ADPCM_START | NW_ADPCM_REPEAT, -5);
    vag_block_fill(blocks[2], 3, 5, NW_ADPCM_REPEAT, 3);
    vag_block_fill(blocks[3], 4, 8, NW_ADPCM_END | NW_ADPCM_REPEAT, -2);
    bl_size = vag_file(bl_vag, 44100, blocks[0], 4);
    const uint8_t *const files[SFX_DAT_FILES] = { a_vag, bl_vag, (const uint8_t *)text, big_vag };
    const size_t sizes[SFX_DAT_FILES] = { a_size, bl_size, sizeof(text) - 1, big_size };
    uint8_t *arc = SDL_malloc(sfx_dat_size(sizes));
    size_t size = arc ? sfx_dat(arc, files, sizes) : 0;

    SDL_snprintf(dir, sizeof(dir), "%s/disc", scratch);
    SDL_snprintf(path, sizeof(path), "%s/SFX.DAT", dir);
    bool ok = arc && SDL_CreateDirectory(dir) && SDL_SaveFile(path, arc, size) && nw_vfs_mount(&vfs, NW_VFS_DISC, dir);
    SDL_free(arc);
    return ok;
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "usage: test_nusound <scratch dir>\n");
        return 2;
    }
    if (!nw_paths_remove_tree(argv[1]) || !SDL_CreateDirectory(argv[1]) || !setup_disc(argv[1])) {
        fprintf(stderr, "%s: %s\n", argv[1], SDL_GetError());
        return 1;
    }
    test_transport();
    test_batch();
    test_status();
    test_init_spu();
    test_dat_path();
    test_sample_info();
    test_max_streams();
    test_write_address();
    test_stream_buffers();
    test_init_stream_data();
    test_transfer_sfx();
    test_local_bank();
    test_play_sfx();
    test_channel_volume();
    test_channel_pitch();
    test_stop_sound();
    test_play_stream();
    nw_nusound_init(NULL, NULL);
    nw_vfs_shutdown(&vfs);
    nw_paths_remove_tree(argv[1]);
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("nusound: all checks passed\n");
    return 0;
}
