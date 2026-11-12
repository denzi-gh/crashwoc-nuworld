/* One low-priority I/O worker, started before gameplay. A bounded SPSC queue never waits on the producer. */
#ifndef NUWORLD_SAVE_SAVEWORKER_H
#define NUWORLD_SAVE_SAVEWORKER_H

#include <SDL3/SDL.h>

typedef struct NwSaveTask {
    void (*run)(void *context);
    void *context;
    SDL_AtomicInt done;
} NwSaveTask;

bool nw_save_worker_init(void);
bool nw_save_worker_submit(NwSaveTask *task, void (*run)(void *), void *context);
bool nw_save_worker_done(NwSaveTask *task);
/* Explicit lifecycle/test synchronization only; never used by normal frame polling. */
void nw_save_worker_wait(NwSaveTask *task);
void nw_save_worker_shutdown(void);

#endif
