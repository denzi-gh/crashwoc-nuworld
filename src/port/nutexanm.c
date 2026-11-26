/* Host TexAnim V2; V1 interpreter and public record layouts remain unchanged. */
#include "port/nutexanm.h"
#include "script/bursts.h"
#include "port/nufpar.h"
#include "port/heap.h"
#include "port/signals.h"
#include "gfx/rndr.h"
#include "nu3d/numtl.h"
#include "nu3d/nutexanm.h"
#include "nucore/nucoretypes.h"
#include "nu3d/nutexanm.retail.h"
#include "gamelib/gcutscn.h"
#include <SDL3/SDL.h>
#include <limits.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct { int64_t time; double value[2]; } UVKey;
typedef struct { NwBurst recipe; bool session; } Burst;
typedef struct {
    double value[7];
    int first[3], count[3], total, capacity, address;
    unsigned linear;
    bool active, mips, policy;
    int64_t denominator, ticks;
    UVKey *keys;
    int64_t time[3][2]; /* step, start, duration in authored units, before final clock conversion */
    unsigned controls, owned;
    NwTexAnimSequence *timing;
    int64_t last_key;
} UVPlan;
typedef struct {
    NwTexAnimSequence sequence;
    UVPlan uv;
    NwTexture *image;
    short image_tid;
    unsigned kind;
    Burst *burst;
} Track;
typedef struct {
    struct nufpar_s *parser;
    const char *path;
    NwTexAnimError *error;
    char words[6][256];
    bool replay;
    UVPlan *uv;
    Track *tracks;
    UVKey *keys;
    Burst *bursts;
    int burst_count, burst_capacity;
    int track_count, sequence_count, uv_count, key_count, track_capacity, key_capacity;
    NwCurve *curves;         /* Param tracks; NULL while measuring. */
    NwCurveKey *curve_keys;
    int curve_count, curve_capacity, curve_key_count, curve_key_capacity;
    float curve_keys_last;   /* Time of the latest key in the current param block. */
    bool cut_clock;          /* "clock cut": the owning cutscene's time instead of the game's. */
    char start[32];          /* "start EVENT after SECONDS": what the effect's param tracks start from */
    float start_after;
    NwCurveEvent events[8];  /* "event NAME frame F" / "event NAME after EVENT": the file's own events */
    int event_count;
    int64_t unit;
    Track primary, uv_primary;
} Compile;

static bool fail(Compile *c, const char *reason)
{
    if (c->error) {
        c->error->line = c->parser ? c->parser->line_num + 1 : 0;
        snprintf(c->error->message, sizeof(c->error->message), "%s:%d: %s",
                 c->path, c->error->line, reason);
    }
    return false;
}

static bool space(char ch)
{
    return ch == ' ' || ch == '\t';
}

/* NuFPar owns file/line buffering. These are V2's strict row and comment rules. */
static int row(Compile *c)
{
    while (c->replay || NuFParGetLine(c->parser)) {
        c->replay = false;
        const char *p = NuFParLine(c->parser);
        int count = 0;
        while (*p) {
            while (space(*p)) p++;
            if (!*p || *p == ';') break;
            if (count == (int)SDL_arraysize(c->words)) {
                fail(c, "too many arguments");
                return -1;
            }
            bool quoted = *p == '"';
            if (quoted) p++;
            int length = 0;
            while (*p && (quoted ? *p != '"' : !space(*p) && *p != ';')) {
                if ((!quoted && (*p == '"' || *p == ',')) || (unsigned char)*p < 32 || length == 255) {
                    fail(c, "invalid or oversized token");
                    return -1;
                }
                c->words[count][length++] = *p++;
            }
            if (quoted) {
                if (*p != '"' || (p[1] && !space(p[1]) && p[1] != ';')) {
                    fail(c, "unterminated string or missing separator");
                    return -1;
                }
                p++;
            }
            c->words[count++][length] = 0;
        }
        if (count) return count;
    }
    const char *error = NuFParGetError(c->parser);
    if (error) {
        fail(c, error);
        return -1;
    }
    return 0;
}

static int64_t gcd(int64_t a, int64_t b)
{
    while (b) {
        int64_t remainder = a % b;
        a = b;
        b = remainder;
    }
    return a;
}

static bool digit(char ch)
{
    return ch >= '0' && ch <= '9';
}

static bool digits(const char **text, int64_t *value)
{
    const char *p = *text;
    int64_t n = 0;
    if (!digit(*p)) return false;
    while (digit(*p)) {
        int d = *p++ - '0';
        if (n > (INT64_MAX - d) / 10) return false;
        n = n * 10 + d;
    }
    *text = p;
    *value = n;
    return true;
}

/* Nonnegative decimal or integer ratio; time declarations additionally require nonzero. */
static bool fraction(const char *text, int64_t *num, int64_t *den)
{
    const char *p = text;
    int64_t n, d = 1;
    if (!digits(&p, &n)) return false;
    if (*p == '.') {
        p++;
        if (!digit(*p)) return false;
        while (digit(*p)) {
            int v = *p++ - '0';
            if (n > (INT64_MAX - v) / 10 || d > INT64_MAX / 10) return false;
            n = n * 10 + v;
            d *= 10;
        }
    } else if (*p == '/') {
        p++;
        if (!digits(&p, &d)) return false;
    }
    if (*p || d <= 0) return false;
    int64_t divisor = gcd(n, d);
    *num = n / divisor;
    *den = d / divisor;
    return true;
}

static bool multiply(int64_t a, int64_t b, int64_t *result)
{
    if (a > INT64_MAX / b) return false;
    *result = a * b;
    return true;
}

static bool name(char dest[32], const char *text)
{
    size_t size = strlen(text);
    if (!size || size >= 32) return false;
    memcpy(dest, text, size + 1);
    return true;
}

static bool frames(const char *text, int *first, int *last)
{
    int64_t a, b;
    if (!digits(&text, &a) || text[0] != '.' || text[1] != '.') return false;
    text += 2;
    if (!digits(&text, &b) || *text || a > b || b > INT_MAX) return false;
    *first = (int)a;
    *last = (int)b;
    return true;
}

static double wide_number(uint64_t n)
{
    return (double)(uint32_t)(n >> 32) * 0x1p32 + (double)(uint32_t)n;
}

static bool number(const char *text, double *value)
{
    bool negative = *text == '-';
    if (negative || *text == '+') text++;
    int64_t n, d;
    if (!fraction(text,&n,&d)) return false;
    *value = wide_number(n)/wide_number(d)*(negative ? -1 : 1);
    return isfinite(*value) && fabs(*value) <= FLT_MAX;
}

static int control(Compile *c, int count)
{
    UVPlan *u=c->uv;
    NwTexAnimSequence *s=u->timing;
    const char *p=c->words[0], *v=c->words[1];
    int id=!strcmp(p,"start") ? 0 : !strcmp(p,"duration") ? 1 : !strcmp(p,"playback") ? 2 :
           !strcmp(p,"before") ? 3 : !strcmp(p,"finish") ? 4 : -1;
    if (id<0) return 0;
    if (count!=2 || (u->controls & (1u<<id))) {
        fail(c,"invalid or duplicate playback control"); return -1;
    }
    u->controls|=1u<<id;
    if (id<2) {
        int64_t *r=u->time[id+1];
        if ((id==1 && s->first>=0) || !fraction(v,&r[0],&r[1]) || (id==1 && !r[0]) ||
            !multiply(u->denominator/gcd(u->denominator,r[1]),r[1],&u->denominator))
        { fail(c,"invalid start/duration"); return -1; }
    } else if (id==2) {
        s->playback=!strcmp(v,"once") ? NW_TEXANIM_ONCE : !strcmp(v,"loop") ? NW_TEXANIM_LOOP :
                    !strcmp(v,"pingpong") ? NW_TEXANIM_PINGPONG : -1;
        if ((int)s->playback<0) { fail(c,"unknown playback mode"); return -1; }
    } else {
        if (strcmp(v,"restore") && strcmp(v,id==3 ? "first" : "hold")) {
            fail(c,"invalid playback policy"); return -1;
        }
        if (id==3) s->before_restore=!strcmp(v,"restore");
        else s->finish_restore=!strcmp(v,"restore");
    }
    return 1;
}
static bool uv_read(Compile *c)
{
    UVPlan *u = c->uv; char id[32];
    if (!u || !name(id,c->words[1])) return fail(c,"invalid or unsupported UV track");
    u->active = true;
    unsigned seen = 0; int count;
    while ((count=row(c)) > 0 && strcmp(c->words[0],"}")) {
        int ctl=control(c,count);
        if (ctl<0) return false;
        if (ctl) continue;
        const char *p = c->words[0];
        int axis = !strcmp(p,"offset") ? 0 : !strcmp(p,"scale") ? 1 : !strcmp(p,"rotation") ? 2 :
                   !strcmp(p,"pivot") ? 3 : !strcmp(p,"address") ? 4 : -1;
        if (axis < 0 || (seen & (1u<<axis))) return fail(c,"unknown or duplicate UV property");
        seen |= 1u<<axis;
        if (axis == 4) {
            u->address = !strcmp(c->words[1],"repeat") ? 0 : !strcmp(c->words[1],"clamp") ? 1 :
                         !strcmp(c->words[1],"mirror") ? 2 : -1;
            if (count != 2 || u->address < 0) return fail(c,"invalid UV addressing");
            continue;
        }
        int width = axis == 2 ? 1 : 2, at = axis == 3 ? 5 : axis*2;
        bool curve = axis < 3 && count == 3 && !strcmp(c->words[2],"{");
        if (!curve) {
            if (count != width+1) return fail(c,"invalid UV constant arity");
            for (int j=0; j<width; j++) if (!number(c->words[j+1],&u->value[at+j]) ||
                (axis == 3 && (u->value[at+j] < 0 || u->value[at+j] > 1))) return fail(c,"invalid UV constant");
            continue;
        }
        if (strcmp(c->words[1],"linear") && strcmp(c->words[1],"step")) return fail(c,"expected linear or step");
        if (!strcmp(c->words[1],"linear")) u->linear |= 1u<<axis;
        u->first[axis] = u->total; int64_t previous = -1;
        while ((count=row(c)) > 0 && strcmp(c->words[0],"}")) {
            int64_t n,d; UVKey key = {0};
            if (count != width+2 || strcmp(c->words[0],"key") || !fraction(c->words[1],&n,&d) ||
                !multiply(u->denominator/gcd(u->denominator,d),d,&u->denominator)) return fail(c,"invalid UV key time");
            for (int j=0; j<width; j++) if (!number(c->words[j+2],&key.value[j])) return fail(c,"invalid UV key value");
            if (u->ticks && (u->ticks % d || !multiply(n,u->ticks/d,&key.time) || key.time <= previous))
                return fail(c,"UV keys must increase and fit the exact clock");
            previous = key.time;
            if (key.time>u->last_key) u->last_key=key.time;
            if ((size_t)u->total >= (8u<<20)/sizeof(UVKey) || (u->keys && u->total >= u->capacity)) return fail(c,"UV IR budget exceeded");
            if (u->keys) u->keys[u->total] = key;
            u->total++; u->count[axis]++;
        }
        if (count<0) return false;
        if (count != 1 || !u->count[axis]) return fail(c,"empty or incomplete UV curve");
    }
    u->owned=seen;
    return count<0 ? false : count == 1 || fail(c,"incomplete UV track");
}

static bool burst_read(Compile *c, Track *t)
{
    static const char *fields[]={"frames","slot","seed","delay","flashes","on","off","algorithm","seed_mode"};
    NwBurst *b=&t->burst->recipe;
    uint32_t *values[]={b->frames,&b->slot,b->seed,b->delay,b->flashes,b->on,b->off};
    unsigned seen=0;
    int count;
    while ((count=row(c))>0 && strcmp(c->words[0],"}")) {
        if (!strcmp(c->words[0],"source")) {
            if ((seen&512) || count!=3 || strcmp(c->words[1],"resource") || !name(t->sequence.source,c->words[2]))
                return fail(c,"expected a single burst source resource ID");
            seen|=512; continue;
        }
        if (!strcmp(c->words[0],"playback") || !strcmp(c->words[0],"finish") || !strcmp(c->words[0],"duration"))
            return fail(c,"burst requires explicit clock or stop, not finite playback controls");
        int ctl=control(c,count);
        if (ctl<0) return false;
        if (ctl) continue;
        int field=0;
        while (field<9 && strcmp(c->words[0],fields[field])) field++;
        if (field==9 || (seen & (1u<<field))) return fail(c,"unknown or duplicate burst property");
        seen|=1u<<field;
        if (field>=7) {
            if (count!=2 || (field==7 ? strcmp(c->words[1],"hash_bursts_1") :
                strcmp(c->words[1],"fixed") && strcmp(c->words[1],"session"))) return fail(c,"unsupported burst algorithm/seed mode");
            if (field==8) t->burst->session=!strcmp(c->words[1],"session");
        } else {
            int width=field==1 ? 1 : 2;
            if (count!=width+1) return fail(c,"expected burst integer pair or slot");
            for (int k=0; k<width; k++) {
                const char *word=c->words[k+1];
                int64_t n;
                if (!digits(&word,&n) || *word || n>UINT32_MAX) return fail(c,"burst integer exceeds uint32");
                values[field][k]=(uint32_t)n;
            }
        }
    }
    if (count<0) return false;
    if (count!=1 || (seen&255)!=255 || !b->slot || !b->flashes[0] || !b->on[0]) return fail(c,"incomplete burst or zero slot/flash/on");
    for (int k=3; k<7; k++) if (values[k][1]<values[k][0] || values[k][1]-values[k][0]==UINT32_MAX)
        return fail(c,"invalid inclusive burst range");
    if (b->frames[0]>INT_MAX || b->frames[1]>INT_MAX) return fail(c,"burst frame exceeds signed index");
    t->sequence.first=(int)SDL_min(b->frames[0],b->frames[1]);
    t->sequence.last=(int)SDL_max(b->frames[0],b->frames[1]);
    if (t->sequence.source[0] && t->sequence.last) return fail(c,"external burst image requires frames 0 0");
    return true;
}

/* ---- param tracks: scalar curves for material values (script/curves.h) ---- */

typedef enum {
    PARAM_KEY, PARAM_WAVE, PARAM_PULSE, PARAM_PERIOD, PARAM_JITTER, PARAM_RANGE, PARAM_PLAYBACK, PARAM_DURATION,
    PARAM_EVENT, PARAM_START, PARAM_FIELDS
} ParamField;
#define PARAM_BIT(field) (1u << (field))
static const char *const param_names[PARAM_FIELDS] = {
    "key", "wave", "pulse", "period", "jitter", "range", "playback", "duration", "event", "start"
};
/* Which properties each generator accepts and needs. */
#define PARAM_ANY (PARAM_BIT(PARAM_EVENT) | PARAM_BIT(PARAM_START))
static const unsigned param_allowed[] = {
    [NW_CURVE_KEYS] = PARAM_BIT(PARAM_KEY) | PARAM_BIT(PARAM_PLAYBACK) | PARAM_BIT(PARAM_DURATION) | PARAM_ANY,
    [NW_CURVE_WAVE] = PARAM_BIT(PARAM_WAVE) | PARAM_BIT(PARAM_RANGE) | PARAM_ANY,
    [NW_CURVE_PULSE] = PARAM_BIT(PARAM_PULSE) | PARAM_BIT(PARAM_PERIOD) | PARAM_BIT(PARAM_JITTER) | PARAM_BIT(PARAM_RANGE) |
                       PARAM_ANY,
};
static const unsigned param_required[] = {
    [NW_CURVE_KEYS] = PARAM_BIT(PARAM_KEY),
    [NW_CURVE_WAVE] = PARAM_BIT(PARAM_WAVE) | PARAM_BIT(PARAM_RANGE),
    [NW_CURVE_PULSE] = PARAM_BIT(PARAM_PULSE) | PARAM_BIT(PARAM_PERIOD) | PARAM_BIT(PARAM_RANGE),
};
#define PARAM_KEY_BUDGET ((8u << 20) / sizeof(NwCurveKey))

static int param_field(const char *word)
{
    for (int field = 0; field < PARAM_FIELDS; field++)
        if (!strcmp(word, param_names[field])) return field;
    return -1;
}

/* A plain decimal within uint32, zero included. */
static bool uint32_word(const char *text, uint32_t *value)
{
    int64_t n;
    if (!digits(&text, &n) || *text || n > UINT32_MAX) return false;
    *value = (uint32_t)n;
    return true;
}

/* start EVENT [after SECONDS], in a file's head or in a param block. */
static bool start_row(Compile *c, int count, char event[32], float *after)
{
    double x = 0;
    if ((count != 2 && count != 4) || !name(event, c->words[1]) ||
        (count == 4 && (strcmp(c->words[2], "after") || !number(c->words[3], &x) || x < 0)))
        return fail(c, "expected start EVENT [after SECONDS]");
    *after = (float)x;
    return true;
}

/* A whole number of time units: 1 .. INT32_MAX. */
static bool whole(const char *text, int64_t *value)
{
    return digits(&text, value) && !*text && *value >= 1 && *value <= INT32_MAX;
}

/* key TIME VALUE [linear|step]: times strictly increase; step is the default. */
static bool param_key(Compile *c, NwCurve *curve, int count)
{
    double time, value;
    bool linear = count == 4 && !strcmp(c->words[3], "linear"), smooth = count == 4 && !strcmp(c->words[3], "smooth");
    if ((count != 3 && count != 4) || (count == 4 && !linear && !smooth && strcmp(c->words[3], "step")))
        return fail(c, "expected key TIME VALUE [linear|smooth|step]");
    if (!number(c->words[1], &time) || time < 0 || !number(c->words[2], &value))
        return fail(c, "invalid key time or value");
    if (curve->key_count && (float)time <= c->curve_keys_last)
        return fail(c, "key times must increase");
    if ((size_t)c->curve_key_count >= PARAM_KEY_BUDGET) return fail(c, "param key budget exceeded");
    if (c->curve_keys) {
        if (c->curve_key_count >= c->curve_key_capacity) return fail(c, "param key layout changed");
        c->curve_keys[c->curve_key_count] = (NwCurveKey){(float)time, (float)value, linear, smooth};
    }
    c->curve_keys_last = (float)time;
    c->curve_key_count++;
    curve->key_count++;
    return true;
}

/* One property row of a param block; the block decides afterwards whether the set is complete. */
static bool param_property(Compile *c, NwCurve *curve, ParamField field, int count)
{
    int64_t a, b;
    double x, y;
    switch (field) {
    case PARAM_KEY:
        return param_key(c, curve, count);
    case PARAM_WAVE:
        if (count != 3 || strcmp(c->words[1], "cosine") || !number(c->words[2], &x) || x <= 0)
            return fail(c, "expected wave cosine PERIOD");
        curve->wave_period = (float)x;
        return true;
    case PARAM_PULSE:
        if (count != 5 || strcmp(c->words[1], "sine") || !whole(c->words[2], &a) ||
            !whole(c->words[3], &curve->pulse.spacing) || !whole(c->words[4], &curve->pulse.width))
            return fail(c, "expected pulse sine COUNT SPACING WIDTH (whole units)");
        curve->pulse.count = (uint32_t)a;
        return true;
    case PARAM_PERIOD:
        if (count != 2 || !whole(c->words[1], &curve->pulse.period)) return fail(c, "expected period UNITS");
        return true;
    case PARAM_JITTER:
        if (count != 3 || !fraction(c->words[1], &a, &b) || a > b || b > UINT32_MAX ||
            !uint32_word(c->words[2], &curve->pulse.seed))
            return fail(c, "expected jitter FRACTION SEED (fraction of the period, at most 1)");
        curve->pulse.delay_num = (uint32_t)a;
        curve->pulse.delay_den = (uint32_t)b;
        return true;
    case PARAM_RANGE:
        if (count != 3 || !number(c->words[1], &x) || !number(c->words[2], &y)) return fail(c, "expected range LOW HIGH");
        curve->low = (float)x;
        curve->span = (float)(y - x);
        return true;
    case PARAM_PLAYBACK:
        if (count != 2 || (strcmp(c->words[1], "once") && strcmp(c->words[1], "loop")))
            return fail(c, "expected playback once|loop");
        curve->loop = !strcmp(c->words[1], "loop");
        return true;
    case PARAM_DURATION:
        if (count != 2 || !number(c->words[1], &x) || x <= 0) return fail(c, "expected positive duration");
        curve->duration = (float)x;
        return true;
    case PARAM_EVENT:
        curve->event_below = count == 6 && !strcmp(c->words[2], "below");
        if (count != 6 || (strcmp(c->words[2], "at") && !curve->event_below) || strcmp(c->words[4], "after") ||
            !name(curve->event, c->words[1]) || !number(c->words[3], &x) || !number(c->words[5], &y) || y < 0)
            return fail(c, "expected event NAME at|below VALUE after SECONDS");
        curve->event_at = (float)x;
        curve->event_after = (float)y;
        return true;
    case PARAM_START:
        return start_row(c, count, curve->start, &curve->start_after);
    default:
        return fail(c, "unknown param property");
    }
}

/* Checks the property set against the generator, then stores the curve (or only counts it while measuring). */
static bool param_finish(Compile *c, NwCurve *curve, unsigned seen, int first_key)
{
    unsigned generators = seen & (PARAM_BIT(PARAM_KEY) | PARAM_BIT(PARAM_WAVE) | PARAM_BIT(PARAM_PULSE));
    if (generators == PARAM_BIT(PARAM_KEY)) curve->kind = NW_CURVE_KEYS;
    else if (generators == PARAM_BIT(PARAM_WAVE)) curve->kind = NW_CURVE_WAVE;
    else if (generators == PARAM_BIT(PARAM_PULSE)) curve->kind = NW_CURVE_PULSE;
    else return fail(c, "a param track needs exactly one of key, wave or pulse");
    if (seen & ~param_allowed[curve->kind]) return fail(c, "property not valid for this param generator");
    if ((seen & param_required[curve->kind]) != param_required[curve->kind])
        return fail(c, "incomplete param track (wave and pulse need range, pulse needs period)");
    if (!(seen & PARAM_BIT(PARAM_JITTER))) curve->pulse.delay_den = 1;
    if (curve->kind == NW_CURVE_PULSE && curve->pulse.width > curve->pulse.spacing)
        return fail(c, "pulse width exceeds its spacing");
    bool timed = seen & PARAM_BIT(PARAM_DURATION);
    if (curve->loop != timed || (timed && curve->duration < c->curve_keys_last))
        return fail(c, "looping keys need a duration of at least the last key time");
    if (c->curves) {
        if (c->curve_count >= c->curve_capacity) return fail(c, "param layout changed");
        for (int i = 0; i < c->curve_count; i++)
            if (!strcmp(c->curves[i].name, curve->name)) return fail(c, "duplicate param name");
        if (curve->kind == NW_CURVE_KEYS) curve->keys = c->curve_keys + first_key;
        c->curves[c->curve_count] = *curve;
    }
    c->curve_count++;
    return true;
}

/* param NAME { ... }: the header row is already read and checked. */
static bool param_read(Compile *c, int64_t rate_num, int64_t rate_den)
{
    NwCurve curve = {.rate_num = rate_num, .rate_den = rate_den};
    if (!name(curve.name, c->words[1])) return fail(c, "invalid param name");
    if (c->cut_clock && !strcmp(curve.name, "frame")) return fail(c, "frame is the cut clock's own signal");
    int first_key = c->curve_key_count, count;
    unsigned seen = 0;
    c->curve_keys_last = 0;
    while ((count = row(c)) > 0 && strcmp(c->words[0], "}")) {
        int field = param_field(c->words[0]);
        if (field < 0) return fail(c, "unknown param property");
        if (field != PARAM_KEY && (seen & PARAM_BIT(field))) return fail(c, "duplicate param property");
        seen |= PARAM_BIT(field);
        if (!param_property(c, &curve, (ParamField)field, count)) return false;
    }
    if (count < 0) return false;
    if (count != 1) return fail(c, "incomplete param track");
    return param_finish(c, &curve, seen, first_key);
}
static bool finish_track(Compile *c, Track *t, int64_t unit, int64_t rate_num, int64_t rate_den)
{
    NwTexAnimSequence *s=&t->sequence;
    UVPlan *u=&t->uv;
    s->ticks_per_unit=unit;
    if (!multiply(rate_num,unit/rate_den,&s->ticks_per_second))
        return fail(c,"time base exceeds signed 64-bit budget");
    int64_t *out[]={&s->step_ticks,&s->start_ticks,&s->duration_ticks};
    for (int j=0; j<3; j++) if (unit%u->time[j][1] || !multiply(u->time[j][0],unit/u->time[j][1],out[j]))
        return fail(c,"track time exceeds signed 64-bit budget");
    if (t->burst) return true;
    if (t->kind==NW_TEXANIM_IMAGE) {
        int64_t n=(int64_t)s->last-s->first+1;
        if (s->playback==NW_TEXANIM_PINGPONG && n>1) n=2*(n-1);
        if (!multiply(n,s->step_ticks,&s->duration_ticks)) return fail(c,"image period overflow");
    } else if ((s->playback || s->finish_restore) && !s->duration_ticks)
        return fail(c,"UV playback requires duration");
    if (t->kind!=NW_TEXANIM_IMAGE && ((s->duration_ticks && u->last_key>s->duration_ticks) ||
        (s->playback==NW_TEXANIM_PINGPONG && s->duration_ticks>INT64_MAX/2)))
        return fail(c,"invalid UV duration");
    return true;
}
static bool sequence_read(Compile *c, Track *t)
{
    unsigned seen=0;
    int count;
    UVPlan *u=&t->uv;
    NwTexAnimSequence *s=&t->sequence;
    while ((count=row(c))>0 && strcmp(c->words[0],"}")) {
        int ctl=control(c,count);
        if (ctl<0) return false;
        if (ctl) continue;
        if (!strcmp(c->words[0],"source")) {
            if ((seen&4) || count!=3 || strcmp(c->words[1],"resource") || !name(s->source,c->words[2]))
                return fail(c,"expected a single source resource ID");
            seen|=4; continue;
        }
        if (count!=2) return fail(c,"expected one value per property");
        if (!strcmp(c->words[0],"frames")) {
            if ((seen&1) || !frames(c->words[1],&s->first,&s->last)) return fail(c,"invalid or duplicate frames");
            seen|=1;
        } else if (!strcmp(c->words[0],"step")) {
            int64_t *r=u->time[0];
            if ((seen&2) || !fraction(c->words[1],&r[0],&r[1]) || !r[0]) return fail(c,"invalid or duplicate step");
            if (!multiply(u->denominator/gcd(u->denominator,r[1]),r[1],&u->denominator)) return fail(c,"step clock overflow");
            seen|=2;
        } else return fail(c,"unknown sequence property");
    }
    if (count<0) return false;
    if (count!=1 || (seen&3)!=3) return fail(c,"incomplete sequence");
    if (s->source[0] && (s->first || s->last)) return fail(c,"external image requires frames 0..0");
    return true;
}
/* In a file's head: event NAME frame F [after SECONDS] (clock cut: its cutscene's frame), or
   event NAME after EVENT [SECONDS]. */
static bool file_event(Compile *c, int count)
{
    if ((size_t)c->event_count == SDL_arraysize(c->events)) return fail(c, "more than 8 events in one file");
    NwCurveEvent *e = &c->events[c->event_count++];
    double at = 0, after = 0;
    bool frame = count >= 4 && !strcmp(c->words[2], "frame"), ok = name(e->name, c->words[1]);
    if (frame) ok = ok && number(c->words[3], &at) && at >= 0 && (count == 4 || (count == 6 &&
                    !strcmp(c->words[4], "after") && number(c->words[5], &after) && after >= 0));
    else ok = ok && count >= 4 && !strcmp(c->words[2], "after") && name(e->follows, c->words[3]) &&
              strcmp(e->follows, e->name) && (count == 4 || (count == 5 && number(c->words[4], &after) && after >= 0));
    if (!ok) return fail(c, "expected event NAME frame F [after SECONDS] or event NAME after EVENT [SECONDS]");
    if (frame && !c->cut_clock) return fail(c, "frame events need clock cut");
    if (frame) e->follows[0] = 0;
    e->frame = (float)at;
    e->after = (float)after;
    return true;
}

static bool compile(Compile *c, NwTexAnimSequence *sequence)
{
    UVPlan *summary=c->uv;
    NwTexAnimSequence base={.sharing=NW_TEXANIM_SHARED};
    int64_t fixed=c->unit, rate_num=1, rate_den=1, unit;
    int count=row(c);
    bool render=false,mips=false;
    c->track_count=c->sequence_count=c->uv_count=c->key_count=c->burst_count=0;
    c->curve_count=c->curve_key_count=0;
    if (count<0) return false;
    if (count!=3 || strcmp(c->words[0],"ats") || strcmp(c->words[1],"2") || !name(base.effect,c->words[2]))
        return fail(c,"expected ats 2 NAME");
    count=row(c);
    if (count<0) return false;
    if (count<2 || strcmp(c->words[0],"time")) return fail(c,"expected time declaration");
    if (!strcmp(c->words[1],"frames")) {
        if (count!=3 || !fraction(c->words[2],&rate_num,&rate_den) || !rate_num) return fail(c,"expected positive frame rate");
    } else if (count!=2 || strcmp(c->words[1],"seconds")) return fail(c,"expected seconds or frames RATE");
    unit=rate_den;
    count=row(c);
    c->cut_clock=false;
    if (count>0 && !strcmp(c->words[0],"clock")) {
        if (count!=2 || (strcmp(c->words[1],"game") && strcmp(c->words[1],"cut"))) return fail(c,"expected clock game|cut");
        c->cut_clock=!strcmp(c->words[1],"cut");
        count=row(c);
    }
    if (count>0 && !strcmp(c->words[0],"mipmaps")) {
        if (!summary || count!=2 || (strcmp(c->words[1],"off") && strcmp(c->words[1],"generated"))) return fail(c,"invalid mipmap policy");
        render=true;
        mips=!strcmp(c->words[1],"generated");
        count=row(c);
    }
    if (count>0 && !strcmp(c->words[0],"sharing")) {
        if (count!=2 || (strcmp(c->words[1],"shared") && strcmp(c->words[1],"independent"))) return fail(c,"invalid sharing");
        if (!strcmp(c->words[1],"independent")) base.sharing=NW_TEXANIM_INDEPENDENT;
        count=row(c);
    }
    c->start[0]=0;
    c->start_after=0;
    if (count>0 && !strcmp(c->words[0],"start")) {
        if (!start_row(c,count,c->start,&c->start_after)) return false;
        count=row(c);
    }
    for (c->event_count=0; count>0 && !strcmp(c->words[0],"event"); count=row(c))
        if (!file_event(c,count)) return false;
    c->primary=(Track){.sequence=base};
    c->uv_primary=(Track){0};
    /* Measure first, then fill the single owned allocation using the common clock. */
    while (count>0) {
        if (!strcmp(c->words[0],"param")) {
            if (count!=3 || strcmp(c->words[2],"{")) return fail(c,"expected param NAME {");
            if (!param_read(c,rate_num,rate_den)) return false;
            count=row(c);
            continue;
        }
        Track t={.sequence=base,.uv={.value={0,0,1,1,0,.5,.5},.address=-1,.active=render,.mips=mips,.policy=render,
            .denominator=1,.ticks=fixed,.keys=c->keys,.capacity=c->key_capacity,.total=c->key_count,.time={{0,1},{0,1},{0,1}}}};
        bool burst=!strcmp(c->words[0],"burst"), image=burst || !strcmp(c->words[0],"sequence");
        Burst recipe={0};
        t.burst=burst ? &recipe : NULL;
        if (count!=3 || (!image && strcmp(c->words[0],"uv")) || strcmp(c->words[2],"{") ||
            !name(t.sequence.track,c->words[1])) return fail(c,"expected sequence/uv NAME {");
        t.kind=image ? NW_TEXANIM_IMAGE : 0;
        if (!image) t.sequence.first=t.sequence.last=-1;
        t.uv.timing=&t.sequence;
        c->uv=&t.uv;
        if (!(burst ? burst_read(c,&t) : image ? sequence_read(c,&t) : uv_read(c))) return false;
        if (burst) {
            if (c->bursts) {
                if (c->burst_count>=c->burst_capacity) return fail(c,"burst IR layout changed");
                c->bursts[c->burst_count]=recipe;
                t.burst=&c->bursts[c->burst_count];
            }
            c->burst_count++;
        }
        if (!multiply(unit/gcd(unit,t.uv.denominator),t.uv.denominator,&unit)) return fail(c,"time base exceeds signed 64-bit budget");
        if (fixed && !finish_track(c,&t,fixed,rate_num,rate_den)) return false;
        c->key_count=t.uv.total;
        if (image && !c->sequence_count++) c->primary=t;
        if (!image && !c->uv_count++) c->uv_primary=t;
        if ((size_t)c->track_count>=(8u<<20)/sizeof(Track) || (c->tracks && c->track_count>=c->track_capacity)) return fail(c,"track IR budget exceeded");
        if (c->tracks) {
            c->tracks[c->track_count]=t;
            c->tracks[c->track_count].uv.timing=&c->tracks[c->track_count].sequence;
        }
        c->track_count++;
        count=row(c);
    }
    if (count<0) return false;
    if ((!c->track_count && !c->curve_count && !c->event_count) || (fixed && unit!=fixed)) return fail(c,"empty or changed track layout");
    c->unit=unit;
    c->uv=summary;
    if (!c->sequence_count && c->uv_count) c->primary=c->uv_primary; /* Param-only effects keep the base name. */
    if (c->track_count && !finish_track(c,&c->primary,unit,rate_num,rate_den)) return false;
    *sequence=c->primary.sequence;
    if (c->sequence_count==1 && c->uv_count==1 && !strcmp(sequence->track,c->uv_primary.sequence.track)) return fail(c,"duplicate track name");
    if (summary) {
        *summary=c->uv_count ? c->uv_primary.uv : c->primary.uv;
        summary->total=c->key_count;
        summary->ticks=unit;
    }
    return true;
}

extern int NuFileSeek(int handle, int offset, int origin);
static bool rewind_parser(Compile *c, struct nufparpos_s *start)
{
    NuFParSetPos(c->parser,start); c->parser->line_num=start->line_num; c->replay=false;
    c->parser->buffend=start->cpos-1;
    return NuFileSeek(c->parser->fh,start->cpos,0) == start->cpos || fail(c,"cannot rewind ATS parser");
}
static bool measure_program(Compile *c, NwTexAnimSequence *s, UVPlan *u, struct nufparpos_s *start)
{
    NuFParGetPos(c->parser,start); c->uv=u;
    bool ok = NuFParSetLimits(c->parser,4096,255) && compile(c,s);
    u->ticks = s->ticks_per_unit;
    return ok;
}
bool nw_texanim_read_sequence(const char *path, NwTexAnimSequence *output, NwTexAnimError *error)
{
    Compile c = {.path = path ? path : "<null>", .error = error};
    NwTexAnimSequence sequence = {0};
    if (error) *error = (NwTexAnimError){0};
    if (!path || !output) return fail(&c, "path and output are required");
    c.parser = NuFParCreate((char *)path);
    if (!c.parser) return fail(&c, "cannot open parser (file, allocation or text budget)");
    UVPlan u={0}; struct nufparpos_s start;
    bool ok = measure_program(&c,&sequence,&u,&start) && !c.burst_count && c.sequence_count==1 && c.uv_count<=1 &&
              rewind_parser(&c,&start) && compile(&c,&sequence);
    if (!ok && error && !error->message[0]) fail(&c,"read_sequence requires one sequence and at most one UV track");
    if (ok) *output = sequence;
    NuFParDestroy(c.parser);
    return ok;
}

static int64_t track_time(const NwTexAnimSequence *s, int64_t ticks, int64_t duration, bool uv)
{
    if (ticks<s->start_ticks) return s->before_restore ? -1 : 0;
    ticks-=s->start_ticks;
    if (s->playback==NW_TEXANIM_ONCE) return s->finish_restore && duration && ticks>=duration ? -1 : ticks;
    if (duration<=0) return -1;
    int64_t period=uv && s->playback==NW_TEXANIM_PINGPONG ? duration*2 : duration;
    ticks%=period;
    return uv && s->playback==NW_TEXANIM_PINGPONG && ticks>duration ? period-ticks : ticks;
}
int nw_texanim_sample_sequence(const NwTexAnimSequence *s, int64_t ticks)
{
    if (!s || s->step_ticks<=0 || s->first<0 || s->last<s->first || s->start_ticks<0 ||
        (unsigned)s->playback>NW_TEXANIM_PINGPONG) return -1;
    int64_t span=(int64_t)s->last-s->first, count=s->playback==NW_TEXANIM_PINGPONG && span ? span*2 : span+1, period;
    if (!multiply(count,s->step_ticks,&period)) return -1;
    int64_t local=track_time(s,ticks,period,false);
    if (local<0) return -1;
    int64_t frame=local/s->step_ticks;
    if (s->playback==NW_TEXANIM_PINGPONG && frame>span) frame=span*2-frame;
    return s->first+(int)(frame>span ? span : frame);
}

/* Exact native view of the private legacy definition; never changes its layout. */
struct nutexanimprog_s {
    struct nutexanimprog_s *next;
    struct nutexanimprog_s *prev;
    char name[32];
    int on_pc[32];
    int off_pc[32];
    unsigned int on_mask;
    unsigned int off_mask;
    short xdef_label[32];
    short xdef_pc[32];
    int num_xdefs;
    short num_codes;
    int dynalloc : 1;
    short code[2];
};
typedef struct {
    struct nutexanimprog_s base;
    Track legacy, *tracks;
    uint32_t *index;
    unsigned index_capacity;
    int track_count, sequence_count, uv_count;
    NwCurve *curves;   /* Param tracks, in file order. */
    int curve_count;
    bool cut_clock;    /* Autonomous environments follow their cutscene's time. */
    char start[32];    /* The event the param tracks start from ("start"), "" none; start_after seconds later. */
    float start_after;
    NwCurveEvent events[8]; /* The file's own events. */
    int event_count;
    unsigned refs;
    bool registered;
} HostProgram;
typedef struct NuTexAnimList {
    struct nutexanim_s *anims;
    struct NuTexAnimList *next, *prev;
} NuTexAnimList;
extern struct nutexanimprog_s *sys_progs;
extern NuTexAnimList *ntal_first, *ntal_free;
extern void *NuMemAllocFn(int size, char *file, int line);
extern void NuMemFreeFn(void *memory, char *file, int line);
extern int NuMemFileOpen(char *start, int size, int mode);
extern void NuMemFileClose(int handle);
extern int NuFileSeek(int handle, int offset, int origin);
enum { HOST_ATS = 0x4154 }; /* Explicit host IR header: 'AT', version 2; never emitted by V1. */
static NwTexAnimError last_error;

static HostProgram *host(struct nutexanimprog_s *program)
{
    return program && program->num_codes == 2 && program->code[0] == HOST_ATS && program->code[1] == 2
        ? (HostProgram *)program : NULL;
}

static bool reject(const char *path, const char *reason)
{
    Compile c = {.path = path ? path : "<TexAnim>", .error = &last_error};
    return fail(&c, reason);
}

const NwTexAnimError *nw_texanim_error(void) { return &last_error; }

const NwTexAnimSequence *nw_texanim_sequence(struct nutexanimprog_s *program)
{
    HostProgram *p = host(program);
    return p && !p->legacy.burst && p->sequence_count==1 && p->uv_count<=1 ? &p->legacy.sequence : NULL;
}

bool nw_texanim_curves(struct nutexanimprog_s *program, NwCurveSet *out)
{
    HostProgram *p = host(program);
    if (!p || (!p->curve_count && !p->event_count) || !out) return reject(NULL, "effect has no param tracks or events");
    *out = (NwCurveSet){.effect = p->base.name, .curves = p->curves, .count = (size_t)p->curve_count,
                        .start = p->start[0] ? p->start : NULL, .after = &p->start_after,
                        .events = p->events, .event_count = (size_t)p->event_count};
    return true;
}

static void free_program(HostProgram *p)
{
    for (int j=0; j<p->track_count; j++) nw_texture_release(p->tracks[j].image);
    NuMemFreeFn(p, __FILE__, __LINE__);
}
static bool c3_controls(const Track *t)
{
    const NwTexAnimSequence *s=&t->sequence, *u=t->uv.timing;
    return !t->burst && !s->start_ticks && !s->playback && !s->before_restore && !s->finish_restore &&
           (!t->uv.owned || (!u->start_ticks && !u->playback && !u->before_restore && !u->finish_restore));
}

/* A single C3 sequence, stepped by the host through the legacy environment API (nw_texanim_time). */
static bool legacy_env(const HostProgram *p)
{
    return nw_texanim_sequence((struct nutexanimprog_s *)&p->base) && c3_controls(&p->legacy);
}
static void auto_forget(const struct nutexanimenv_s *env);
static void auto_host(const struct nutexanimenv_s *env);

static void release(HostProgram *p)
{
    if (p && p->refs && !--p->refs && !p->registered) free_program(p);
}

bool nw_texanim_retain(struct nutexanimprog_s *program)
{
    HostProgram *p = host(program);
    if (!p || p->refs == UINT_MAX) return reject(NULL, "cannot retain TexAnim program");
    p->refs++;
    return true;
}

void nw_texanim_release(struct nutexanimprog_s *program) { release(host(program)); }

void NuTexAnimProgDestroy(struct nutexanimprog_s *program)
{
    HostProgram *p = host(program);
    if (!p) { NuTexAnimProgDestroy__retail(program); return; }
    if (!p->registered) return;
    p->registered = false;
    NuTexAnimProgDestroy__retail(program); /* The host container has dynalloc=0: unlink only. */
    if (!p->refs) free_program(p);
}

static void listed_clear(void);
static void scene_add(HostProgram *p);
static void scene_replace(HostProgram *was, HostProgram *next);
static void scene_clear(void);

static void retire(void)
{
    listed_clear();
    for (struct nutexanimprog_s *p = sys_progs, *next; p; p = next) {
        next = p->next;
        if (host(p)) NuTexAnimProgDestroy(p);
    }
}

/* ---- Scene lists: the ATS V2 effects a cutscene or level loads before its retail scripts ---- */

extern s32 cutmovie, Level;
static char *scene_list;
static NwTexAnimIO scene_io;

bool nw_texanim_scene_list(const char *path, NwTexAnimIO io)
{
    SDL_free(scene_list);
    scene_list = path ? SDL_strdup(path) : NULL;
    scene_io = io;
    return !path || scene_list;
}

/* "cut N" applies while cutscene N loads, "level N" while level N loads outside a cutscene. */
static bool scene_header(const char *text)
{
    char kind[8];
    int n;
    if (SDL_sscanf(text, "%7s %d", kind, &n) != 2) return false;
    return !SDL_strcmp(kind, "cut") ? cutmovie == n : !SDL_strcmp(kind, "level") && cutmovie < 0 && Level == n;
}

/* The programs the scene list loaded, by file: what a reload replaces. Forgotten with the registry (retire). */
typedef struct { char *path; struct nutexanimprog_s *program; } Listed;
static Listed *listed;
static size_t listed_count;

static void listed_clear(void)
{
    scene_clear();
    for (size_t i = 0; i < listed_count; i++) SDL_free(listed[i].path);
    listed_count = 0;
}

static Listed *listed_find(const char *path)
{
    for (size_t i = 0; i < listed_count; i++) if (!SDL_strcmp(listed[i].path, path)) return &listed[i];
    return NULL;
}

static void listed_log(const char *what, const char *path)
{
    SDL_Log("ATS %s: %s: %s", what, path, nw_texanim_error()->message[0] ? nw_texanim_error()->message : SDL_GetError());
}

/* source resource ID in a listed file: the PNG ID beside it, read through the list's io. The program keeps its own
   reference; the images made while it compiles are given back afterwards. */
typedef struct { NwTexAnimIO io; const char *path; NwTexture *made[8]; size_t count; } ListedImages;
static struct nutexanimprog_s *replace_resolved(struct nutexanimprog_s *old, const char *path, const void *text,
                                               size_t size, NwTexAnimResolver resolve, void *context);

static NwTexture *listed_image(void *context, const char *id)
{
    ListedImages *l = context;
    const char *slash = SDL_strrchr(l->path, '/');
    char path[256];
    SDL_snprintf(path, sizeof(path), "%.*s%s", slash ? (int)(slash - l->path + 1) : 0, l->path, id);
    if (l->count == SDL_arraysize(l->made)) { SDL_SetError("more than 8 images in one file"); return NULL; }
    NwTexAnimFile data = {0};
    NwTexture *image = l->io.read(l->io.context, path, &data) ? nw_texture_png(data.bytes, data.size) : NULL;
    if (data.bytes) l->io.release(l->io.context, &data);
    if (image) l->made[l->count++] = image;
    return image;
}

/* A listed file compiled, in old's place when there is one. */
static struct nutexanimprog_s *listed_compile(NwTexAnimIO io, struct nutexanimprog_s *old, const char *path)
{
    NwTexAnimFile data = {0};
    ListedImages images = {io, path};
    struct nutexanimprog_s *p = !io.read(io.context, path, &data) ? NULL :
        old ? replace_resolved(old, path, data.bytes, data.size, listed_image, &images) :
              nw_texanim_from_memory_resolved(path, data.bytes, data.size, listed_image, &images);
    if (data.bytes) io.release(io.context, &data);
    for (size_t i = 0; i < images.count; i++) nw_texture_release(images.made[i]);
    return p;
}

static void load_listed(void *context, const char *path)
{
    (void)context;
    struct nutexanimprog_s *p = listed_compile(scene_io, NULL, path);
    if (!p) { listed_log("scene list", path); return; }
    if (!host(p)->track_count) scene_add(host(p)); /* only param tracks: no model will wear it */
    Listed *more = SDL_realloc(listed, (listed_count + 1) * sizeof(*more));
    char *copy = SDL_strdup(path);
    if (more) listed = more;
    if (!more || !copy) { SDL_free(copy); return; } /* loaded, only not reloadable */
    listed[listed_count++] = (Listed){copy, p};
}

/* A listed file read again in place of its program; a file new to the list loads. */
static void reload_listed(void *context, const char *path)
{
    Listed *l = listed_find(path);
    if (!l) { load_listed(context, path); return; }
    struct nutexanimprog_s *p = listed_compile(scene_io, l->program, path);
    if (p) l->program = p;
    else listed_log("reload", path);
}

/* Calls entry() for each .ats a list names: in the loading scene's sections, or in every section.
   Section headers start a line; indented lines name .ats files; ';' starts a comment. */
static void scene_list_each(const char *list, NwTexAnimIO io, bool every_section,
                            void (*entry)(void *context, const char *path), void *context)
{
    NwTexAnimFile data = {0};
    if (!io.read(io.context, list, &data)) { SDL_Log("ATS scene list %s: %s", list, SDL_GetError()); return; }
    bool active = false;
    for (const char *p = data.bytes, *end = data.bytes + data.size; p < end;) {
        const char *eol = memchr(p, '\n', (size_t)(end - p));
        size_t length = (size_t)((eol ? eol : end) - p);
        char line[256];
        SDL_strlcpy(line, p, SDL_min(length + 1, sizeof(line)));
        p += length + 1;
        line[strcspn(line, ";\r")] = 0;
        char *text = line + strspn(line, " \t");
        for (char *back = text + SDL_strlen(text); back > text && space(back[-1]);) *--back = 0;
        if (!*text) continue;
        if (text == line) active = every_section || scene_header(text);
        else if (active) entry(context, text);
    }
    io.release(io.context, &data);
}

static void load_scene_list(void)
{
    if (scene_list) scene_list_each(scene_list, scene_io, false, load_listed, NULL);
}

void nw_texanim_scene_reload(void)
{
    if (scene_list) scene_list_each(scene_list, scene_io, false, reload_listed, NULL);
}

/* Every param track of every listed effect, so a host can make their signals before any scene loads. */
typedef struct {
    NwTexAnimIO io;
    void (*each)(void *context, const char *effect, const NwCurve *curve);
    void *context;
} CurveVisit;

static void visit_curves(void *context, const char *path)
{
    const CurveVisit *v = context;
    struct nutexanimprog_s *program = listed_compile(v->io, NULL, path);
    HostProgram *p = host(program);
    for (int i = 0; p && i < p->curve_count; i++) v->each(v->context, p->base.name, &p->curves[i]);
    if (program) NuTexAnimProgDestroy(program);
}

void nw_texanim_scene_curves(const char *list, NwTexAnimIO io,
                             void (*each)(void *context, const char *effect, const NwCurve *curve), void *context)
{
    CurveVisit visit = {io, each, context};
    scene_list_each(list, io, true, visit_curves, &visit);
}

void NuTexAnimProgSysInit(void)
{
    retire();
    NuTexAnimProgSysInit__retail();
    load_scene_list(); /* Before the retail scripts, so a listed V2 replaces a V1 of the same name. */
}

void NuTexAnimProgRelease(void)
{
    retire();
    NuTexAnimProgRelease__retail();
}

static bool collision(struct nutexanimprog_s *program)
{
    for (struct nutexanimprog_s *p = sys_progs; p; p = p->next)
        if (p != program && (host(p) || host(program)) && !SDL_strcasecmp(p->name, program->name)) return true;
    return false;
}

/* Puts a V2 program in the registry: a new one, or one a failed reload puts back. */
static void enlist(HostProgram *p)
{
    p->registered = true;
    p->base.prev = NULL;
    p->base.next = sys_progs;
    if (sys_progs) sys_progs->prev = &p->base;
    sys_progs = &p->base;
}

/* The registered V2 program with this name, ignoring retail ones. */
static struct nutexanimprog_s *host_named(const char *name)
{
    for (struct nutexanimprog_s *p = sys_progs; p; p = p->next)
        if (host(p) && !SDL_strcasecmp(p->name, name)) return p;
    return NULL;
}

static unsigned track_slot(const HostProgram *p, const char *name)
{
    uint32_t h=2166136261u;
    for (const unsigned char *s=(const unsigned char *)name; *s; s++) h=(h^*s)*16777619u;
    h&=p->index_capacity-1;
    while (p->index[h] && strcmp(p->tracks[p->index[h]-1].sequence.track,name)) h=(h+1)&(p->index_capacity-1);
    return h;
}
static struct nutexanimprog_s *register_sequence(Compile *c, const NwTexAnimSequence *sequence, UVPlan *u,
                                                struct nufparpos_s *start, const char *path,
                                                NwTexAnimResolver resolve, void *context)
{
    if (NuTexAnimProgFind((char *)sequence->effect)) { reject(path,"effect name already loaded"); return NULL; }
    unsigned slots=2;
    while (slots<2u*(unsigned)c->track_count) slots*=2;
    size_t curve_bytes=(size_t)c->curve_count*sizeof(NwCurve)+(size_t)c->curve_key_count*sizeof(NwCurveKey);
    size_t bytes=sizeof(HostProgram)+(size_t)c->track_count*sizeof(Track)+slots*sizeof(uint32_t)+(size_t)u->total*sizeof(UVKey)+
                 curve_bytes+(size_t)c->burst_count*sizeof(Burst);
    if (bytes>(8u<<20)) { reject(path,"track/key IR budget exceeded"); return NULL; }
    HostProgram *p=NuMemAllocFn((int)bytes,__FILE__,__LINE__);
    if (!p) { reject(path,"cannot allocate ATS V2"); return NULL; }
    memset(p,0,bytes);
    p->tracks=(Track *)(p+1);
    p->index=(uint32_t *)(p->tracks+c->track_count);
    p->index_capacity=slots;
    p->track_count=c->track_count;
    union variptr_u layout={.voidptr=p};
    NuTexAnimProgCreate(&layout,0,(char *)sequence->effect);
    if (layout.u8!=(unsigned char *)p+sizeof(p->base)) { free_program(p); reject(path,"incompatible native program layout"); return NULL; }
    c->tracks=p->tracks;
    c->track_capacity=p->track_count;
    c->keys=(UVKey *)(p->index+slots);
    c->key_capacity=u->total;
    /* 8-byte aligned after the UV keys: curves, their keys, then the 4-byte aligned bursts. */
    c->curves=(NwCurve *)(c->keys+u->total);
    c->curve_capacity=c->curve_count;
    c->curve_keys=(NwCurveKey *)(c->curves+c->curve_count);
    c->curve_key_capacity=c->curve_key_count;
    c->bursts=(Burst *)(c->curve_keys+c->curve_key_count);
    c->burst_capacity=c->burst_count;
    NwTexAnimSequence clock={0};
    if (!rewind_parser(c,start) || !compile(c,&clock) || c->track_count!=p->track_count) { free_program(p); return NULL; }
    p->sequence_count=c->sequence_count;
    p->uv_count=c->uv_count;
    p->curves=c->curves;
    p->curve_count=c->curve_count;
    p->cut_clock=c->cut_clock;
    memcpy(p->start,c->start,sizeof(p->start));
    p->start_after=c->start_after;
    memcpy(p->events,c->events,sizeof(p->events));
    p->event_count=c->event_count;
    for (int j=0; j<p->track_count; j++) {
        Track *t=&p->tracks[j];
        unsigned slot=track_slot(p,t->sequence.track);
        if (p->index[slot]) { reject(path,"duplicate track name"); free_program(p); return NULL; }
        p->index[slot]=(unsigned)j+1;
        if (t->sequence.source[0]) {
            SDL_ClearError();
            NwTexture *image=resolve ? resolve(context,t->sequence.source) : NULL;
            if (!image || !nw_texture_retain(image)) { reject(path,SDL_GetError()[0] ? SDL_GetError() : "external texture requires a resolver"); free_program(p); return NULL; }
            t->image=image;
            t->image_tid=(short)nw_texture_tid(image);
        }
    }
    p->legacy=c->primary;
    for (int j=0; j<p->track_count; j++) if (p->tracks[j].kind==NW_TEXANIM_IMAGE) { p->legacy=p->tracks[j]; break; }
    if (p->uv_count==1) for (int j=0; j<p->track_count; j++) if (!p->tracks[j].kind) p->legacy.uv=p->tracks[j].uv;
    p->base.num_codes=2;
    p->base.code[0]=HOST_ATS;
    p->base.code[1]=2;
    enlist(p);
    return &p->base;
}

static bool memory_parser(Compile *c, const void *text, size_t size)
{
    if (!text || !size || size > INT_MAX || memchr(text,0,size)) return fail(c,"invalid ATS text size or embedded NUL");
    int handle = NuMemFileOpen((char *)text,(int)size,0);
    c->parser = handle ? NuFParOpenSized(handle,(int)size) : NULL;
    if (!c->parser) {
        if (handle) NuMemFileClose(handle);
        return fail(c,"cannot open ATS memory parser");
    }
    return true;
}

struct nutexanimprog_s *nw_texanim_from_memory(const char *path, const void *text, size_t size)
{
    return nw_texanim_from_memory_resolved(path,text,size,NULL,NULL);
}

struct nutexanimprog_s *nw_texanim_from_memory_resolved(const char *path, const void *text, size_t size,
                                                     NwTexAnimResolver resolve, void *context)
{
    last_error = (NwTexAnimError){0};
    Compile c = {.path = path ? path : "<memory ATS>", .error = &last_error};
    if (!memory_parser(&c,text,size)) return NULL;
    NwTexAnimSequence sequence = {0}; UVPlan u={0}; struct nufparpos_s start;
    bool ok = measure_program(&c,&sequence,&u,&start);
    struct nutexanimprog_s *p = ok ? register_sequence(&c,&sequence,&u,&start,path,resolve,context) : NULL;
    NuFParDestroy(c.parser);
    return p;
}

struct nutexanimprog_s *NuTexAnimProgReadScript(union variptr_u *buffer, char *path)
{
    last_error = (NwTexAnimError){0};
    Compile c = {.path = path, .error = &last_error, .parser = NuFParCreate(path)};
    if (!c.parser) { reject(path, "cannot open ATS parser"); return NULL; }
    struct nufparpos_s start; NuFParGetPos(c.parser,&start);
    NuFParSetLimits(c.parser, 4096, 255);
    bool marked = false;
    while (NuFParGetLine(c.parser)) {
        const char *p = NuFParLine(c.parser);
        while (space(*p)) p++;
        if (!*p || *p == ';') continue;
        NuFParGetWord(c.parser);
        marked = !SDL_strcasecmp(NuFParWord(c.parser), "ats");
        break;
    }
    const char *error = NuFParGetError(c.parser);
    struct nutexanimprog_s *compiled = NULL;
    if (marked && !error && rewind_parser(&c,&start)) {
        NwTexAnimSequence sequence={0}; UVPlan u={0};
        if (measure_program(&c,&sequence,&u,&start)) compiled=register_sequence(&c,&sequence,&u,&start,path,NULL,NULL);
    }
    bool ok = error ? fail(&c,error) : !marked || compiled != NULL;
    NuFParDestroy(c.parser);
    if (!ok) return NULL;
    if (!marked) {
        union variptr_u cursor = {.voidptr = buffer ? buffer->voidptr : NULL};
        struct nutexanimprog_s *p = NuTexAnimProgReadScript__retail(buffer ? &cursor : NULL, path);
        if (p && collision(p)) {
            /* A listed V2 with this name replaces the retail script (nw_texanim_scene_list). */
            struct nutexanimprog_s *replacement = host_named(p->name);
            bool heap = !buffer && !p->dynalloc;
            NuTexAnimProgDestroy__retail(p);
            if (heap) NuMemFreeFn(p, __FILE__, __LINE__);
            if (host(replacement)) return replacement;
            reject(path, "name already belongs to an ATS V2");
            return NULL;
        }
        if (p && buffer) *buffer = cursor;
        return p;
    }
    return compiled;
}

void NuTexAnimProgWrite(char *path, struct nutexanimprog_s *program)
{
    if (host(program)) { reject(path, "ATS V2 has no native binary writer"); return; }
    NuTexAnimProgWrite__retail(path, program);
}

struct nutexanimprog_s *NuTexAnimProgRead(union variptr_u *buffer, char *path)
{
    union variptr_u cursor = {.voidptr = buffer ? buffer->voidptr : NULL};
    struct nutexanimprog_s *p = NuTexAnimProgRead__retail(buffer ? &cursor : NULL, path);
    if (p && (p->code[0] == HOST_ATS || collision(p))) {
        NuTexAnimProgDestroy__retail(p); /* A native dump is never a host container. */
        reject(path, "unsupported or conflicting native TexAnim program");
        return NULL;
    }
    if (p && buffer) *buffer = cursor;
    return p;
}

struct nutexanimenv_s *NuTexAnimEnvCreate(union variptr_u *buffer, struct numtl_s *mtl,
                                        short *tids, struct nutexanimprog_s *program)
{
    HostProgram *p = host(program);
    if (p && p->refs == UINT_MAX) { reject(NULL, "too many ATS instances"); return NULL; }
    struct nutexanimenv_s *env = NuTexAnimEnvCreate__retail(buffer, mtl, tids, program);
    if (env && p) {
        p->refs++;
        if (legacy_env(p) && p->legacy.image) env->tids = &p->legacy.image_tid;
    }
    return env;
}

void NuTexAnimEnvDestroy(struct nutexanimenv_s *env)
{
    if (!env) return;
    auto_forget(env);
    HostProgram *p = host(env->prog);
    if (p && p->legacy.image && env->mtl && env->mtl->tid == p->legacy.image_tid) env->mtl->tid = 0;
    env->prog = NULL;
    NuTexAnimEnvDestroy__retail(env);
    release(p);
}

void NuTexAnimEnvReset(struct nutexanimenv_s *env)
{
    auto_forget(env); /* An autonomous effect starts over with its next frame. */
    NuTexAnimEnvReset__retail(env);
    HostProgram *p = env ? host(env->prog) : NULL;
    if (p) env->tex_ix = p->legacy.sequence.first;
}

void NuTexAnimEnvProc(struct nutexanimenv_s *env)
{
    HostProgram *p = host(env->prog);
    if (!p) { NuTexAnimEnvProc__retail(env); return; }
    if (env->mtl && env->tids) env->mtl->tid = env->tids[env->tex_ix];
}

static bool material_conflict(struct nutexanim_s *anim, struct nutexanim_s *list, bool v2)
{
    for (; list; list = list->succ)
        if (list != anim && list->env && list->env->prog && list->mtl == anim->mtl &&
            (v2 || host(list->env->prog))) return true;
    return false;
}

bool nw_texanim_bind(struct nutexanim_s *anim, struct nutexanimprog_s *program)
{
    if (!anim || !anim->env) return reject(NULL, "missing texture animation environment");
    HostProgram *p = host(program);
    bool legacy = p && legacy_env(p); /* Others are autonomous: their players check their own images. */
    if (p && (!anim->mtl || p->refs == UINT_MAX ||
              (legacy && !p->legacy.image && (!anim->tids || p->legacy.sequence.last >= anim->numtids))))
        return reject(anim->ntaname, "texture interval exceeds binding capacity");
    struct nutexanim_s *root = anim;
    while (root->prev) root = root->prev;
    if (program && material_conflict(anim, root, p != NULL))
        return reject(anim->ntaname, "shared material unsupported for V2");
    for (NuTexAnimList *list = ntal_first; list; list = list->next)
        if (program && material_conflict(anim, list->anims, p != NULL))
            return reject(anim->ntaname, "material already animated");
    struct nutexanimprog_s *old = anim->env->prog;
    if (host(old) && host(old)->legacy.image && !p && anim->mtl) anim->mtl->tid = anim->tids && anim->numtids > 0 ? anim->tids[0] : 0;
    if (old != program) {
        if (p) p->refs++;
        anim->env->prog = program;
        release(host(old));
    }
    anim->env->mtl = anim->mtl;
    anim->env->tids = legacy && p->legacy.image ? &p->legacy.image_tid : anim->tids;
    NuTexAnimEnvReset(anim->env);
    if (legacy) NuTexAnimEnvProc(anim->env);
    return true;
}

bool nw_texanim_time(struct nutexanim_s *anim, int64_t ticks)
{
    HostProgram *p = anim && anim->env ? host(anim->env->prog) : NULL;
    if (!p || !legacy_env(p) || (!p->legacy.image && p->legacy.sequence.last >= anim->numtids)) return false;
    if (!nw_texanim_sequence(anim->env->prog) || !c3_controls(&p->legacy) || p->legacy.uv.active || p->legacy.uv.mips)
        return reject(NULL,"track controls/UV/mips require an instance binding");
    auto_host(anim->env); /* From now on the host clocks it, not NuTexAnimProcess. */
    anim->env->tex_ix = nw_texanim_sample_sequence(&p->legacy.sequence, ticks);
    NuTexAnimEnvProc(anim->env);
    return true;
}

static bool clock_ticks(const NwTexAnimSequence *sequence, double local, double rate, int64_t *value)
{
    if (!sequence || !isfinite(local) || !isfinite(rate) || rate <= 0)
        return reject(NULL, "invalid TexAnim clock or source rate");
    /* The EE double launcher supports exact 32-bit conversions, not direct int64 conversions. */
    double frequency = wide_number((uint64_t)sequence->ticks_per_second);
    double ratio = frequency / rate;
    if (!isfinite(ratio)) return reject(NULL, "TexAnim clock ratio exceeds conversion precision");
    double ticks = local * ratio;
    *value = ticks >= 0x1p63 ? INT64_MAX : ticks <= -0x1p63 ? INT64_MIN : (int64_t)floor(ticks);
    return true;
}

bool nw_texanim_clock(struct nutexanim_s *anim, double local, double rate)
{
    const NwTexAnimSequence *sequence = anim && anim->env ? nw_texanim_sequence(anim->env->prog) : NULL;
    int64_t ticks;
    return clock_ticks(sequence, local, rate, &ticks) && nw_texanim_time(anim, ticks);
}

struct NwTexAnimInstance {
    struct nutexanimprog_s *program;
    const Track *track;
    NwTexture *image;
    NwTexAnimConfig config;
    NwTexAnimInputs inputs;
    unsigned refs;
    uint32_t binding;
    int count, frame;
    int64_t ticks;
    bool stopped, completed;
    short tids[];
};
static uint32_t next_binding;
uint32_t nw_texanim_binding(void)
{
    if (next_binding==UINT32_MAX) { reject(NULL,"texture binding identity exhausted"); return 0; }
    return ++next_binding;
}
NwTexAnimInstance *nw_texanim_instance(struct nutexanimprog_s *p, const struct nutexanim_s *s,
                                     NwTexAnimConfig c, NwTexAnimInstance *share)
{
    return nw_texanim_instance_track(p,NULL,s,c,share);
}
NwTexAnimInstance *nw_texanim_instance_track(struct nutexanimprog_s *program, const char *name,
    const struct nutexanim_s *source, NwTexAnimConfig config, NwTexAnimInstance *share)
{
    HostProgram *p=host(program);
    const Track *t=NULL;
    if (p && name) {
        unsigned slot=track_slot(p,name);
        if (p->index[slot]) t=&p->tracks[p->index[slot]-1];
    }
    else if (p && nw_texanim_sequence(program) && c3_controls(&p->legacy)) t=&p->legacy;
    if (!t || !config.clock) { reject(NULL,"missing track selector, source or clock"); return NULL; }
    const NwTexAnimSequence *s=&t->sequence;
    if (!t->burst) config.channel=0;
    if (!t->burst || !t->burst->session) config.variant=0;
    bool image=t->kind==NW_TEXANIM_IMAGE;
    short base=source && source->mtl ? (short)source->mtl->tid : 0;
    bool model=source && source->tids && source->numtids>0;
    const short *tids=t->image ? &t->image_tid : !image && t->uv.mips && !model ? &base : source ? source->tids : NULL;
    int count=t->image ? 1 : !image ? (t->uv.mips ? (model ? source->numtids : 1) : 0) : source ? source->numtids : 0;
    if ((image && (!tids || count<=s->last)) || (!image && t->uv.mips && base<=0)) { reject(NULL,"track source does not cover images/mips"); return NULL; }
    if (config.sharing==NW_TEXANIM_DEFAULT) config.sharing=s->sharing;
    if (config.sharing!=NW_TEXANIM_SHARED && config.sharing!=NW_TEXANIM_INDEPENDENT) { reject(NULL,"invalid instance sharing"); return NULL; }
    if (share) {
        if (config.sharing!=NW_TEXANIM_SHARED || share->config.sharing!=config.sharing || share->track!=t ||
            share->program!=program || share->config.channel!=config.channel || share->config.variant!=config.variant || share->config.clock!=config.clock || share->config.origin!=config.origin ||
            share->count!=count || share->refs==UINT_MAX || (count && memcmp(share->tids,tids,(size_t)count*sizeof(short)))) {
            reject(NULL,"incompatible shared instance"); return NULL;
        }
        share->refs++;
        return share;
    }
    if ((size_t)count>(INT_MAX-sizeof(NwTexAnimInstance))/sizeof(short) ||
        ((t->uv.active || t->uv.mips) && next_binding==UINT32_MAX)) { reject(NULL,"instance allocation/identity budget exceeded"); return NULL; }
    NwTexAnimInstance *i=NuMemAllocFn(sizeof(*i)+(size_t)count*sizeof(short),__FILE__,__LINE__);
    if (!i) { reject(NULL,"cannot allocate texture instance"); return NULL; }
    if (!nw_texanim_retain(program)) { NuMemFreeFn(i,__FILE__,__LINE__); return NULL; }
    *i=(NwTexAnimInstance){.program=program,.track=t,.config=config,.inputs={100,true,true},.refs=1,.count=count,.frame=-1};
    if (t->uv.active || t->uv.mips) i->binding=nw_texanim_binding();
    if (count) memcpy(i->tids,tids,(size_t)count*sizeof(short));
    nw_texanim_instance_time(i,config.origin);
    return i;
}
void nw_texanim_instance_drop(NwTexAnimInstance *i)
{
    if (i && !--i->refs) { nw_texture_release(i->image); nw_texanim_release(i->program); NuMemFreeFn(i,__FILE__,__LINE__); }
}
bool nw_texanim_instance_inputs(NwTexAnimInstance *i, NwTexAnimInputs inputs)
{
    if (!i || !i->track->burst || !isfinite(inputs.chance) || inputs.chance<0 || inputs.chance>100)
        return reject(NULL,"burst inputs require a burst and finite chance in 0..100");
    i->inputs=inputs; /* Consumed by the next explicit time/clock sample. */
    return true;
}
bool nw_texanim_instance_time(NwTexAnimInstance *i, int64_t ticks)
{
    if (!i) return reject(NULL,"missing texture instance");
    int64_t origin=i->config.origin;
    ticks=origin>0 && ticks<INT64_MIN+origin ? INT64_MIN : origin<0 && ticks>INT64_MAX+origin ? INT64_MAX : ticks-origin;
    const Track *t=i->track;
    const NwTexAnimSequence *s=&t->sequence;
    int frame=-1;
    if (t->burst) {
        if (i->inputs.enabled && !(ticks<s->start_ticks && s->before_restore)) {
            bool on=false;
            int64_t local=ticks<s->start_ticks ? 0 : (ticks-s->start_ticks)/s->ticks_per_unit;
            if (i->inputs.condition && !nw_burst_sample(&t->burst->recipe,local,i->config.channel^i->config.variant,i->inputs.chance,0,&on))
                return reject(NULL,"invalid burst sample or operation budget exceeded");
            frame=(int)t->burst->recipe.frames[on ? 1 : 0];
        }
    } else if (t->kind==NW_TEXANIM_IMAGE) frame=nw_texanim_sample_sequence(s,ticks);
    i->ticks=ticks;
    i->frame=frame;
    i->stopped=i->completed=false;
    return true;
}
bool nw_texanim_instance_clock_integer(NwTexAnimInstance *i, int64_t value, uint32_t rate)
{
    if (!i || !rate) return reject(NULL,"missing integer clock or source rate");
    uint64_t g=(uint64_t)gcd(i->track->sequence.ticks_per_second,rate);
    uint64_t mul=(uint64_t)i->track->sequence.ticks_per_second/g, den=rate/g;
    bool negative=value<0;
    uint64_t mag=negative ? (uint64_t)(-(value+1))+1 : (uint64_t)value, limit=(uint64_t)INT64_MAX+negative;
    uint64_t q=mag/den, r=mag%den, product=r*(mul%den);
    /* den is uint32: its remainder product fits uint64, without compiler-specific int128. */
    uint64_t tail=r*(mul/den)+product/den+(negative && product%den!=0);
    if (q>limit/mul || tail>limit-q*mul) return reject(NULL,"integer clock exceeds signed 64-bit budget");
    uint64_t total=q*mul+tail;
    int64_t ticks;
    if (negative) ticks=total==(uint64_t)INT64_MAX+1 ? INT64_MIN : -(int64_t)total;
    else ticks=(int64_t)total;
    return nw_texanim_instance_time(i,ticks);
}
bool nw_texanim_instance_clock(NwTexAnimInstance *i, double local, double rate)
{
    int64_t ticks;
    return clock_ticks(i ? &i->track->sequence : NULL,local,rate,&ticks) && nw_texanim_instance_time(i,ticks);
}
unsigned nw_texanim_instance_mask(const NwTexAnimInstance *i)
{
    if (!i || i->stopped) return 0;
    const UVPlan *u=&i->track->uv;
    unsigned mask=i->frame>=0 ? NW_TEXANIM_IMAGE : 0;
    if (u->owned && (i->completed ? !u->timing->finish_restore : track_time(u->timing,i->ticks,u->timing->duration_ticks,true)>=0)) mask|=u->owned;
    return mask;
}
bool nw_texanim_claim(unsigned *owned, unsigned properties)
{
    if (!owned || (properties&~63u) || (*owned&properties)) return reject(NULL,"conflicting or invalid track properties");
    *owned|=properties;
    return true;
}
bool nw_texanim_instance_complete(NwTexAnimInstance *i)
{
    if (!i || i->track->burst || i->track->sequence.playback || (i->track->uv.owned && i->track->uv.timing->playback))
        return reject(NULL,"cyclic tracks require an explicit time or restore");
    /* Completion is a state, not a seek: origin/start can make MAX an earlier sample. */
    i->ticks=INT64_MAX;
    i->stopped=false;
    i->completed=true;
    i->frame=i->track->kind==NW_TEXANIM_IMAGE && !i->track->sequence.finish_restore ? i->track->sequence.last : -1;
    return true;
}
void nw_texanim_instance_stop(NwTexAnimInstance *i) { if (i) i->stopped=true; }
int nw_texanim_instance_frame(const NwTexAnimInstance *i) { return i && !i->stopped ? i->frame : -1; }
int nw_texanim_instance_tid(const NwTexAnimInstance *i) { int f=nw_texanim_instance_frame(i); return f>=0 ? i->tids[f] : 0; }
bool nw_texanim_instance_array(NwTexAnimInstance *i, const NwTexAnimInstance *reuse)
{
    if (!i) return reject(NULL,"missing array instance");
    if (i->image || !i->count) return true;
    return nw_texture_images(i->tids,i->count,i->track->uv.mips,reuse ? reuse->image : NULL,&i->image) || reject(NULL,SDL_GetError());
}
const NwGpuTex *nw_texanim_instance_texture(const NwTexAnimInstance *i) { return i ? nw_texture_gpu(i->image) : NULL; }
int64_t nw_texanim_instance_uv_cycle(const NwTexAnimInstance *i)
{
    const NwTexAnimSequence *s=i && i->track->uv.owned ? i->track->uv.timing : NULL;
    return s && s->playback && i->ticks>=s->start_ticks ? (i->ticks-s->start_ticks)/s->duration_ticks : 0;
}
bool nw_texanim_instance_uv(const NwTexAnimInstance *i, NwUV *out)
{
    if (!i || !out) return reject(NULL,"missing UV instance/output");
    const UVPlan *u=&i->track->uv;
    int64_t ticks=i->completed ? (u->owned && u->timing->finish_restore ? -1 : INT64_MAX) :
                  u->owned ? track_time(u->timing,i->ticks,u->timing->duration_ticks,true) : i->ticks;
    *out=(NwUV){.binding=i->stopped || ticks<0 ? 0 : i->binding,.linear=u->linear,.address=u->address,.mips=u->mips};
    for (int j=0; j<7; j++) out->value[j]=(float)u->value[j];
    for (int axis=0; axis<3; axis++) {
        out->owner[axis]=(u->owned & (1u<<axis)) ? i->binding : 0;
        out->cycle[axis]=nw_texanim_instance_uv_cycle(i);
        out->segment[axis]=-1;
        int count=u->count[axis]; if (!count) continue;
        const UVKey *k=u->keys+u->first[axis]; int lo=0, hi=count;
        while (lo<hi) { int mid=lo+(hi-lo)/2; if (k[mid].time<=ticks) lo=mid+1; else hi=mid; }
        int n=lo ? lo-1 : 0; double ratio=0;
        if (lo && lo<count) {
            out->segment[axis]=n;
            if (u->linear & (1u<<axis)) ratio=wide_number(ticks-k[n].time)/wide_number(k[n+1].time-k[n].time);
        }
        for (int j=0; j<(axis==2 ? 1 : 2); j++)
            out->value[axis*2+j]=(float)(k[n].value[j]+(ratio ? (k[n+1].value[j]-k[n].value[j])*ratio : 0));
    }
    return true;
}

bool nw_texanim_compose(NwTexAnimInstance *const *items, size_t count, uint32_t binding, NwMtlBinding *out)
{
    if (!out || !out->material || (count && !items)) return reject(NULL,"missing composition target");
    NwMtlBinding next=*out;
    next.uv=(NwUV){.value={0,0,1,1,0,.5,.5},.segment={-1,-1,-1},.address=-1};
    unsigned owned=0;
    int policy=-1;
    const NwTexAnimInstance *mips=NULL;
    const NwTexAnimInstance *writers[6]={0};
    for (size_t n=0; n<count; n++) {
        NwTexAnimInstance *i=items[n];
        if (!i) return reject(NULL,"missing composition player");
        unsigned mask=nw_texanim_instance_mask(i);
        for (int bit=0; bit<6; bit++) if (mask & (1u<<bit)) {
            if (writers[bit]==i) mask&=~(1u<<bit);
            else writers[bit]=i;
        } /* Multiple sources may explicitly join one shared player. */
        if (!nw_texanim_claim(&owned,mask)) return false;
        if (!mask) continue;
        NwUV u;
        nw_texanim_instance_uv(i,&u);
        if (i->track->uv.policy) {
            if (policy>=0 && policy!=(int)u.mips) return reject(NULL,"conflicting mipmap policies");
            policy=(int)u.mips;
            if (u.mips) mips=i;
        }
        if (mask & NW_TEXANIM_IMAGE) {
            next.tid=nw_texanim_instance_tid(i);
            next.texture=nw_texanim_instance_texture(i);
            next.layer=nw_texanim_instance_frame(i);
        }
        for (int axis=0; axis<3; axis++) if (mask & (1u<<axis)) {
            for (int k=0; k<(axis==2 ? 1 : 2); k++) next.uv.value[axis*2+k]=u.value[axis*2+k];
            next.uv.linear|=u.linear & (1u<<axis);
            next.uv.segment[axis]=u.segment[axis];
            next.uv.owner[axis]=u.owner[axis];
            next.uv.cycle[axis]=u.cycle[axis];
        }
        if (mask & NW_TEXANIM_PIVOT) memcpy(next.uv.value+5,u.value+5,2*sizeof(float));
        if (mask & NW_TEXANIM_ADDRESS) next.uv.address=u.address;
    }
    if (owned & ~NW_TEXANIM_IMAGE || policy>=0) {
        if (!binding) return reject(NULL,"missing stable composition identity");
        next.uv.binding=binding;
        next.uv.mips=policy>0;
    }
    if (mips && !(owned & NW_TEXANIM_IMAGE)) {
        int frame=0;
        while (frame<mips->count && mips->tids[frame]!=next.tid) frame++;
        if (frame==mips->count || !mips->image) return reject(NULL,"mip fallback source not prepared");
        next.texture=nw_texture_gpu(mips->image);
        next.layer=frame;
    }
    if (mips && (owned & NW_TEXANIM_IMAGE) && (!next.texture || next.texture->tex.nlevels<2)) {
        const NwGpuTex *gpu=next.texture ? next.texture : nw_rndr_texture(next.tid);
        if (!gpu || gpu->tex.levels[0].width>1 || gpu->tex.levels[0].height>1)
            return reject(NULL,"image channel does not provide the requested mipmaps");
    }
    *out=next;
    return true;
}

/* ---- Autonomous V2 environments: a model's effect, advanced by NuTexAnimProcess as V1 is ---- */

extern s32 cutworldix;
extern struct instNUGCUTSCENE_s *CutInst[];
extern float TITLEFPS; /* CUT frames per second */
enum { CLOCK_GAME = 1, CLOCK_CUT = 2, GAME_RATE = 60 };

typedef struct {
    const struct nutexanimenv_s *env;
    const struct nutexanim_s *anim;
    NwTexAnimInstance **tracks;          /* one player per texture track */
    int track_count;
    uint32_t binding;
    int64_t steps;                       /* clock game: frames since its first */
    const struct instNUGCUTSCENE_s *cut; /* clock cut: the cutscene it first played in */
    bool failed, hosted;                 /* hosted: clocked through nw_texanim_time, stepped as legacy */
    NwCurvePlayer params;                /* its param tracks, writing the host's signals */
    bool params_made, params_on;
    float *frame;                        /* clock cut: EFFECT.frame, its cutscene's frame */
} AutoEnv;
static AutoEnv *autos;
static size_t auto_count;
static NwMtlBinding *auto_samples;
static size_t auto_sample_capacity;
static uint32_t auto_variant; /* session token of session-seeded bursts */

static void auto_drop(AutoEnv *a)
{
    for (int k = 0; k < a->track_count; k++) nw_texanim_instance_drop(a->tracks[k]);
    SDL_free(a->tracks);
    nw_curve_player_destroy(&a->params);
}

static void auto_forget(const struct nutexanimenv_s *env)
{
    for (size_t i = 0; i < auto_count; i++) if (autos[i].env == env) {
        auto_drop(&autos[i]);
        autos[i] = autos[--auto_count];
        return;
    }
}

static AutoEnv *auto_find(const struct nutexanimenv_s *env)
{
    for (size_t i = 0; i < auto_count; i++) if (autos[i].env == env) return &autos[i];
    return NULL;
}

static AutoEnv *auto_add(const struct nutexanim_s *anim)
{
    AutoEnv *more = SDL_realloc(autos, (auto_count + 1) * sizeof(*more));
    if (!more) return NULL;
    autos = more;
    autos[auto_count] = (AutoEnv){.env = anim->env, .anim = anim};
    return &autos[auto_count++];
}

static void auto_host(const struct nutexanimenv_s *env)
{
    AutoEnv *a = auto_find(env);
    if (a && !a->hosted) { auto_drop(a); *a = (AutoEnv){.env = env, .hosted = true}; }
    if (!a && (a = SDL_realloc(autos, (auto_count + 1) * sizeof(*a))) != NULL) {
        autos = a;
        autos[auto_count++] = (AutoEnv){.env = env, .hosted = true};
    }
}

/* One player per texture track. Bursts of one effect take a channel per model material, in start order. */
static bool auto_start(AutoEnv *a, HostProgram *p)
{
    uint32_t channel = 0;
    for (size_t i = 0; i < auto_count; i++) if (&autos[i] != a && autos[i].env->prog == &p->base) channel++;
    if (!auto_variant) auto_variant = (uint32_t)SDL_GetPerformanceCounter() | 1u;
    a->tracks = SDL_calloc((size_t)p->track_count + 1, sizeof(*a->tracks));
    a->binding = nw_texanim_binding();
    if (!a->tracks || !a->binding) return false;
    NwTexAnimConfig config = {.clock = p->cut_clock ? CLOCK_CUT : CLOCK_GAME, .channel = channel, .variant = auto_variant};
    for (int j = 0; j < p->track_count; j++) {
        NwTexAnimInstance *i = nw_texanim_instance_track(&p->base, p->tracks[j].sequence.track, a->anim, config, NULL);
        if (!i) return false;
        a->tracks[a->track_count++] = i;
        if (!nw_texanim_instance_array(i, NULL)) return false;
    }
    return true;
}

/* clock cut: the signal EFFECT.frame, the cutscene's frame, which the file's frame events wait for. */
static float *cut_frame(const HostProgram *p)
{
    return p->cut_clock ? nw_signal_of(p->base.name, "frame") : NULL;
}

/* The effect's param tracks write their signals, EFFECT.TRACK (port/signals.h); its events are declared. */
static void auto_params(AutoEnv *a, HostProgram *p)
{
    NwCurveSet set;
    if (a->params_made) return;
    a->params_made = true;
    if ((!p->curve_count && !p->event_count) || !nw_texanim_curves(&p->base, &set)) return;
    a->frame = cut_frame(p);
    a->params_on = nw_signal_player(&a->params, set);
    if (!a->params_on) SDL_Log("ATS %s: %s", p->base.name, a->params.error);
}

/* The params' local time: game frames at 60 per second, or CUT frames (in file units when the file counts them). */
static NwCurveTime cut_curve_time(const HostProgram *p, const struct instNUGCUTSCENE_s *cut)
{
    const NwCurve *c = &p->curves[0];
    bool cut_units = (float)c->rate_num == TITLEFPS * (float)c->rate_den;
    float at = cut_units ? cut->time : cut->time / TITLEFPS;
    float units = cut_units ? at : at * (float)c->rate_num / (float)c->rate_den;
    return (NwCurveTime){.seconds = at, .units = (int64_t)floorf(units), .in_units = cut_units};
}

static NwCurveTime auto_curve_time(const AutoEnv *a, const HostProgram *p, const struct instNUGCUTSCENE_s *cut)
{
    if (!p->cut_clock) return (NwCurveTime){.seconds = (float)a->steps / GAME_RATE, .units = a->steps};
    return cut_curve_time(p, cut);
}

/* Game clock: frames since the effect started. Cut clock: its cutscene's time, held complete once another plays. */
static bool auto_clock(AutoEnv *a, HostProgram *p)
{
    const struct instNUGCUTSCENE_s *cut = cutmovie >= 0 && cutworldix >= 0 ? CutInst[cutworldix] : NULL;
    if (p->cut_clock && !a->cut) a->cut = cut;
    bool playing = !p->cut_clock || (cut && cut == a->cut);
    for (int k = 0; k < a->track_count; k++) {
        NwTexAnimInstance *i = a->tracks[k];
        bool ok = !p->cut_clock ? nw_texanim_instance_clock_integer(i, a->steps, GAME_RATE) :
                  playing ? nw_texanim_instance_clock(i, cut->time, TITLEFPS) :
                  nw_texanim_instance_complete(i) || (nw_texanim_instance_stop(i), true);
        if (!ok) return false;
    }
    auto_params(a, p);
    if (a->params_on && playing && a->frame) *a->frame = cut->time;
    if (a->params_on && playing && p->curve_count) nw_curve_player_write(&a->params, auto_curve_time(a, p, cut));
    else if (a->params_on) nw_curve_player_finish(&a->params);
    a->steps++;
    return true;
}

/* An autonomous environment from its clock: frames played and the cutscene it plays in (none yet: a new one). */
static AutoEnv *auto_begin(const struct nutexanim_s *anim, HostProgram *p, int64_t steps,
                           const struct instNUGCUTSCENE_s *cut)
{
    AutoEnv *a = auto_add(anim);
    if (!a) return NULL;
    a->steps = steps;
    a->cut = cut;
    if (!auto_start(a, p)) {
        a->failed = true;
        SDL_Log("ATS %s on %s: %s", p->base.name, anim->ntaname ? anim->ntaname : "?", nw_texanim_error()->message);
    }
    return a;
}

/* Samples one autonomous environment into its material's frame binding. */
static bool auto_frame(const struct nutexanim_s *anim, HostProgram *p, NwMtlBinding *sample)
{
    AutoEnv *a = auto_find(anim->env);
    if (!a && !(a = auto_begin(anim, p, 0, NULL))) return false;
    if (a->failed) return false;
    bool clocked = auto_clock(a, p);
    if (clocked && (!a->track_count || !anim->mtl)) return false; /* only param tracks: no material binding */
    *sample = (NwMtlBinding){.material = anim->mtl, .tid = anim->mtl->tid};
    if (clocked && nw_texanim_compose(a->tracks, (size_t)a->track_count, a->binding, sample)) return true;
    a->failed = true;
    SDL_Log("ATS %s: %s", p->base.name, nw_texanim_error()->message);
    return false;
}

int nw_texanim_env_frame(const struct nutexanimenv_s *env)
{
    const AutoEnv *a = auto_find(env);
    for (int k = 0; a && k < a->track_count; k++) {
        int frame = nw_texanim_instance_frame(a->tracks[k]);
        if (frame >= 0) return frame;
    }
    return -1;
}

/* Retail steps every environment; V2 ones are sampled here and published as the frame's material bindings. */
/* ---- Scene effects: listed effects with only param tracks, played with their scene; no model wears them ---- */

typedef struct {
    HostProgram *program;                 /* retained */
    NwCurvePlayer player;
    bool on;
    const struct instNUGCUTSCENE_s *cut;  /* clock cut: the cutscene it first played in */
    float *frame;                         /* clock cut: EFFECT.frame */
} SceneEffect;
static SceneEffect *scene_effects;
static size_t scene_effect_count;
static int64_t scene_frames; /* the frames played since the scene loaded */
static const int *host_ticks; /* the host's clock (nw_texanim_scene_clock), at host_rate a second */
static uint32_t host_rate;

void nw_texanim_scene_clock(const int *ticks, uint32_t rate)
{
    host_ticks = rate ? ticks : NULL;
    host_rate = rate;
}

/* The scene's clock in seconds: the host's, else the frames the scene has played. */
static float scene_seconds(void)
{
    return host_ticks ? (float)*host_ticks / (float)host_rate : (float)scene_frames / GAME_RATE;
}

/* The effect's player: its curves write their signals, each from its start event if it names one. */
static void scene_start(SceneEffect *e)
{
    NwCurveSet set;
    e->frame = cut_frame(e->program);
    e->on = nw_texanim_curves(&e->program->base, &set) && nw_signal_player(&e->player, set);
    if (!e->on) SDL_Log("ATS %s: %s", e->program->base.name, e->player.error);
}

static void scene_add(HostProgram *p)
{
    SceneEffect *more = SDL_realloc(scene_effects, (scene_effect_count + 1) * sizeof(*more));
    if (!more || !nw_texanim_retain(&p->base)) return;
    scene_effects = more;
    SceneEffect *e = &scene_effects[scene_effect_count++];
    *e = (SceneEffect){.program = p};
    scene_start(e);
}

/* A reloaded effect: its new program and player, its clock where it was. */
static void scene_replace(HostProgram *was, HostProgram *next)
{
    for (size_t i = 0; i < scene_effect_count; i++) {
        SceneEffect *e = &scene_effects[i];
        if (e->program != was || !nw_texanim_retain(&next->base)) continue;
        nw_curve_player_destroy(&e->player);
        e->program = next;
        scene_start(e);
        release(was);
    }
}

static void scene_clear(void)
{
    for (size_t i = 0; i < scene_effect_count; i++) {
        nw_curve_player_destroy(&scene_effects[i].player);
        release(scene_effects[i].program);
    }
    scene_effect_count = 0;
    scene_frames = 0;
}

/* clock cut: a cutscene has moved on (its camera's update, before its objects are placed); the effects playing in
   it follow at once, and so do the events they reach. Each plays in the cutscene current when it is first updated. */
void nw_texanim_scene_cut(const struct instNUGCUTSCENE_s *scene)
{
    const struct instNUGCUTSCENE_s *current = cutmovie >= 0 && cutworldix >= 0 ? CutInst[cutworldix] : NULL;
    for (size_t i = 0; i < scene_effect_count; i++) {
        SceneEffect *e = &scene_effects[i];
        if (!e->on || !e->program->cut_clock) continue;
        if (!e->cut && scene == current) e->cut = scene;
        if (scene != e->cut) continue;
        *e->frame = scene->time;
        if (e->program->curve_count) nw_curve_player_write(&e->player, cut_curve_time(e->program, scene));
    }
    nw_signal_events_update(scene_seconds()); /* the events they reach happen now, not a frame later */
}

/* Each scene effect on its clock: the scene's, or, for clock cut, held complete once another cutscene plays. */
static void scene_play(void)
{
    const struct instNUGCUTSCENE_s *cut = cutmovie >= 0 && cutworldix >= 0 ? CutInst[cutworldix] : NULL;
    for (size_t i = 0; i < scene_effect_count; i++) {
        SceneEffect *e = &scene_effects[i];
        if (!e->on) continue;
        if (e->program->cut_clock) {
            if (e->cut && cut != e->cut) nw_curve_player_finish(&e->player);
        } else if (!nw_curve_player_sample(&e->player, host_ticks ? *host_ticks : scene_frames,
                                           host_ticks ? host_rate : GAME_RATE)) {
            SDL_Log("ATS %s: %s", e->program->base.name, e->player.error);
            e->on = false;
        }
    }
    scene_frames++;
}

void NuTexAnimProcess(void)
{
    size_t count = 0;
    for (NuTexAnimList *list = ntal_first; list; list = list->next)
        for (struct nutexanim_s *anim = list->anims; anim; anim = anim->succ) {
            HostProgram *p = anim->env ? host(anim->env->prog) : NULL;
            const AutoEnv *a = p ? auto_find(anim->env) : NULL;
            if (!p || (a && a->hosted)) {
                if (anim->env) NuTexAnimEnvProc(anim->env);
                continue;
            }
            if (count == auto_sample_capacity) {
                NwMtlBinding *more = SDL_realloc(auto_samples, (count * 2 + 4) * sizeof(*more));
                if (!more) break;
                auto_samples = more;
                auto_sample_capacity = count * 2 + 4;
            }
            if (auto_frame(anim, p, &auto_samples[count])) count++;
        }
    if (!nw_rndr_mtl_frame_bindings(auto_samples, count)) SDL_Log("ATS frame bindings: %s", SDL_GetError());
    /* The events the models' curves have reached (title.formed: bolt.first), then what starts from them; then the
       events the scene effects' own curves have reached. */
    nw_signal_events_update(scene_seconds());
    scene_play();
    nw_signal_events_update(scene_seconds());
}

/* anim wears p from now on; an autonomous effect goes on from where its clock was. */
static void rebind(struct nutexanim_s *anim, HostProgram *p)
{
    const AutoEnv *a = auto_find(anim->env);
    AutoEnv clock = a ? *a : (AutoEnv){0}; /* only its clock is read: binding drops the rest */
    if (!nw_texanim_bind(anim, &p->base)) {
        SDL_Log("ATS %s on %s: %s", p->base.name, anim->ntaname ? anim->ntaname : "?", nw_texanim_error()->message);
        return;
    }
    if (a && !clock.hosted) auto_begin(anim, p, clock.steps, clock.cut);
}

static struct nutexanimprog_s *replace_resolved(struct nutexanimprog_s *old, const char *path, const void *text,
                                               size_t size, NwTexAnimResolver resolve, void *context)
{
    HostProgram *was = host(old);
    if (!was || was->refs == UINT_MAX) { reject(path, "only an ATS V2 program can be replaced"); return NULL; }
    bool registered = was->registered;
    was->refs++; /* kept while it is out of the registry */
    if (registered) NuTexAnimProgDestroy(old);
    struct nutexanimprog_s *next = nw_texanim_from_memory_resolved(path, text, size, resolve, context);
    if (next && SDL_strcasecmp(next->name, old->name)) {
        NuTexAnimProgDestroy(next);
        next = NULL;
        reject(path, "a reload cannot rename the effect");
    }
    if (!next) {
        if (registered) enlist(was);
        release(was);
        return NULL;
    }
    for (NuTexAnimList *list = ntal_first; list; list = list->next)
        for (struct nutexanim_s *anim = list->anims; anim; anim = anim->succ)
            if (anim->env && anim->env->prog == old) rebind(anim, host(next));
    scene_replace(was, host(next));
    release(was);
    return next;
}

struct nutexanimprog_s *nw_texanim_replace(struct nutexanimprog_s *old, const char *path, const void *text, size_t size)
{
    return replace_resolved(old, path, text, size, NULL, NULL);
}

struct nutexanim_s *NuTexAnimCreate(union variptr_u *buffer, struct nutexanimprog_s *program,
                                  struct numtl_s *mtl, short *tids, int count)
{
    union variptr_u cursor = {.voidptr = buffer ? buffer->voidptr : NULL};
    struct nutexanim_s *anim = NuTexAnimCreate__retail(buffer ? &cursor : NULL, program, mtl, tids, count);
    if (!anim) return NULL;
    anim->dynalloc = !buffer;
    if (!nw_texanim_bind(anim, program)) { NuTexAnimDestroy(anim); return NULL; }
    if (buffer) *buffer = cursor;
    return anim;
}

void nw_texanim_release_list(void *owner)
{
    struct nutexanim_s **list = owner;
    if (!*list) return;
    /* The EE baton is held. NuTexAnimRemoveList unmasks VBlank and may yield inside a heap release. */
    for (NuTexAnimList *node = ntal_first; node; node = node->next) {
        if (node->anims != *list) continue;
        if (node->next) node->next->prev = node->prev;
        if (node->prev) node->prev->next = node->next;
        else ntal_first = node->next;
        node->next = ntal_free;
        ntal_free = node;
        break;
    }
    for (struct nutexanim_s *a = *list; a; a = a->succ) {
        NuTexAnimEnvDestroy(a->env);
        a->env = NULL;
    }
    *list = NULL;
}

bool nw_texanim_own_list(NwHeapSide *side, struct nutexanim_s **list)
{
    if (!*list) return true;
    nw_heap_side_cleanup(side, nw_texanim_release_list, list);
    for (struct nutexanim_s *a = *list; a; a = a->succ)
        if (!nw_texanim_bind(a, a->env->prog)) { nw_texanim_release_list(list); return false; }
    return true;
}
