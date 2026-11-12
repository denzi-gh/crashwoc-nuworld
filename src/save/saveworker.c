#include "save/saveworker.h"

#define NW_SAVE_QUEUE 8

static struct {
    SDL_Thread *thread;
    SDL_Semaphore *wake, *completed;
    SDL_AtomicInt read, write, stop;
    NwSaveTask *queue[NW_SAVE_QUEUE];
} nw_worker;

static int SDLCALL nw_save_worker_run(void *unused)
{
    (void)unused;
    SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_LOW);
    for (;;) {
        SDL_WaitSemaphore(nw_worker.wake);
        int read = SDL_GetAtomicInt(&nw_worker.read);
        int write = SDL_GetAtomicInt(&nw_worker.write);
        if (read == write) {
            if (SDL_GetAtomicInt(&nw_worker.stop)) break;
            continue;
        }
        NwSaveTask *task = nw_worker.queue[read];
        SDL_SetAtomicInt(&nw_worker.read, (read + 1) % NW_SAVE_QUEUE);
        task->run(task->context);
        SDL_SetAtomicInt(&task->done, 1);
        SDL_SignalSemaphore(nw_worker.completed);
    }
    return 0;
}

bool nw_save_worker_init(void)
{
    if (nw_worker.thread) return true;
    nw_worker.wake = SDL_CreateSemaphore(0);
    nw_worker.completed = SDL_CreateSemaphore(0);
    if (nw_worker.wake && nw_worker.completed) {
        nw_worker.thread = SDL_CreateThread(nw_save_worker_run, "nw_save_io", NULL);
    }
    if (!nw_worker.thread) {
        SDL_DestroySemaphore(nw_worker.wake);
        SDL_DestroySemaphore(nw_worker.completed);
        SDL_zero(nw_worker);
        return false;
    }
    return true;
}

bool nw_save_worker_submit(NwSaveTask *task, void (*run)(void *), void *context)
{
    if (!nw_worker.thread) return SDL_SetError("save worker is not initialized");
    int write = SDL_GetAtomicInt(&nw_worker.write);
    int next = (write + 1) % NW_SAVE_QUEUE;
    if (next == SDL_GetAtomicInt(&nw_worker.read)) return SDL_SetError("save queue is full");
    task->run = run;
    task->context = context;
    SDL_SetAtomicInt(&task->done, 0);
    nw_worker.queue[write] = task;
    SDL_SetAtomicInt(&nw_worker.write, next);
    SDL_SignalSemaphore(nw_worker.wake);
    return true;
}

bool nw_save_worker_done(NwSaveTask *task) { return SDL_GetAtomicInt(&task->done) != 0; }

void nw_save_worker_wait(NwSaveTask *task)
{
    while (!nw_save_worker_done(task)) SDL_WaitSemaphore(nw_worker.completed);
}

void nw_save_worker_shutdown(void)
{
    if (!nw_worker.thread) return;
    SDL_SetAtomicInt(&nw_worker.stop, 1);
    SDL_SignalSemaphore(nw_worker.wake);
    SDL_WaitThread(nw_worker.thread, NULL);
    SDL_DestroySemaphore(nw_worker.wake);
    SDL_DestroySemaphore(nw_worker.completed);
    SDL_zero(nw_worker);
}
