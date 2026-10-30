/* The .ghg loader override (src/port/nuhgobj.c) on a synthetic file; with `retail`, every .ghg of the disc. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "gfx/packet.h"
#include "gfx/vif.h"
#include "nu3d/nuanim.h"
#include "nu3d/nugobj.h"
#include "nu3d/nuhgobj.h"
#include "nu3d/numtl.h"
#include "nu3d/nurndr.h"
#include "nu3d/nutex.h"
#include "nu3d/nutexanm.h"
#include "nucore/nufile.h"
#include "numath/numtx.h"
#include "nw_ps2types.h"
#include "port/game.h"
#include "port/nuhgobj.h"
#include "shim/shim.h"
#include "vfs/dat.h"
#include "vfs/extract.h"
#include "vfs/manifest.h"
#include "vfs/vfs.h"

#include "ghg_builder.h"

extern char vu_load_tri_mpg[];

/* Declared by no header. */
void NuHGobjEval(struct NUHGOBJ_s *hobj, int njanims, struct NUJOINTANIM_s *janim, struct numtx_s *mtx_array);

static int failures;
static char ghg_buffer[4 << 20];

/* A host record: beside the buffer, never in it (src/port/heap.h). */
#define BESIDE(p) ((char *)(p) < ghg_buffer || (char *)(p) >= ghg_buffer + sizeof(ghg_buffer))
#define ALIGN16(p) ((char *)(((uintptr_t)(p) + 0xF) & ~(uintptr_t)0xF))

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

static int count_mtls(void)
{
    int n = 0;
    for (struct numtl_s *m = NuMtlIterate(1); m; m = m->snext) {
        n++;
    }
    return n;
}

static const NwVifMesh *mesh_of(const struct nugeom_s *geom)
{
    return geom->dmastream ? &((const NwGpuMesh *)geom->dmastream)->mesh : NULL;
}

static void check_hobj(struct NUHGOBJ_s *h, char *file, const GhgFile *f)
{
    CHECK(h->type == 1 && h->numtid == 1 && h->nummtl == 2 && h->num_joints == 3 && h->num_layers == 1);
    CHECK(h->tids[0] > 0 && h->mtls[0]->tid == h->tids[0] && h->mtls[1]->tid == 0);
    CHECK(h->mtls[0]->next == NULL && h->mtls[0]->rs.mpg == (unsigned char *)vu_load_tri_mpg);
    CHECK(h->scale == 1.25f && h->string_table == file + f->strings && h->string_table_size == 36);
    CHECK(h->T == (struct numtx_s *)(file + f->T) && h->INV_WT == (struct numtx_s *)(file + f->INV_WT));
    CHECK(h->num_joint_ixs == 3 && h->joint_ixs == (u8 *)file + f->joint_ixs && h->joint_ixs[0] == 2);
    CHECK(strcmp(h->joints[1].name, "arm") == 0 && h->joints[2].name == file + f->strings + f->name[2]);
    CHECK(h->joints[0].parent_ix == -1 && h->joints[2].parent_ix == 1 && h->joints[2].flags == 0x12);
    CHECK(h->joints[2].orient._30 == 2.0f);
    CHECK(h->num_poi_ixs == 1 && h->poi_ixs == (u8 *)file + f->poi_ixs && h->num_pois == 1);
    CHECK(strcmp(h->pois[0].name, "tip") == 0 && h->pois[0].parent_joint == 2 && h->pois[0].offset._33 == 1.0f);
    CHECK(h->sphere_radius == 2.0f && h->sphere_y == 0.5f && h->bbox_min.z == -3.0f && h->bbox_max.y == 2.0f);
    CHECK(h->cyl_y == 0.25f && h->cyl_height == 1.5f && h->cyl_radius == 0.75f && h->texanim_tids == NULL);

    struct NULAYERDATA_s *l = h->layers;
    CHECK(strcmp(l->name, "layer") == 0 && l->blend_gobjs == NULL && l->blend_skin_gobj == NULL);
    CHECK(l->gobjs[1] == NULL && l->gobjs[0] && l->gobjs[2] && l->skin_gobj);
    if (l->gobjs[0] && l->gobjs[2] && l->skin_gobj) {
        struct nugobj_s *g = l->gobjs[0], *g2 = g->next_gobj;
        CHECK(g->sysnext == NULL && g->syslast == NULL && g->ngobjs == 1 && g->max.z == 3.0f && g->faceon_geom == NULL);
        CHECK(g->blend == NULL && g->tricount == 0 && g->faceon_blend == NULL);
        struct nugeom_s *geom = g->geom;
        CHECK(geom && geom->mtl == h->mtls[0] && geom->next && geom->next->next == NULL && geom->skindma == NULL);
        CHECK(mesh_of(geom) && mesh_of(geom)->nverts == 4 && mesh_of(geom)->ntris == 2);
        CHECK(geom->next && mesh_of(geom->next) && mesh_of(geom->next)->nverts == 6);
        CHECK(geom->tristream == NULL && geom->mtl_id == 0 && geom->blendgeom == NULL);
        CHECK(g2 && g2->next_gobj == NULL && g2->geom->mtl == h->mtls[1] && mesh_of(g2->geom)->nverts == 5);
        CHECK(l->gobjs[2]->geom && mesh_of(l->gobjs[2]->geom)->ntris == 3);
        struct nugeom_s *sg = l->skin_gobj->geom;
        const NwVifSkin *s = sg && sg->skindma ? &((const NwGpuSkin *)sg->skindma)->skin : NULL;
        CHECK(s && sg->dmastream == NULL && s->nverts == 3 && s->ntris == 1 && s->maxjoint == 2);
        CHECK(s && s->verts[2].joint[1] == 2 && s->verts[2].weight[1] == 24576 && s->verts[0].nrmjoint == 2);
    }

    struct nuhcollide_s *c = h->collide;
    CHECK(c && c->nentries == 2 && c->entries[1].type == 2 && c->entries[1].data == file + f->collide_data[1]);
    struct nutexanim_s *a = h->texanims;
    CHECK(h->numtexanims == 1 && a->mtl == h->mtls[1] && a->tids == (short *)(file + f->texanim_tids));
    CHECK(a->numtids == 1 && a->tids[0] == h->tids[0] && strcmp(a->ntaname, "anim") == 0 && !a->succ && !a->prev);
    CHECK(strcmp(a->scriptname, "script") == 0 && a->env && a->env->mtl == h->mtls[1] && a->env->prog == NULL);
    CHECK(BESIDE(a->env));
    struct NUSHADOWDATA_s *sh = h->shadow;
    CHECK(sh && (char *)sh[0].ellipsoids == file + f->ellipsoids && sh[0].cylinders == (void *)0x1234 && sh[0].joint == 2);
    CHECK(sh && sh[0].nellipsoids == 1 && sh[0].nshadow_meshes == 1 && sh[1].joint == 0xFF);
    CHECK(sh && sh[0].shadow_meshes[0].normal_counts == (struct nushadowvtx_s *)(file + f->shadow_vtx[0]));
    CHECK(sh && sh[0].shadow_meshes[0].vert_counts == (struct nushadowvtx_s *)(file + f->shadow_vtx[1]));
}

static void test_load(const char *scratch)
{
    char *root = NULL, *disc = NULL, *path = NULL;
    GscBuild b;
    GhgFile f;
    NwVfs vfs;

    SDL_asprintf(&root, "%snughg/", scratch);
    SDL_asprintf(&disc, "%s" NW_DISC_DIR "/", root);
    SDL_RemovePath(root);
    CHECK(SDL_CreateDirectory(disc));
    SDL_asprintf(&path, "%s" NW_DISC_STAMP, disc);
    CHECK(SDL_SaveFile(path, "", 0));
    SDL_free(path);
    ghg_build(&b, &f);
    SDL_asprintf(&path, "%sTEST.GHG", disc);
    CHECK(SDL_SaveFile(path, b.buf, b.len));
    SDL_free(path);

    CHECK(nw_vfs_init(&vfs, root));
    nw_shim_file_init(&vfs);
    NuFileInitEx(1, 1);
    NuTexInit();
    NuMtlInit();

    char *file = ALIGN16(ghg_buffer + 3);
    /* Retail's charge: the image, then the object and the texanim's env at their PS2 sizes. */
    char *end = ALIGN16(ALIGN16(ALIGN16(file + f.size) + sizeof(nw_ps2_NUHGOBJ_s)) + sizeof(nw_ps2_nutexanimenv_s));
    int mtls = count_mtls();
    for (int pass = 0; pass < 2; pass++) {
        union variptr_u buf;
        buf.charptr = ghg_buffer + 3;
        struct NUHGOBJ_s *h = NuGHGRead(&buf, "test.ghg");
        CHECK(h && BESIDE(h) && memcmp(file, b.buf, 16) == 0 && buf.charptr == end);
        if (h) {
            check_hobj(h, file, &f);
            CHECK(count_mtls() == mtls + 2 && nw_ghg_count() == 1);
            NuHGobjDestroy(h);
            CHECK(count_mtls() == mtls && nw_ghg_count() == 0 && h->num_layers == 0 && h->nummtl == 0);
        }
    }
    union variptr_u buf;
    buf.charptr = ghg_buffer;
    CHECK(NuGHGRead(&buf, "missing.ghg") == NULL && buf.charptr == ghg_buffer);

    NwGpuJob *jobs = NULL;
    uint32_t cap = 0, ops[NW_GPU_FREE_HOST + 1] = { 0 }, n = nw_gpu_take(UINT64_MAX, &jobs, &cap);
    for (uint32_t i = 0; i < n; i++) {
        ops[jobs[i].op]++;
        if (jobs[i].op == NW_GPU_FREE_HOST) {
            SDL_free(jobs[i].obj);
        }
    }
    CHECK(ops[NW_GPU_UPLOAD_MESH] == 8 && ops[NW_GPU_FREE_MESH] == 8);
    CHECK(ops[NW_GPU_UPLOAD_SKIN] == 2 && ops[NW_GPU_FREE_SKIN] == 2 && ops[NW_GPU_FREE_HOST] >= 2);
    /* NuTexDestroy keeps a native texture's slot data, as retail: the second load's texture replaces the first. */
    CHECK(ops[NW_GPU_UPLOAD_TEX] == 2 && ops[NW_GPU_FREE_TEX] == 1);
    SDL_free(jobs);

    nw_shim_file_init(NULL);
    nw_vfs_shutdown(&vfs);
    SDL_RemovePath(root);
    gsc_free(&b);
    SDL_free(disc);
    SDL_free(root);
}

typedef struct RetailStats {
    unsigned files, meshes, skins, batches, verts, dupes, tris, deltas, bad;
    unsigned unnormal; /* points whose weights do not sum to 1, which the VU leaves as they are */
    double bind;       /* max |INV_WT x default pose - I| over the joints the skins use */
    bool used[256];
} RetailStats;

/* The title's TTCHAR and the Crash models are skinned in their default pose. */
static bool retail_strict(const char *path)
{
    return SDL_strcasestr(path, "TTCHAR.GHG") || SDL_strcasestr(path, "CRASH.GHG");
}

static void retail_skin(RetailStats *st, const char *path, const struct NUHGOBJ_s *h, struct nugobj_s *gobj)
{
    for (; gobj; gobj = gobj->next_gobj) {
        for (struct nugeom_s *geom = gobj->geom; geom; geom = geom->next) {
            st->meshes += geom->dmastream != NULL;
            if (!geom->skindma) {
                continue;
            }
            const NwVifSkin *s = &((const NwGpuSkin *)geom->skindma)->skin;
            st->skins++;
            st->batches += s->nbatches;
            st->verts += s->nverts;
            st->dupes += s->ndupes;
            st->tris += s->ntris;
            st->deltas += s->ndeltas;
            for (uint32_t i = 0; i < s->nverts; i++) {
                const NwVifSkinVertex *v = &s->verts[i];
                uint32_t sum = 0, best = 0;
                for (int k = 0; k < 8; k++) {
                    sum += v->weight[k];
                    best = v->weight[k] > v->weight[best] ? (uint32_t)k : best;
                    st->used[v->joint[k]] |= v->weight[k] != 0;
                }
                st->used[v->nrmjoint] = true;
                st->unnormal += fabs(sum / 32768.0 - 1.0) > 0.01;
                /* The converter picked the heaviest before rounding: a near tie may go either way. */
                bool nrm = false;
                for (int k = 0; k < 8; k++) {
                    nrm = nrm || (v->joint[k] == v->nrmjoint && v->weight[k] + 2 >= v->weight[best]);
                }
                bool inside = true;
                for (int c = 0; c < 3 && SDL_strcasestr(path, "TTCHAR.GHG"); c++) {
                    inside = inside && v->pos[c] >= (&h->bbox_min.x)[c] - 0.01f &&
                             v->pos[c] <= (&h->bbox_max.x)[c] + 0.01f;
                }
                if ((!nrm || !inside) && st->bad++ < 5) {
                    fprintf(stderr, "retail: %s vertex %u: normal joint %u, inside the box %d\n", path, i,
                            v->nrmjoint, inside);
                }
            }
        }
    }
}

static void retail_file(RetailStats *st, const char *path)
{
    static struct numtx_s pose[256];
    union variptr_u buf;
    buf.charptr = ghg_buffer;
    int mtls = count_mtls();
    struct NUHGOBJ_s *h = NuGHGRead(&buf, (char *)path);
    if (!h) {
        fprintf(stderr, "retail: %s does not load\n", path);
        st->bad++;
        return;
    }
    memset(st->used, 0, sizeof(st->used));
    unsigned verts = st->verts, dupes = st->dupes, batches = st->batches, deltas = st->deltas;
    for (int i = 0; i < h->num_layers; i++) {
        const struct NULAYERDATA_s *l = &h->layers[i];
        for (int j = 0; j < h->num_joints; j++) {
            retail_skin(st, path, h, l->gobjs ? l->gobjs[j] : NULL);
            retail_skin(st, path, h, l->blend_gobjs ? l->blend_gobjs[j] : NULL);
        }
        retail_skin(st, path, h, l->skin_gobj);
        retail_skin(st, path, h, l->blend_skin_gobj);
    }
    NuHGobjEval(h, 0, NULL, pose);
    double bind = 0.0;
    for (int j = 0; j < h->num_joints; j++) {
        struct numtx_s m;
        NuMtxMul(&m, &h->INV_WT[j], &pose[j]);
        const float *e = &m._00;
        for (int k = 0; k < 16 && st->used[j]; k++) {
            double d = fabs(e[k] - (k % 5 == 0 ? 1.0 : 0.0));
            bind = d > bind ? d : bind;
        }
    }
    st->bind = bind > st->bind ? bind : st->bind;
    if (retail_strict(path) && bind > 1e-4) {
        fprintf(stderr, "retail: %s: INV_WT x default pose off by %g\n", path, bind);
        st->bad++;
    }
    printf("GHG %s joints=%u layers=%u batches=%u verts=%u dupes=%u deltas=%u bind=%.2g\n", path, h->num_joints,
           h->num_layers, st->batches - batches, st->verts - verts, st->dupes - dupes, st->deltas - deltas, bind);
    /* The counts a second decoder of nucvtskn.c's format found. */
    static const struct {
        const char *file;
        unsigned batches, verts, dupes, deltas;
    } census[] = { { "LOGOS\\TTCHAR.GHG", 255, 4268, 1280, 0 }, { "INTRO2\\CSCRASH.GHG", 193, 3394, 882, 26480 } };
    for (size_t i = 0; i < SDL_arraysize(census); i++) {
        if (SDL_strcasestr(path, census[i].file) &&
            (st->batches - batches != census[i].batches || st->verts - verts != census[i].verts ||
             st->dupes - dupes != census[i].dupes || st->deltas - deltas != census[i].deltas)) {
            fprintf(stderr, "retail: %s decodes to other counts\n", path);
            st->bad++;
        }
    }
    NuHGobjDestroy(h);
    if (count_mtls() != mtls || nw_ghg_count() != 0) {
        fprintf(stderr, "retail: %s leaves materials or meshes\n", path);
        st->bad++;
    }
    st->files++;
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

static RetailStats retail;
static NwVfs retail_vfs;

static void retail_entry(void *user, int index, const char *path)
{
    (void)user;
    (void)index;
    size_t n = strlen(path);
    if (n > 4 && SDL_strcasecmp(path + n - 4, ".ghg") == 0) {
        retail_file(&retail, path);
    }
}

/* On the game thread: a load error is fatal there. */
static int retail_main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    nw_shim_file_init(&retail_vfs);
    NuFileInitEx(1, 1);
    NuTexInit();
    NuMtlInit();
    for (size_t i = 0; i < nw_disc_manifest.count; i++) {
        const char *p = nw_disc_manifest.entries[i].path;
        size_t n = strlen(p);
        if (n > 4 && SDL_strcasecmp(p + n - 4, ".GHG") == 0) {
            char path[NW_VFS_PATH_MAX];
            SDL_strlcpy(path, p, sizeof(path));
            for (char *c = path; *c; c++) {
                *c = *c == '/' ? '\\' : *c;
            }
            retail_file(&retail, path);
        }
    }
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

/* Every .ghg loads and decodes, its normals turn with the heaviest joint and it frees all it made; TTCHAR and the
   Crash models are skinned in their default pose (INV_WT is its inverse), TTCHAR's points lie in its box. */
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
    printf("TOTAL files=%u meshes=%u skins=%u batches=%u verts=%u dupes=%u tris=%u deltas=%u unnormal=%u "
           "bind=%.2g bad=%u\n", retail.files, retail.meshes, retail.skins, retail.batches, retail.verts, retail.dupes,
           retail.tris, retail.deltas, retail.unnormal, retail.bind, retail.bad);
    CHECK(state == NW_GAME_RETURNED && retail.files == 238 && retail.bad == 0);
    return failures ? 1 : 0;
}

int main(int argc, char *argv[])
{
    if (argc > 1 && strcmp(argv[1], "retail") == 0) {
        return test_retail();
    }
    if (argc != 2) {
        fprintf(stderr, "usage: test_nughg <scratch dir> | retail\n");
        return 2;
    }
    char *scratch = NULL;
    SDL_asprintf(&scratch, "%s/", argv[1]);
    SDL_CreateDirectory(scratch);
    test_load(scratch);
    SDL_free(scratch);
    printf("%s\n", failures ? "FAILED" : "nughg: all checks passed");
    return failures ? 1 : 0;
}
