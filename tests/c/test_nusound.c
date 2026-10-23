/* The NUSOUND.IRX port (src/audio/nusound.c) behind the SIF shim, driven by the decomp's own nusound.c.
   Usage: test_nusound. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "audio/nusound.h"
#include "audio/spu2.h"
#include "nusound/nusound.h"

/* decomp nusound.c (no header declares them) */
int SOUND_InitIOP(void);
void SetIOPCommand(short command, short count);
int FlushIOPCommand(int mode);
void SOUND_CopyIOPBuffer(void);
void SOUND_GetStatus(int request);
extern volatile int sbuff2[128];

static int failures;

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
    nw_nusound_init();

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

    nw_nusound_init();
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

    nw_nusound_init();
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

int main(void)
{
    test_transport();
    test_batch();
    test_status();
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("nusound: all checks passed\n");
    return 0;
}
