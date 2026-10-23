/* The .gsc loader override (src/port/nugscn.c) on the synthetic scene; locally the animation library of every .gsc
   of the imported disc. Usage: test_nugscn <scratch dir> | retail. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "gfx/gsc.h"
#include "gfx/rndr.h"
#include "gfx/vif.h"
#include "nu3d/nuanim.h"
#include "nu3d/nugobj.h"
#include "nu3d/nugscn.h"
#include "nu3d/numtl.h"
#include "nu3d/nutex.h"
#include "nu3d/nutexanm.h"
#include "nucore/nufile.h"
#include "nw_ps2types.h"
#include "port/game.h"
#include "port/nugscn.h"
#include "shim/shim.h"
#include "vfs/extract.h"
#include "vfs/manifest.h"
#include "vfs/vfs.h"

#include "anim_check.h"
#include "gsc_builder.h"

/* The NU20 reader's record offsets are the PS2 layout's. */
_Static_assert(sizeof(nw_ps2_numtl_s) == NW_GSC_MTL_SIZE && offsetof(nw_ps2_numtl_s, attrib) == 0x16C, "numtl_s");
_Static_assert(offsetof(nw_ps2_numtl_s, diffuse) == 0x180 && offsetof(nw_ps2_numtl_s, alpha) == 0x1A0, "numtl_s");
_Static_assert(offsetof(nw_ps2_numtl_s, tid) == 0x1A4, "numtl_s.tid");
_Static_assert(sizeof(nw_ps2_nuinstance_s) == NW_GSC_INST_SIZE && offsetof(nw_ps2_nuinstance_s, objid) == 0x40,
               "nuinstance_s");

extern int nugscn_mtlupdate_enabled;
extern char vu_load_tricu_mpg[], vu_load_tric_mpg[];
extern int NuRndrShadMaskCount;
extern struct nugspline_s *NuRndrShadMasks[];
void NuRndrInitWorld(void);
void buildBitCountTable(void);

static int failures;
static char scene_buffer[1 << 20];

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

/* The reader's attrib macros against the host bitfields. */
static void test_attrib(void)
{
    struct numtlattrib_s a;
    uint32_t w[2];
    memset(&a, 0, sizeof(a));
    a.fx = 1;
    a.lighting = 3;
    a.colour = 1;
    a.ot = 1;
    memcpy(w, &a, sizeof(w));
    CHECK(NW_GSC_ATTRIB_FX(w) == 1 && NW_GSC_ATTRIB_LIGHTING(w) == 3 && NW_GSC_ATTRIB_COLOUR(w) == 1);
    CHECK(NW_GSC_ATTRIB_OT(w) == 1);
    memset(&a, 0, sizeof(a));
    a.alpha = 6;
    a.filter = 2;
    a.utc = 1;
    a.vtc = 2;
    a.zmode = 3;
    a.atst = 5;
    a.aref = 0x7E;
    a.afail = 3;
    memcpy(w, &a, sizeof(w));
    CHECK(NW_GSC_ATTRIB_ALPHA(w) == 6 && NW_GSC_ATTRIB_FILTER(w) == 2 && NW_GSC_ATTRIB_UTC(w) == 1);
    CHECK(NW_GSC_ATTRIB_VTC(w) == 2 && NW_GSC_ATTRIB_ZMODE(w) == 3 && NW_GSC_ATTRIB_ATST(w) == 5);
    CHECK(NW_GSC_ATTRIB_AREF(w) == 0x7E && NW_GSC_ATTRIB_AFAIL(w) == 3 && NW_GSC_ATTRIB_OT(w) == 0);
}

static void check_scene(struct nugscn_s *sc, const char *file, const GscScene *s)
{
    CHECK(sc->numtid == 0 && sc->nummtl == 3);
    CHECK(sc->mtls[0]->next == sc->mtls[2] && sc->mtls[1]->next == NULL && sc->mtls[2]->next == NULL);
    CHECK(sc->mtls[0]->tid == 0 && sc->mtls[0]->attrib.lighting == 2 && sc->mtls[1]->attrib.ot == 1);
    CHECK(sc->mtls[0]->rs.mpg == (unsigned char *)vu_load_tricu_mpg);
    CHECK(sc->mtls[1]->rs.mpg == (unsigned char *)vu_load_tric_mpg && sc->mtls[2]->vuattr.u32 != NULL);
    CHECK(nw_rndr_state(sc->mtls[2])->diffuse[1] == 64.0f);
    CHECK(sc->mtls[2]->diffuse.g == 0.5f && sc->mtls[2]->alpha == 1.0f);

    CHECK(sc->numgobj == 2);
    struct nugobj_s *g = sc->gobjs[0];
    CHECK(g->ngobjs == 1 && g->type == 0 && g->origin.z == 3.0f && g->tricount == 7);
    CHECK(g->min.x == -1.0f && g->max.z == 6.0f && g->cntr.y == 1.5f && g->r2 == 77.0f && g->r == 0.0f);
    CHECK(g->cntr_r2 == 38.75f && g->cntr_r == (float)sqrt(38.75));
    struct nugeom_s *geom = g->geom;
    CHECK(geom->next == geom + 1 && geom[1].next == NULL);
    CHECK(geom->mtl == sc->mtls[0] && geom[1].mtl == sc->mtls[2] && geom->otbatch == NULL);
    for (int i = 0; i < 2; i++) {
        const NwVifMesh *m = &((const NwGpuMesh *)geom[i].dmastream)->mesh;
        CHECK(m && m->nbatches == s->batches[i] && m->nverts == s->verts[i] && m->ntris == s->tris[i]);
    }
    g = sc->gobjs[1];
    CHECK(g->ngobjs == 2 && g[0].type == 0 && g[1].type == 1);
    const NwGpuMesh *ot = g[0].geom->dmastream;
    CHECK(g[0].geom->otbatch == (void *)(file + s->blob[2]) && ot && ot->mesh.ntris == 1 && ot->mesh.nverts == 3);
    CHECK(ot && ot->mesh.verts[2].pos[2] == 6.0f && ot->mesh.verts[1].tex[0] == 4096 && ot->mesh.verts[2].col[0] == 32);
    CHECK(ot && ot->mesh.verts[0].col[3] == 0x80 && ot->mesh.tris[2] == 2);
    struct nufaceongeom_s *f = g[1].faceon_geom;
    CHECK(f->mtl == sc->mtls[0] && f->faceon_type == 3 && f->faceon_radius == 1.5f);
    const NwGpuSprites *sprites = f->dmastream;
    CHECK(sprites && sprites->sprites.count == 2 && !sprites->sprites.facey && f->next == NULL);
    CHECK(sprites && sprites->sprites.sprites[1].pos[0] == 3.0f && sprites->sprites.sprites[0].size[0] == -1.0f);

    CHECK(sc->numinstance == 3);
    struct nuinstance_s *in = sc->instances;
    CHECK(in[0].flags.visitest && in[0].flags.visible && in[1].objid == 1 && in[2].mtx._30 == 20.0f);
    CHECK(in[0].anim == NULL && in[1].anim == sc->instanimblock && in[1].anim->anim_ix == 9);
    CHECK(memcmp(&in[1].anim->mtx, &in[1].mtx, sizeof(struct numtx_s)) == 0);

    CHECK(strcmp(sc->nametable + s->names[0], "stencil_a") == 0);
    CHECK(sc->numspecial == 1 && sc->specials[0].instance == &in[2] && sc->specials[0].flags == 2);
    CHECK(strcmp(sc->specials[0].name, "special") == 0 && sc->specials[0].mtx._30 == 20.0f);
    CHECK(sc->numsplines == 1 && sc->splines[0].len == 3 && sc->splines[0].ptsize == 12);
    CHECK(sc->splines[0].name == sc->nametable + s->names[0] && ((float *)sc->splines[0].pts)[8] == 8.0f);
    CHECK(NuRndrShadMaskCount == 1 && NuRndrShadMasks[0] == &sc->splines[0]);

    struct nuanimdata_s *anim = sc->instanimdata[0];
    CHECK(anim && sc->instanimdata[1] == NULL);
    if (anim) {
        CHECK(anim->time == 2.5f && anim->node_names == (char *)0x1234 && anim->nchunks == 1);
        struct nuanimdatachunk_s *chunk = anim->chunks[0];
        CHECK(chunk->numnodes == 1 && chunk->keys == (void *)0x55 && chunk->curves == (void *)0x66);
        struct nuanimcurveset_s *set = chunk->animcurvesets[0];
        CHECK(set->flags == 1 && set->constants[1] == -0.75f && set->ncurves == 2 && set->set[1] == NULL);
        CHECK(set->set[0]->mask.bits == 0xF0 && set->set[0]->numkeys == 2 && set->set[0]->animkeys[1].time == 2.0f);
    }
    CHECK(sc->numinstanceixs == 2 && sc->instanceixs[0] == 5 && sc->instanceixs[1] == 7);

    CHECK(sc->numtexanims == 1);
    struct nutexanim_s *tex = sc->texanims;
    CHECK(tex->mtl == sc->mtls[2] && strcmp(tex->ntaname, "anim") == 0 && !tex->succ && !tex->prev);
    CHECK(tex->env && tex->env->mtl == sc->mtls[2] && tex->env->prog == NULL);
    CHECK((char *)tex->env > file && (char *)tex->env < scene_buffer + sizeof(scene_buffer));
}

static int count_mtls(void)
{
    int n = 0;
    for (struct numtl_s *m = NuMtlIterate(1); m; m = m->snext) {
        n++;
    }
    return n;
}

static void test_load(const char *scratch)
{
    char *root = NULL, *disc = NULL, *path = NULL;
    GscBuild b;
    GscScene s;
    NwVfs vfs;

    SDL_asprintf(&root, "%snugscn/", scratch);
    SDL_asprintf(&disc, "%s" NW_DISC_DIR "/", root);
    SDL_RemovePath(root);
    CHECK(SDL_CreateDirectory(disc));
    SDL_asprintf(&path, "%s" NW_DISC_STAMP, disc);
    CHECK(SDL_SaveFile(path, "", 0));
    SDL_free(path);
    gsc_build_scene(&b, &s);
    SDL_asprintf(&path, "%sTEST.GSC", disc);
    CHECK(SDL_SaveFile(path, b.buf, b.len));
    SDL_free(path);

    CHECK(nw_vfs_init(&vfs, root));
    nw_shim_file_init(&vfs);
    NuFileInitEx(1, 1);
    NuTexInit();
    NuMtlInit();
    nugscn_mtlupdate_enabled = 1;

    char *end = scene_buffer + sizeof(scene_buffer);
    char *file = (char *)(((uintptr_t)scene_buffer + 3 + 0x7F) & ~(uintptr_t)0x7F);
    int mtls = count_mtls();
    /* Twice into the same buffer, as the scene viewer switches scenes. */
    for (int pass = 0; pass < 2; pass++) {
        char *buf = scene_buffer + 3;
        NuRndrInitWorld();
        struct nugscn_s *sc = NuGScnRead(&buf, end, "test.gsc");
        CHECK(sc != NULL && ((uintptr_t)buf & 15) == 0 && buf > file + b.len);
        CHECK(memcmp(file, b.buf, 16) == 0);
        if (sc) {
            check_scene(sc, file, &s);
            CHECK(count_mtls() == mtls + sc->nummtl);
            nw_gscn_remove(sc);
        }
        CHECK(count_mtls() == mtls);
    }
    NwGpuJob *jobs = NULL;
    uint32_t cap = 0, uploads = 0, n = nw_gpu_take(UINT64_MAX, &jobs, &cap);
    for (uint32_t i = 0; i < n; i++) {
        uploads += jobs[i].op < NW_GPU_FREE_MESH;
    }
    CHECK(n > 0 && uploads * 2 == n);
    SDL_free(jobs);

    nw_shim_file_init(NULL);
    nw_vfs_shutdown(&vfs);
    SDL_RemovePath(root);
    gsc_free(&b);
    SDL_free(disc);
    SDL_free(root);
}

static NwVfs retail_vfs;
static AnimStats retail = ANIM_STATS_INIT;
static unsigned retail_scenes, retail_alib;

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

/* On the game thread: a load error is fatal there. The anims are the ones the instances play (NuGScnUpdate); the
   bit counts they need come from the .cut and .ani loaders in the game. INTRO/FINAL and TITLES/TITLESPL are
   truncated, never drawn by the game, and skipped as by --scene-check. */
static int retail_main(int argc, char **argv)
{
    (void)argc, (void)argv;
    size_t size = 0x2400000; /* the scene viewer's buffer */
    char *buffer = SDL_malloc(size);
    nw_shim_file_init(&retail_vfs);
    NuFileInitEx(1, 1);
    NuTexInit();
    NuMtlInit();
    buildBitCountTable();
    for (size_t i = 0; buffer && i < nw_disc_manifest.count; i++) {
        const NwManifestEntry *e = &nw_disc_manifest.entries[i];
        size_t n = strlen(e->path);
        if (n < 4 || SDL_strcasecmp(e->path + n - 4, ".GSC") != 0 || (e->flags & NW_MANIFEST_SKIP) ||
            SDL_strcasecmp(e->path, "LEVELS/B/INTRO/FINAL.GSC") == 0 ||
            SDL_strcasecmp(e->path, "LEVELS/B/TITLES/TITLESPL.GSC") == 0) {
            continue;
        }
        char path[NW_VFS_PATH_MAX];
        SDL_strlcpy(path, e->path, sizeof(path));
        for (char *c = path; *c; c++) {
            *c = *c == '/' ? '\\' : *c;
        }
        char *buf = buffer;
        NuRndrInitWorld();
        struct nugscn_s *sc = NuGScnRead(&buf, buffer + size, path);
        if (!sc) {
            fprintf(stderr, "retail: %s does not load\n", path);
            retail.bad++;
            continue;
        }
        bool seen[256] = { 0 };
        AnimStats st = ANIM_STATS_INIT;
        for (int j = 0; sc->instanimdata && j < sc->numinstance; j++) {
            struct nuinstanim_s *anim = sc->instances[j].anim;
            if (anim && !seen[anim->anim_ix] && sc->instanimdata[anim->anim_ix]) {
                seen[anim->anim_ix] = true;
                anim_check(&st, path, buffer, buf, sc->instanimdata[anim->anim_ix]);
            }
        }
        if (sc->instanimdata) {
            printf("ALIB %s anims=%u sets=%u curves=%u keys=%u\n", path, st.anims, st.sets, st.curves, st.keys);
        }
        retail_scenes++;
        retail_alib += sc->instanimdata != NULL;
        anim_add(&retail, &st);
        nw_gscn_remove(sc);
        drain_gpu();
    }
    SDL_free(buffer);
    return 0;
}

/* Every .gsc loads, and the anims its instances play count to the pinned numbers, have the nine curves
   NuAnimCurveSetApplyToMatrix reads in every set and read keys inside the scene at every frame. */
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
    printf("TOTAL scenes=%u alib=%u\n", retail_scenes, retail_alib);
    anim_print("TOTAL", &retail);
    CHECK(state == NW_GAME_RETURNED && retail.bad == 0 && retail_scenes == 64 && retail_alib == 26);
    CHECK(retail.anims == 306 && retail.sets == 1371 && retail.curves == 5837 && retail.keys == 52883);
    CHECK(retail.minsets == 9);
    CHECK(retail.before == 0 && retail.past == 0 && retail.nonfinite == 0);
    return failures ? 1 : 0;
}

int main(int argc, char *argv[])
{
    if (argc > 1 && strcmp(argv[1], "retail") == 0) {
        return test_retail();
    }
    if (argc != 2) {
        fprintf(stderr, "usage: test_nugscn <scratch dir> | retail\n");
        return 2;
    }
    char *scratch = NULL;
    SDL_asprintf(&scratch, "%s/", argv[1]);
    SDL_CreateDirectory(scratch);
    test_attrib();
    test_load(scratch);
    SDL_free(scratch);
    printf("%s\n", failures ? "FAILED" : "nugscn: all checks passed");
    return failures ? 1 : 0;
}
