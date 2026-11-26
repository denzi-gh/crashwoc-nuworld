/* ATS V2 "param" tracks: scalar curves that animate material values over time.
   Pure data and math: no files, no game state. nutexanm.c reads the curves from ATS files,
   the sequence director plays them and writes one named signal per curve. */
#ifndef NW_SCRIPT_CURVES_H
#define NW_SCRIPT_CURVES_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { NW_CURVE_KEYS, NW_CURVE_WAVE, NW_CURVE_PULSE } NwCurveKind;

/* One key: the value holds until the next key, or ramps to it when linear. */
typedef struct {
    float time, value;
    bool linear;
    bool smooth; /* towards the next key along u*u*(3 - 2u), u the fraction of the way */
} NwCurveKey;

/* Groups of half-sine pulses, repeated every period after a seeded random delay.
   All lengths are whole time units (frames in a "time frames N" file). */
typedef struct {
    int64_t period, spacing, width;
    uint32_t count, seed;
    uint32_t delay_num, delay_den; /* Random delay: 0 .. period * num / den. */
} NwCurvePulse;

typedef struct {
    char name[32];
    NwCurveKind kind;
    int64_t rate_num, rate_den;   /* Time units per second: 1/1 for seconds, N/1 for frames N. */
    float low, span;              /* Wave and pulse output: low + span * shape (shape is 0..1). */
    bool loop;                    /* Keys only: repeat every duration. */
    float duration;               /* Keys only: loop length in time units. */
    const NwCurveKey *keys;       /* Keys only, increasing times. */
    uint32_t key_count;
    float wave_period;            /* Wave only: cosine period in time units. */
    NwCurvePulse pulse;           /* Pulse only. */
    char event[32];               /* An event (port/signals.h): the curve first reaches event_at, event_after later; */
    float event_at, event_after;
    bool event_below;             /* or, once it has reached it, first falls below it. */
    char start[32];               /* The event this curve starts from, start_after seconds later; "": the set's. */
    float start_after;
} NwCurve;

/* An event a file declares at its top: at a frame of its cutscene (EFFECT.frame), or when the event it follows has
   happened; after seconds later. */
typedef struct {
    char name[32], follows[32]; /* follows: "" for a frame event */
    float frame, after;
} NwCurveEvent;

/* The curves and events of one ATS effect, borrowed from the loaded program. */
typedef struct {
    const char *effect;
    const NwCurve *curves;
    size_t count;
    const char *start;  /* The event the set starts from, after seconds later; NULL: from the clock's start. */
    const float *after;
    const NwCurveEvent *events;
    size_t event_count;
} NwCurveSet;

/* Local time of a curve: seconds for keys and waves (or file units when in_units), whole units for pulses. */
typedef struct {
    float seconds;
    int64_t units;
    bool in_units; /* The host's clock already counts the file's units (an ATS cut clock at the file's rate). */
} NwCurveTime;

float nw_curve_sample(const NwCurve *curve, NwCurveTime time);

/* Host signal table: name -> storage. Curve outputs are named EFFECT.TRACK. */
typedef struct {
    const char *name;
    float *value;
} NwCurveOutput;

/* One curve's host output, and its optional start inputs in seconds (borrowed). */
typedef struct {
    float *value;
    const float *origin, *delay;
} NwCurveTarget;

/* Plays one curve set on the director's integer clock. Before its origin a curve's output is 0. */
typedef struct {
    NwCurveSet set;
    NwCurveTarget *targets;        /* Owned array: one per curve. */
    char error[256];
} NwCurvePlayer;

bool nw_curve_player_init(NwCurvePlayer *player, NwCurveSet set, const NwCurveOutput *outputs, size_t output_count,
                          const float *origin, const float *delay);
/* One curve starts from its own origin and delay instead of the set's. */
bool nw_curve_player_start(NwCurvePlayer *player, size_t curve, const float *origin, const float *delay);
bool nw_curve_player_sample(NwCurvePlayer *player, int64_t ticks, uint32_t rate);
/* For hosts with their own clock (ATS autonomous effects): every output at one local time, no origin. */
void nw_curve_player_write(NwCurvePlayer *player, NwCurveTime time);
/* Finished: keyed curves that do not loop hold their last key; the others keep their last value. */
void nw_curve_player_finish(NwCurvePlayer *player);
void nw_curve_player_destroy(NwCurvePlayer *player);
#endif
