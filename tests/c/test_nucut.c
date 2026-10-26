/* The .cut loader override (src/port/gcutscn.c) on a synthetic file; locally on every .cut of the imported disc.
   Usage: test_nucut <scratch dir> | retail. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "gamelib/gcutscn.h"
#include "gamelib/trigger.h"
#include "gfx/packet.h"
#include "gfx/vif.h"
#include "nu3d/nuanim.h"
#include "nu3d/nuhgobj.h"
#include "nu3d/numtl.h"
#include "nu3d/nutex.h"
#include "nucore/nufile.h"
#include "port/game.h"
#include "shim/shim.h"
#include "vfs/extract.h"
#include "vfs/manifest.h"
#include "vfs/vfs.h"

#include "cut_builder.h"

/* Declared by no header. */
void NuAnimData2CalcTime(struct nuanimdata2_s *data, float time, struct nuanimtime_s *atime);

static int failures;
static char cut_heap[8 << 20];
static char ghg_buffer[4 << 20];

/* A host record: beside the heap, never in it (src/port/heap.h). */
#define BESIDE(p) ((char *)(p) < cut_heap || (char *)(p) >= cut_heap + sizeof(cut_heap))

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

/* Curve i at frame t, false if the keys it reads leave the image. A frame before the curve's first key reads the
   16 bytes before its keys, as on the PS2: *before counts those. */
static bool eval2(const char *file, size_t size, struct nuanimdata2_s *data, int i, float t, float *val,
                  unsigned *before)
{
    struct nuanimtime_s at;
    int type = data->curveflags[i];
    if (type == 0) {
        *val = data->curves[i].constant;
        return true;
    }
    if (data->nchunks <= 0 || (type != 1 && type != 3)) {
        return false;
    }
    NuAnimData2CalcTime(data, t, &at);
    const struct nuanimcurvedata2_s *cd = data->curves[i].curvedata;
    int key = (int)floorf(at.time_offset) - 1;
    if (key < 0 || key > 31) {
        return false;
    }
    int ix = cd->key_ixs[at.chunk] + __builtin_popcount(cd->mask[at.chunk].bits & (uint32_t)((2ull << key) - 1)) - 1;
    *before += ix < 0;
    ptrdiff_t stride = type == 3 ? 8 : 16, need = type == 3 ? 8 : 32;
    const char *k = (const char *)cd->keys + ix * stride;
    if (k < file || k + need > file + size) {
        return false;
    }
    *val = NuAnimCurve2CalcVal(&data->curves[i], &at, type);
    return true;
}

static float eval_synth(struct nuanimdata2_s *data, int i, float t)
{
    unsigned before = 0;
    float val = NAN;
    return eval2(cut_heap, sizeof(cut_heap), data, i, t, &val, &before) && !before ? val : NAN;
}

static void check_anim2(struct nuanimdata2_s *a, char *file, size_t flags, int n)
{
    CHECK(a && a->time == 4.0f && a->nnodes == 1 && a->ncurves == n && a->nchunks == 1);
    CHECK(a && a->curveflags == (signed char *)file + flags && a->curvesetflags == file + flags + n);
    CHECK(a && BESIDE(a->curves) && (((uintptr_t)a->curves) & 7) == 0);
}

static void check_cut(struct NUGCUTSCENE_s *def, char *file, const CutFile *f)
{
    CHECK(def->version == 1 && def->base == (int)CUT_BASE && def->duration == 40.0f);
    CHECK(def->names == file + f->names && def->bounds == (struct NUGCUTBBOX_s *)(file + f->bounds));
    CHECK(def->bounds->max.z == 3.0f);

    struct NUGCUTCAMSYS_s *cam = def->camdef;
    CHECK(cam->count == 2 && cam->cams == (struct NUGCUTCAM_s *)(file + f->cams) && cam->first_camera == 1);
    CHECK(cam->cams[0].flags == 1 && cam->cams[0].mtx._30 == 1.0f && cam->cams[1].anim_ix == 1);
    CHECK(memcmp(cam->remap, file + f->camsys + 0x11, 15) == 0 && cam->remap[14] == 0x7E && cam->remap[2] == -1);
    check_anim2(cam->anim, file, f->camanim_flags, 3);
    CHECK(cam->anim->curves[0].constant == 0.5f);
    CHECK(eval_synth(cam->anim, 0, 2.0f) == 0.5f && eval_synth(cam->anim, 1, 1.0f) == 2.0f);
    CHECK(eval_synth(cam->anim, 1, 2.0f) == 4.0f && eval_synth(cam->anim, 2, 2.0f) == 7.0f);
    CHECK(eval_synth(cam->anim, 2, 3.5f) == 9.0f);
    struct NUSTATEANIM_s *sa = cam->stateanim;
    CHECK(sa->count == 2 && sa->times == (float *)(file + f->cam_times) && sa->times[1] == 20.0f);
    CHECK(sa->values == (unsigned char *)file + f->cam_values && sa->values[1] == 1);

    struct NUGCUTLOCATORSYS_s *ls = def->locatordef;
    CHECK(ls->count == 2 && ls->ntypes == 2);
    struct NUGCUTLOCATOR_s *loc = ls->locators;
    CHECK(((struct numtx_s *)loc[0].pad)->_30 == 5.0f && *(float *)&loc[0].pad[0x40] == 0.25f);
    CHECK(loc[0].value == 0.75f && *(int *)loc[0].pad54 == 3 && loc[0].flags == 0x9 && loc[0].type == 1);
    CHECK(loc[0].joint_ix == 0xFF && loc[1].anim == NULL && loc[1].flags == 0x8 && loc[1].joint_ix == 4);
    check_anim2(loc[0].anim, file, f->locanim_flags, 1);
    CHECK(eval_synth(loc[0].anim, 0, 2.0f) == 4.0f);
    CHECK(strcmp(ls->types[0].name, "debris") == 0 && ls->types[0].flags == 1 && ls->types[0].fn == 0xFF);
    CHECK(strcmp(ls->types[1].name, "func") == 0 && ls->types[1].flags == 2);

    struct NUGCUTRIGID_s *r = def->rigiddef->rigids;
    CHECK(def->rigiddef->count == 2 && r[0].name == file + f->names && r[1].name == NULL);
    CHECK(r[0].mtx._30 == 10.0f && r[0].locators == (void *)1 && r[0].nlocators == 1 && r[0].flags == 3);
    CHECK(r[0].obj.scene == NULL && r[0].obj.special == NULL && r[0].stateanim == NULL);
    check_anim2(r[0].anim, file, f->rigidanim_flags, 9);
    CHECK(r[0].anim->curves[8].constant == 8.5f && eval_synth(r[0].anim, 4, 3.0f) == 4.5f);
    CHECK((nusintptr)r[1].locators == (int32_t)0x8BC618E8 && r[1].anim == NULL && r[1].stateanim->count == 1);

    struct NUGCUTCHAR_s *c = def->chardef->chars;
    CHECK(def->chardef->count == 1 && strcmp(c->name, "char0") == 0 && c->mtx._30 == 11.0f);
    CHECK(c->locators == NULL && c->nlocators == 1 && c->character == NULL && c->face_anim == NULL);
    CHECK(*(float *)c->pad58 == 0.5f && c->flags == 1 && c->pad5D == 2 && c->pad61[0] == 7);
    check_anim2(c->char_anim, file, f->charanim_flags, 3);
    check_anim2(c->joint_anim, file, f->jointanim_flags, 1);
    CHECK(eval_synth(c->char_anim, 0, 2.0f) == 4.0f && eval_synth(c->char_anim, 2, 3.0f) == 9.0f);

    struct NUGCUTTRIGGERSYS_s *ts = def->triggerdef;
    CHECK(ts->count == 1 && ts->triggers[0].target == -1 && ts->triggers[0].name == file + f->names + 25);
    CHECK(ts->triggers[0].anim->count == 3 && ts->triggers[0].anim->times == (float *)(file + f->trig_times));
}

static struct NUGCUTSCENE_s *found_chars;

static void find_chars(struct NUGCUTSCENE_s *def)
{
    found_chars = def;
    struct NUGCUTCHAR_s *c = &def->chardef->chars[0];
    c->first_locator = (unsigned char)(nusintptr)c->locators;
    c->locators = &def->locatordef->locators[(nusintptr)c->locators];
}

static void locator_fn(struct instNUGCUTSCENE_s *scene, struct NUGCUTLOCATORSYS_s *def,
                       struct instNUGCUTLOCATOR_s *locator, struct NUGCUTLOCATOR_s *data, float time,
                       struct numtx_s *mtx)
{
    (void)scene, (void)def, (void)locator, (void)data, (void)time, (void)mtx;
}

/* Retail's second pass on the loaded records. */
static void test_fixup(struct NUGCUTSCENE_s *def)
{
    static struct NUGCUTLOCFNDATA_s fns[] = { { "FUNC", locator_fn }, { NULL, NULL } };
    struct NUTRIGGER_s triggers[2] = { { .triggername = "other" }, { .triggername = "TRIG" } };
    struct NUTRIGGERSYS_s sys = { .ntriggers = 2, .triggers = triggers };
    NuGCutSceneSysInit(fns);
    NuSetCutSceneFindCharactersFn(find_chars);
    NuGCutSceneFixUp(def, NULL, &sys);
    CHECK(found_chars == def && def->chardef->chars[0].locators == &def->locatordef->locators[0]);
    CHECK(def->locatordef->types[0].fn == 0xFFFF && def->locatordef->types[1].fn == 0);
    CHECK(def->triggerdef->triggers[0].target == 1);
    NuSetCutSceneFindCharactersFn(NULL);
    NuGCutSceneSysInit(NULL);
}

/* An animated rigid (one no retail cut has) is never drawn: retail reads its draw flag from a stack word nothing
   writes, 0 under PlayCutMovie, and a draw would go through the NULL scene here. */
static void test_rigid_render(struct NUGCUTSCENE_s *def)
{
    struct NUGCUTRIGID_s *data = &def->rigiddef->rigids[0];
    struct nuinstance_s instance = { .flagword = 1 };
    struct nuspecial_s special = { .instance = &instance };
    struct instNUGCUTRIGID_s rigids[2] = { { .obj = { NULL, &special } }, { .obj = { NULL, &special } } };
    struct instNUGCUTRIGIDSYS_s sys = { rigids };
    struct instNUGCUTSCENE_s scene = { .def = def, .rigidsys = &sys };
    unsigned char flags = data->flags, first = data->first_locator;
    data->flags = 0x6; /* ANIMATED | FOUND */
    data->first_locator = 0xFF;
    instNuGCutRigidSysRender(&scene, 1.0f);
    CHECK(instance.flagword == 1);
    data->flags = flags;
    data->first_locator = first;
}

static int missing_main(int argc, char **argv)
{
    (void)argc, (void)argv;
    union variptr_u heap, end;
    heap.charptr = cut_heap;
    end.charptr = cut_heap + sizeof(cut_heap);
    NuGCutSceneLoad("missing.cut", &heap, &end);
    return 0;
}

static void test_load(const char *scratch)
{
    char *root = NULL, *disc = NULL, *path = NULL;
    GscBuild b;
    CutFile f;
    NwVfs vfs;

    SDL_asprintf(&root, "%snucut/", scratch);
    SDL_asprintf(&disc, "%s" NW_DISC_DIR "/", root);
    SDL_RemovePath(root);
    CHECK(SDL_CreateDirectory(disc));
    SDL_asprintf(&path, "%s" NW_DISC_STAMP, disc);
    CHECK(SDL_SaveFile(path, "", 0));
    SDL_free(path);
    cut_build(&b, &f);
    SDL_asprintf(&path, "%sTEST.CUT", disc);
    CHECK(SDL_SaveFile(path, b.buf, b.len));
    SDL_free(path);

    CHECK(nw_vfs_init(&vfs, root));
    nw_shim_file_init(&vfs);
    NuFileInitEx(1, 1);

    /* Twice in a row on one heap, as LoadCutComponents loads a movie's cuts; the heap takes the image alone, as
       retail's. */
    union variptr_u heap, end;
    heap.charptr = cut_heap + 3;
    end.charptr = cut_heap + sizeof(cut_heap);
    for (int pass = 0; pass < 2; pass++) {
        char *file = (char *)(((uintptr_t)heap.charptr + 15) & ~(uintptr_t)15);
        struct NUGCUTSCENE_s *def = NuGCutSceneLoad("test.cut", &heap, &end);
        CHECK(memcmp(file, b.buf, b.len) == 0);
        CHECK(heap.charptr == file + b.len && BESIDE(def));
        check_cut(def, file, &f);
        if (pass == 0) {
            test_fixup(def);
            test_rigid_render(def);
        }
    }

    CHECK(nw_game_init(NW_CLOCK_REALTIME, 50));
    CHECK(nw_game_start(missing_main, 0, NULL));
    NwGameState state;
    while ((state = nw_game_state()) == NW_GAME_RUNNING) {
        SDL_Delay(10);
    }
    CHECK(state == NW_GAME_FATAL && strstr(nw_game_error(), "gcutscn.c(332) : Could not read from cutscene"));

    nw_shim_file_init(NULL);
    nw_vfs_shutdown(&vfs);
    SDL_RemovePath(root);
    gsc_free(&b);
    SDL_free(disc);
    SDL_free(root);
}

typedef struct RetailStats {
    unsigned files, cams, locators, types, rigids, chars, triggers, states, anims, keyed, joints, bad;
    unsigned before, nonfinite; /* frames read before a curve's first key, and those that come out inf or NaN */
} RetailStats;

static RetailStats retail;

static void drain_gpu(void)
{
    NwGpuJob *jobs = NULL;
    uint32_t cap = 0, n = nw_gpu_take(UINT64_MAX, &jobs, &cap);
    for (uint32_t i = 0; i < n; i++) {
        if (jobs[i].op == NW_GPU_FREE_MESH) {
            nw_vif_free(&((NwGpuMesh *)jobs[i].obj)->mesh);
        } else if (jobs[i].op == NW_GPU_FREE_SKIN) {
            nw_vif_skin_free(&((NwGpuSkin *)jobs[i].obj)->skin);
        }
        if (jobs[i].op >= NW_GPU_FREE_MESH && jobs[i].op != NW_GPU_FREE_TEX) {
            SDL_free(jobs[i].obj);
        }
    }
    SDL_free(jobs);
}

/* Every curve of an anim at each frame, and the anim's curve flags not seen in the file before. */
static void retail_anim(const char *path, const char *file, size_t size, struct nuanimdata2_s *a,
                        const signed char **seen, unsigned *nseen)
{
    if (!a) {
        return;
    }
    for (unsigned i = 0; i < *nseen; i++) {
        if (seen[i] == a->curveflags && retail.bad++ < 10) {
            fprintf(stderr, "retail: %s: an anim is shared\n", path);
        }
    }
    if (*nseen < 4096) {
        seen[(*nseen)++] = a->curveflags;
    }
    retail.anims++;
    int count = a->nnodes * a->ncurves;
    for (int i = 0; i < count; i++) {
        retail.keyed += a->curveflags[i] != 0;
        for (float t = 1.0f; a->curveflags[i] && t <= a->time + 1.0f; t += 1.0f) {
            float val;
            if (!eval2(file, size, a, i, t, &val, &retail.before)) {
                if (retail.bad++ < 10) {
                    fprintf(stderr, "retail: %s: curve %d at frame %g reads outside the file\n", path, i, t);
                }
            } else if (!isfinite(val)) {
                retail.nonfinite++;
            }
        }
    }
}

/* The .ghg of a character in the cut's folder, if there is one: its joints against the joint anim's nodes. */
static void retail_joints(const char *path, const struct NUGCUTCHAR_s *c)
{
    char ghg[NW_VFS_PATH_MAX];
    const char *slash = strrchr(path, '\\');
    if (!c->joint_anim || !c->name) {
        return;
    }
    SDL_snprintf(ghg, sizeof(ghg), "%.*s\\%s.GHG", (int)(slash - path), path, c->name);
    for (size_t i = 0; i < nw_disc_manifest.count; i++) {
        char p[NW_VFS_PATH_MAX];
        SDL_strlcpy(p, nw_disc_manifest.entries[i].path, sizeof(p));
        for (char *s = p; *s; s++) {
            *s = *s == '/' ? '\\' : *s;
        }
        if (SDL_strcasecmp(p, ghg) != 0) {
            continue;
        }
        union variptr_u buf;
        buf.charptr = ghg_buffer;
        struct NUHGOBJ_s *h = NuGHGRead(&buf, p);
        int nodes = ((const struct nuanimdata2_s *)c->joint_anim)->nnodes;
        if (!h || nodes < h->num_joints) {
            fprintf(stderr, "retail: %s: %s has %d joints, its anim %d nodes\n", path, p, h ? h->num_joints : -1,
                    nodes);
            retail.bad++;
        }
        if (h) {
            NuHGobjDestroy(h);
            drain_gpu();
        }
        retail.joints++;
        return;
    }
}

static void retail_file(const char *path)
{
    static const signed char *seen[4096];
    unsigned nseen = 0;
    union variptr_u heap, end;
    heap.charptr = cut_heap;
    end.charptr = cut_heap + sizeof(cut_heap);
    struct NUGCUTSCENE_s *def = NuGCutSceneLoad((char *)path, &heap, &end);
    const char *file = cut_heap;
    size_t size = (size_t)(heap.charptr - file);
    CHECK(BESIDE(def));
    RetailStats was = retail;

    if (def->camdef) {
        retail.cams += def->camdef->count;
        retail.states += def->camdef->stateanim != NULL;
        retail_anim(path, file, size, def->camdef->anim, seen, &nseen);
    }
    if (def->locatordef) {
        retail.locators += def->locatordef->count;
        retail.types += def->locatordef->ntypes;
        for (int i = 0; i < def->locatordef->count; i++) {
            retail_anim(path, file, size, def->locatordef->locators[i].anim, seen, &nseen);
        }
    }
    for (int i = 0; def->rigiddef && i < def->rigiddef->count; i++) {
        struct NUGCUTRIGID_s *r = &def->rigiddef->rigids[i];
        retail.rigids++;
        retail.states += r->stateanim != NULL;
        retail_anim(path, file, size, r->anim, seen, &nseen);
    }
    for (int i = 0; def->chardef && i < def->chardef->count; i++) {
        struct NUGCUTCHAR_s *c = &def->chardef->chars[i];
        retail.chars++;
        retail_anim(path, file, size, c->char_anim, seen, &nseen);
        retail_anim(path, file, size, c->joint_anim, seen, &nseen);
        retail_anim(path, file, size, c->face_anim, seen, &nseen);
        retail_joints(path, c);
    }
    if (def->triggerdef) {
        retail.triggers += (unsigned)def->triggerdef->count;
    }
    retail.files++;
    printf("CUT %s cams=%u locators=%u types=%u rigids=%u chars=%u anims=%u keyed=%u states=%u bytes=%zu\n", path,
           retail.cams - was.cams, retail.locators - was.locators, retail.types - was.types,
           retail.rigids - was.rigids, retail.chars - was.chars, retail.anims - was.anims,
           retail.keyed - was.keyed, retail.states - was.states, size);
}

static NwVfs retail_vfs;

/* On the game thread: a load error is fatal there. */
static int retail_main(int argc, char **argv)
{
    (void)argc, (void)argv;
    nw_shim_file_init(&retail_vfs);
    NuFileInitEx(1, 1);
    NuTexInit();
    NuMtlInit();
    for (size_t i = 0; i < nw_disc_manifest.count; i++) {
        const char *p = nw_disc_manifest.entries[i].path;
        size_t n = strlen(p);
        if (n > 4 && SDL_strcasecmp(p + n - 4, ".CUT") == 0) {
            char path[NW_VFS_PATH_MAX];
            SDL_strlcpy(path, p, sizeof(path));
            for (char *c = path; *c; c++) {
                *c = *c == '/' ? '\\' : *c;
            }
            retail_file(path);
        }
    }
    return 0;
}

/* Every .cut loads to the pinned counts, every keyed curve reads keys inside its file at every frame, no anim is
   shared (retail would relocate it twice), and the joint anims cover their characters' joints. The 5 non-finite
   values: ICEBERG's joint anim before its first key, where the slope read is the exporter's FLT_MAX padding. */
static int test_retail(void)
{
    const char *dir = getenv("NUWORLD_TEST_DATA");
    if (!dir) {
        printf("NUWORLD_TEST_DATA is not set: skipped\n");
        return 77;
    }
    CHECK(nw_vfs_mount(&retail_vfs, NW_VFS_DISC, dir));
    CHECK(nw_game_init(NW_CLOCK_REALTIME, 50));
    CHECK(nw_game_start(retail_main, 0, NULL));
    NwGameState state;
    while ((state = nw_game_state()) == NW_GAME_RUNNING) {
        SDL_Delay(10);
    }
    if (state == NW_GAME_FATAL) {
        fprintf(stderr, "retail: %s\n", nw_game_error());
    }
    printf("TOTAL files=%u cams=%u locators=%u types=%u rigids=%u chars=%u triggers=%u anims=%u keyed=%u states=%u "
           "before=%u nonfinite=%u joints=%u bad=%u\n", retail.files, retail.cams, retail.locators,
           retail.types, retail.rigids, retail.chars, retail.triggers, retail.anims, retail.keyed, retail.states,
           retail.before, retail.nonfinite, retail.joints, retail.bad);
    CHECK(state == NW_GAME_RETURNED && retail.files == 18 && retail.bad == 0);
    CHECK(retail.cams == 70 && retail.locators == 128 && retail.types == 40 && retail.rigids == 178);
    CHECK(retail.chars == 67 && retail.anims == 385 && retail.keyed == 4284 && retail.states == 107);
    CHECK(retail.before == 4823 && retail.nonfinite == 5 && retail.joints == 5);
    return failures ? 1 : 0;
}

int main(int argc, char *argv[])
{
    if (argc > 1 && strcmp(argv[1], "retail") == 0) {
        return test_retail();
    }
    if (argc != 2) {
        fprintf(stderr, "usage: test_nucut <scratch dir> | retail\n");
        return 2;
    }
    char *scratch = NULL;
    SDL_asprintf(&scratch, "%s/", argv[1]);
    SDL_CreateDirectory(scratch);
    test_load(scratch);
    SDL_free(scratch);
    printf("%s\n", failures ? "FAILED" : "nucut: all checks passed");
    return failures ? 1 : 0;
}
