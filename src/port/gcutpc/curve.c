/* Curves (docs/csp-spec.md, CURV): the table of curves, the keys of each kind, and a curve's value at a time. Outside
   its keys a curve holds the nearest one; two keys at one time are a jump. */
#include <math.h>

#include "port/gcutpc/internal.h"

#define BEZIER_STEPS 30 /* bisection steps for a Bézier segment's parameter: past a float's precision */

typedef struct CurveRecord {
    uint8_t kind;
    float constant;
    uint32_t first, count;
} CurveRecord;

static const NUGCUTPCFIELD_s curve_fields[] = {
    NUGCUTPC_FIELD(CurveRecord, kind, NUGCUTPC_U8, 1, 0),
    NUGCUTPC_FIELD(CurveRecord, constant, NUGCUTPC_F32, 1, 0),
    NUGCUTPC_FIELD(CurveRecord, first, NUGCUTPC_U32, 1, 0),
    NUGCUTPC_FIELD(CurveRecord, count, NUGCUTPC_U32, 1, 0),
};

/* Every kind of key is read into NUGCUTPCKEY_s; each table has the fields of its kind, the others stay 0. */
#define KEY_FIELD(member) NUGCUTPC_FIELD(NUGCUTPCKEY_s, member, NUGCUTPC_F32, 1, 0)
static const NUGCUTPCFIELD_s step_fields[] = { KEY_FIELD(t), KEY_FIELD(v) };
static const NUGCUTPCFIELD_s hermite_fields[] = { KEY_FIELD(t), KEY_FIELD(v), KEY_FIELD(slope) };
static const NUGCUTPCFIELD_s bezier_fields[] = {
    KEY_FIELD(t), KEY_FIELD(v), KEY_FIELD(in_dt), KEY_FIELD(in_dv), KEY_FIELD(out_dt), KEY_FIELD(out_dv),
};

static const NUGCUTPCRECORD_s curve_record = NUGCUTPC_RECORD("curve", CurveRecord, curve_fields);
static const NUGCUTPCRECORD_s key_records[NUGCUTPC_CURVE_KINDS] = {
    [NUGCUTPC_STEP] = NUGCUTPC_RECORD("key_step", NUGCUTPCKEY_s, step_fields),
    [NUGCUTPC_LINEAR] = NUGCUTPC_RECORD("key_linear", NUGCUTPCKEY_s, step_fields),
    [NUGCUTPC_HERMITE] = NUGCUTPC_RECORD("key_hermite", NUGCUTPCKEY_s, hermite_fields),
    [NUGCUTPC_BEZIER] = NUGCUTPC_RECORD("key_bezier", NUGCUTPCKEY_s, bezier_fields),
};

typedef struct KeyTables {
    const NUGCUTPCKEY_s *keys[NUGCUTPC_CURVE_KINDS];
    uint32_t count[NUGCUTPC_CURVE_KINDS];
} KeyTables;

static bool read_keys(NUGCUTPCFILE_s *file, KeyTables *tables)
{
    for (int kind = NUGCUTPC_STEP; kind < NUGCUTPC_CURVE_KINDS; kind++) {
        void *keys;
        if (!NuGCutPCTableRead(file, "CURV", &key_records[kind], &keys, &tables->count[kind])) {
            return false;
        }
        tables->keys[kind] = keys;
    }
    return true;
}

static bool check_keys(NUGCUTPCFILE_s *file, uint32_t index, const NUGCUTPCKEY_s *keys, uint32_t count)
{
    for (uint32_t k = 0; k < count; k++) {
        if (!isfinite(keys[k].t)) {
            return NuGCutPCFail(file->error, "curve %u: key %u has no finite time", index, k);
        }
        if (k > 0 && keys[k].t < keys[k - 1].t) {
            return NuGCutPCFail(file->error, "curve %u: key %u comes before the key ahead of it", index, k);
        }
    }
    return true;
}

static bool load_curve(NUGCUTPCFILE_s *file, const KeyTables *tables, uint32_t index, const CurveRecord *rec,
                       NUGCUTPCCURVE_s *out)
{
    if (rec->kind >= NUGCUTPC_CURVE_KINDS) {
        return NuGCutPCFail(file->error, "curve %u: kind %u unknown", index, rec->kind);
    }
    *out = (NUGCUTPCCURVE_s){ .kind = (NuGCutPCCurveKind)rec->kind, .constant = rec->constant };
    if (out->kind == NUGCUTPC_CONSTANT) {
        return true;
    }
    uint32_t available = tables->count[out->kind];
    if (rec->count == 0 || rec->first > available || rec->count > available - rec->first) {
        return NuGCutPCFail(file->error, "curve %u: keys %u..%u not in its table of %u", index, rec->first,
                            rec->first + rec->count, available);
    }
    out->keys = tables->keys[out->kind] + rec->first;
    out->key_count = rec->count;
    return check_keys(file, index, out->keys, out->key_count);
}

bool NuGCutPCCurveSysLoad(NUGCUTPCFILE_s *file, NUGCUTPCSCENE_s *scene)
{
    void *records;
    uint32_t count;
    KeyTables tables = { 0 };
    if (!NuGCutPCTableRead(file, "CURV", &curve_record, &records, &count) || !read_keys(file, &tables)) {
        return false;
    }
    NUGCUTPCCURVE_s *curves = NuGCutPCArenaAlloc(file->arena, count, sizeof(*curves));
    if (!curves && count) {
        return NuGCutPCFail(file->error, "out of memory");
    }
    for (uint32_t i = 0; i < count; i++) {
        if (!load_curve(file, &tables, i, (const CurveRecord *)records + i, &curves[i])) {
            return false;
        }
    }
    scene->curves = curves;
    scene->curve_count = count;
    return true;
}

bool NuGCutPCCurveRef(NUGCUTPCFILE_s *file, const NUGCUTPCSCENE_s *scene, uint32_t ref, bool optional,
                      const char *what, const NUGCUTPCCURVE_s **out)
{
    *out = NULL;
    if (ref == NUGCUTPC_NONE && optional) {
        return true;
    }
    if (ref >= scene->curve_count) {
        return NuGCutPCFail(file->error, "%s: curve %u of %u", what, ref, scene->curve_count);
    }
    *out = &scene->curves[ref];
    return true;
}

/* Values. */

static float segment_linear(const NUGCUTPCKEY_s *k0, const NUGCUTPCKEY_s *k1, float time)
{
    return k0->v + (k1->v - k0->v) * (time - k0->t) / (k1->t - k0->t);
}

/* The cubic hermite with tangents slope * d, NuAnimCurve2CalcVal's formula. */
static float segment_hermite(const NUGCUTPCKEY_s *k0, const NUGCUTPCKEY_s *k1, float time)
{
    float d = k1->t - k0->t;
    float u = (time - k0->t) / d;
    float a = k0->v - k1->v, b = k0->slope * d, c = k1->slope * d;
    return ((u * (2 * a + b + c) - 3 * a - 2 * b - c) * u + b) * u + k0->v;
}

static float cubic(const float p[4], float s)
{
    float r = 1 - s;
    return r * r * r * p[0] + 3 * r * r * s * p[1] + 3 * r * s * s * p[2] + s * s * s * p[3];
}

/* The Bézier through k0, its out handle, k1's in handle and k1, at the parameter that gives this time. */
static float segment_bezier(const NUGCUTPCKEY_s *k0, const NUGCUTPCKEY_s *k1, float time)
{
    const float xs[4] = { k0->t, k0->t + k0->out_dt, k1->t + k1->in_dt, k1->t };
    const float ys[4] = { k0->v, k0->v + k0->out_dv, k1->v + k1->in_dv, k1->v };
    float lo = 0, hi = 1;
    for (int i = 0; i < BEZIER_STEPS; i++) {
        float mid = (lo + hi) / 2;
        if (cubic(xs, mid) < time) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return cubic(ys, (lo + hi) / 2);
}

/* The last key at or before the time; the caller has checked that the first one is. */
static uint32_t key_at(const NUGCUTPCCURVE_s *curve, float time)
{
    uint32_t lo = 0, hi = curve->key_count; /* the answer is in [lo, hi) */
    while (hi - lo > 1) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (curve->keys[mid].t <= time) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return lo;
}

float NuGCutPCCurveEval(const NUGCUTPCCURVE_s *curve, float time)
{
    if (curve->kind == NUGCUTPC_CONSTANT) {
        return curve->constant;
    }
    const NUGCUTPCKEY_s *keys = curve->keys;
    if (time < keys[0].t) {
        return keys[0].v;
    }
    uint32_t i = key_at(curve, time);
    if (i + 1 >= curve->key_count) {
        return keys[curve->key_count - 1].v;
    }
    switch (curve->kind) {
    case NUGCUTPC_LINEAR:
        return segment_linear(&keys[i], &keys[i + 1], time);
    case NUGCUTPC_HERMITE:
        return segment_hermite(&keys[i], &keys[i + 1], time);
    case NUGCUTPC_BEZIER:
        return segment_bezier(&keys[i], &keys[i + 1], time);
    default:
        return keys[i].v; /* step */
    }
}
