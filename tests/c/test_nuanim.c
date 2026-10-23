/* The .ani loader override (src/port/nuanim.c) on a synthetic file; locally on every .ani and .bsa of the imported
   disc. Usage: test_nuanim <scratch dir> | retail. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "gfx/packet.h"
#include "gfx/vif.h"
#include "nu3d/nuanim.h"
#include "nu3d/nuhgobj.h"
#include "nu3d/numtl.h"
#include "nu3d/nutex.h"
#include "nucore/nufile.h"
#include "port/game.h"
#include "shim/shim.h"
#include "vfs/dat.h"
#include "vfs/extract.h"
#include "vfs/manifest.h"
#include "vfs/vfs.h"

#include "ani_builder.h"

/* Declared by no header. */
int NuFileLoadBuffer(char *filename, void *mem, int buffsize);
struct nuanimdata_s *NuAnimDataLoadBuff(char *file, union variptr_u *buffer, union variptr_u *buffend);
void NuAnimDataCalcTime(struct nuanimdata_s *data, float time, struct nuanimtime_s *atime);
extern int D_0062EE18; /* bitcount_table_built */

static int failures;
static char ani_buffer[8 << 20];
static char ghg_buffer[4 << 20];

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

static float eval1(struct nuanimdata_s *data, struct nuanimcurve_s *curve, float t)
{
    struct nuanimtime_s at;
    NuAnimDataCalcTime(data, t, &at);
    return NuAnimCurveCalcVal2(curve, &at);
}

static void check_anim(struct nuanimdata_s *data, char *file, const AniFile *f)
{
    CHECK(data->time == 2.5f && data->node_names == file + f->names && strcmp(data->node_names, "root") == 0);
    CHECK(data->nchunks == 2 && data->chunks[1] == NULL);
    struct nuanimdatachunk_s *chunk = data->chunks[0];
    CHECK(chunk->numnodes == 2 && chunk->num_valid_animcurvesets == 1 && chunk->animcurvesets[1] == NULL);
    CHECK(chunk->keys == (void *)0x55 && chunk->curves == (void *)0x66);
    struct nuanimcurveset_s *set = chunk->animcurvesets[0];
    CHECK(set->flags == 1 && set->ncurves == 9 && set->constants == (float *)(file + f->constants));
    CHECK(set->constants[8] == 2.0f && set->set[1] == NULL && set->set[8] == NULL);
    struct nuanimcurve_s *c = set->set[0], *step = set->set[3];
    CHECK(c->mask.bits == 0x5 && c->numkeys == 2 && c->flags == 0 && c->animkeys == (void *)(file + f->keys[0]));
    CHECK(step->flags == 1 && step->animkeys == (void *)(file + f->keys[1]));
    CHECK(eval1(data, c, 1.0f) == 2.0f && eval1(data, c, 2.0f) == 4.0f && eval1(data, step, 2.0f) == 7.0f);
}

static int missing_main(int argc, char **argv)
{
    (void)argc, (void)argv;
    union variptr_u buf, end;
    buf.charptr = ani_buffer;
    end.charptr = ani_buffer + sizeof(ani_buffer);
    NuAnimDataLoadBuff("missing.ani", &buf, &end);
    return 0;
}

static void test_load(const char *scratch)
{
    char *root = NULL, *disc = NULL, *path = NULL;
    GscBuild b;
    AniFile f;
    NwVfs vfs;

    SDL_asprintf(&root, "%snuanim/", scratch);
    SDL_asprintf(&disc, "%s" NW_DISC_DIR "/", root);
    SDL_RemovePath(root);
    CHECK(SDL_CreateDirectory(disc));
    SDL_asprintf(&path, "%s" NW_DISC_STAMP, disc);
    CHECK(SDL_SaveFile(path, "", 0));
    SDL_free(path);
    ani_build(&b, &f);
    SDL_asprintf(&path, "%sTEST.ANI", disc);
    CHECK(SDL_SaveFile(path, b.buf, b.len));
    SDL_free(path);

    CHECK(nw_vfs_init(&vfs, root));
    nw_shim_file_init(&vfs);
    NuFileInitEx(1, 1);

    /* Twice in a row on one buffer, as InstAnimDataLoad loads a character's anims; the first builds the bit counts. */
    CHECK(D_0062EE18 == 0);
    union variptr_u buf, end;
    buf.charptr = ani_buffer + 3;
    end.charptr = ani_buffer + sizeof(ani_buffer);
    for (int pass = 0; pass < 2; pass++) {
        char *file = (char *)(((uintptr_t)buf.charptr + 15) & ~(uintptr_t)15);
        struct nuanimdata_s *data = NuAnimDataLoadBuff("test.ani", &buf, &end);
        CHECK(D_0062EE18 == 1 && memcmp(file, b.buf, b.len) == 0);
        CHECK((char *)data >= file + b.len && buf.charptr > (char *)(data + 1) && buf.charptr < end.charptr);
        check_anim(data, file, &f);
    }

    CHECK(nw_game_init(NW_CLOCK_REALTIME, 50));
    CHECK(nw_game_start(missing_main, 0, NULL));
    NwGameState state;
    while ((state = nw_game_state()) == NW_GAME_RUNNING) {
        SDL_Delay(10);
    }
    CHECK(state == NW_GAME_FATAL && strstr(nw_game_error(), "nuanim.c(760) : Could not read from <missing.ani>"));

    nw_shim_file_init(NULL);
    nw_vfs_shutdown(&vfs);
    SDL_RemovePath(root);
    gsc_free(&b);
    SDL_free(disc);
    SDL_free(root);
}

typedef struct RetailStats {
    unsigned files, other, chunks, sets, curves, keys, bad;
    int minsets[2]; /* the fewest curves in an .ani and a .bsa set: the joint appliers read set[0..8] unchecked */
    unsigned before, past, nonfinite; /* reads before a curve's first key, past its last; inf or NaN values */
    size_t host;
} RetailStats;

static RetailStats retail = { .minsets = { 127, 127 } };

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

/* Every curve at every frame: the keys it reads lie in the file. */
static void retail_eval(const char *path, const char *file, size_t size, struct nuanimdata_s *data)
{
    for (float t = 1.0f; data->nchunks > 0 && t <= data->time + 1.0f; t += 1.0f) {
        struct nuanimtime_s at;
        NuAnimDataCalcTime(data, t, &at);
        struct nuanimdatachunk_s *chunk = data->chunks[at.chunk];
        int key = (int)floorf(at.time_offset) - 1;
        if (key < 0 || key > 31) {
            retail.bad++;
            continue;
        }
        uint32_t upto = (uint32_t)((2ull << key) - 1);
        for (int j = 0; chunk && chunk->animcurvesets && j < chunk->numnodes; j++) {
            struct nuanimcurveset_s *set = chunk->animcurvesets[j];
            for (int k = 0; set && set->set && k < set->ncurves; k++) {
                struct nuanimcurve_s *c = set->set[k];
                if (!c) {
                    continue;
                }
                int ix = __builtin_popcount(c->mask.bits & upto) - 1, last = c->flags & 1 ? ix : ix + 1;
                const char *lo = (const char *)c->animkeys + ix * 16, *hi = (const char *)c->animkeys + last * 16 + 16;
                retail.before += ix < 0;
                retail.past += last >= (int)c->numkeys;
                if (lo < file || hi > file + size) {
                    if (retail.bad++ < 10) {
                        fprintf(stderr, "retail: %s: chunk %d node %d curve %d at frame %g reads outside the file\n",
                                path, at.chunk, j, k, t);
                    }
                    continue;
                }
                retail.nonfinite += !isfinite(NuAnimCurveCalcVal2(c, &at));
            }
        }
    }
}

static void retail_file(const char *path)
{
    /* Other versions are another format, which retail's loader rejects too. */
    int version = 0;
    if (NuFileLoadBuffer((char *)path, ani_buffer, sizeof(ani_buffer)) >= 4) {
        memcpy(&version, ani_buffer, 4);
    }
    if (version != 1) {
        retail.other++;
        return;
    }
    int *minsets = &retail.minsets[SDL_strcasecmp(path + strlen(path) - 4, ".bsa") == 0];
    union variptr_u buf, end;
    buf.charptr = ani_buffer;
    end.charptr = ani_buffer + sizeof(ani_buffer);
    struct nuanimdata_s *data = NuAnimDataLoadBuff((char *)path, &buf, &end);
    /* The image up to the host records, with at most their alignment's padding. */
    size_t size = (size_t)((char *)data - ani_buffer), host = (size_t)(buf.charptr - (char *)data);
    unsigned chunks = 0, sets = 0, curves = 0, keys = 0;
    for (int i = 0; data->chunks && i < data->nchunks; i++) {
        struct nuanimdatachunk_s *chunk = data->chunks[i];
        chunks += chunk != NULL;
        for (int j = 0; chunk && chunk->animcurvesets && j < chunk->numnodes; j++) {
            struct nuanimcurveset_s *set = chunk->animcurvesets[j];
            if (!set) {
                continue;
            }
            sets++;
            *minsets = set->ncurves < *minsets ? set->ncurves : *minsets;
            for (int k = 0; set->set && k < set->ncurves; k++) {
                curves += set->set[k] != NULL;
                keys += set->set[k] ? set->set[k]->numkeys : 0;
            }
        }
    }
    retail_eval(path, ani_buffer, size, data);
    printf("ANI %s time=%g chunks=%u sets=%u curves=%u keys=%u bytes=%zu host=%zu\n", path, data->time, chunks,
           sets, curves, keys, size, host);
    retail.files++;
    retail.chunks += chunks;
    retail.sets += sets;
    retail.curves += curves;
    retail.keys += keys;
    retail.host += host;
}

/* BGLoadScreenInit: the character and its fall in the superbuffer's last 1 MB. */
static void retail_bgload(const char *ghg, const char *ani)
{
    union variptr_u buf, end;
    buf.charptr = ghg_buffer;
    end.charptr = ghg_buffer + 0x100000;
    struct NUHGOBJ_s *h = NuGHGRead(&buf, (char *)ghg);
    struct nuanimdata_s *data = h && buf.charptr < end.charptr ? NuAnimDataLoadBuff((char *)ani, &buf, &end) : NULL;
    printf("BGLOAD %s + %s: 0x%zx bytes\n", ghg, ani, (size_t)(buf.charptr - ghg_buffer));
    if (!data) {
        fprintf(stderr, "retail: %s and %s do not fit the bgload buffer\n", ghg, ani);
        retail.bad++;
    }
    if (h) {
        NuHGobjDestroy(h);
        drain_gpu();
    }
}

static void retail_entry(void *user, int index, const char *path)
{
    (void)user, (void)index;
    size_t n = strlen(path);
    if (n > 4 && (SDL_strcasecmp(path + n - 4, ".ani") == 0 || SDL_strcasecmp(path + n - 4, ".bsa") == 0)) {
        retail_file(path);
    }
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
        if (n > 4 && SDL_strcasecmp(p + n - 4, ".ANI") == 0) {
            char path[NW_VFS_PATH_MAX];
            SDL_strlcpy(path, p, sizeof(path));
            for (char *c = path; *c; c++) {
                *c = *c == '/' ? '\\' : *c;
            }
            retail_file(path);
        }
    }
    NwDat raw;
    struct nudathdr_s *dat = NuDatOpen("CHARS.DAT", NULL, NULL);
    if (nw_dat_open(&raw, &retail_vfs, "cdrom0:\\CHARS.DAT;1") && dat) {
        NuDatSet(dat);
        nw_dat_list(&raw, retail_entry, NULL);
        retail_bgload("chars\\a\\crash\\crash.ghg", "chars\\a\\crash\\g\\longfall.ani");
        retail_bgload("chars\\a\\coco\\coco.ghg", "chars\\a\\coco\\b\\falllong.ani");
        NuDatSet(NULL);
        NuDatClose(dat);
        nw_dat_close(&raw);
    } else {
        retail.bad++;
    }
    return 0;
}

/* Every .ani and .bsa loads to the pinned counts, every curve reads keys inside its file at every frame, every .ani
   set has the nine curves the joint appliers read, and the bgload falls fit their buffer. The 5 non-finite values:
   coco's slamland and flckhair have two keys on one frame, whose dtime the exporter wrote as 1/0, an inf on the host
   and a finite maximum on the EE FPU. */
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
    printf("TOTAL files=%u other=%u chunks=%u sets=%u curves=%u keys=%u minsets=%d/%d before=%u past=%u nonfinite=%u "
           "host=%zu bad=%u\n", retail.files, retail.other, retail.chunks, retail.sets, retail.curves, retail.keys,
           retail.minsets[0], retail.minsets[1], retail.before, retail.past, retail.nonfinite, retail.host, retail.bad);
    CHECK(state == NW_GAME_RETURNED && retail.bad == 0 && retail.files == 712 && retail.other == 17);
    CHECK(retail.chunks == 2075 && retail.sets == 45833 && retail.curves == 47940 && retail.keys == 517751);
    CHECK(retail.minsets[0] == 9 && retail.before == 0 && retail.past == 69 && retail.nonfinite == 5);
    return failures ? 1 : 0;
}

int main(int argc, char *argv[])
{
    if (argc > 1 && strcmp(argv[1], "retail") == 0) {
        return test_retail();
    }
    if (argc != 2) {
        fprintf(stderr, "usage: test_nuanim <scratch dir> | retail\n");
        return 2;
    }
    char *scratch = NULL;
    SDL_asprintf(&scratch, "%s/", argv[1]);
    SDL_CreateDirectory(scratch);
    test_load(scratch);
    SDL_free(scratch);
    printf("%s\n", failures ? "FAILED" : "nuanim: all checks passed");
    return failures ? 1 : 0;
}
