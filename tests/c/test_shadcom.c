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
#include "numath/numtx.h"
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
void ShadRndr(struct numtx_s *mtx, struct shadentry_s *shad, f32 frame, f32 scale);
void NuRndrInitEx(int streamsize);
void NuRndrEndScene(void);
void NuRndrSwapStreamBuffers(void);
extern s32 palids[4];
extern int no_default_font;

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

/* ShadRndr: the frame's cell of a 2 x 2 atlas, a strip under the matrix with its axes scaled; the frame's palette
   (frame & 3), one of ShadComInit's four CLUTs that each show one bit of the texels, is the record's CLUT. */
static void test_draw(void)
{
    no_default_font = 1;
    NuRndrInitEx(0x10000);
    ShadComInit();
    struct numtl_s *mtl = NuMtlCreate(1);
    NuMtlUpdate(mtl);
    struct shadentry_s e;
    memset(&e, 0, sizeof(e));
    e.xframes = 2;
    e.yframes = 2;
    e.mtl = mtl;
    struct numtx_s mtx;
    NuMtxSetIdentity(&mtx);
    mtx._30 = 5.0f;
    ShadRndr(&mtx, &e, 6.0f, 2.0f);
    NuRndrEndScene();
    const NwPacket *p = nw_rndr_packet();
    const NwCmd *c = p->ncmds == 1 ? p->cmds : NULL;
    CHECK(c && c->op == NW_CMD_VERTS && c->mode == NW_VERTS_TRIS && c->count == 6 && c->material == mtl);
    CHECK(c && c->local[0] == 2.0f && c->local[5] == 2.0f && c->local[12] == 5.0f && c->local[15] == 1.0f);
    const NwGpuTex *pal1 = nw_rndr_texture(palids[1]);
    CHECK(c && c->clut == pal1 && pal1 && pal1->tex.nlevels == 0 && pal1->tex.ncolours == 16);
    CHECK(pal1 && pal1->tex.clut[1][3] == 0 && pal1->tex.clut[2][3] == 0x20 && pal1->tex.clut[2][0] == 0xFF);
    if (c && c->count == 6) {
        const NwVuSpriteVertex *v = &p->verts[c->first];
        CHECK(v[0].pos[0] == -1.0f && v[0].pos[2] == 1.0f && v[5].pos[0] == 1.0f && v[5].pos[2] == -1.0f);
        CHECK(v[0].tex[0] == 2048.0f && v[0].tex[1] == 0.0f && v[5].tex[0] == 4096.0f && v[5].tex[1] == 2048.0f);
        CHECK(v[0].col[0] == 0xFF && v[0].col[3] == 0x7F);
    }
    NuRndrSwapStreamBuffers();
    ShadRndr(&mtx, &e, 0.0f, 1.0f);
    NuRndrEndScene();
    p = nw_rndr_packet();
    CHECK(p->ncmds == 1 && p->cmds[0].clut == nw_rndr_texture(palids[0]) && p->verts[p->cmds[0].first].tex[0] == 0.0f);
    NuRndrSwapStreamBuffers();
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
    test_draw();
    SDL_free(scratch);
    printf("%s\n", failures ? "FAILED" : "shadcom: all checks passed");
    return failures ? 1 : 0;
}
