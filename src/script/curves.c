/* ATS V2 "param" curves: sampling and the director's player. See curves.h. */
#include "curves.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI_F 3.14159265f
#define TWO_PI_F 6.2831853f

/* Integer hash behind the pulse delays; changing it changes every authored pattern. */
static uint32_t hash32(uint32_t x)
{
    x ^= x >> 16; x *= 0x7FEB352Du;
    x ^= x >> 15; x *= 0x846CA68Bu;
    return x ^ (x >> 16);
}

/* Step to the last key at or before t, or ramp towards the next one when that key is linear or smooth. */
static float keys_value(const NwCurve *c, float t)
{
    if (c->loop) t = fmodf(t, c->duration);
    const NwCurveKey *k = c->keys;
    uint32_t i = 0;
    while (i + 1 < c->key_count && k[i + 1].time <= t) i++;
    if (i + 1 == c->key_count || !(k[i].linear || k[i].smooth) || t <= k[i].time) return k[i].value;
    if (!k[i].smooth) return k[i].value + (k[i + 1].value - k[i].value) * (t - k[i].time) / (k[i + 1].time - k[i].time);
    float u = (t - k[i].time) / (k[i + 1].time - k[i].time); /* as the opening's nw_smooth, bit for bit */
    return k[i].value + (k[i + 1].value - k[i].value) * (u * u * (3 - 2 * u));
}

/* Raised cosine: 0 at the start of each period, 1 halfway. */
static float wave_shape(float t, float period)
{
    return .5f - .5f * cosf(t * TWO_PI_F / period);
}

/* Each period starts after a seeded delay, then plays COUNT half-sine pulses SPACING units apart. */
static float pulse_shape(const NwCurvePulse *p, int64_t units)
{
    if (units < 0) return 0;
    int64_t cycle = units / p->period;
    uint64_t latest = (uint64_t)p->period * p->delay_num / p->delay_den;   /* <= period <= INT32_MAX */
    int64_t delay = hash32((uint32_t)cycle * p->seed) % (uint32_t)(latest + 1);
    int64_t at = units % p->period - delay;
    if (at < 0) return 0;
    int64_t pulse = at / p->spacing;
    if (pulse >= p->count) pulse = p->count - 1;
    float f = (float)(at - pulse * p->spacing) / (float)p->width;
    return f >= 0 && f < 1 ? sinf(f * PI_F) : 0;
}

float nw_curve_sample(const NwCurve *c, NwCurveTime time)
{
    float t = time.in_units ? time.seconds : time.seconds * (float)c->rate_num / (float)c->rate_den;
    switch (c->kind) {
    case NW_CURVE_KEYS: return keys_value(c, t);
    case NW_CURVE_WAVE: return c->low + c->span * wave_shape(t, c->wave_period);
    case NW_CURVE_PULSE: return c->low + c->span * pulse_shape(&c->pulse, time.units);
    }
    return 0;
}

static bool refuse(NwCurvePlayer *p, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(p->error, sizeof(p->error), format, args);
    va_end(args);
    return false;
}

static float *find_output(const NwCurveOutput *outputs, size_t count, const char *name)
{
    for (size_t i = 0; i < count; i++)
        if (outputs[i].name && !strcmp(outputs[i].name, name)) return outputs[i].value;
    return NULL;
}

bool nw_curve_player_init(NwCurvePlayer *p, NwCurveSet set, const NwCurveOutput *outputs, size_t output_count,
                          const float *origin, const float *delay)
{
    NwCurvePlayer next = {.set = set};
    if (!set.effect || (set.count && !set.curves) || (output_count && !outputs)) return refuse(p, "missing curve set or outputs");
    next.targets = calloc(set.count ? set.count : 1, sizeof(*next.targets));
    if (!next.targets) return refuse(p, "cannot allocate curve player");
    for (size_t i = 0; i < set.count; i++) {
        char name[80];
        snprintf(name, sizeof(name), "%s.%s", set.effect, set.curves[i].name);
        float *value = find_output(outputs, output_count, name);
        if (!value || value == origin || value == delay) {
            free(next.targets);
            return refuse(p, value ? "curve output '%s' is also its clock input" : "unknown curve output '%s'", name);
        }
        next.targets[i] = (NwCurveTarget){value, origin, delay};
    }
    nw_curve_player_destroy(p);
    *p = next;
    return true;
}

bool nw_curve_player_start(NwCurvePlayer *p, size_t curve, const float *origin, const float *delay)
{
    if (!p->targets || curve >= p->set.count) return refuse(p, "no such curve");
    for (size_t i = 0; i < p->set.count; i++)
        if (p->targets[i].value == origin || p->targets[i].value == delay)
            return refuse(p, "curve output '%s.%s' is also its clock input", p->set.effect, p->set.curves[i].name);
    p->targets[curve].origin = origin;
    p->targets[curve].delay = delay;
    return true;
}

/* Seconds as the director has always computed them: int32 ticks in float, divided by the rate. */
static float clock_seconds(int64_t ticks, uint32_t rate)
{
    float f = ticks <= INT32_MAX ? (float)(int32_t)ticks : (float)ticks;
    return f / (float)rate;
}

/* Whole curve units since the origin: exact integer math for the clock, float seconds for the scene offset.
   No doubles: the EE build has no int64 -> double conversion. */
static int64_t clock_units(const NwCurve *c, int64_t ticks, uint32_t rate, float offset)
{
    int64_t scale = (int64_t)rate * c->rate_den;
    int64_t whole = ticks * c->rate_num / scale;
    if (offset == 0) return whole;
    float fraction = (float)(ticks * c->rate_num % scale) / (float)scale;
    return whole + (int64_t)floorf(fraction - offset * (float)c->rate_num / (float)c->rate_den);
}

bool nw_curve_player_sample(NwCurvePlayer *p, int64_t ticks, uint32_t rate)
{
    if (!p->targets || ticks < 0 || !rate) return refuse(p, "invalid curve clock");
    for (size_t i = 0; i < p->set.count; i++)
        if (ticks > INT64_MAX / p->set.curves[i].rate_num) return refuse(p, "clock exceeds the curve range");
    float clock = clock_seconds(ticks, rate);
    for (size_t i = 0; i < p->set.count; i++) {
        const NwCurveTarget *t = &p->targets[i];
        float local = clock, offset = 0;
        if (t->origin) { local -= *t->origin; offset += *t->origin; }
        if (t->delay) { local -= *t->delay; offset += *t->delay; }
        if (!isfinite(local)) return refuse(p, "invalid curve origin or delay");
        bool started = local > 0 || (local == 0 && !t->origin && !t->delay);
        const NwCurve *c = &p->set.curves[i];
        NwCurveTime time = {local, clock_units(c, ticks, rate, offset)};
        *t->value = started ? nw_curve_sample(c, time) : 0;
    }
    return true;
}

void nw_curve_player_write(NwCurvePlayer *p, NwCurveTime time)
{
    for (size_t i = 0; p->targets && i < p->set.count; i++) *p->targets[i].value = nw_curve_sample(&p->set.curves[i], time);
}

void nw_curve_player_finish(NwCurvePlayer *p)
{
    for (size_t i = 0; p->targets && i < p->set.count; i++) {
        const NwCurve *c = &p->set.curves[i];
        if (c->kind == NW_CURVE_KEYS && !c->loop) *p->targets[i].value = c->keys[c->key_count - 1].value;
    }
}

void nw_curve_player_destroy(NwCurvePlayer *p)
{
    free(p->targets);
    memset(p, 0, sizeof(*p));
}
