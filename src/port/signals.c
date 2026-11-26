/* Named float signals (port/signals.h): a fixed table, so storage never moves. */
#include "port/signals.h"
#include <SDL3/SDL.h>

enum { SIGNALS = 256, NAME = 64 };
static struct {
    char name[NAME];
    float *value; /* own, or the engine's */
    float own;
} signals[SIGNALS];
static int signal_count;

enum { EVENTS = 32 };
#define NOT_YET 1e9f
static struct {
    float *time;
    NwEventKind kind;
    const float *source;
    float at, after;
    bool armed, happened; /* armed: BELOW's source has reached its value */
} events[EVENTS];
static int event_count;
static float events_now; /* the clock of the latest update */

static int lookup(const char *name)
{
    for (int i = 0; name && i < signal_count; i++)
        if (!SDL_strcmp(signals[i].name, name)) return i;
    return -1;
}

static int add(const char *name)
{
    if (!name || SDL_strlen(name) >= NAME) {
        SDL_SetError("signal name too long: %s", name ? name : "(none)");
        return -1;
    }
    if (signal_count == SIGNALS) {
        SDL_SetError("more than %d signals", SIGNALS);
        return -1;
    }
    SDL_strlcpy(signals[signal_count].name, name, NAME);
    signals[signal_count].value = &signals[signal_count].own;
    return signal_count++;
}

bool nw_signal_bind(const char *name, float *storage)
{
    int i = lookup(name);
    if (i >= 0) return signals[i].value == storage || SDL_SetError("signal %s is already in use", name);
    if ((i = add(name)) < 0) return false;
    signals[i].value = storage;
    return true;
}

float *nw_signal(const char *name)
{
    int i = lookup(name);
    if (i < 0) i = add(name);
    return i < 0 ? NULL : signals[i].value;
}

const float *nw_signal_find(const char *name)
{
    int i = lookup(name);
    return i < 0 ? NULL : signals[i].value;
}

float *nw_signal_of(const char *effect, const char *track)
{
    char name[NAME * 2];
    SDL_snprintf(name, sizeof(name), "%s.%s", effect, track);
    return nw_signal(name);
}

/* The curve's start: its own event, else the set's; no event, from the clock's start. */
static bool curve_start(NwCurvePlayer *player, size_t i, const char *start, const float *after)
{
    const float *origin = start ? nw_signal_find(start) : NULL;
    if (start && !origin) return SDL_SetError("no one writes the event %s", start);
    return nw_curve_player_start(player, i, origin, origin ? after : NULL) || SDL_SetError("%s", player->error);
}

bool nw_signal_player(NwCurvePlayer *player, NwCurveSet set)
{
    NwCurveOutput *outputs = SDL_calloc(set.count ? set.count : 1, sizeof(*outputs));
    bool ok = outputs != NULL;
    for (size_t i = 0; ok && i < set.count; i++) {
        char name[NAME * 2];
        SDL_snprintf(name, sizeof(name), "%s.%s", set.effect, set.curves[i].name);
        int k = nw_signal(name) ? lookup(name) : -1;
        ok = k >= 0;
        if (ok) outputs[i] = (NwCurveOutput){signals[k].name, signals[k].value};
    }
    if (!ok) SDL_snprintf(player->error, sizeof(player->error), "%s", SDL_GetError());
    bool made = ok = ok && nw_curve_player_init(player, set, outputs, set.count, NULL, NULL);
    for (size_t i = 0; ok && i < set.event_count; i++) { /* the events first: a curve may start from any of them */
        const NwCurveEvent *e = &set.events[i];
        ok = e->follows[0] ? nw_signal_event(e->name, NW_EVENT_AFTER, nw_signal(e->follows), 0, e->after)
                           : nw_signal_event(e->name, NW_EVENT_REACH, nw_signal_of(set.effect, "frame"), e->frame, e->after);
    }
    for (size_t i = 0; ok && i < set.count; i++) {
        const NwCurve *c = &set.curves[i];
        ok = !c->event[0] || nw_signal_event(c->event, c->event_below ? NW_EVENT_BELOW : NW_EVENT_REACH,
                                             outputs[i].value, c->event_at, c->event_after);
    }
    for (size_t i = 0; ok && i < set.count; i++) {
        const NwCurve *c = &set.curves[i];
        ok = curve_start(player, i, c->start[0] ? c->start : set.start, c->start[0] ? &c->start_after : set.after);
    }
    if (made && !ok) SDL_snprintf(player->error, sizeof(player->error), "%s", SDL_GetError());
    SDL_free(outputs);
    return ok;
}

static int event_of(const float *time)
{
    for (int i = 0; time && i < event_count; i++)
        if (events[i].time == time) return i;
    return -1;
}

bool nw_signal_event(const char *name, NwEventKind kind, const float *source, float at, float after)
{
    float *time = nw_signal(name);
    if (!time || !source) return false;
    int i = event_of(time);
    if (i < 0) {
        if (event_count == EVENTS) return SDL_SetError("more than %d events", EVENTS);
        i = event_count++;
        events[i].time = time;
        events[i].armed = events[i].happened = false;
        *time = NOT_YET;
    }
    events[i].kind = kind;
    events[i].source = source;
    events[i].at = at;
    events[i].after = after;
    return true;
}

/* Due now: an AFTER's event has happened; a REACH's source is at its value, a BELOW's under it once it has been. */
static bool due(int i)
{
    int k = events[i].kind == NW_EVENT_AFTER ? event_of(events[i].source) : -1;
    if (events[i].kind == NW_EVENT_AFTER) return k >= 0 && events[k].happened;
    float v = *events[i].source;
    events[i].armed |= v >= events[i].at;
    return events[i].kind == NW_EVENT_REACH ? v >= events[i].at : events[i].armed && v < events[i].at;
}

void nw_signal_events_update(float now)
{
    events_now = now;
    for (bool more = true; more;) { /* again while one happens: the events that follow it */
        more = false;
        for (int i = 0; i < event_count; i++) {
            if (events[i].happened || !due(i)) continue;
            events[i].happened = more = true;
            *events[i].time = (events[i].kind == NW_EVENT_AFTER ? *events[i].source : now) + events[i].after;
        }
    }
}

bool nw_signal_happened(const float *event)
{
    int i = event_of(event);
    return i >= 0 && events[i].happened && *event <= events_now;
}

void nw_signal_events_reset(void)
{
    for (int i = 0; i < event_count; i++) {
        events[i].armed = events[i].happened = false;
        *events[i].time = NOT_YET;
    }
}
