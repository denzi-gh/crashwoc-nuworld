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
#include "nu3d/nucamera.h"
#include "nu3d/nugobj.h"
#include "nu3d/nugscn.h"
#include "nu3d/numtl.h"
#include "nu3d/nurndr.h"
#include "nu3d/nustream.h"
#include "nu3d/nuvport.h"
#include "nu3d/nutex.h"
#include "nu3d/nutexanm.h"
#include "nucore/nufile.h"
#include "numath/numtx.h"
#include "numath/nutrig.h"
#include "nw_ps2types.h"
#include "port/game.h"
#include "port/nugscn.h"
#include "port/nutexanm.h"
#include "shim/shim.h"
#include "vfs/extract.h"
#include "vfs/manifest.h"
#include "vfs/vfs.h"

#include "anim_check.h"
#include "gsc_builder.h"

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
void NuCameraSet(struct nucamera_s *cam);
void NuRndrEndScene(void);
int NuRndrGScnObj(struct nugobj_s *gobj, struct numtx_s *wm);
extern int no_default_font, PS2_REZ_W, PS2_REZ_H, PS2_VCNTR_X, PS2_VCNTR_Y;

static int failures;
static char scene_buffer[1 << 20];

#define BESIDE(p) ((char *)(p) < scene_buffer || (char *)(p) >= scene_buffer + sizeof(scene_buffer))

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

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
    CHECK(geom->mtl == sc->mtls[0] && geom[1].mtl == sc->mtls[2] && geom->tristream == NULL);
    for (int i = 0; i < 2; i++) {
        const NwVifMesh *m = &((const NwGpuMesh *)geom[i].dmastream)->mesh;
        CHECK(m && m->nbatches == s->batches[i] && m->nverts == s->verts[i] && m->ntris == s->tris[i]);
    }
    g = sc->gobjs[1];
    CHECK(g->ngobjs == 2 && g[0].type == 0 && g[1].type == 1);
    const NwGpuMesh *ot = g[0].geom->dmastream;
    CHECK(g[0].geom->tristream == (void *)(file + s->blob[2]) && ot && ot->mesh.ntris == 1 && ot->mesh.nverts == 3);
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
    CHECK(BESIDE(sc) && BESIDE(sc->gobjs) && BESIDE(sc->gobjs[0]) && BESIDE(sc->instances) && BESIDE(tex->env));
}

static char *carve(char *c, size_t align, size_t size)
{
    return (char *)(((uintptr_t)c + align - 1) & ~(uintptr_t)(align - 1)) + size;
}

/* Where retail's NuGScnLoadFix leaves the buffer: its records at their PS2 sizes, in the builder's block order. */
static char *retail_end(const struct nugscn_s *sc, char *c)
{
    c = carve(c, 4, sizeof(nw_ps2_nugscn_s));
    c = carve(c, 4, (size_t)sc->numtid * 4);
    c = carve(c, 4, (size_t)sc->nummtl * 4);
    c = carve(c, 4, (size_t)sc->numgobj * 4);
    for (int i = 0; i < sc->numgobj; i++) {
        struct nugobj_s *gobj = sc->gobjs[i];
        c = carve(c, 4, (size_t)gobj->ngobjs * sizeof(nw_ps2_nugobj_s));
        for (int j = 0; j < gobj->ngobjs; j++) {
            size_t n = 0;
            if (gobj[j].type == 0) {
                for (struct nugeom_s *g = gobj[j].geom; g; g = g->next) {
                    n += sizeof(nw_ps2_nugeom_s);
                }
            } else {
                for (struct nufaceongeom_s *g = gobj[j].faceon_geom; g; g = g->next) {
                    n += sizeof(nw_ps2_nufaceongeom_s);
                }
            }
            c = carve(c, 4, n);
        }
    }
    c = carve(c, 4, (size_t)sc->numsplines * sizeof(nw_ps2_nugspline_s));
    for (int i = 0; i < sc->numsplines; i++) {
        c = carve(c, 4, (size_t)sc->splines[i].ptsize * (size_t)sc->splines[i].len);
    }
    for (int i = 0; i < sc->numtexanims; i++) {
        c = carve(c, 16, sizeof(nw_ps2_nutexanimenv_s));
    }
    return carve(c, 16, 0);
}

static int count_mtls(void)
{
    int n = 0;
    for (struct numtl_s *m = NuMtlIterate(1); m; m = m->snext) {
        n++;
    }
    return n;
}

static uint32_t drain_gpu(uint32_t ops[NW_GPU_FREE_HOST + 1])
{
    NwGpuJob *jobs = NULL;
    uint32_t cap = 0, n = nw_gpu_take(UINT64_MAX, &jobs, &cap);
    for (uint32_t i = 0; i < n; i++) {
        if (ops) {
            ops[jobs[i].op]++;
        }
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
    return n;
}

/* Add a real texture and replace the original empty TAS0, without changing the legacy fixture checks. */
static void textured_animation(GscBuild *b, const uint8_t colour[4])
{
    uint8_t anim[32] = {0};
    for (size_t at = 16; at < b->len;) {
        uint32_t size;
        memcpy(&size, b->buf + at + 4, 4);
        if (!memcmp(b->buf + at, "TST0", 4)) memcpy(b->buf + at, "XXXX", 4);
        if (!memcmp(b->buf + at, "TAS0", 4)) {
            memcpy(anim, b->buf + at + 16, sizeof(anim));
            memcpy(b->buf + at, "XXXX", 4);
        }
        at += size;
    }
    gsc_open(b, "TST0");
    gsc_u32(b, 1);
    gsc_u32(b, 0);
    size_t size_at = b->len;
    gsc_u32(b, 0);
    size_t padding = 16 - (b->len + 4) % 16;
    gsc_u32(b, (uint32_t)padding);
    gsc_zero(b, padding);
    size_t data = b->len;
    uint8_t palette[16][4] = {{0}}; const uint8_t pixels[32] = {0};
    if (colour) memcpy(palette[0],colour,4);
    gsc_texture(b, 4, 8, 8, palette, pixels, 8, 8, sizeof(pixels), 2, 2, 1);
    gsc_put32(b, size_at, (uint32_t)(b->len - data));
    gsc_close(b);
    gsc_open(b, "TAS0");
    gsc_u32(b, 1);
    gsc_u32(b, 0);
    const uint16_t one = 1;
    memcpy(anim + 12, &one, sizeof(one));
    gsc_bytes(b, anim, sizeof(anim));
    gsc_u32(b, 4);
    gsc_u32(b, 0);
    gsc_close(b);
    gsc_put32(b, 4, (uint32_t)-(int32_t)b->len);
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
    static const char ats[] = "ats 2 script\ntime seconds\nsequence x {\nframes 0..0\nstep 1\n}\n";
    SDL_asprintf(&path, "%sV2.ATS", disc);
    CHECK(SDL_SaveFile(path, ats, sizeof(ats) - 1));
    SDL_free(path);
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
        CHECK(sc != NULL && buf == retail_end(sc, file + b.len));
        CHECK(memcmp(file, b.buf, 16) == 0);
        if (sc) {
            check_scene(sc, file, &s);
            CHECK(count_mtls() == mtls + sc->nummtl);
            nw_gscn_remove(sc);
        }
        CHECK(count_mtls() == mtls);
    }
    uint32_t ops[NW_GPU_FREE_HOST + 1] = { 0 }, n = drain_gpu(ops), uploads = 0;
    for (int op = 0; op < NW_GPU_FREE_MESH; op++) {
        uploads += ops[op];
    }
    CHECK(n > 0 && uploads * 2 == n);
    /* Five times with no remove, as the game drops a scene by reusing its buffer (ResetSuperBuffer) after a level
       init: each load frees the one before. */
    textured_animation(&b,NULL);
    SDL_asprintf(&path, "%sTEST.GSC", disc);
    CHECK(SDL_SaveFile(path, b.buf, b.len));
    SDL_free(path);
    NuTexAnimProgSysInit();
    struct nutexanimprog_s *p = NuTexAnimProgReadScript(NULL, "v2.ats");
    struct nugscn_s *live = NULL;
    for (int pass = 0; pass < 5; pass++) {
        char *buf = scene_buffer + 3;
        NuTexInit();
        NuMtlInit();
        NuRndrInitWorld();
        live = NuGScnRead(&buf, end, "test.gsc");
        CHECK(p && live && live->texanims->env->prog == p && nw_texanim_time(live->texanims, 0));
        NuSceneReAssignTexAnims(live->texanims, live->numtexanims);
        CHECK(live->texanims->env->prog == p);
    }
    SDL_memset(ops, 0, sizeof(ops));
    drain_gpu(ops);
    uint32_t made = ops[NW_GPU_UPLOAD_MESH] + ops[NW_GPU_UPLOAD_SPRITES];
    uint32_t freed = ops[NW_GPU_FREE_MESH] + ops[NW_GPU_FREE_SPRITES];
    CHECK(made > 0 && made % 5 == 0 && freed == made / 5 * 4);
    NuTexAnimProgRelease();
    CHECK(!NuTexAnimProgFind("script") && nw_texanim_time(live->texanims, INT64_MAX));
    nw_gscn_remove(live); /* The fifth scene drops the final reference after four heap overwrites. */
    drain_gpu(ops);

    nw_shim_file_init(NULL);
    nw_vfs_shutdown(&vfs);
    SDL_RemovePath(root);
    gsc_free(&b);
    SDL_free(disc);
    SDL_free(root);
}

/* Same immutable ATS across two independently loaded scenes; no presentation coordinator. */
static void test_ats_reuse(const char *scratch)
{
    char *root=NULL, *disc=NULL, *path=NULL; NwVfs vfs;
    const char *names[]={"A.GSC","B.GSC"};
    const uint8_t colours[2][4]={{255,0,0,128},{0,255,0,128}};
    SDL_asprintf(&root,"%sats-scenes/",scratch); SDL_asprintf(&disc,"%s" NW_DISC_DIR "/",root);
    CHECK(SDL_CreateDirectory(disc)); SDL_asprintf(&path,"%s" NW_DISC_STAMP,disc);
    CHECK(SDL_SaveFile(path,"",0)); SDL_free(path);
    for (int n=0; n<2; n++) {
        GscBuild b; GscScene offsets; gsc_build_scene(&b,&offsets); textured_animation(&b,colours[n]);
        gsc_put32(&b,offsets.geom0_mtl,2); /* Both loaded geoms draw the animated material. */
        SDL_asprintf(&path,"%s%s",disc,names[n]); CHECK(SDL_SaveFile(path,b.buf,b.len)); SDL_free(path); gsc_free(&b);
    }
    CHECK(nw_vfs_init(&vfs,root)); nw_shim_file_init(&vfs); NuFileInitEx(1,1);
    PS2_REZ_W=640; PS2_REZ_H=512; PS2_VCNTR_X=PS2_VCNTR_Y=0x800; no_default_font=1;
    NuTexInit(); NuMtlInit(); NuTrigInit(); NuVpInit(); NuRndrInitEx(0x10000);
    struct nucamera_s *cam=NuCameraCreate(); NuMtxSetIdentity(&cam->mtx); cam->mtx._32=-20; NuCameraSet(cam);
    struct numtx_s world; NuMtxSetIdentity(&world);
    const char text[]="ats 2 fixture_scroll\ntime frames 60\nsequence image {\nframes 0..0\nstep 1\n}\nuv drift {\nplayback loop\nduration 60\n"
        "offset linear {\nkey 0 0 0\nkey 60 1 0\n}\n}\n";
    struct nutexanimprog_s *program=nw_texanim_from_memory("fixture.ats",text,sizeof(text)-1);
    CHECK(program && nw_texanim_retain(program)); if (!program) return;
    int baseline=count_mtls(); uint32_t ops[NW_GPU_FREE_HOST+1]={0}, previous_id=0; drain_gpu(NULL);
    unsigned draws=0;
    for (int pass=0; pass<4; pass++) {
        int which=pass%2; char *buffer=scene_buffer+3; NuRndrInitWorld();
        struct nugscn_s *scene=NuGScnRead(&buffer,scene_buffer+sizeof(scene_buffer),(char *)names[which]);
        CHECK(scene && count_mtls()==baseline+3); if (!scene) break;
        struct nutexanim_s *source=scene->texanims;
        int base_tid=source->mtl->tid, texture_tid=source->tids[0]; CHECK(source->env->prog==NULL && source->numtids==1);
        const NwGpuTex *base_texture=nw_rndr_texture(base_tid);
        const NwGpuTex *texture=nw_rndr_texture(source->tids[0]); CHECK(texture);
        if (texture) CHECK(!memcmp(texture->tex.clut[0],colours[which],3));
        NwTexAnimInstance *player=nw_texanim_instance_track(program,"drift",source,
            (NwTexAnimConfig){.clock=17,.origin=which*30},NULL);
        NwTexAnimInstance *image=nw_texanim_instance_track(program,"image",source,
            (NwTexAnimConfig){.clock=17,.origin=which*30},NULL), *players[]={image,player};
        CHECK(player && image && nw_texanim_instance_clock_integer(player,30,60) && nw_texanim_instance_clock_integer(image,30,60));
        uint32_t id=nw_texanim_binding(); CHECK(id && id!=previous_id); previous_id=id;
        if (pass==1) { NuTexAnimProgDestroy(program); CHECK(!NuTexAnimProgFind("fixture_scroll")); }
        for (int stopped=0; stopped<2; stopped++) {
            if (stopped) { nw_texanim_instance_stop(player); nw_texanim_instance_stop(image); }
            NwMtlBinding binding={.material=source->mtl,.tid=source->mtl->tid};
            CHECK(nw_texanim_compose(players,2,id,&binding) && nw_rndr_mtl_frame_bindings(&binding,1));
            CHECK(NuRndrGScnObj(scene->gobjs[0],&world)); NuRndrEndScene(); NuRndrVu1Draw();
            const NwPacket *packet=nw_packet_take(); unsigned seen=0, meshes=0;
            for (uint32_t k=0; k<packet->ncmds; k++) {
                const NwCmd *cmd=&packet->cmds[k]; CHECK(cmd->op==NW_CMD_DRAW && cmd->material==source->mtl);
                if (cmd->mtl>=packet->nmtls) { CHECK(false); continue; }
                const NwMtlSnap *snap=&packet->mtls[cmd->mtl];
                CHECK(snap->key==source->mtl && snap->tex==(stopped ? base_texture : texture));
                for (int g=0; g<2; g++) if (cmd->mesh==scene->gobjs[0]->geom[g].dmastream) meshes|=1u<<g;
                CHECK(snap->uv.binding==(stopped ? 0 : id) && snap->uv.value[0]==(stopped || which ? 0 : .5f)); seen++;
            }
            CHECK(seen==2 && meshes==3 && source->mtl->tid==base_tid); draws+=seen;
            NuRndrSwapStreamBuffers(); CHECK(!nw_rndr_record(NW_CMD_DRAW,source->mtl,NULL,0)->cmd.texture_override);
        }
        nw_texanim_instance_drop(player); nw_texanim_instance_drop(image); nw_gscn_remove(scene);
        CHECK(count_mtls()==baseline && !nw_rndr_texture(texture_tid)); drain_gpu(ops);
    }
    CHECK(draws==16 && ops[NW_GPU_UPLOAD_TEX]==4 && ops[NW_GPU_FREE_TEX]==4);
    CHECK(ops[NW_GPU_UPLOAD_MESH]==12 && ops[NW_GPU_FREE_MESH]==12 && ops[NW_GPU_UPLOAD_SPRITES]==4 && ops[NW_GPU_FREE_SPRITES]==4);
    nw_texanim_release(program); NuCameraDestroy(cam); nw_shim_file_init(NULL); nw_vfs_shutdown(&vfs);
    SDL_free(disc); SDL_free(root); printf("ATS reuse: A/B twice, one retired IR, %u production draws, resources balanced\n",draws);
}

static NwVfs retail_vfs;
static AnimStats retail = ANIM_STATS_INIT;
static unsigned retail_scenes, retail_alib;


/* On the game thread: a load error is fatal there; INTRO/FINAL and TITLES/TITLESPL are truncated, never drawn. */
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
        drain_gpu(NULL);
    }
    SDL_free(buffer);
    return 0;
}

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

static void test_destroy(void)
{
    static NwGpuMesh mesh;
    NwGpuJob *jobs = NULL;
    uint32_t cap = 0;
    nw_gpu_take(UINT64_MAX, &jobs, &cap);
    struct nugobj_s *g = NuGobjCreate();
    struct nugeom_s *geom = NuGeomCreate();
    geom->dmastream = &mesh;
    NuGobjAddGeom(g, geom);
    NuGobjDestroy(g);
    CHECK(nw_gpu_take(UINT64_MAX, &jobs, &cap) == 0);
    SDL_free(jobs);
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
    test_ats_reuse(scratch);
    test_destroy();
    SDL_free(scratch);
    printf("%s\n", failures ? "FAILED" : "nugscn: all checks passed");
    return failures ? 1 : 0;
}
