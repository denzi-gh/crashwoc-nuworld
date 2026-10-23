/* The .gsc loader override (src/port/nugscn.c) on the synthetic scene. Usage: test_nugscn <scratch dir>. */
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
#include "port/nugscn.h"
#include "shim/shim.h"
#include "vfs/extract.h"
#include "vfs/vfs.h"

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
    CHECK(f->dmastream == (void *)(file + s->blob[3]) && f->next == NULL);

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

    char *buf = scene_buffer + 3, *end = scene_buffer + sizeof(scene_buffer);
    char *file = (char *)(((uintptr_t)buf + 0x7F) & ~(uintptr_t)0x7F);
    struct nugscn_s *sc = NuGScnRead(&buf, end, "test.gsc");
    CHECK(sc != NULL && ((uintptr_t)buf & 15) == 0 && buf > file + b.len);
    CHECK(memcmp(file, b.buf, 16) == 0);
    if (sc) {
        check_scene(sc, file, &s);
        NuTexAnimRemoveList(sc->texanims);
    }
    nw_gscn_free_meshes();

    nw_shim_file_init(NULL);
    nw_vfs_shutdown(&vfs);
    SDL_RemovePath(root);
    gsc_free(&b);
    SDL_free(disc);
    SDL_free(root);
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "usage: test_nugscn <scratch dir>\n");
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
