/* The .shd loader override (src/port/shadcom.c) on a synthetic file; locally every .shd in CHARS.DAT of the imported
   disc. Usage: test_shadcom <scratch dir> | retail. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "game/shadcom.h"
#include "gfx/rndr.h"
#include "nu3d/numtl.h"
#include "nu3d/nutex.h"
#include "nucore/nufile.h"
#include "port/game.h"
#include "shim/shim.h"
#include "vfs/dat.h"
#include "vfs/extract.h"
#include "vfs/manifest.h"
#include "vfs/vfs.h"

#include "shd_builder.h"

/* Declared by no header. */
struct shaddata_s *ShadDataLoad(char *file, union variptr_u *buffer);
void ShadDataDestroy(struct shaddata_s *data);
struct shadentry_s *ShadFindData(struct shaddata_s *data, char *name);

static int failures;
static _Alignas(16) char shd_heap[8 << 20];

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

/* GPU jobs since the last call, counted by op. */
static void take_gpu(uint32_t ops[NW_GPU_FREE_HOST + 1])
{
    NwGpuJob *jobs = NULL;
    uint32_t cap = 0, n = nw_gpu_take(UINT64_MAX, &jobs, &cap);
    memset(ops, 0, (NW_GPU_FREE_HOST + 1) * sizeof(*ops));
    for (uint32_t i = 0; i < n; i++) {
        ops[jobs[i].op]++;
        if (jobs[i].op == NW_GPU_FREE_HOST) {
            SDL_free(jobs[i].obj);
        }
    }
    SDL_free(jobs);
}

static void check_entry(const struct shadentry_s *e, int16_t xframes)
{
    CHECK(e->xframes == xframes && e->yframes == 3 && e->pad04 == 0x55 && e->tid != 0 && e->mtl);
    if (e->mtl) {
        CHECK(e->mtl->tid == e->tid && e->mtl->sort_pri == 100 && e->mtl->alpha == 0.999f);
        CHECK(e->mtl->diffuse.r == 1.0f && e->mtl->diffuse.g == 1.0f && e->mtl->diffuse.b == 1.0f);
        CHECK(e->mtl->attrib.cull == 2 && e->mtl->attrib.zmode == 0 && e->mtl->attrib.alpha == 3);
        CHECK(e->mtl->attrib.filter == 1);
    }
}

static int nobuffer_main(int argc, char **argv)
{
    (void)argc, (void)argv;
    ShadDataLoad("test.shd", NULL);
    return 0;
}

static void test_load(const char *scratch)
{
    char *root = NULL, *disc = NULL, *path = NULL;
    GscBuild b;
    ShdFile f;
    NwVfs vfs;
    uint32_t ops[NW_GPU_FREE_HOST + 1];

    SDL_asprintf(&root, "%sshadcom/", scratch);
    SDL_asprintf(&disc, "%s" NW_DISC_DIR "/", root);
    SDL_RemovePath(root);
    CHECK(SDL_CreateDirectory(disc));
    SDL_asprintf(&path, "%s" NW_DISC_STAMP, disc);
    CHECK(SDL_SaveFile(path, "", 0));
    SDL_free(path);
    shd_build(&b, &f);
    SDL_asprintf(&path, "%sTEST.SHD", disc);
    CHECK(SDL_SaveFile(path, b.buf, b.len));
    SDL_free(path);

    CHECK(nw_vfs_init(&vfs, root));
    nw_shim_file_init(&vfs);
    NuFileInitEx(1, 1);
    NuTexInit();
    NuMtlInit();

    union variptr_u buf;
    buf.charptr = shd_heap + 3;
    char *file = shd_heap + 16;
    struct shaddata_s *data = ShadDataLoad("test.shd", &buf);
    CHECK(memcmp(file, b.buf, b.len) == 0);
    char *mtls = file + ((b.len + 15) & ~(size_t)15);
    CHECK(data && (char *)data >= mtls + 2 * sizeof(struct numtl_s));
    for (size_t i = 0; i < 2 * sizeof(struct numtl_s); i++) {
        CHECK(mtls[i] == 0);
    }
    if (data) {
        CHECK(data->count == 2 && data->external == 1);
        struct shadentry_s *e0 = data->table[0].entry, *e1 = data->table[1].entry;
        CHECK(buf.charptr == (char *)(e0 + 2) && e1 == e0 + 1);
        check_entry(e0, 2);
        check_entry(e1, 3);
        CHECK(e0->tid != e1->tid && e0->mtl != e1->mtl);
        CHECK(data->table[2].name == file + f.name[0] && data->table[3].name == file + f.name[1]);
        CHECK(ShadFindData(data, NULL) == e0 && ShadFindData(data, "shadow_b") == e1);
        CHECK(ShadFindData(data, "nope") == e0);
        take_gpu(ops);
        CHECK(ops[NW_GPU_UPLOAD_TEX] == 2);
        ShadDataDestroy(data);
    }
    char *keep = buf.charptr;
    CHECK(ShadDataLoad("missing.shd", &buf) == NULL && buf.charptr == keep);

    CHECK(nw_game_init(NW_CLOCK_REALTIME, 50));
    CHECK(nw_game_start(nobuffer_main, 0, NULL));
    NwGameState state;
    while ((state = nw_game_state()) == NW_GAME_RUNNING) {
        SDL_Delay(10);
    }
    CHECK(state == NW_GAME_FATAL && strstr(nw_game_error(), "shadcom.c(299) : shadow data <test.shd>: no buffer"));
    take_gpu(ops);

    nw_shim_file_init(NULL);
    nw_vfs_shutdown(&vfs);
    SDL_RemovePath(root);
    gsc_free(&b);
    SDL_free(disc);
    SDL_free(root);
}

typedef struct RetailStats {
    unsigned files, entries, textures, bad;
} RetailStats;

static NwVfs retail_vfs;
static RetailStats retail;

static void retail_entry(void *user, int index, const char *path)
{
    (void)user, (void)index;
    size_t n = strlen(path);
    if (n < 4 || SDL_strcasecmp(path + n - 4, ".shd") != 0) {
        return;
    }
    union variptr_u buf;
    buf.charptr = shd_heap;
    struct shaddata_s *data = ShadDataLoad((char *)path, &buf);
    if (!data || data->count <= 0) {
        fprintf(stderr, "retail: %s: no shadows\n", path);
        retail.bad++;
        return;
    }
    unsigned textures = 0;
    for (int i = 0; i < data->count; i++) {
        const struct shadentry_s *e = data->table[i].entry;
        textures += e->tid != 0 && e->mtl && e->mtl->tid == e->tid;
        if (!data->table[data->count + i].name[0] || e->xframes <= 0 || e->yframes <= 0) {
            fprintf(stderr, "retail: %s: entry %d has no name or frames\n", path, i);
            retail.bad++;
        }
    }
    printf("SHD %s entries=%d textures=%u bytes=%zu\n", path, data->count, textures,
           (size_t)(buf.charptr - shd_heap));
    retail.files++;
    retail.entries += (unsigned)data->count;
    retail.textures += textures;
    if (textures == (unsigned)data->count) {
        ShadDataDestroy(data);
    }
    uint32_t ops[NW_GPU_FREE_HOST + 1];
    take_gpu(ops);
}

/* On the game thread: a load error is fatal there. */
static int retail_main(int argc, char **argv)
{
    (void)argc, (void)argv;
    nw_shim_file_init(&retail_vfs);
    NuFileInitEx(1, 1);
    NuTexInit();
    NuMtlInit();
    NwDat raw;
    struct nudathdr_s *dat = NuDatOpen("CHARS.DAT", NULL, NULL);
    if (nw_dat_open(&raw, &retail_vfs, "cdrom0:\\CHARS.DAT;1") && dat) {
        NuDatSet(dat);
        nw_dat_list(&raw, retail_entry, NULL);
        NuDatSet(NULL);
        NuDatClose(dat);
        nw_dat_close(&raw);
    } else {
        retail.bad++;
    }
    return 0;
}

/* Every .shd loads to the pinned counts with a texture and a material per entry. */
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
    printf("TOTAL files=%u entries=%u textures=%u bad=%u\n", retail.files, retail.entries, retail.textures,
           retail.bad);
    CHECK(state == NW_GAME_RETURNED && retail.files == 141 && retail.bad == 0);
    CHECK(retail.entries == 403 && retail.textures == retail.entries);
    return failures ? 1 : 0;
}

int main(int argc, char *argv[])
{
    if (argc > 1 && strcmp(argv[1], "retail") == 0) {
        return test_retail();
    }
    if (argc != 2) {
        fprintf(stderr, "usage: test_shadcom <scratch dir> | retail\n");
        return 2;
    }
    char *scratch = NULL;
    SDL_asprintf(&scratch, "%s/", argv[1]);
    SDL_CreateDirectory(scratch);
    test_load(scratch);
    SDL_free(scratch);
    printf("%s\n", failures ? "FAILED" : "shadcom: all checks passed");
    return failures ? 1 : 0;
}
