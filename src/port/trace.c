#include "port/trace.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "gamelib/gcutscn.h"
#include "nu3d/nutexanm.h"
#include "port/level.h"

#define NW_TRACE_CHANNELS 6

/* decomp nusound.c, ps2video.c and cut.c */
extern volatile unsigned char SOUND_SPUKeyStatus[48];
extern volatile int nuvideo_global_vbcnt;
extern int cutmovie;
extern int cutworldix;
extern int cutmovie_hold;
extern int cut_on;
extern int SOUND_IOP_REQUEST_LOAD;
extern int SOUND_CD_STATUS;
extern int SOUND_STREAM_CD_ACCESS;
extern struct instNUGCUTSCENE_s *CutInst[];

/* nutexanm.c's NuTexAnimList */
struct nw_ntal {
    struct nutexanim_s *anims;
    struct nw_ntal *next;
    struct nw_ntal *prev;
};
extern struct nw_ntal *ntal_first;

static struct {
    bool on;
    bool synced;
    int hold;
    int base;
    char last[96];
} nw_trace;

/* The frame the title logo's flipbook (title.ats) shows; -1 before it loads. */
static int nw_trace_tex(void)
{
    struct nutexanimprog_s *title = NuTexAnimProgFind("title");
    for (struct nw_ntal *list = ntal_first; title && list; list = list->next) {
        for (struct nutexanim_s *anim = list->anims; anim; anim = anim->succ) {
            if (anim->env && anim->env->prog == title) {
                return anim->env->tex_ix;
            }
        }
    }
    return -1;
}

void nw_trace_sound_start(void)
{
    nw_trace.on = true;
}

void nw_trace_sound_vblank(void)
{
    int vblank = nuvideo_global_vbcnt;
    if (!nw_trace.synced) {
        bool held = cutmovie_hold == 1 && nw_trace.hold == 0 && cutmovie == 0;
        nw_trace.synced = nw_level_arrival() >= 0 ? nw_level_arrived() : held;
        nw_trace.hold = cutmovie_hold;
        nw_trace.base = vblank;
        if (!nw_trace.synced) {
            return;
        }
    }
    if (!nw_trace.on) {
        return;
    }
    char key[NW_TRACE_CHANNELS + 1], state[sizeof(nw_trace.last)];
    for (int i = 0; i < NW_TRACE_CHANNELS; i++) {
        key[i] = (char)('0' + SOUND_SPUKeyStatus[i] % 10);
    }
    key[NW_TRACE_CHANNELS] = '\0';
    snprintf(state, sizeof(state), "key %s movie %d world %d hold %d on %d req %d busy %d cd %d tex %d", key,
             cutmovie, cutworldix, cutmovie_hold, cut_on, SOUND_IOP_REQUEST_LOAD, SOUND_CD_STATUS,
             SOUND_STREAM_CD_ACCESS, nw_trace_tex());
    if (strcmp(state, nw_trace.last) != 0) {
        printf("vblank %d: %s\n", vblank - nw_trace.base, state);
        fflush(stdout);
        strcpy(nw_trace.last, state);
    }
}

bool nw_trace_vblank(int vbcnt, int *vblank)
{
    *vblank = vbcnt - nw_trace.base;
    return nw_trace.synced;
}

void nw_trace_frame(int vblank)
{
    const struct instNUGCUTSCENE_s *cut = CutInst[cutworldix];
    printf("frame %d: world %d time %.2f tex %d\n", vblank, cutworldix, cut ? cut->time : -1.0f, nw_trace_tex());
    fflush(stdout);
}
