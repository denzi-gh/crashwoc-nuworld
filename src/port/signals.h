/* Named float signals: what ATS param tracks write (EFFECT.TRACK), the engine's own values (sky.seconds, the
   opening's settings), and what .looks drives read. A signal's storage never moves: a pointer taken once stays valid. */
#ifndef NW_PORT_SIGNALS_H
#define NW_PORT_SIGNALS_H
#include <stdbool.h>
#include "script/curves.h"

/* An engine value under a name: the signal is storage, which must outlive the program. Binding the same storage
   again is fine; a name already in use elsewhere is an error. */
bool nw_signal_bind(const char *name, float *storage);
/* The signal's storage, made (0) on first use; NULL when the name is too long or there are too many. */
float *nw_signal(const char *name);
/* The signal if someone has made or bound it, else NULL. */
const float *nw_signal_find(const char *name);
/* EFFECT.TRACK, made (0) on first use. */
float *nw_signal_of(const char *effect, const char *track);
/* A player writing each curve of set to its signal EFFECT.TRACK (made if new), each curve from its start event or the
   set's, and the events its curves and its file declare; the error is in player->error. */
bool nw_signal_player(NwCurvePlayer *player, NwCurveSet set);

/* Events: a signal holding the time, on the host's clock, it happened, plus a delay; 1e9 until then. It happens when
   its source first reaches a value (REACH), first falls below it once it has reached it (BELOW), or, for AFTER, when
   the event whose time the source is has happened: at that time. Declaring one again keeps whether it has happened. */
typedef enum { NW_EVENT_REACH, NW_EVENT_BELOW, NW_EVENT_AFTER } NwEventKind;
bool nw_signal_event(const char *name, NwEventKind kind, const float *source, float at, float after);
/* The host's clock now: the events due happen, those that follow them too. */
void nw_signal_events_update(float now);
/* Whether the event (its signal) has happened by the clock of the latest update; false for a signal no event has. */
bool nw_signal_happened(const float *event);
/* A new scene: every event back to not yet. */
void nw_signal_events_reset(void);

#endif
