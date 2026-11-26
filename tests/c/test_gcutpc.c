/* The PC cutscene reader (src/port/gcutpc) against docs/csp-spec.md.

     nuworld_test_gcutpc                          synthetic files: values, newer files, refused files
     nuworld_test_gcutpc --samples DIR NAME...    DIR/NAME.CSP against DIR/NAME.SAMPLES (crashWoCEditor's
                                                  csp_tool.py samples: the reference evaluator's values) */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "csp_builder.h"
#include "port/gcutpc/internal.h"

#define PI_2 1.57079632679489661923
#define TOLERANCE 1e-5 /* relative to the value, absolute below 1: single precision's noise */

static int failures;

#define CHECK(x)                                                                                                     \
    do {                                                                                                             \
        if (!(x)) {                                                                                                  \
            fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #x);                                                 \
            failures++;                                                                                              \
        }                                                                                                            \
    } while (0)

static bool near(float a, float b)
{
    return fabsf(a - b) <= 1e-5f * fmaxf(1.0f, fabsf(a));
}

static const float IDENTITY[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };

/* Building files. */

static void put_meta(CspBuilder *b, float duration)
{
    CspTable *meta = csp_table(b, "META", "meta", 1);
    csp_set_str(b, meta, 0, "name", "test");
    csp_set(b, meta, 0, "duration", 0, duration);
    csp_set(b, meta, 0, "units", 0, 1);
}

typedef struct Key {
    float t, v, a, b, c, d; /* hermite: a = slope; bezier: in_dt in_dv out_dt out_dv */
} Key;

typedef struct CurveSpec {
    int kind; /* 0 constant, 1 step, 2 linear, 3 hermite, 4 bezier */
    float constant;
    Key keys[4];
    int nkeys;
} CurveSpec;

static void put_curves(CspBuilder *b, const CurveSpec *curves, int n)
{
    static const char *const tables[] = { NULL, "key_step", "key_linear", "key_hermite", "key_bezier" };
    uint32_t used[5] = { 0 };
    CspTable *table = csp_table(b, "CURV", "curve", (uint32_t)n);
    CspTable *keys[5] = { NULL };
    for (int kind = 1; kind < 5; kind++) {
        keys[kind] = csp_table(b, "CURV", tables[kind], 16);
    }
    for (int i = 0; i < n; i++) {
        const CurveSpec *c = &curves[i];
        csp_set(b, table, (uint32_t)i, "kind", 0, c->kind);
        csp_set(b, table, (uint32_t)i, "constant", 0, c->constant);
        csp_set(b, table, (uint32_t)i, "first", 0, used[c->kind]);
        csp_set(b, table, (uint32_t)i, "count", 0, c->nkeys);
        for (int k = 0; k < c->nkeys; k++) {
            CspTable *kt = keys[c->kind];
            uint32_t r = used[c->kind]++;
            const Key *key = &c->keys[k];
            csp_set(b, kt, r, "t", 0, key->t);
            csp_set(b, kt, r, "v", 0, key->v);
            if (c->kind == 3) {
                csp_set(b, kt, r, "slope", 0, key->a);
            } else if (c->kind == 4) {
                csp_set(b, kt, r, "in_dt", 0, key->a);
                csp_set(b, kt, r, "in_dv", 0, key->b);
                csp_set(b, kt, r, "out_dt", 0, key->c);
                csp_set(b, kt, r, "out_dv", 0, key->d);
            }
        }
    }
}

static void put_transform(CspBuilder *b, CspTable *transforms, CspTable *channels, uint32_t index, int kind,
                          int flags, float length, const uint32_t *curves, uint32_t count, uint32_t first)
{
    csp_set(b, transforms, index, "kind", 0, kind);
    csp_set(b, transforms, index, "flags", 0, flags);
    csp_set(b, transforms, index, "length", 0, length);
    csp_set(b, transforms, index, "first", 0, first);
    csp_set(b, transforms, index, "count", 0, count);
    for (uint32_t c = 0; c < count; c++) {
        csp_set(b, channels, first + c, "curve", 0, curves[c]);
    }
}

enum { C_ZERO, C_TWO, C_QUARTER, C_TX, C_VISIBLE, C_HERMITE, C_BEZIER, C_SWITCHES, C_JUMP, C_COUNT };

#define KEY(time, value) { .t = (time), .v = (value) }
#define HERMITE(time, value, slope) { .t = (time), .v = (value), .a = (slope) }
#define BEZIER(time, value, in_dt, in_dv, out_dt, out_dv)                                                            \
    { .t = (time), .v = (value), .a = (in_dt), .b = (in_dv), .c = (out_dt), .d = (out_dv) }

static const CurveSpec scene_curves[C_COUNT] = {
    [C_ZERO] = { .kind = 0, .constant = 0 },
    [C_TWO] = { .kind = 0, .constant = 2 },
    [C_QUARTER] = { .kind = 0, .constant = (float)PI_2 },
    [C_TX] = { .kind = 2, .keys = { KEY(1, 0), KEY(11, 10) }, .nkeys = 2 },
    [C_VISIBLE] = { .kind = 1, .keys = { KEY(1, 0), KEY(5, 1) }, .nkeys = 2 },
    [C_HERMITE] = { .kind = 3, .keys = { HERMITE(0, 0, 0), HERMITE(2, 1, 0) }, .nkeys = 2 },
    [C_BEZIER] = { .kind = 4, .keys = { BEZIER(0, 0, 0, 0, 1, 0), BEZIER(3, 1, -1, 0, 0, 0) }, .nkeys = 2 },
    [C_SWITCHES] = { .kind = 1, .keys = { KEY(1, 0), KEY(5, 1) }, .nkeys = 2 },
    [C_JUMP] = { .kind = 1, .keys = { KEY(1, 0), KEY(5, 1), KEY(5, 3) }, .nkeys = 3 },
};

/* The scene of the tests below, the same as crashWoCEditor/tests/test_csp_eval.py's: a rigid whose tx rises 0..10
   over frames 1..11 and holds after its transform's length (6), turned a quarter about z, its x axis scaled by 2,
   moved to x 100; a still rigid; two cameras, switched at frame 5, the second on the rigid's transform's kind of
   channels but a camera's. */
static void put_scene(CspBuilder *b, int camera_kind)
{
    csp_spec_types(b);
    put_meta(b, 100);
    put_curves(b, scene_curves, C_COUNT);
    CspTable *transforms = csp_table(b, "XFRM", "transform", 2);
    CspTable *channels = csp_table(b, "XFRM", "channel", 8);
    const uint32_t rigid[7] = { C_TX, C_ZERO, C_ZERO, C_ZERO, C_ZERO, C_QUARTER, C_TWO };
    put_transform(b, transforms, channels, 0, 1, NUGCUTPC_ROTATES | NUGCUTPC_SCALES, 6, rigid, 7, 0);
    const uint32_t camera[1] = { C_TX };
    put_transform(b, transforms, channels, 1, camera_kind, NUGCUTPC_ROTATES, 100, camera, 1, 7);

    CspTable *sys = csp_table(b, "CAMS", "camera_system", 1);
    csp_set(b, sys, 0, "initial", 0, 0);
    csp_set(b, sys, 0, "switches", 0, C_SWITCHES);
    CspTable *cams = csp_table(b, "CAMS", "camera", 2);
    for (uint32_t i = 0; i < 2; i++) {
        csp_set(b, cams, i, "id", 0, i);
        csp_set_str(b, cams, i, "name", NULL);
        csp_set_matrix(b, cams, i, IDENTITY);
        csp_set(b, cams, i, "transform", 0, i == 1 ? 1 : CSP_NONE);
    }
    float moved[16];
    memcpy(moved, IDENTITY, sizeof(moved));
    moved[12] = 100;
    CspTable *rigids = csp_table(b, "RIGS", "rigid", 2);
    for (uint32_t i = 0; i < 2; i++) {
        csp_set_str(b, rigids, i, "name", i == 0 ? "a" : "b");
        csp_set_str(b, rigids, i, "scene", NULL);
        csp_set(b, rigids, i, "flags", 0, i == 0);
        csp_set_matrix(b, rigids, i, moved);
        csp_set(b, rigids, i, "transform", 0, i == 0 ? 0 : CSP_NONE);
        csp_set(b, rigids, i, "visibility", 0, i == 0 ? C_VISIBLE : CSP_NONE);
    }
}

static NUGCUTPCSCENE_s *load(CspBuilder *b, NUGCUTPCERROR_s *error)
{
    size_t size;
    uint8_t *data = csp_finish(b, &size);
    NUGCUTPCSCENE_s *scene = NuGCutPCSceneLoad(data, size, error);
    memset(data, 0xAB, size); /* the definition keeps nothing of the bytes it was read from */
    free(data);
    csp_free(b);
    return scene;
}

static NUGCUTPCSCENE_s *load_scene(void)
{
    CspBuilder b = { 0 };
    put_scene(&b, 0);
    NUGCUTPCERROR_s error;
    NUGCUTPCSCENE_s *scene = load(&b, &error);
    if (!scene) {
        fprintf(stderr, "the test scene did not load: %s\n", error.text);
        exit(1);
    }
    return scene;
}

/* Values. */

static void test_curves(void)
{
    NUGCUTPCSCENE_s *scene = load_scene();
    const NUGCUTPCCURVE_s *c = scene->curves;
    CHECK(NuGCutPCCurveEval(&c[C_TWO], 50) == 2);
    CHECK(NuGCutPCCurveEval(&c[C_TX], -5) == 0);    /* before the first key: the first */
    CHECK(NuGCutPCCurveEval(&c[C_TX], 500) == 10);  /* after the last: the last */
    CHECK(near(NuGCutPCCurveEval(&c[C_TX], 3.5f), 2.5f));
    CHECK(NuGCutPCCurveEval(&c[C_JUMP], 4.9f) == 0);
    CHECK(NuGCutPCCurveEval(&c[C_JUMP], 5) == 3);   /* two keys at one time: a jump to the later */
    CHECK(near(NuGCutPCCurveEval(&c[C_HERMITE], 1), 0.5f));
    CHECK(near(NuGCutPCCurveEval(&c[C_HERMITE], 0.5f), 0.15625f));
    CHECK(near(NuGCutPCCurveEval(&c[C_BEZIER], 1.5f), 0.5f));
    CHECK(NuGCutPCCurveEval(&c[C_BEZIER], 0.5f) < 0.5f / 3);
    NuGCutPCSceneFree(scene);
}

static void test_rigids(void)
{
    NUGCUTPCSCENE_s *scene = load_scene();
    NUGCUTPCMTX_s m;
    NuGCutPCRigidMatrix(scene, 0, 3, &m);
    const float expected[16] = { 0, 2, 0, 0, -1, 0, 0, 0, 0, 0, 1, 0, 102, 0, 0, 1 };
    for (int i = 0; i < 16; i++) {
        CHECK(fabsf(m.m[i] - expected[i]) < 1e-5f);
    }
    NuGCutPCRigidMatrix(scene, 0, 50, &m);
    CHECK(near(m.m[12], 105)); /* held at the transform's length, then moved */
    NuGCutPCRigidMatrix(scene, 1, 3, &m);
    CHECK(m.m[12] == 100 && m.m[0] == 1);
    CHECK(!NuGCutPCRigidVisible(scene, 0, 1) && !NuGCutPCRigidVisible(scene, 0, 4.9f));
    CHECK(NuGCutPCRigidVisible(scene, 0, 5));
    CHECK(!NuGCutPCRigidVisible(scene, 1, 1)); /* no track: its flag */
    CHECK(NuGCutPCRigidCount(scene) == 2 && strcmp(NuGCutPCRigidName(scene, 1), "b") == 0);
    CHECK(strcmp(NuGCutPCSceneName(scene), "test") == 0 && NuGCutPCSceneDuration(scene) == 100);
    NuGCutPCSceneFree(scene);
}

static void test_cameras(void)
{
    NUGCUTPCSCENE_s *scene = load_scene();
    CHECK(NuGCutPCCameraCount(scene) == 2);
    CHECK(NuGCutPCCameraShown(scene, 1) == 0 && NuGCutPCCameraShown(scene, 5) == 1);
    NUGCUTPCMTX_s m;
    NuGCutPCCameraMatrix(scene, 1, 3, &m);
    CHECK(near(m.m[12], 2)); /* a camera has no base position */
    NuGCutPCCameraMatrix(scene, 0, 3, &m);
    CHECK(memcmp(m.m, IDENTITY, sizeof(IDENTITY)) == 0);
    NuGCutPCSceneFree(scene);
}

static void test_instance(void)
{
    NUGCUTPCSCENE_s *scene = load_scene();
    instNUGCUTPCSCENE_s *inst = instNuGCutPCSceneCreate(scene);
    NUGCUTPCMTX_s m;
    instNuGCutPCSceneStart(inst);
    CHECK(instNuGCutPCSceneTime(inst) == 1 && instNuGCutPCSceneCamera(inst, &m) == 0);
    instNuGCutPCSceneUpdate(inst, 4);
    CHECK(instNuGCutPCSceneCamera(inst, &m) == 1 && near(m.m[12], 4));
    CHECK(instNuGCutPCSceneRigid(inst, 0, &m) && near(m.m[12], 104));
    instNuGCutPCSceneUpdate(inst, 200);
    CHECK(instNuGCutPCSceneIsFinished(inst) && instNuGCutPCSceneTime(inst) == 100);
    instNuGCutPCSceneSetLooping(inst, true);
    instNuGCutPCSceneStart(inst);
    instNuGCutPCSceneUpdate(inst, 104);
    CHECK(!instNuGCutPCSceneIsFinished(inst) && near(instNuGCutPCSceneTime(inst), 6));
    instNuGCutPCSceneDestroy(inst);
    NuGCutPCSceneFree(scene);
}

/* A newer writer: a field, a record type and a section this reader does not know; an older one: a field missing. */
static void test_newer_and_older_files(void)
{
    CspBuilder b = { 0 };
    CSP_TYPE(&b, "rigid", { "lod", CSP_U32, 1 }, { "visibility", CSP_REF, 1 }, { "name", CSP_STR, 1 },
             { "flags", CSP_U8, 1 }, { "matrix", CSP_F32, 16 }, { "transform", CSP_REF, 1 });
    CSP_TYPE(&b, "meta", { "name", CSP_STR, 1 }, { "duration", CSP_F32, 1 }); /* no units */
    CSP_TYPE(&b, "future_thing", { "x", CSP_F32, 1 });
    CspTable *meta = csp_table(&b, "META", "meta", 1);
    csp_set(&b, meta, 0, "duration", 0, 10);
    csp_set_str(&b, meta, 0, "name", NULL);
    CspTable *rigids = csp_table(&b, "RIGS", "rigid", 1);
    csp_set(&b, rigids, 0, "lod", 0, 3);
    csp_set(&b, rigids, 0, "visibility", 0, CSP_NONE);
    csp_set(&b, rigids, 0, "transform", 0, CSP_NONE);
    csp_set_str(&b, rigids, 0, "name", "box");
    csp_set(&b, rigids, 0, "flags", 0, 1);
    csp_set_matrix(&b, rigids, 0, IDENTITY);
    csp_table(&b, "RIGS", "future_thing", 2);
    static const uint8_t anything[] = "anything";
    csp_raw(&b, "WHAT", anything, sizeof(anything));
    NUGCUTPCERROR_s error;
    NUGCUTPCSCENE_s *scene = load(&b, &error);
    CHECK(scene != NULL);
    if (!scene) {
        fprintf(stderr, "  %s\n", error.text);
        return;
    }
    CHECK(scene->units == 1 && scene->name == NULL);
    CHECK(NuGCutPCRigidCount(scene) == 1 && strcmp(NuGCutPCRigidName(scene, 0), "box") == 0);
    CHECK(scene->rigids.rigids[0].scene == NULL && NuGCutPCRigidVisible(scene, 0, 1));
    CHECK(NuGCutPCCameraCount(scene) == 0 && NuGCutPCCameraShown(scene, 1) == NUGCUTPC_NO_CAMERA);
    NuGCutPCSceneFree(scene);
}

/* Refused files: each is the test scene with one thing wrong, and the reason names it. */

typedef void (*Spoil)(CspBuilder *b);

static void refused(const char *what, Spoil spoil, int camera_kind, const char *reason)
{
    CspBuilder b = { 0 };
    put_scene(&b, camera_kind);
    if (spoil) {
        spoil(&b);
    }
    NUGCUTPCERROR_s error;
    NUGCUTPCSCENE_s *scene = load(&b, &error);
    if (scene || !strstr(error.text, reason)) {
        fprintf(stderr, "test_refused: %s: %s (expected \"%s\")\n", what, scene ? "loaded" : error.text, reason);
        failures++;
    }
    NuGCutPCSceneFree(scene);
}

static CspTable *table_of(CspBuilder *b, const char *section, const char *type)
{
    CspSection *s = csp_section(b, section);
    int t = csp_type_named(b, type);
    for (int i = 0; i < s->ntables; i++) {
        if (s->tables[i].type == t) {
            return &s->tables[i];
        }
    }
    return NULL;
}

static void spoil_ref(CspBuilder *b)
{
    csp_set(b, table_of(b, "RIGS", "rigid"), 0, "visibility", 0, 99);
}

static void spoil_visibility_kind(CspBuilder *b)
{
    csp_set(b, table_of(b, "RIGS", "rigid"), 0, "visibility", 0, C_TX);
}

static void spoil_key_order(CspBuilder *b)
{
    csp_set(b, table_of(b, "CURV", "key_linear"), 1, "t", 0, -3);
}

static void spoil_key_range(CspBuilder *b)
{
    csp_set(b, table_of(b, "CURV", "curve"), C_TX, "first", 0, 15);
}

static void spoil_curve_kind(CspBuilder *b)
{
    csp_set(b, table_of(b, "CURV", "curve"), 0, "kind", 0, 9);
}

static void spoil_field_type(CspBuilder *b)
{
    b->types[csp_type_named(b, "rigid")].fields[2].type = CSP_U32; /* flags written as a u32 */
}

static void spoil_channels(CspBuilder *b)
{
    csp_set(b, table_of(b, "XFRM", "transform"), 0, "count", 0, 12);
}

static void test_refused(void)
{
    refused("a reference past its table", spoil_ref, 0, "curve 99");
    refused("a visibility that is no step", spoil_visibility_kind, 0, "not a step curve");
    refused("keys out of order", spoil_key_order, 0, "comes before");
    refused("keys outside their table", spoil_key_range, 0, "not in its table");
    refused("an unknown curve kind", spoil_curve_kind, 0, "kind 9");
    refused("a field of another type", spoil_field_type, 0, "rigid.flags");
    refused("channels past their table", spoil_channels, 0, "outside their table");
    refused("a camera on a rigid's transform", NULL, 1, "is a rigid, not a camera");
}

static void test_refused_containers(void)
{
    NUGCUTPCERROR_s error;
    CHECK(!NuGCutPCSceneLoad("CUTP", 4, &error) && strstr(error.text, "shorter"));
    uint8_t other[32] = "NOPE";
    CHECK(!NuGCutPCSceneLoad(other, sizeof(other), &error) && strstr(error.text, "signature"));

    CspBuilder b = { 0 };
    put_scene(&b, 0);
    b.major = 2;
    CHECK(!load(&b, &error) && strstr(error.text, "version 2"));

    CspBuilder c = { 0 };
    put_scene(&c, 0);
    size_t size;
    uint8_t *data = csp_finish(&c, &size);
    csp_put_u32(data + 32 + 8, (uint32_t)size); /* the first section starts at the end of the file */
    CHECK(!NuGCutPCSceneLoad(data, size, &error) && strstr(error.text, "past the end"));
    for (size_t cut = 0; cut < size; cut += 7) { /* every truncation is refused, none read out of bounds */
        NUGCUTPCSCENE_s *s = NuGCutPCSceneLoad(data, cut, &error);
        CHECK(s == NULL);
        NuGCutPCSceneFree(s);
    }
    free(data);
    csp_free(&c);
}

/* Against the reference evaluator's samples. */

static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc(n > 0 ? (size_t)n : 1);
    *size = data ? fread(data, 1, (size_t)n, f) : 0;
    fclose(f);
    return data;
}

typedef struct Compare {
    long samples, wrong;
    double worst;
} Compare;

static void compare_matrix(Compare *c, const NUGCUTPCMTX_s *have, const float want[16], const char *line)
{
    double worst = 0;
    for (int i = 0; i < 16; i++) {
        double error = fabs((double)have->m[i] - want[i]) / fmax(1.0, fabs(want[i]));
        worst = fmax(worst, error);
    }
    c->samples++;
    c->worst = fmax(c->worst, worst);
    if (worst > TOLERANCE && c->wrong++ < 5) {
        fprintf(stderr, "  off by %.3g: %.80s\n", worst, line);
    }
}

static bool read_matrix(const char *text, float m[16])
{
    char *end;
    for (int i = 0; i < 16; i++) {
        m[i] = strtof(text, &end);
        if (end == text) {
            return false;
        }
        text = end;
    }
    return true;
}

static void compare_line(const NUGCUTPCSCENE_s *scene, Compare *c, const char *line)
{
    unsigned index;
    int shown, visible, used;
    float t, want[16];
    NUGCUTPCMTX_s have;
    if (sscanf(line, "camera %f %d", &t, &shown) == 2) {
        c->samples++;
        if (NuGCutPCCameraShown(scene, t) != shown && c->wrong++ < 5) {
            fprintf(stderr, "  camera %d expected: %.80s\n", NuGCutPCCameraShown(scene, t), line);
        }
    } else if (sscanf(line, "cammtx %u %f%n", &index, &t, &used) == 2 && read_matrix(line + used, want)) {
        NuGCutPCCameraMatrix(scene, index, t, &have);
        compare_matrix(c, &have, want, line);
    } else if (sscanf(line, "rigid %u %f %d%n", &index, &t, &visible, &used) == 3 && read_matrix(line + used, want)) {
        NuGCutPCRigidMatrix(scene, index, t, &have);
        compare_matrix(c, &have, want, line);
        if (NuGCutPCRigidVisible(scene, index, t) != (visible != 0) && c->wrong++ < 5) {
            fprintf(stderr, "  visibility: %.80s\n", line);
        }
    }
}

static int compare_samples(const char *dir, const char *name)
{
    char path[1024];
    size_t size, text_size;
    snprintf(path, sizeof(path), "%s/%s.CSP", dir, name);
    uint8_t *data = read_file(path, &size);
    snprintf(path, sizeof(path), "%s/%s.SAMPLES", dir, name);
    char *text = (char *)read_file(path, &text_size);
    NUGCUTPCERROR_s error = { "" };
    NUGCUTPCSCENE_s *scene = data ? NuGCutPCSceneLoad(data, size, &error) : NULL;
    if (!scene || !text) {
        fprintf(stderr, "%s: %s\n", name, scene ? "no samples" : error.text[0] ? error.text : "no .CSP");
        free(data);
        free(text);
        NuGCutPCSceneFree(scene);
        return 1;
    }
    text = realloc(text, text_size + 1);
    text[text_size] = '\0';
    Compare c = { 0 };
    for (char *line = strtok(text, "\n"); line; line = strtok(NULL, "\n")) {
        compare_line(scene, &c, line);
    }
    printf("%-10s %8ld samples, worst %.1e, %ld wrong\n", name, c.samples, c.worst, c.wrong);
    NuGCutPCSceneFree(scene);
    free(data);
    free(text);
    return c.wrong != 0 || c.samples == 0;
}

int main(int argc, char **argv)
{
    if (argc >= 3 && strcmp(argv[1], "--samples") == 0) {
        int wrong = 0;
        for (int i = 3; i < argc; i++) {
            wrong += compare_samples(argv[2], argv[i]);
        }
        printf("%d of %d match the reference evaluator\n", argc - 3 - wrong, argc - 3);
        return wrong != 0;
    }
    test_curves();
    test_rigids();
    test_cameras();
    test_instance();
    test_newer_and_older_files();
    test_refused();
    test_refused_containers();
    if (failures) {
        fprintf(stderr, "gcutpc: %d checks failed\n", failures);
        return 1;
    }
    printf("gcutpc: all checks passed\n");
    return 0;
}
