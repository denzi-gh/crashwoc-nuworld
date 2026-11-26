/* .ghg loader override: host copies live until NuHGobjDestroy; carved records keep their PS2 size in the buffer. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "gfx/packet.h"
#include "gfx/vif.h"
#include "nw_convert.h"
#include "port/heap.h"
#include "port/nuhgobj.h"
#include "port/nutexanm.h"
#include "port/opening.h"
#include "port/xfile.h"

#define nuhgobj_srcfile "..\\nu2crash.ps2\\nu3d\\nuhgobj.c"
#define NW_GHG_VERSION 0 /* nughg_version */
#define NW_GHG_GOBJ 0x5C /* a file gobj: nugobj_s up to origin */
#define NW_GHG_BLOCK 0x10000

/* NuGHGWrite's header: every pointer an offset from the image. */
struct nughgheader_s {
    int32_t size, version;
    int32_t numtid, tids, nummtl, mtls;
    int32_t num_joints, joints, T, INV_WT, num_joint_ixs, joint_ixs;
    int32_t string_table, string_table_size, num_poi_ixs, poi_ixs, num_pois, pois, num_layers, layers, shadow;
    float sphere_radius, sphere_y;
    struct nuvec_s bbox_min, bbox_max;
    float cyl_y, cyl_height, cyl_radius;
    int32_t collide;
    float scale;
    int32_t numtexanims, texanims;
};
_Static_assert(sizeof(struct nughgheader_s) == 0x90, "nughgheader_s");

struct nughglayer_s {
    int32_t name, gobjs, skin_object, blend_gobjs, blend_skin_object;
};

/* Declared by no header. */
int NuFileLoadBuffer(char *filename, void *mem, int buffsize);
int NuFileLoadBuffer(char *filename, void *mem, int buffsize);
void NuDisableVBlank(void);
void NuEnableVBlank(void);
extern NuMtlReadEventFunc NuMtlReadEvent;

typedef struct NwGhgBlock {
    struct NwGhgBlock *next;
    size_t used, size;
} NwGhgBlock;

#define NW_GHG_HEAD ((sizeof(NwGhgBlock) + 15) & ~(size_t)15)

typedef struct NwGhg {
    struct NUHGOBJ_s *hobj;
    NwGhgBlock *blocks;
    struct {
        void *obj;
        NwGpuOp free;
    } *gpu;
    uint32_t ngpu, gpucap;
} NwGhg;

static NwGhg **nw_ghgs;
static uint32_t nw_nghgs, nw_ghgcap;

/* One load: NuEnableVBlank switches threads, so another load may run in between. */
typedef struct NwGhgLoad {
    char *base;
    size_t size;
    const char *file;
    struct NUHGOBJ_s *hobj;
    NwGhg *g;
    NwHeapSide *side;
} NwGhgLoad;

uint32_t nw_ghg_count(void)
{
    return nw_nghgs;
}

static void *nw_ghg_new(NwGhgLoad *ld, size_t size, size_t align)
{
    NwGhgBlock *b = ld->g->blocks;
    size_t at = b ? (b->used + align - 1) & ~(align - 1) : 0;
    if (!b || size > b->size || at > b->size - size) {
        size_t cap = size > NW_GHG_BLOCK ? size : NW_GHG_BLOCK;
        b = SDL_calloc(1, NW_GHG_HEAD + cap);
        if (!b) {
            NuErrorProlog(nuhgobj_srcfile, 0x622)("out of memory for \"%s\"", ld->file);
        }
        b->size = cap;
        b->next = ld->g->blocks;
        ld->g->blocks = b;
        at = 0;
    }
    b->used = at + size;
    return (char *)b + NW_GHG_HEAD + at;
}

/* A negative or larger count than the file has bytes is a damaged file, which would wrap the size. */
static size_t nw_ghg_count_ok(NwGhgLoad *ld, long long n, size_t each)
{
    if (n < 0 || (unsigned long long)n > ld->size) {
        NuErrorProlog(nuhgobj_srcfile, 0x622)("\"%s\": a count of %lld in a %llu byte file", ld->file, n,
                                              (unsigned long long)ld->size);
    }
    return (size_t)n * each;
}
#define NW_GHG_NEW(ld, type, n) ((type *)nw_ghg_new((ld), nw_ghg_count_ok((ld), (n), sizeof(type)), _Alignof(type)))

static char *nw_ghg_at(const NwGhgLoad *ld, int32_t off, size_t size)
{
    if (off < 0 || (size_t)off > ld->size || size > ld->size - (size_t)off) {
        NuErrorProlog(nuhgobj_srcfile, 0x622)("\"%s\": 0x%zx bytes at 0x%x outside the file", ld->file, size, off);
    }
    return ld->base + off;
}

static int32_t nw_ghg_int(const NwGhgLoad *ld, int32_t off)
{
    int32_t v;
    memcpy(&v, nw_ghg_at(ld, off, 4), 4);
    return v;
}

static void nw_ghg_gpu(NwGhgLoad *ld, void *obj, bool ok, NwGpuOp upload, NwGpuOp free)
{
    NwGhg *g = ld->g;
    if (!ok) {
        NuErrorProlog(nuhgobj_srcfile, 0x622)("\"%s\": %s", ld->file, SDL_GetError());
    }
    if (g->ngpu == g->gpucap) {
        uint32_t n = g->gpucap ? g->gpucap * 2 : 64;
        void *grown = SDL_realloc(g->gpu, n * sizeof(*g->gpu));
        if (!grown) {
            NuErrorProlog(nuhgobj_srcfile, 0x622)("out of memory for the meshes of \"%s\"", ld->file);
        }
        g->gpu = grown;
        g->gpucap = n;
    }
    g->gpu[g->ngpu].obj = obj;
    g->gpu[g->ngpu++].free = free;
    nw_gpu_queue(upload, obj);
}

/* The DMA has no size, so the image's end bounds it. */
static struct nugeom_s *nw_ghg_geoms(NwGhgLoad *ld, int32_t off)
{
    struct nugeom_s *first = NULL, **link = &first;
    for (size_t n = 0; off; n++) {
        int32_t rec[4];
        memcpy(rec, nw_ghg_at(ld, off, sizeof(rec)), sizeof(rec));
        if (rec[1] < 0 || rec[1] >= ld->hobj->nummtl || n > ld->size / sizeof(rec)) {
            NuErrorProlog(nuhgobj_srcfile, 0x622)("\"%s\": geom at 0x%x of material %d", ld->file, off, rec[1]);
        }
        struct nugeom_s *geom = NW_GHG_NEW(ld, struct nugeom_s, 1);
        geom->mtl = ld->hobj->mtls[rec[1]];
        if (rec[2]) {
            NwGpuMesh *mesh = SDL_calloc(1, sizeof(*mesh));
            char *dma = nw_ghg_at(ld, rec[2], 0);
            bool ok = mesh && nw_vif_decode(&mesh->mesh, dma, ld->size - (size_t)rec[2]);
            nw_ghg_gpu(ld, mesh, ok, NW_GPU_UPLOAD_MESH, NW_GPU_FREE_MESH);
            geom->dmastream = mesh;
        }
        if (rec[3]) {
            NwGpuSkin *skin = SDL_calloc(1, sizeof(*skin));
            char *dma = nw_ghg_at(ld, rec[3], 0);
            bool ok = skin && nw_vif_skin(&skin->skin, dma, ld->size - (size_t)rec[3]);
            if (ok && skin->skin.maxjoint >= ld->hobj->num_joints) {
                ok = SDL_SetError("skin joint %u of %u", skin->skin.maxjoint, ld->hobj->num_joints);
            }
            nw_ghg_gpu(ld, skin, ok, NW_GPU_UPLOAD_SKIN, NW_GPU_FREE_SKIN);
            geom->skindma = (struct SkinDmaTag *)skin;
        }
        *link = geom;
        link = &geom->next;
        off = rec[0];
    }
    return first;
}

/* NuGHGFixupGobj's work (its int base cannot hold a host address); a file gobj is nugobj_s up to origin. */
static struct nugobj_s *nw_ghg_gobjs(NwGhgLoad *ld, int32_t off)
{
    struct nugobj_s *first = NULL, **link = &first;
    for (size_t n = 0; off; n++) {
        nw_ps2_nugobj_s rec;
        memset(&rec, 0, sizeof(rec));
        memcpy(&rec, nw_ghg_at(ld, off, NW_GHG_GOBJ), NW_GHG_GOBJ);
        if (n > ld->size / NW_GHG_GOBJ) {
            NuErrorProlog(nuhgobj_srcfile, 0x622)("\"%s\": gobj chain loops at 0x%x", ld->file, off);
        }
        struct nugobj_s *gobj = NW_GHG_NEW(ld, struct nugobj_s, 1);
        nw_x_nugobj_s(gobj, &rec, &nw_file_x);
        int32_t geom = NW_FILE_IX(gobj->geom), next = NW_FILE_IX(gobj->next_gobj);
        gobj->sysnext = gobj->syslast = NULL;
        gobj->next_gobj = NULL;
        gobj->geom = geom ? nw_ghg_geoms(ld, geom) : NULL;
        *link = gobj;
        link = &gobj->next_gobj;
        off = next;
    }
    return first;
}

static struct nugobj_s **nw_ghg_joint_gobjs(NwGhgLoad *ld, int32_t off)
{
    struct nugobj_s **gobjs = NW_GHG_NEW(ld, struct nugobj_s *, ld->hobj->num_joints);
    for (int j = 0; j < ld->hobj->num_joints; j++) {
        int32_t gobj = nw_ghg_int(ld, off + 4 * j);
        gobjs[j] = gobj ? nw_ghg_gobjs(ld, gobj) : NULL;
    }
    return gobjs;
}

static void nw_ghg_layers(NwGhgLoad *ld, const struct nughgheader_s *h)
{
    struct NUHGOBJ_s *hobj = ld->hobj;
    hobj->num_layers = (u8)h->num_layers;
    hobj->layers = NW_GHG_NEW(ld, struct NULAYERDATA_s, hobj->num_layers);
    for (int i = 0; i < hobj->num_layers; i++) {
        struct nughglayer_s f;
        struct NULAYERDATA_s *layer = &hobj->layers[i];
        memcpy(&f, nw_ghg_at(ld, h->layers + i * (int32_t)sizeof(f), sizeof(f)), sizeof(f));
        layer->name = f.name ? nw_ghg_at(ld, f.name, 1) : NULL;
        layer->gobjs = f.gobjs ? nw_ghg_joint_gobjs(ld, f.gobjs) : NULL;
        layer->skin_gobj = f.skin_object ? nw_ghg_gobjs(ld, f.skin_object) : NULL;
        layer->blend_gobjs = f.blend_gobjs ? nw_ghg_joint_gobjs(ld, f.blend_gobjs) : NULL;
        layer->blend_skin_gobj = f.blend_skin_object ? nw_ghg_gobjs(ld, f.blend_skin_object) : NULL;
    }
}

static void nw_ghg_materials(NwGhgLoad *ld, const struct nughgheader_s *h)
{
    struct NUHGOBJ_s *hobj = ld->hobj;
    if (h->tids) {
        hobj->numtid = h->numtid;
        hobj->tids = NW_GHG_NEW(ld, short, hobj->numtid);
        for (int i = 0; i < hobj->numtid; i++) {
            hobj->tids[i] = (short)NuTexCreateNative(nw_ghg_at(ld, nw_ghg_int(ld, h->tids + 4 * i), 16));
        }
    }
    if (h->mtls) {
        hobj->nummtl = h->nummtl;
        hobj->mtls = NW_GHG_NEW(ld, struct numtl_s *, hobj->nummtl);
        for (int i = 0; i < hobj->nummtl; i++) {
            struct numtl_s *mtl = NW_GHG_NEW(ld, struct numtl_s, 1);
            NW_FILE_X(numtl_s, mtl, nw_ghg_at(ld, nw_ghg_int(ld, h->mtls + 4 * i), sizeof(nw_ps2_numtl_s)));
            hobj->mtls[i] = mtl;
            if (mtl->tid) {
                if (mtl->tid < 0 || mtl->tid > hobj->numtid) {
                    NuErrorProlog(nuhgobj_srcfile, 0x622)("\"%s\": material %d of texture %d", ld->file, i, mtl->tid);
                }
                mtl->tid = hobj->tids[mtl->tid - 1];
            }
            NuDisableVBlank();
            NuMtlAdd(mtl);
            if (!NuMtlReadEvent(mtl)) {
                NuMtlUpdate(mtl);
            }
            NuEnableVBlank();
        }
    }
}

static void nw_ghg_joints(NwGhgLoad *ld, const struct nughgheader_s *h)
{
    struct NUHGOBJ_s *hobj = ld->hobj;
    hobj->scale = h->scale;
    hobj->string_table = nw_ghg_at(ld, h->string_table, (size_t)h->string_table_size);
    hobj->string_table_size = h->string_table_size;
    hobj->num_joints = (u8)h->num_joints;
    hobj->joints = NW_GHG_NEW(ld, struct NUJOINTDATA_s, hobj->num_joints);
    hobj->T = (struct numtx_s *)nw_ghg_at(ld, h->T, hobj->num_joints * sizeof(struct numtx_s));
    hobj->INV_WT = (struct numtx_s *)nw_ghg_at(ld, h->INV_WT, hobj->num_joints * sizeof(struct numtx_s));
    hobj->num_joint_ixs = (u8)h->num_joint_ixs;
    hobj->joint_ixs = (u8 *)nw_ghg_at(ld, h->joint_ixs, hobj->num_joint_ixs);
    for (int i = 0; i < hobj->num_joints; i++) {
        struct NUJOINTDATA_s *joint = &hobj->joints[i];
        NW_FILE_X(NUJOINTDATA_s, joint, nw_ghg_at(ld, h->joints + i * 0x60, sizeof(nw_ps2_NUJOINTDATA_s)));
        joint->name = joint->name ? nw_ghg_at(ld, NW_FILE_IX(joint->name), 1) : NULL;
    }
    if (h->num_poi_ixs) {
        hobj->num_poi_ixs = (u8)h->num_poi_ixs;
        hobj->poi_ixs = (u8 *)nw_ghg_at(ld, h->poi_ixs, hobj->num_poi_ixs);
    }
    if (h->num_pois) {
        hobj->num_pois = (u8)h->num_pois;
        hobj->pois = NW_GHG_NEW(ld, struct NUPOINTOFINTEREST_s, hobj->num_pois);
        for (int i = 0; i < hobj->num_pois; i++) {
            struct NUPOINTOFINTEREST_s *poi = &hobj->pois[i];
            NW_FILE_X(NUPOINTOFINTEREST_s, poi, nw_ghg_at(ld, h->pois + i * 0x50, sizeof(nw_ps2_NUPOINTOFINTEREST_s)));
            poi->name = poi->name ? nw_ghg_at(ld, NW_FILE_IX(poi->name), 1) : NULL;
        }
    }
}

/* The collision entries' data, the shadows' shapes and vertices hold no pointers: they stay in the image. */
static void nw_ghg_collide(NwGhgLoad *ld, const struct nughgheader_s *h)
{
    struct NUHGOBJ_s *hobj = ld->hobj;
    if (h->collide) {
        struct nuhcollide_s *collide = hobj->collide = NW_GHG_NEW(ld, struct nuhcollide_s, 1);
        NW_FILE_X(nuhcollide_s, collide, nw_ghg_at(ld, h->collide, sizeof(nw_ps2_nuhcollide_s)));
        int32_t entries = NW_FILE_IX(collide->entries);
        collide->entries = NW_GHG_NEW(ld, struct NUCOLLISIONHDR_s, collide->nentries);
        for (unsigned i = 0; i < collide->nentries; i++) {
            struct NUCOLLISIONHDR_s *e = &collide->entries[i];
            NW_FILE_X(NUCOLLISIONHDR_s, e, nw_ghg_at(ld, entries + (int32_t)i * 8, sizeof(nw_ps2_NUCOLLISIONHDR_s)));
            e->data = nw_ghg_at(ld, NW_FILE_IX(e->data), 0);
        }
    }
    if (h->shadow) {
        int32_t n = 0;
        while (*(u8 *)nw_ghg_at(ld, h->shadow + n * 0x10 + 0xF, 1) != 0xFF) {
            n++;
        }
        struct NUSHADOWDATA_s *shadow = hobj->shadow = NW_GHG_NEW(ld, struct NUSHADOWDATA_s, n + 1);
        for (int32_t i = 0; i <= n; i++, shadow++) {
            NW_FILE_X(NUSHADOWDATA_s, shadow, nw_ghg_at(ld, h->shadow + i * 0x10, sizeof(nw_ps2_NUSHADOWDATA_s)));
            if (i == n) {
                break;
            }
            if (shadow->nellipsoids) {
                shadow->ellipsoids = (struct NUELLIPSOID_s *)nw_ghg_at(ld, NW_FILE_IX(shadow->ellipsoids),
                                                                       shadow->nellipsoids * 0x30u);
            }
            if (shadow->ncylinders) {
                shadow->cylinders = (struct NUCYLINDER_s *)nw_ghg_at(ld, NW_FILE_IX(shadow->cylinders),
                                                                     shadow->ncylinders * 0x40u);
            }
            if (shadow->nshadow_meshes) {
                int32_t meshes = NW_FILE_IX(shadow->shadow_meshes);
                shadow->shadow_meshes = NW_GHG_NEW(ld, struct NUSHADOWMESH_s, shadow->nshadow_meshes);
                for (int m = 0; m < shadow->nshadow_meshes; m++) {
                    struct NUSHADOWMESH_s *mesh = &shadow->shadow_meshes[m];
                    NW_FILE_X(NUSHADOWMESH_s, mesh, nw_ghg_at(ld, meshes + m * 8, sizeof(nw_ps2_NUSHADOWMESH_s)));
                    mesh->vert_counts = (struct nushadowvtx_s *)nw_ghg_at(ld, NW_FILE_IX(mesh->vert_counts), 0x10);
                    mesh->normal_counts = (struct nushadowvtx_s *)nw_ghg_at(ld, NW_FILE_IX(mesh->normal_counts), 0x10);
                }
            }
        }
    }
}

/* As ReadTexAnimSet; ptr is the buffer's, returned past the envs carved after the object. */
static char *nw_ghg_texanims(NwGhgLoad *ld, const struct nughgheader_s *h, char *ptr)
{
    struct NUHGOBJ_s *hobj = ld->hobj;
    hobj->numtexanims = h->numtexanims;
    if (hobj->numtexanims == 0) {
        return ptr;
    }
    hobj->texanims = NW_GHG_NEW(ld, struct nutexanim_s, hobj->numtexanims);
    for (int i = 0; i < hobj->numtexanims; i++) {
        struct nutexanim_s *anim = &hobj->texanims[i];
        NW_FILE_X(nutexanim_s, anim, nw_ghg_at(ld, h->texanims + i * 0x20, sizeof(nw_ps2_nutexanim_s)));
        int mtl = NW_FILE_IX(anim->mtl);
        anim->tids = (short *)nw_ghg_at(ld, NW_FILE_IX(anim->tids), anim->numtids * sizeof(short));
        anim->prev = 0;
        anim->succ = 0;
        if (mtl < 0 || mtl >= hobj->nummtl) {
            NuErrorProlog(nuhgobj_srcfile, 0x622)("\"%s\": texanim %d of material %d", ld->file, i, mtl);
        }
        for (int j = 0; j < anim->numtids; j++) {
            if (anim->tids[j] < 0 || anim->tids[j] >= hobj->numtid) {
                NuErrorProlog(nuhgobj_srcfile, 0x622)("\"%s\": texanim %d of texture %d", ld->file, i, anim->tids[j]);
            }
            anim->tids[j] = hobj->tids[anim->tids[j]];
        }
        anim->mtl = hobj->mtls[mtl];
        anim->scriptname = nw_ghg_at(ld, NW_FILE_IX(anim->scriptname), 1);
        anim->ntaname = nw_ghg_at(ld, NW_FILE_IX(anim->ntaname), 1);
        struct nutexanimprog_s *prog = NuTexAnimProgFind(anim->scriptname);
        union variptr_u buff;
        ptr = (char *)(((uintptr_t)ptr + 0xF) & ~(uintptr_t)0xF);
        buff.voidptr = nw_heap_side_at(ld->side, sizeof(struct nutexanimenv_s), _Alignof(struct nutexanimenv_s), ptr);
        anim->env = NuTexAnimEnvCreate(&buff, anim->mtl, anim->tids, prog);
        ptr += sizeof(nw_ps2_nutexanimenv_s);
    }
    for (int j = 0; j < hobj->numtexanims - 1; j++) {
        hobj->texanims[j].succ = &hobj->texanims[j] + 1;
        hobj->texanims[j + 1].prev = &hobj->texanims[j];
    }
    if (!nw_texanim_own_list(ld->side, &hobj->texanims))
        NuErrorProlog(nuhgobj_srcfile, 0x622)("%s: %s", ld->file, nw_texanim_error()->message);
    NuTexAnimAddList(hobj->texanims);
    return ptr;
}

static struct NUHGOBJ_s *nw_ghg_parse(union variptr_u *buffer, char *base, int size, const char *file)
{
    NwGhgLoad ld = { .base = base, .size = (size_t)size, .file = file };
    struct nughgheader_s h;
    memcpy(&h, nw_ghg_at(&ld, 0, sizeof(h)), sizeof(h));
    if (h.version < NW_GHG_VERSION) {
        NuErrorProlog(nuhgobj_srcfile, 0x61D)("GHG file is out of date - needs to be reconverted (FILE VER:%d SYS "
                                              "VER:%d)", h.version, NW_GHG_VERSION);
    }
    if (h.version > NW_GHG_VERSION) {
        NuErrorProlog(nuhgobj_srcfile, 0x61F)("GHG loader is out of date - need newer 3d system (FILE VER:%d SYS "
                                              "VER:%d)", h.version, NW_GHG_VERSION);
    }
    nw_ghg_at(&ld, 0, (size_t)h.size);
    ld.side = nw_heap_side();
    char *at = (char *)(((uintptr_t)base + (size_t)h.size + 0xF) & ~(uintptr_t)0xF);
    struct NUHGOBJ_s *hobj = nw_heap_side_at(ld.side, sizeof(*hobj), _Alignof(struct NUHGOBJ_s), at);
    hobj->type = 1;
    ld.hobj = hobj;
    ld.g = SDL_calloc(1, sizeof(NwGhg));
    if (!ld.g) {
        NuErrorProlog(nuhgobj_srcfile, 0x622)("out of memory for \"%s\"", file);
    }
    ld.g->hobj = hobj;
    nw_ghg_materials(&ld, &h);
    nw_ghg_joints(&ld, &h);
    nw_ghg_layers(&ld, &h);
    hobj->sphere_radius = h.sphere_radius;
    hobj->sphere_y = h.sphere_y;
    hobj->bbox_min = h.bbox_min;
    hobj->bbox_max = h.bbox_max;
    hobj->cyl_y = h.cyl_y;
    hobj->cyl_height = h.cyl_height;
    hobj->cyl_radius = h.cyl_radius;
    nw_ghg_collide(&ld, &h);
    char *ptr = at + sizeof(nw_ps2_NUHGOBJ_s);
    ptr = nw_ghg_texanims(&ld, &h, ptr);
    buffer->charptr = (char *)(((uintptr_t)ptr + 0xF) & ~(uintptr_t)0xF);
    nw_heap_side_own(ld.side, base, buffer->charptr);
    if (nw_nghgs == nw_ghgcap) {
        uint32_t n = nw_ghgcap ? nw_ghgcap * 2 : 64;
        void *grown = SDL_realloc(nw_ghgs, n * sizeof(*nw_ghgs));
        if (!grown) {
            NuErrorProlog(nuhgobj_srcfile, 0x622)("out of memory for \"%s\"", file);
        }
        nw_ghgs = grown;
        nw_ghgcap = n;
    }
    nw_ghgs[nw_nghgs++] = ld.g;
    return hobj;
}

struct NUHGOBJ_s *NuGHGRead(union variptr_u *buffer, char *file)
{
    file = nw_opening_data(file);
    char *base = (char *)(((uintptr_t)buffer->voidptr + 0xF) & ~(uintptr_t)0xF);
    int size = NuFileLoadBuffer(file, base, 0x7FFFFFFF);
    if (size <= 0) {
        return 0;
    }
    return nw_ghg_parse(buffer, base, size, file);
}

struct NUHGOBJ_s *nw_ghg_read_image(union variptr_u *buffer, const void *image, size_t size, const char *name)
{
    char *base = (char *)(((uintptr_t)buffer->voidptr + 0xF) & ~(uintptr_t)0xF);
    if (size == 0 || size > INT32_MAX) {
        return 0;
    }
    memcpy(base, image, size);
    return nw_ghg_parse(buffer, base, (int)size, name);
}

/* Retail's; the meshes and records are freed once the frames that drew the object are on screen. */
void NuHGobjDestroy(struct NUHGOBJ_s *hobj)
{
    if (!hobj) {
        return;
    }
    switch (hobj->type) {
    case 0:
    default:
        NuHGobjDestroyDynamic(hobj);
        return;
    case 1:
        nw_texanim_release_list(&hobj->texanims);
        for (int i = 0; i < hobj->nummtl; i++) {
            NuMtlDestroy(hobj->mtls[i]);
        }
        for (int i = 0; i < hobj->numtid; i++) {
            NuTexDestroy(hobj->tids[i]);
        }
        break;
    }
    for (uint32_t i = 0; i < nw_nghgs; i++) {
        NwGhg *g = nw_ghgs[i];
        if (g->hobj != hobj) {
            continue;
        }
        for (uint32_t k = 0; k < g->ngpu; k++) {
            nw_gpu_queue(g->gpu[k].free, g->gpu[k].obj);
        }
        for (NwGhgBlock *b = g->blocks, *next; b; b = next) {
            next = b->next;
            nw_gpu_queue(NW_GPU_FREE_HOST, b);
        }
        SDL_free(g->gpu);
        SDL_free(g);
        nw_ghgs[i] = nw_ghgs[--nw_nghgs];
        hobj->texanims = NULL;
        hobj->numtexanims = hobj->nummtl = hobj->numtid = 0;
        hobj->num_layers = 0;
        break;
    }
}

/* The joint's number when its name is PREFIX followed by a number in first..last, else -1. */
static int nw_joint_number(const char *name, const char *prefix, int first, int last)
{
    size_t n = SDL_strlen(prefix);
    if (!name || SDL_strncmp(name, prefix, n)) {
        return -1;
    }
    char *end;
    long k = SDL_strtol(name + n, &end, 10);
    return end != name + n && k >= first && k <= last ? (int)k : -1;
}

/* A copy of src registered with the material system: everything but its list links and render-state owner. */
static struct numtl_s *nw_material_copy(const struct numtl_s *src)
{
    struct numtl_s *c = NuMtlCreate(1);
    if (!c) {
        return NULL;
    }
    struct numtl_s keep = *c;
    *c = *src;
    c->snext = keep.snext;
    c->slast = keep.slast;
    c->rs = keep.rs;
    c->rs.mtl = c;
    c->used = keep.used;
    c->fromblock = keep.fromblock;
    c->intalloc = keep.intalloc;
    c->next = NULL;
    return c;
}

int nw_hgobj_own_material(struct NUHGOBJ_s *h, int slot, const char *prefix, int first, int last,
                          struct numtl_s **copies)
{
    struct numtl_s *src = slot >= 0 && slot < h->nummtl ? h->mtls[slot] : NULL;
    SDL_memset(copies, 0, (size_t)(last - first + 1) * sizeof(*copies));
    if (!src || !h->joints || !h->layers || h->num_layers < 1 || !h->layers[0].gobjs) {
        return -1;
    }
    int made = 0;
    for (int j = 0; j < h->num_joints; j++) {
        int k = nw_joint_number(h->joints[j].name, prefix, first, last);
        if (k < 0) {
            continue;
        }
        struct numtl_s *c = nw_material_copy(src);
        if (!c) {
            return -1;
        }
        for (struct nugobj_s *g = h->layers[0].gobjs[j]; g; g = g->next_gobj) {
            for (struct nugeom_s *geom = g->geom; geom; geom = geom->next) {
                if (geom->mtl == src) {
                    geom->mtl = c;
                }
            }
        }
        copies[k - first] = c;
        made++;
    }
    return made;
}
