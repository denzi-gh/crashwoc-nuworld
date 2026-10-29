#include "platform/clock.h"

uint64_t nw_pacer_next(uint64_t next, uint64_t now, uint64_t period)
{
    return next && now < next + period ? next + period : now + period;
}

void nw_pacer_wait(NwPacer *p, unsigned hz)
{
    if (!hz) {
        p->next = 0;
        return;
    }
    uint64_t now = SDL_GetTicksNS();
    if (p->next > now) {
        SDL_DelayPrecise(p->next - now);
        now = SDL_GetTicksNS();
    }
    p->next = nw_pacer_next(p->next, now, SDL_NS_PER_SECOND / hz);
}

unsigned nw_clock_due(NwClockTiming *t, uint64_t now)
{
    uint64_t passed = now > t->t0 ? (now - t->t0) / t->period : 0;
    if (passed <= t->k) {
        return 0;
    }
    uint64_t n = passed - t->k;
    if (n > NW_CLOCK_MAX_LATE) {
        t->t0 = now;
        t->k = 0;
        return 1;
    }
    t->k = passed;
    return (unsigned)n;
}

uint64_t nw_clock_deadline(const NwClockTiming *t)
{
    return t->t0 + (t->k + 1) * t->period;
}

static void nw_clock_emit(NwClock *c, unsigned n)
{
    if (c->on_edge) {
        c->on_edge(c->user, n);
    }
    SDL_LockMutex(c->lock);
    c->edges += n;
    c->edge_ns = SDL_GetTicksNS();
    SDL_BroadcastCondition(c->cond);
    SDL_UnlockMutex(c->lock);
}

static int SDLCALL nw_clock_run(void *arg)
{
    NwClock *c = arg;
    SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_TIME_CRITICAL);
    for (;;) {
        SDL_LockMutex(c->lock);
        bool quit = c->quit;
        SDL_UnlockMutex(c->lock);
        if (quit) {
            return 0;
        }
        uint64_t deadline = nw_clock_deadline(&c->timing), now = SDL_GetTicksNS();
        if (now < deadline) {
            SDL_DelayPrecise(deadline - now);
        }
        unsigned n = nw_clock_due(&c->timing, SDL_GetTicksNS());
        if (n) {
            nw_clock_emit(c, n);
        }
    }
}

bool nw_clock_start(NwClock *c, NwClockMode mode, unsigned hz, NwClockEdge on_edge, void *user)
{
    SDL_zerop(c);
    c->mode = mode;
    c->timing.period = SDL_NS_PER_SECOND / hz;
    c->on_edge = on_edge;
    c->user = user;
    c->lock = SDL_CreateMutex();
    c->cond = SDL_CreateCondition();
    if (c->lock && c->cond && mode == NW_CLOCK_REALTIME) {
        c->timing.t0 = SDL_GetTicksNS();
        c->thread = SDL_CreateThread(nw_clock_run, "nw_clock", c);
    }
    if (!c->lock || !c->cond || (mode == NW_CLOCK_REALTIME && !c->thread)) {
        nw_clock_stop(c);
        return false;
    }
    return true;
}

void nw_clock_stop(NwClock *c)
{
    if (c->thread) {
        SDL_LockMutex(c->lock);
        c->quit = true;
        SDL_UnlockMutex(c->lock);
        SDL_WaitThread(c->thread, NULL);
        c->thread = NULL;
    }
    if (c->lock) {
        nw_clock_wake(c);
    }
    SDL_DestroyCondition(c->cond);
    SDL_DestroyMutex(c->lock);
    c->cond = NULL;
    c->lock = NULL;
}

uint64_t nw_clock_edges(NwClock *c)
{
    SDL_LockMutex(c->lock);
    uint64_t edges = c->edges;
    SDL_UnlockMutex(c->lock);
    return edges;
}

double nw_clock_time(NwClock *c)
{
    uint64_t now = SDL_GetTicksNS();
    SDL_LockMutex(c->lock);
    uint64_t edges = c->edges, at = c->edge_ns;
    SDL_UnlockMutex(c->lock);
    double part = now > at && c->timing.period ? (double)(now - at) / (double)c->timing.period : 0.0;
    return (double)edges + (part < 1.0 ? part : 1.0);
}

void nw_clock_step(NwClock *c, unsigned n)
{
    if (n) {
        nw_clock_emit(c, n);
    }
}

uint64_t nw_clock_wait(NwClock *c, uint64_t seen, int32_t timeout_ms)
{
    if (c->mode == NW_CLOCK_STEPPED && nw_clock_edges(c) <= seen) {
        nw_clock_step(c, 1);
    }
    return nw_clock_watch(c, seen, timeout_ms);
}

uint64_t nw_clock_watch(NwClock *c, uint64_t seen, int32_t timeout_ms)
{
    SDL_LockMutex(c->lock);
    uint64_t wakes = c->wakes, start = SDL_GetTicks();
    while (c->edges <= seen && c->wakes == wakes) {
        if (timeout_ms < 0) {
            SDL_WaitCondition(c->cond, c->lock);
            continue;
        }
        uint64_t spent = SDL_GetTicks() - start;
        if (spent >= (uint64_t)timeout_ms) {
            break;
        }
        SDL_WaitConditionTimeout(c->cond, c->lock, (Sint32)((uint64_t)timeout_ms - spent));
    }
    uint64_t edges = c->edges;
    SDL_UnlockMutex(c->lock);
    return edges;
}

void nw_clock_wake(NwClock *c)
{
    SDL_LockMutex(c->lock);
    c->wakes++;
    SDL_BroadcastCondition(c->cond);
    SDL_UnlockMutex(c->lock);
}
