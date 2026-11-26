/* .looks files (port/lookfile.h): "key values..." lines, ';' comments, material and object blocks in braces. */
#include "port/lookfile.h"
#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "gfx/material_schema.h"
#include "port/signals.h"

enum { NAME = 32 };

/* field = signal * scale + offset (times the recipe's own with multiply), while when, if any, is positive. */
typedef struct {
    const float *signal, *when;
    float scale, offset;
    int at, count;
    bool multiply;
} Drive;

typedef struct {
    char name[NAME];
    float values[NW_MATERIAL_VALUES];
    int first, count; /* its drives */
} Look;

typedef struct {
    char object[NAME]; /* "" for the model's own slots */
    char look[NAME];
    int slot;          /* -1: the default */
    int index, line;   /* the look, once the file is read; where it was named */
} Slot;

/* split: the joints PREFIXfirst..last get their own copy of material slot, the group's slots 1... */
typedef struct { int slot, first, last; char prefix[NAME], group[NAME]; } Split;

/* count: slot n of the group wears lit[n - 1] while signal is above n - 0.5, else off. The lit recipes are named
   lit_prefix and first..last in width digits; off and lit are found once the file is read. */
typedef struct {
    char group[NAME], off_name[NAME], lit_prefix[NAME];
    int first, last, width, line, off, *lit;
    const float *signal;
} Count;

struct NwLookFile {
    Look *looks;
    Drive *drives;
    Slot *slots;
    Split *splits;
    Count *counts;
    int look_count, drive_count, slot_count, split_count, count_count;
    float frame[NW_MATERIAL_VALUES];
};

/* The reader's place: the line, and the block it is in. */
typedef struct {
    const char *path;
    int line;
    NwLookFile *file;
    int look;                       /* inside material { }, else -1 */
    const NwMaterialShader *shader;
    unsigned given;                 /* the look's values already given, one bit each */
    char object[NAME];              /* inside object { }, else "" */
} Reader;

static bool fail(const Reader *r, const char *format, ...)
{
    char reason[192];
    va_list args;
    va_start(args, format);
    SDL_vsnprintf(reason, sizeof(reason), format, args);
    va_end(args);
    return SDL_SetError("%s:%d: %s", r->path, r->line, reason);
}

static char *token(char **rest)
{
    char *start = *rest + strspn(*rest, " \t\r");
    if (!*start) { *rest = start; return NULL; }
    char *end = start + strcspn(start, " \t\r");
    *rest = *end ? end + 1 : end;
    *end = 0;
    return start;
}

/* A decimal as the other text files read it: strtod, then float. */
static bool number(const char *text, float *out)
{
    char *end;
    double v = text ? strtod(text, &end) : 0;
    if (!text || end == text || *end || !(v >= -FLT_MAX && v <= FLT_MAX)) return false;
    *out = (float)v;
    return true;
}

static bool name_of(const char *text, char out[NAME])
{
    if (!text || SDL_strlen(text) >= NAME) return false;
    SDL_strlcpy(out, text, NAME);
    return true;
}

static bool grow(void **array, int count, size_t size)
{
    void *more = SDL_realloc(*array, (size_t)(count + 1) * size);
    if (more) *array = more;
    return more != NULL;
}

static const NwMaterialField *field_of(const NwMaterialShader *shader, const char *name)
{
    for (size_t i = 0; name && i < shader->field_count; i++)
        if (!SDL_strcmp(shader->fields[i].name, name)) return &shader->fields[i];
    return NULL;
}

/* ---- Lines ---- */

/* material NAME SHADER { */
static bool open_material(Reader *r, char *rest)
{
    char *name = token(&rest), *shader = token(&rest), *brace = token(&rest);
    NwLookFile *f = r->file;
    Look look = {.first = f->drive_count};
    if (!name_of(name, look.name) || !shader || !brace || SDL_strcmp(brace, "{") || token(&rest))
        return fail(r, "expected material NAME SHADER {");
    if (nw_lookfile_find(f, name) >= 0) return fail(r, "material %s already defined", name);
    r->shader = NULL;
    for (size_t i = 0; i < SDL_arraysize(nw_material_shaders); i++)
        if (!SDL_strcmp(nw_material_shaders[i].name, shader)) r->shader = &nw_material_shaders[i];
    if (!r->shader) return fail(r, "unknown shader %s (gloss, metal, vortex)", shader);
    if (!grow((void **)&f->looks, f->look_count, sizeof(Look))) return false;
    look.values[0] = r->shader->id;
    f->looks[f->look_count] = look;
    r->look = f->look_count++;
    r->given = 1;
    return true;
}

/* FIELD VALUES: its numbers, or one of its named values. */
static bool set_field(Reader *r, const char *key, char *rest)
{
    const NwMaterialField *field = field_of(r->shader, key);
    if (!field) return fail(r, "%s has no field %s", r->shader->name, key);
    unsigned bits = ((1u << field->count) - 1) << field->at;
    if (r->given & bits) return fail(r, "%s overlaps a field given before", key);
    float values[4];
    if (field->choices) {
        char *choice = token(&rest);
        const NwFieldChoice *c = field->choices;
        while (c->name && (!choice || SDL_strcmp(c->name, choice))) c++;
        if (!c->name || token(&rest)) return fail(r, "%s: unknown value", key);
        values[0] = c->value;
    } else {
        for (int i = 0; i < field->count; i++)
            if (!number(token(&rest), &values[i])) return fail(r, "%s expects %d number(s)", key, field->count);
        if (token(&rest)) return fail(r, "%s expects %d number(s)", key, field->count);
    }
    SDL_memcpy(&r->file->looks[r->look].values[field->at], values, sizeof(float) * (size_t)field->count);
    r->given |= bits;
    return true;
}

/* drive FIELD SIGNAL [scale N] [offset N] [multiply] [when SIGNAL] */
static bool add_drive(Reader *r, char *rest)
{
    char *key = token(&rest), *signal = token(&rest), *option;
    const NwMaterialField *field = field_of(r->shader, key);
    Drive d = {.scale = 1};
    if (!field || field->choices) return fail(r, "drive: %s has no numeric field %s", r->shader->name, key ? key : "");
    if (!(d.signal = nw_signal_find(signal))) return fail(r, "drive: no one writes the signal %s", signal ? signal : "");
    while ((option = token(&rest)) != NULL) {
        bool ok = true;
        if (!SDL_strcmp(option, "scale")) ok = number(token(&rest), &d.scale);
        else if (!SDL_strcmp(option, "offset")) ok = number(token(&rest), &d.offset);
        else if (!SDL_strcmp(option, "multiply")) d.multiply = true;
        else if (!SDL_strcmp(option, "when")) {
            char *when = token(&rest);
            if (!(d.when = nw_signal_find(when))) return fail(r, "drive: no one writes the signal %s", when ? when : "");
        } else ok = false;
        if (!ok) return fail(r, "drive: expected scale N, offset N, multiply or when SIGNAL");
    }
    NwLookFile *f = r->file;
    if (!grow((void **)&f->drives, f->drive_count, sizeof(Drive))) return false;
    d.at = field->at;
    d.count = field->count;
    f->drives[f->drive_count++] = d;
    f->looks[r->look].count++;
    return true;
}

/* slot N NAME, or default NAME: in an object's block, its geometry's; else the model's. */
static bool add_slot(Reader *r, bool fallback, char *rest)
{
    char *number_text = fallback ? NULL : token(&rest), *look = token(&rest), *end;
    Slot s = {.slot = -1, .line = r->line};
    if (!fallback) {
        long value = number_text ? strtol(number_text, &end, 10) : -1;
        if (!number_text || *end || value < 0 || value > 1023) return fail(r, "expected slot 0..1023 NAME");
        s.slot = (int)value;
    }
    if (!name_of(look, s.look) || token(&rest)) return fail(r, fallback ? "expected default NAME" : "expected slot N NAME");
    SDL_strlcpy(s.object, r->object, NAME);
    NwLookFile *f = r->file;
    for (int i = 0; i < f->slot_count; i++)
        if (f->slots[i].slot == s.slot && !SDL_strcmp(f->slots[i].object, s.object))
            return fail(r, fallback ? "default given twice" : "slot %d given twice", s.slot);
    if (!grow((void **)&f->slots, f->slot_count, sizeof(Slot))) return false;
    f->slots[f->slot_count++] = s;
    return true;
}

/* NAMEa..b: the name's prefix, a and b, and a's digits (led-01..15: "led-", 1, 15, 2). */
static bool name_range(const char *text, char prefix[NAME], int *first, int *last, int *width)
{
    const char *dots = text ? SDL_strstr(text, "..") : NULL, *digits = dots;
    char *end_a, *end_b;
    while (digits && digits > text && SDL_isdigit((unsigned char)digits[-1])) digits--;
    if (!dots || digits == dots || digits == text || (size_t)(digits - text) >= NAME) return false;
    long a = SDL_strtol(digits, &end_a, 10), b = SDL_strtol(dots + 2, &end_b, 10);
    if (end_a != dots || end_b == dots + 2 || *end_b || b < a || b - a >= 64) return false;
    SDL_strlcpy(prefix, text, (size_t)(digits - text) + 1);
    *first = (int)a;
    *last = (int)b;
    *width = (int)(dots - digits);
    return true;
}

/* split SLOT JOINTa..b GROUP */
static bool add_split(Reader *r, char *rest)
{
    char *slot = token(&rest), *range = token(&rest), *group = token(&rest), *end;
    long value = slot ? strtol(slot, &end, 10) : -1;
    Split s = {.slot = (int)value};
    int width;
    if (!slot || *end || value < 0 || value > 1023 || !name_range(range, s.prefix, &s.first, &s.last, &width) ||
        !name_of(group, s.group) || token(&rest))
        return fail(r, "expected split SLOT JOINTa..b GROUP");
    NwLookFile *f = r->file;
    if (!grow((void **)&f->splits, f->split_count, sizeof(Split))) return false;
    f->splits[f->split_count++] = s;
    return true;
}

/* count GROUP SIGNAL off RECIPE lit RECIPEa..b */
static bool add_count(Reader *r, char *rest)
{
    char *group = token(&rest), *signal = token(&rest), *off = token(&rest), *off_name = token(&rest);
    char *lit = token(&rest), *range = token(&rest);
    Count c = {.line = r->line};
    if (!name_of(group, c.group) || !off || SDL_strcmp(off, "off") || !name_of(off_name, c.off_name) || !lit ||
        SDL_strcmp(lit, "lit") || !name_range(range, c.lit_prefix, &c.first, &c.last, &c.width) || token(&rest))
        return fail(r, "expected count GROUP SIGNAL off RECIPE lit RECIPEa..b");
    if (!(c.signal = nw_signal_find(signal))) return fail(r, "count: no one writes the signal %s", signal ? signal : "");
    NwLookFile *f = r->file;
    if (!grow((void **)&f->counts, f->count_count, sizeof(Count))) return false;
    f->counts[f->count_count++] = c;
    return true;
}

/* A count's recipes by name, once every material is read. */
static bool resolve_count(Reader *r, Count *c)
{
    r->line = c->line;
    if ((c->off = nw_lookfile_find(r->file, c->off_name)) < 0) return fail(r, "no material %s", c->off_name);
    if (!(c->lit = SDL_calloc((size_t)(c->last - c->first + 1), sizeof(int)))) return false;
    for (int n = c->first; n <= c->last; n++) {
        char name[NAME * 2];
        SDL_snprintf(name, sizeof(name), "%s%0*d", c->lit_prefix, c->width, n);
        if ((c->lit[n - c->first] = nw_lookfile_find(r->file, name)) < 0) return fail(r, "no material %s", name);
    }
    return true;
}

/* A gradient needs a height to spread over. */
static bool close_material(Reader *r)
{
    const float *v = r->file->looks[r->look].values;
    if (v[0] == 3 && v[11] == -2 && !(v[1] > 0)) return fail(r, "a gradient needs a positive height");
    r->look = -1;
    return true;
}

static bool read_line(Reader *r, char *line, bool *versioned)
{
    char *rest = line, *key = token(&rest);
    if (!key) return true;
    if (!*versioned) {
        char *version = token(&rest);
        if (SDL_strcmp(key, "looks") || !version || SDL_strcmp(version, "1") || token(&rest))
            return fail(r, "the file starts with: looks 1");
        *versioned = true;
        return true;
    }
    if (!SDL_strcmp(key, "}")) {
        if (token(&rest)) return fail(r, "} stands alone");
        if (r->look >= 0) return close_material(r);
        if (!r->object[0]) return fail(r, "} without a block");
        r->object[0] = 0;
        return true;
    }
    if (r->look >= 0) return !SDL_strcmp(key, "drive") ? add_drive(r, rest) : set_field(r, key, rest);
    if (!SDL_strcmp(key, "slot")) return add_slot(r, false, rest);
    if (!SDL_strcmp(key, "default")) return add_slot(r, true, rest);
    if (r->object[0]) return fail(r, "an object's block holds slot and default lines");
    if (!SDL_strcmp(key, "material")) return open_material(r, rest);
    if (!SDL_strcmp(key, "split")) return add_split(r, rest);
    if (!SDL_strcmp(key, "count")) return add_count(r, rest);
    if (!SDL_strcmp(key, "object")) {
        char *name = token(&rest), *brace = token(&rest);
        if (!name_of(name, r->object) || !brace || SDL_strcmp(brace, "{") || token(&rest)) {
            r->object[0] = 0;
            return fail(r, "expected object NAME {");
        }
        return true;
    }
    return fail(r, "unknown line %s", key);
}

NwLookFile *nw_lookfile_read(const char *path, const char *text, size_t size)
{
    NwLookFile *f = SDL_calloc(1, sizeof(*f));
    Reader r = {.path = path, .file = f, .look = -1};
    bool versioned = false, ok = f != NULL;
    for (const char *p = text, *end = text + size; ok && p < end;) {
        const char *eol = memchr(p, '\n', (size_t)(end - p));
        size_t length = (size_t)((eol ? eol : end) - p);
        char line[256];
        r.line++;
        if (length >= sizeof(line)) { ok = fail(&r, "line longer than %d bytes", (int)sizeof(line) - 1); break; }
        SDL_memcpy(line, p, length);
        line[length] = 0;
        line[strcspn(line, ";")] = 0;
        p += length + 1;
        ok = read_line(&r, line, &versioned);
    }
    if (ok && (r.look >= 0 || r.object[0])) ok = fail(&r, "a block is missing its }");
    if (ok && !versioned) ok = fail(&r, "the file starts with: looks 1");
    for (int i = 0; ok && i < f->slot_count; i++) {
        Slot *s = &f->slots[i];
        r.line = s->line;
        if ((s->index = nw_lookfile_find(f, s->look)) < 0) ok = fail(&r, "no material %s", s->look);
    }
    for (int i = 0; ok && i < f->count_count; i++) ok = resolve_count(&r, &f->counts[i]);
    if (ok) return f;
    nw_lookfile_free(f);
    return NULL;
}

void nw_lookfile_free(NwLookFile *file)
{
    if (!file) return;
    SDL_free(file->looks);
    SDL_free(file->drives);
    SDL_free(file->slots);
    SDL_free(file->splits);
    for (int i = 0; i < file->count_count; i++) SDL_free(file->counts[i].lit);
    SDL_free(file->counts);
    SDL_free(file);
}

int nw_lookfile_find(const NwLookFile *file, const char *name)
{
    for (int i = 0; file && name && i < file->look_count; i++)
        if (!SDL_strcmp(file->looks[i].name, name)) return i;
    return -1;
}

int nw_lookfile_slot(const NwLookFile *file, const char *object, int slot)
{
    int fallback = -1;
    const char *in = object ? object : "";
    for (int i = 0; file && i < file->slot_count; i++) {
        const Slot *s = &file->slots[i];
        if (SDL_strcmp(s->object, in)) continue;
        if (s->slot == slot) return s->index;
        if (s->slot < 0) fallback = s->index;
    }
    return fallback;
}

bool nw_lookfile_split(const NwLookFile *file, int k, NwLookSplit *out)
{
    if (!file || k < 0 || k >= file->split_count) return false;
    const Split *s = &file->splits[k];
    *out = (NwLookSplit){.slot = s->slot, .first = s->first, .last = s->last, .prefix = s->prefix, .group = s->group};
    return true;
}

int nw_lookfile_counted(const NwLookFile *file, const char *group, int n)
{
    for (int i = 0; file && i < file->count_count; i++) {
        const Count *c = &file->counts[i];
        if (SDL_strcmp(c->group, group) || n < 1) continue;
        bool lit = (float)(n - 1) + 0.5f < *c->signal && n <= c->last - c->first + 1;
        return lit ? c->lit[n - 1] : c->off;
    }
    return -1;
}

const float *nw_lookfile_frame(NwLookFile *file, int look)
{
    if (!file || look < 0 || look >= file->look_count) return NULL;
    const Look *l = &file->looks[look];
    SDL_memcpy(file->frame, l->values, sizeof(file->frame));
    for (int i = l->first; i < l->first + l->count; i++) {
        const Drive *d = &file->drives[i];
        if (d->when && !isfinite(*d->when)) return NULL;
        if (d->when && *d->when <= 0) continue;
        for (int k = d->at; k < d->at + d->count; k++) {
            float v = *d->signal * d->scale + d->offset;
            if (d->multiply) v = file->frame[k] * v;
            if (!isfinite(v)) return NULL;
            file->frame[k] = v;
        }
    }
    return file->frame;
}
