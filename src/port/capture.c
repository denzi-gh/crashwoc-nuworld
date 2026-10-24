#include "port/capture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "port/trace.h"

#define NW_CAPTURE_MAX 512
#define NW_CAPTURE_WAIT_MS 10000 /* a renderer that does not save a frame in time */

extern volatile int nuvideo_global_vbcnt;

static struct {
    int vblanks[NW_CAPTURE_MAX];
    int count;
    SDL_Mutex *lock;
    SDL_Condition *saved;
    uint64_t seq; /* under lock: the packet waited on */
    int vblank;
    SDL_AtomicInt finished;
} nw_cap;

static int nw_capture_cmp(const void *a, const void *b)
{
    return *(const int *)a - *(const int *)b;
}

bool nw_capture_start(const char *list)
{
    size_t size = 0;
    char *text = SDL_LoadFile(list, &size);
    const char *s = text ? text : list;
    while (*s && nw_cap.count < NW_CAPTURE_MAX) {
        char *end;
        if (text && SDL_strncmp(s, "frame ", 6) != 0) {
            s += strcspn(s, "\n");
            s += *s == '\n';
            continue;
        }
        long v = SDL_strtol(text ? s + 6 : s, &end, 10);
        if (end == (text ? s + 6 : s)) {
            break;
        }
        nw_cap.vblanks[nw_cap.count++] = (int)v;
        s = text ? end + strcspn(end, "\n") : end + (*end == ',');
    }
    SDL_free(text);
    if (!nw_cap.count) {
        return false;
    }
    qsort(nw_cap.vblanks, (size_t)nw_cap.count, sizeof(int), nw_capture_cmp);
    nw_cap.lock = SDL_CreateMutex();
    nw_cap.saved = SDL_CreateCondition();
    return nw_cap.lock && nw_cap.saved;
}

static bool nw_capture_near(int vblank)
{
    for (int i = 0; i < nw_cap.count; i++) {
        if (vblank >= nw_cap.vblanks[i] - NW_CAPTURE_BEFORE && vblank <= nw_cap.vblanks[i] + NW_CAPTURE_AFTER) {
            return true;
        }
    }
    return false;
}

void nw_capture_vblank(void)
{
    int vblank;
    if (!nw_cap.count || !nw_trace_vblank(nuvideo_global_vbcnt, &vblank)) {
        return;
    }
    for (int i = 0; i < nw_cap.count; i++) {
        if (nw_cap.vblanks[i] == vblank && (i == 0 || nw_cap.vblanks[i - 1] != vblank)) {
            nw_trace_frame(vblank);
        }
    }
    if (vblank > nw_cap.vblanks[nw_cap.count - 1] + NW_CAPTURE_AFTER) {
        SDL_SetAtomicInt(&nw_cap.finished, 1);
    }
}

void nw_capture_published(uint64_t seq, int vbcnt)
{
    int vblank;
    if (!nw_cap.count || !nw_trace_vblank(vbcnt, &vblank) || !nw_capture_near(vblank)) {
        return;
    }
    SDL_LockMutex(nw_cap.lock);
    nw_cap.seq = seq;
    nw_cap.vblank = vblank;
    uint64_t start = SDL_GetTicks();
    while (nw_cap.seq == seq && SDL_GetTicks() - start < NW_CAPTURE_WAIT_MS) {
        SDL_WaitConditionTimeout(nw_cap.saved, nw_cap.lock, 100);
    }
    if (nw_cap.seq == seq) {
        fprintf(stderr, "capture: the frame of vblank %d was not saved\n", vblank);
        nw_cap.seq = 0;
    }
    SDL_UnlockMutex(nw_cap.lock);
}

uint64_t nw_capture_wanted(int *vblank)
{
    if (!nw_cap.count) {
        return 0;
    }
    SDL_LockMutex(nw_cap.lock);
    uint64_t seq = nw_cap.seq;
    *vblank = nw_cap.vblank;
    SDL_UnlockMutex(nw_cap.lock);
    return seq;
}

void nw_capture_release(void)
{
    SDL_LockMutex(nw_cap.lock);
    nw_cap.seq = 0;
    SDL_SignalCondition(nw_cap.saved);
    SDL_UnlockMutex(nw_cap.lock);
}

bool nw_capture_finished(void)
{
    return SDL_GetAtomicInt(&nw_cap.finished) != 0;
}
