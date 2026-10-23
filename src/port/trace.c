#include "port/trace.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define NW_TRACE_CHANNELS 6

/* decomp nusound.c, ps2video.c and cut.c (cutworldix is D_00633388) */
extern volatile unsigned char SOUND_SPUKeyStatus[48];
extern volatile int nuvideo_global_vbcnt;
extern int cutmovie;
extern int D_00633388;
extern int cutmovie_hold;
extern int cut_on;

static struct {
    bool on;
    bool synced;
    int hold;
    int base;
    char last[96];
} nw_trace;

void nw_trace_sound_start(void)
{
    nw_trace.on = true;
}

void nw_trace_sound_vblank(void)
{
    if (!nw_trace.on) {
        return;
    }
    int vblank = nuvideo_global_vbcnt;
    if (!nw_trace.synced) {
        nw_trace.synced = cutmovie_hold == 1 && nw_trace.hold == 0 && cutmovie == 0;
        nw_trace.hold = cutmovie_hold;
        nw_trace.base = vblank;
        if (!nw_trace.synced) {
            return;
        }
    }
    char key[NW_TRACE_CHANNELS + 1], state[sizeof(nw_trace.last)];
    for (int i = 0; i < NW_TRACE_CHANNELS; i++) {
        key[i] = (char)('0' + SOUND_SPUKeyStatus[i] % 10);
    }
    key[NW_TRACE_CHANNELS] = '\0';
    snprintf(state, sizeof(state), "key %s movie %d world %d hold %d on %d", key, cutmovie, D_00633388, cutmovie_hold,
             cut_on);
    if (strcmp(state, nw_trace.last) != 0) {
        printf("vblank %d: %s\n", vblank - nw_trace.base, state);
        fflush(stdout);
        strcpy(nw_trace.last, state);
    }
}
