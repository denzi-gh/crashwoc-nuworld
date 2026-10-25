/* The .ter loader override (src/port/terrain.c) on a synthetic file, then under terraininit; locally every .ter of
   the imported disc over its level scene. Usage: test_terrain <scratch dir> | retail. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "gamelib/terrain.h"
#include "gfx/rndr.h"
#include "gfx/vif.h"
#include "nu3d/nugscn.h"
#include "nu3d/numtl.h"
#include "nu3d/nutex.h"
#include "nucore/nufile.h"
#include "port/game.h"
#include "port/nugscn.h"
#include "shim/shim.h"
#include "vfs/extract.h"
#include "vfs/manifest.h"
#include "vfs/vfs.h"

#include "ter_builder.h"

/* Declared by no header. */
s32 ReadTerrain(char *name, s32 situ, short **store, TerrainSetType *terrset);
void NuRndrInitWorld(void);
void buildBitCountTable(void);
extern void *crashdata;
extern s32 situtotal;
extern s32 terraincnt;

static int failures;
static _Alignas(16) char ter_heap[1 << 20];
static TerrainSetType ter_set;
static TerrainSituType ter_terr[0x800];

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

static WallSplineType *next_wall(WallSplineType *w)
{
    return ((WallSplineType **)w)[-1];
}

/* ReadTerrain on its own: objects into terr[], the walls as host copies after the untouched image. */
static void test_read(const GscBuild *b, const TerFile *f)
{
    memset(&ter_set, 0, sizeof(ter_set));
    memset(ter_terr, 0, sizeof(ter_terr));
    ter_set.terr = ter_terr;
    short *store = (short *)ter_heap;
    char *file = ter_heap;
    CHECK(ReadTerrain("test", 0, &store, &ter_set) == 2);
    CHECK(memcmp(file, b->buf, b->len) == 0);

    TerrainSituType *t = ter_terr;
    CHECK(t[0].type == 0 && t[0].id == 5 && t[0].info == 3 && t[0].flags == 2 && t[0].Location.x == 10.0f);
    CHECK(t[0].model == (s16 *)(file + f->model[0]));
    CHECK(t[1].type == 1 && t[1].id == 6 && t[1].info == 0 && t[1].flags == 1 && t[1].Location.x == 20.0f);
    CHECK(t[1].model == (s16 *)(file + f->model[1]) && t[2].type == 0 && t[2].model == NULL);
    CHECK(crashdata == file + f->crash);

    WallSplineType *w1 = ter_set.wallinfo, *w0 = w1 ? next_wall(w1) : NULL;
    CHECK(w1 && w0 && next_wall(w0) == NULL);
    if (w1 && w0) {
        CHECK((char *)w0 > file + b->len && (char *)w1 > (char *)w0);
        CHECK(((uintptr_t)w0 & 7) == 0 && ((uintptr_t)w1 & 7) == 0);
        CHECK(w0->count == 17 && w0->val == 7 && w1->count == 2 && w1->val == 9);
        CHECK(w0->spl[0].y == 0.0f && w0->spl[1].y == 16.0f && w0->spl[2].y == 0.0f && w0->spl[3].y == 32.0f);
        CHECK(w0->spl[4].y == 0.0f && w0->spl[16].y == 2147483647.0f && w0->spl[17].x == 17.0f);
        CHECK(w1->spl[0].y == 2147483647.0f && w1->spl[2].z == 4.0f);
        CHECK((char *)store == (char *)w1 + offsetof(WallSplineType, spl) + 3 * sizeof(struct nuvec_s));
    }

    short *keep = store;
    CHECK(ReadTerrain("missing", 0, &store, &ter_set) == -1 && store == keep);
}

/* terraininit around it: the set and terr[] carved at host sizes, then the file, the objects grouped and the
   platform bound to its instance. */
static void test_init(const TerFile *f)
{
    static struct nuinstance_s inst[1];
    struct nugscn_s scene;
    memset(&scene, 0, sizeof(scene));
    memset(inst, 0, sizeof(inst));
    inst[0].flags.visible = 1;
    scene.numinstance = 1;
    scene.instances = inst;
    short *store = (short *)ter_heap;
    TerrainSetType *set = (TerrainSetType *)terraininit(0, &store, (short *)(ter_heap + sizeof(ter_heap)), 0, "test",
                                                        &scene, 0);
    char *file = (char *)set + sizeof(TerrainSetType) + 0x880 * sizeof(TerrainSituType);
    CHECK(set == (TerrainSetType *)ter_heap && (char *)set->terr == (char *)set + sizeof(TerrainSetType));
    CHECK(situtotal == 2 && terraincnt == 2 && set->miny == 0);

    TerrainSituType *t = set->terr;
    CHECK(t[0].model == (s16 *)(file + f->model[0]) && t[0].radius == 2.0f);
    CHECK(t[0].min.x == 9.0f && t[0].max.x == 11.0f && t[0].min.z == -1.0f && t[0].max.z == 1.0f);
    const tertype *ter = (const tertype *)((const char *)t[0].model + sizeof(TerrainGroupType));
    CHECK(ter->info[0] == 6 && ter->info[1] == 0 && ter->info[2] == 0x81);
    CHECK(set->terrgroupcnt == 1 && set->terrgroup[0].count == 1 && set->terrlist[set->terrgroup[0].start] == 0);

    CHECK(t[1].type == 1 && t[1].info == 0 && t[1].min.x == 0.0f && t[1].max.x == 1.0f);
    CHECK(set->terrgroup[256].count == 1 && set->terrlist[set->terrgroup[256].start] == 1);
    CHECK(set->platdata[0].curmtx == &inst[0].mtx && set->platdata[0].instance == 0);
    CHECK(set->platdata[0].terrno == 1 && set->platdata[0].status.bit.rotate == 1);
}

static int corrupt_main(int argc, char **argv)
{
    (void)argc, (void)argv;
    short *store = (short *)ter_heap;
    ter_set.terr = ter_terr;
    ReadTerrain("bad", 0, &store, &ter_set);
    return 0;
}

static void test_load(const char *scratch)
{
    char *root = NULL, *disc = NULL, *path = NULL;
    GscBuild b;
    TerFile f;
    NwVfs vfs;

    SDL_asprintf(&root, "%sterrain/", scratch);
    SDL_asprintf(&disc, "%s" NW_DISC_DIR "/", root);
    SDL_RemovePath(root);
    CHECK(SDL_CreateDirectory(disc));
    SDL_asprintf(&path, "%s" NW_DISC_STAMP, disc);
    CHECK(SDL_SaveFile(path, "", 0));
    SDL_free(path);
    ter_build(&b, &f);
    SDL_asprintf(&path, "%sTEST.TER", disc);
    CHECK(SDL_SaveFile(path, b.buf, b.len));
    SDL_free(path);
    /* The directory says 7 entries; the file ends after 3. */
    SDL_asprintf(&path, "%sBAD.TER", disc);
    CHECK(SDL_SaveFile(path, b.buf, f.dir + 4 + 3 * sizeof(OffType)));
    SDL_free(path);

    CHECK(nw_vfs_init(&vfs, root));
    nw_shim_file_init(&vfs);
    NuFileInitEx(1, 1);
    test_read(&b, &f);
    test_init(&f);

    CHECK(nw_game_init(NW_CLOCK_REALTIME, 50));
    CHECK(nw_game_start(corrupt_main, 0, NULL));
    NwGameState state;
    while ((state = nw_game_state()) == NW_GAME_RUNNING) {
        SDL_Delay(10);
    }
    CHECK(state == NW_GAME_FATAL && strstr(nw_game_error(), "terrain.c(787) : terrain <bad.ter>: directory"));

    nw_shim_file_init(NULL);
    nw_vfs_shutdown(&vfs);
    SDL_RemovePath(root);
    gsc_free(&b);
    SDL_free(disc);
    SDL_free(root);
}

typedef struct RetailStats {
    unsigned files, objects, platforms, bound, triangles, walls, points, crash, bad;
} RetailStats;

static NwVfs retail_vfs;
static RetailStats retail;

static void drain_gpu(void)
{
    NwGpuJob *jobs = NULL;
    uint32_t cap = 0, n = nw_gpu_take(UINT64_MAX, &jobs, &cap);
    for (uint32_t i = 0; i < n; i++) {
        if (jobs[i].op == NW_GPU_FREE_MESH) {
            nw_vif_free(&((NwGpuMesh *)jobs[i].obj)->mesh);
        } else if (jobs[i].op == NW_GPU_FREE_SPRITES) {
            nw_vif_sprites_free(&((NwGpuSprites *)jobs[i].obj)->sprites);
        }
        if (jobs[i].op >= NW_GPU_FREE_MESH && jobs[i].op != NW_GPU_FREE_TEX) {
            SDL_free(jobs[i].obj);
        }
    }
    SDL_free(jobs);
}

static void retail_file(const char *base, char *buffer, size_t size)
{
    char gsc[NW_VFS_PATH_MAX];
    SDL_snprintf(gsc, sizeof(gsc), "%s.GSC", base);
    char *buf = buffer;
    NuRndrInitWorld();
    struct nugscn_s *sc = NuGScnRead(&buf, buffer + size, gsc);
    if (!sc) {
        fprintf(stderr, "retail: %s does not load\n", gsc);
        retail.bad++;
        return;
    }
    buf = (char *)(((uintptr_t)buf + 15) & ~(uintptr_t)15);
    TerrainSetCur(buf);
    short *store = (short *)buf;
    TerrainSetType *set = (TerrainSetType *)terraininit(0, &store, (short *)(buffer + size), 0, (char *)base, sc, 0);
    RetailStats was = retail;
    for (int i = 0; i < 0x800; i++) {
        TerrainSituType *t = &set->terr[i];
        if (!t->model) {
            continue;
        }
        retail.objects += t->type == 0;
        for (const TerrainGroupType *g = (const TerrainGroupType *)t->model; g->start >= 0;
             g = (const TerrainGroupType *)((const tertype *)(g + 1) + g->count)) {
            retail.triangles += (unsigned)g->count;
        }
    }
    retail.platforms += (unsigned)set->terrgroup[256].count;
    for (int i = 0; i < set->terrgroup[256].count; i++) {
        retail.bound += set->terr[set->terrlist[set->terrgroup[256].start + i]].type == 1;
    }
    for (WallSplineType *w = set->wallinfo; w; w = next_wall(w)) {
        retail.walls++;
        retail.points += w->count + 1u;
    }
    retail.crash += crashdata != NULL;
    retail.files++;
    if (situtotal <= 0) {
        fprintf(stderr, "retail: %s: no terrain objects\n", base);
        retail.bad++;
    }
    printf("TER %s objects=%u platforms=%u bound=%u triangles=%u walls=%u points=%u crash=%u\n", base,
           retail.objects - was.objects, retail.platforms - was.platforms, retail.bound - was.bound,
           retail.triangles - was.triangles, retail.walls - was.walls, retail.points - was.points,
           retail.crash - was.crash);
    nw_gscn_remove(sc);
    drain_gpu();
}

/* On the game thread: a load error is fatal there. */
static int retail_main(int argc, char **argv)
{
    (void)argc, (void)argv;
    size_t size = 0x2400000; /* main's superbuffer, doubled for the host's larger records */
    char *buffer = SDL_malloc(size);
    nw_shim_file_init(&retail_vfs);
    NuFileInitEx(1, 1);
    NuTexInit();
    NuMtlInit();
    buildBitCountTable();
    for (size_t i = 0; buffer && i < nw_disc_manifest.count; i++) {
        const NwManifestEntry *e = &nw_disc_manifest.entries[i];
        size_t n = strlen(e->path);
        if (n < 4 || SDL_strcasecmp(e->path + n - 4, ".TER") != 0 || (e->flags & NW_MANIFEST_SKIP)) {
            continue;
        }
        char base[NW_VFS_PATH_MAX];
        SDL_strlcpy(base, e->path, n - 4 + 1 < sizeof(base) ? n - 4 + 1 : sizeof(base));
        for (char *c = base; *c; c++) {
            *c = *c == '/' ? '\\' : *c;
        }
        retail_file(base, buffer, size);
    }
    SDL_free(buffer);
    return 0;
}

/* Every .ter loads over its scene to the pinned counts: platforms bound to a visible instance, and no level has
   crash data. */
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
    printf("TOTAL files=%u objects=%u platforms=%u bound=%u triangles=%u walls=%u points=%u crash=%u bad=%u\n",
           retail.files, retail.objects, retail.platforms, retail.bound, retail.triangles, retail.walls,
           retail.points, retail.crash, retail.bad);
    CHECK(state == NW_GAME_RETURNED && retail.files == 40 && retail.bad == 0);
    CHECK(retail.objects == 2460 && retail.platforms == 275 && retail.bound == 175 && retail.triangles == 199369);
    CHECK(retail.walls == 243 && retail.points == 14103 && retail.crash == 0);
    return failures ? 1 : 0;
}

int main(int argc, char *argv[])
{
    if (argc > 1 && strcmp(argv[1], "retail") == 0) {
        return test_retail();
    }
    if (argc != 2) {
        fprintf(stderr, "usage: test_terrain <scratch dir> | retail\n");
        return 2;
    }
    char *scratch = NULL;
    SDL_asprintf(&scratch, "%s/", argv[1]);
    SDL_CreateDirectory(scratch);
    test_load(scratch);
    SDL_free(scratch);
    printf("%s\n", failures ? "FAILED" : "terrain: all checks passed");
    return failures ? 1 : 0;
}
