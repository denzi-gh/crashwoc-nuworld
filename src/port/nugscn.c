/* nu3d/nugscn's loader (port/units.toml override): retail's, with host copies of the records retail fixes in place. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "gfx/packet.h"
#include "gfx/rndr.h"
#include "nw_convert.h"
#include "port/nuanim.h"
#include "port/nugscn.h"
#include "port/nw_newlib.h"
#include "port/xfile.h"

#define nugscn_srcfile "..\\nu2crash.ps2\\nu3d\\nugscn.c"
#define NW_GSCN_VERSION 6 /* scene_version */

struct nugscnload_s {
    int size;
    char *filebuffer;
    char **buffer;
    char *bufferend;
    char *filename;
};

/* Declared by no header. */
int NuFileLoadBuffer(char *filename, void *mem, int buffsize);
int NuFileRead(int fh, void *data, int size);
int NuFileReadInt(int fh);
float NuFileReadFloat(int fh);
int NuFileSeek(int fh, int offset, int origin);
int NuFileBeginBlkRead(int fh, int id);
void NuFileEndBlkRead(int fh);
int NuMemFileOpen(char *start, int size, int mode);
void NuMemFileClose(int fh);
void *NuMemFileAddr(int fh);
int NuMemFileSeek(int fh, int offset, int origin);
void timeUserReset(void);
void NuDisableVBlank(void);
void NuEnableVBlank(void);
void NuCvtGeoFixup(struct nucolourblend_s *ref, int offset);
void NuRndrSplineSearch(void *scene);
extern NuMtlReadEventFunc NuMtlReadEvent;
extern int nugscn_mtlupdate_enabled;
extern int in_protected_bit;

static char **buffer_freeptr; /* the caller's buffer cursor */
static char *buffer_tmpptr;   /* the buffer's end (no temp workspace) */
static char *readfile_base;
static struct nugscn_s *gscene;
static struct nugscnload_s gsc_load_context;

/* The GPU objects of the scenes loaded so far, and the job that frees each. */
static struct {
    void *obj;
    NwGpuOp free;
} *nw_gscn_gpu;
static uint32_t nw_gscn_ngpu, nw_gscn_gpucap;

/* Carves size bytes off the scene load buffer. */
static void *GScnBufferAlloc(size_t size, size_t align)
{
    char *ptr = (char *)(((uintptr_t)*buffer_freeptr + align - 1) & ~(uintptr_t)(align - 1));
    if (ptr < buffer_tmpptr && size < (size_t)(buffer_tmpptr - ptr)) {
        *buffer_freeptr = ptr + size;
        return ptr;
    }
    NuErrorProlog(nugscn_srcfile, 0x184)("NuGScnBufferAllocAligned failed, scene buffer overrun. ");
    return 0;
}

/* n host records, at least 4-aligned as retail's. */
#define NW_GSCN_NEW(type, n) \
    ((type *)GScnBufferAlloc((size_t)(n) * sizeof(type), _Alignof(type) > 4 ? _Alignof(type) : 4))

/* RELOCATE: a self-relative pointer of the file image, NULL for 0. */
static char *nw_gscn_rel(char *field)
{
    int32_t off;
    memcpy(&off, field, 4);
    return off ? field + off : 0;
}

/* A decoded geom queued for the GPU, or a fatal error. */
static void *nw_gscn_gpu_add(void *obj, bool ok, NwGpuOp upload, NwGpuOp free)
{
    if (!ok) {
        NuErrorProlog(nugscn_srcfile, 0xB95)("\"%s\": %s", gsc_load_context.filename, SDL_GetError());
    }
    if (nw_gscn_ngpu == nw_gscn_gpucap) {
        uint32_t n = nw_gscn_gpucap ? nw_gscn_gpucap * 2 : 1024;
        void *grown = SDL_realloc(nw_gscn_gpu, n * sizeof(*nw_gscn_gpu));
        if (!grown) {
            NuErrorProlog(nugscn_srcfile, 0xB95)("out of memory for the geom meshes");
        }
        nw_gscn_gpu = grown;
        nw_gscn_gpucap = n;
    }
    nw_gscn_gpu[nw_gscn_ngpu].obj = obj;
    nw_gscn_gpu[nw_gscn_ngpu++].free = free;
    nw_gpu_queue(upload, obj);
    return obj;
}

/* A geom's DMA chain, or an ordering-table geom's triangle batch, decoded once. */
static NwGpuMesh *nw_gscn_mesh(void *stream, int size, bool ot)
{
    NwGpuMesh *mesh = SDL_calloc(1, sizeof(*mesh));
    bool ok = mesh && (ot ? nw_vif_otbatch(&mesh->mesh, stream, (size_t)size)
                          : nw_vif_decode(&mesh->mesh, stream, (size_t)size));
    return nw_gscn_gpu_add(mesh, ok, NW_GPU_UPLOAD_MESH, NW_GPU_FREE_MESH);
}

/* A face-on geom's sprites. */
static NwGpuSprites *nw_gscn_sprites(void *stream, int size)
{
    NwGpuSprites *sprites = SDL_calloc(1, sizeof(*sprites));
    bool ok = sprites && nw_vif_sprites(&sprites->sprites, stream, (size_t)size);
    return nw_gscn_gpu_add(sprites, ok, NW_GPU_UPLOAD_SPRITES, NW_GPU_FREE_SPRITES);
}

void nw_gscn_free_meshes(void)
{
    for (uint32_t i = 0; i < nw_gscn_ngpu; i++) {
        nw_gpu_queue(nw_gscn_gpu[i].free, nw_gscn_gpu[i].obj);
    }
    SDL_free(nw_gscn_gpu);
    nw_gscn_gpu = NULL;
    nw_gscn_ngpu = nw_gscn_gpucap = 0;
}

void nw_gscn_remove(struct nugscn_s *scene)
{
    NuGScnRemove(scene);
    for (int i = 0; i < scene->numtid; i++) {
        nw_rndr_texture_free(scene->tids[i]);
    }
    nw_gscn_free_meshes();
}

struct nugscn_s *NuGScnLoadFix(void);

/* The animation library on the anim v1 walk: self-relative pointers, node_names keeps its file value. */
static char *nw_gscn_anim_at(NwAnimFile *f, char *field, size_t size)
{
    (void)f, (void)size;
    return nw_gscn_rel(field);
}

static void *nw_gscn_anim_new(NwAnimFile *f, size_t size, size_t align)
{
    (void)f;
    void *p = GScnBufferAlloc(size, align > 4 ? align : 4);
    memset(p, 0, size);
    return p;
}

void ReadAnimationLibrary(int fh)
{
    NwAnimFile file = { nw_gscn_anim_at, nw_gscn_anim_new, 0 };
    int count = NuFileReadInt(fh);
    NuFileReadInt(fh);
    char *table = NuMemFileAddr(fh);
    gscene->instanimdata = NW_GSCN_NEW(struct nuanimdata_s *, count);
    for (int i = 0; i < count; i++) {
        char *p = nw_gscn_rel(table + 4 * i);
        gscene->instanimdata[i] = p ? nw_anim_data(&file, p) : 0;
    }
}

int NuGScnLoad(char **buffer, char *bufend, char *filename)
{
    buffer_freeptr = buffer;
    buffer_tmpptr = bufend;
    char *filebuf = (char *)(((uintptr_t)*buffer_freeptr + 0x7F) & ~(uintptr_t)0x7F);
    if (filebuf < buffer_tmpptr) {
        *buffer_freeptr = filebuf;
    } else {
        NuErrorProlog(nugscn_srcfile, 0x184)("NuGScnBufferAllocAligned failed, scene buffer overrun. ");
        filebuf = 0;
    }
    readfile_base = filebuf;
    int size = NuFileLoadBuffer(filename, filebuf, (int)(bufend - *buffer));
    gsc_load_context.size = size;
    gsc_load_context.filebuffer = filebuf;
    gsc_load_context.buffer = buffer;
    gsc_load_context.bufferend = bufend;
    gsc_load_context.filename = filename;
    return size;
}

void ReadTextureSet(int fh)
{
    int ntex = NuFileReadInt(fh);
    NuFileReadInt(fh);
    gscene->tids = NW_GSCN_NEW(int, ntex);
    memset(gscene->tids, 0, ntex * sizeof(int));
    gscene->numtid = ntex;
    for (int i = 0; i < ntex; i++) {
        int size = NuFileReadInt(fh);
        int skip = NuFileReadInt(fh);
        if (skip == 0) {
            NuFileReadInt(fh);
            NuFileReadInt(fh);
        } else {
            NuFileSeek(fh, skip, 1);
        }
        void *data = NuMemFileAddr(fh);
        NuMemFileSeek(fh, size, 1);
        gscene->tids[i] = NuTexCreateNative(data);
    }
}

void ReadMaterialSet(int fh)
{
    int nmtls = NuFileReadInt(fh);
    NuFileReadInt(fh);
    gscene->mtls = NW_GSCN_NEW(struct numtl_s *, nmtls);
    gscene->nummtl = nmtls;
    for (int i = 0; i < nmtls; i++) {
        struct numtl_s *mtl = NW_GSCN_NEW(struct numtl_s, 1);
        NW_FILE_X(numtl_s, mtl, NuMemFileAddr(fh));
        NuMemFileSeek(fh, sizeof(nw_ps2_numtl_s), 1);
        gscene->mtls[i] = mtl;
        if (mtl->tid == -1) {
            mtl->tid = 0;
        } else {
            mtl->tid = gscene->tids[mtl->tid];
        }
        if (NW_FILE_IX(mtl->next) == -1) {
            mtl->next = 0;
        }
        struct numtl_s *next = mtl->next;
        if (nugscn_mtlupdate_enabled) {
            NuDisableVBlank();
            in_protected_bit = 1;
            NuMtlAdd(mtl);
            mtl->next = next;
            if (!NuMtlReadEvent(mtl)) {
                NuMtlUpdate(mtl);
            }
            in_protected_bit = 0;
            NuEnableVBlank();
        }
    }
    for (int i = 0; i < nmtls; i++) {
        struct numtl_s *mtl = gscene->mtls[i];
        unsigned int ix = (unsigned int)(uintptr_t)mtl->next;
        if (ix == 0xFFFFFFFF) {
            mtl->next = 0;
        } else if (ix >= 1 && ix <= 0xFFFF) {
            mtl->next = gscene->mtls[ix - 1];
        }
    }
}

#define DIST2(dx, dy, dz) ((dx) * (dx) + (dy) * (dy) + (dz) * (dz))
#define KEEP_MAX(dst, val) ((dst) = ((val) < (dst)) ? (dst) : (val))

void ReadObjSet(int fh)
{
    struct nuvec_s origin;
    memset(&origin, 0, sizeof(origin));
    int ngobjs = NuFileReadInt(fh);
    int colourref = NuFileReadInt(fh);
    gscene->gobjs = NW_GSCN_NEW(struct nugobj_s *, ngobjs);
    memset(gscene->gobjs, 0, ngobjs * sizeof(struct nugobj_s *));
    gscene->numgobj = ngobjs;
    for (int i = 0; i < ngobjs; i++) {
        int nsub = NuFileReadInt(fh);
        NuFileReadInt(fh);
        NuFileReadInt(fh);
        NuFileReadInt(fh);
        struct nugobj_s *gobj = NW_GSCN_NEW(struct nugobj_s, nsub);
        gscene->gobjs[i] = gobj;
        memset(gobj, 0, nsub * sizeof(struct nugobj_s));
        gobj->ngobjs = nsub;
        for (int j = 0; j < nsub; j++) {
            int count, size;
            switch (gobj->type = NuFileReadInt(fh)) {
            case 0: {
                count = NuFileReadInt(fh);
                NuFileRead(fh, &gobj->origin, sizeof(struct nuvec_s));
                struct nugeom_s *geom = NW_GSCN_NEW(struct nugeom_s, count);
                memset(geom, 0, count * sizeof(struct nugeom_s));
                int tricount = NuFileReadInt(fh);
                NuFileReadInt(fh);
                NuFileReadInt(fh);
                gobj->geom = geom;
                for (int k = 0; k < count; k++, geom++) {
                    if (k != count - 1) {
                        geom->next = geom + 1;
                    }
                    NuFileReadInt(fh);
                    int skip = NuFileReadInt(fh);
                    geom->mtl = gscene->mtls[NuFileReadInt(fh)];
                    NuMemFileSeek(fh, skip, 1);
                    size = NuFileReadInt(fh);
                    if (size & 0xF) {
                        NuErrorProlog(nugscn_srcfile, 0xB95)("invalid qwc");
                    }
                    /* No in-place block->data = block->qw: a host pointer does not fit the 32-bit field. An OT
                       geom keeps its batch, which XFormVuTriZ reads, and its triangles as a mesh in dmastream,
                       which NuRndrGScnObj draws through the ordering table. */
                    if (geom->mtl->attrib.ot == 1) {
                        geom->otbatch = NuMemFileAddr(fh);
                    }
                    geom->dmastream = nw_gscn_mesh(NuMemFileAddr(fh), size, geom->otbatch != NULL);
                    NuMemFileSeek(fh, size, 1);
                }
                gobj->tricount = tricount;
                break;
            }
            case 1: {
                count = NuFileReadInt(fh);
                NuFileReadInt(fh);
                NuFileReadInt(fh);
                gobj->origin = origin;
                struct nufaceongeom_s *fgeom = NW_GSCN_NEW(struct nufaceongeom_s, count);
                memset(fgeom, 0, count * sizeof(struct nufaceongeom_s));
                gobj->faceon_geom = fgeom;
                for (int k = 0; k < count; k++, fgeom++) {
                    if (k != count - 1) {
                        fgeom->next = fgeom + 1;
                    }
                    fgeom->mtl = gscene->mtls[NuFileReadInt(fh)];
                    fgeom->faceon_type = NuFileReadInt(fh);
                    fgeom->faceon_radius = NuFileReadFloat(fh);
                    size = NuFileReadInt(fh);
                    fgeom->dmastream = nw_gscn_sprites(NuMemFileAddr(fh), size);
                    NuMemFileSeek(fh, size, 1);
                }
                break;
            }
            default:
                NuErrorProlog(nugscn_srcfile, 0xBD2)("Unknown geom type");
                break;
            }
            NuFileRead(fh, &gobj->min, sizeof(struct nuvec_s));
            NuFileRead(fh, &gobj->max, sizeof(struct nuvec_s));
            NuFileReadInt(fh);
            NuFileReadInt(fh);
            gobj->cntr.x = (gobj->min.x + gobj->max.x) * 0.5f;
            gobj->cntr.y = (gobj->min.y + gobj->max.y) * 0.5f;
            gobj->cntr.z = (gobj->min.z + gobj->max.z) * 0.5f;
            gobj->r2 = DIST2(gobj->min.x, gobj->min.y, gobj->min.z);
            KEEP_MAX(gobj->r2, DIST2(gobj->max.x, gobj->min.y, gobj->min.z));
            KEEP_MAX(gobj->r2, DIST2(gobj->min.x, gobj->max.y, gobj->min.z));
            KEEP_MAX(gobj->r2, DIST2(gobj->max.x, gobj->max.y, gobj->min.z));
            KEEP_MAX(gobj->r2, DIST2(gobj->min.x, gobj->min.y, gobj->max.z));
            KEEP_MAX(gobj->r2, DIST2(gobj->max.x, gobj->min.y, gobj->max.z));
            KEEP_MAX(gobj->r2, DIST2(gobj->min.x, gobj->max.y, gobj->max.z));
            KEEP_MAX(gobj->r2, DIST2(gobj->max.x, gobj->max.y, gobj->max.z));
            gobj->r = sqrt(gobj->r); /* retail: r stays 0 */
            float lx = gobj->min.x - gobj->cntr.x, ly = gobj->min.y - gobj->cntr.y, lz = gobj->min.z - gobj->cntr.z;
            float hx = gobj->max.x - gobj->cntr.x, hy = gobj->max.y - gobj->cntr.y, hz = gobj->max.z - gobj->cntr.z;
            gobj->cntr_r2 = DIST2(lx, ly, lz);
            KEEP_MAX(gobj->cntr_r2, DIST2(hx, ly, lz));
            KEEP_MAX(gobj->cntr_r2, DIST2(lx, hy, lz));
            KEEP_MAX(gobj->cntr_r2, DIST2(hx, hy, lz));
            KEEP_MAX(gobj->cntr_r2, DIST2(lx, ly, hz));
            KEEP_MAX(gobj->cntr_r2, DIST2(hx, ly, hz));
            KEEP_MAX(gobj->cntr_r2, DIST2(lx, hy, hz));
            KEEP_MAX(gobj->cntr_r2, DIST2(hx, hy, hz));
            gobj->cntr_r = sqrt(gobj->cntr_r2);
            gobj++;
        }
        gscene->gobjs[i]->blend = 0;
        gscene->gobjs[i]->faceon_blend = 0;
        if (colourref == 1) {
            gscene->gobjs[i]->tricount = NuFileReadInt(fh);
            NuFileReadInt(fh);
            NuFileReadInt(fh);
            NuFileReadInt(fh);
            if (gscene->gobjs[i]->tricount != 0) {
                gscene->gobjs[i]->blend = NuMemFileAddr(fh);
                NuMemFileSeek(fh, gscene->gobjs[i]->tricount, 1);
                NuCvtGeoFixup(gscene->gobjs[i]->blend, (int)(intptr_t)readfile_base);
            }
        }
    }
}

void ReadTexAnimSet(int fh)
{
    gscene->numtexanims = NuFileReadInt(fh);
    NuFileReadInt(fh);
    char *src = NuMemFileAddr(fh);
    NuMemFileSeek(fh, gscene->numtexanims * sizeof(nw_ps2_nutexanim_s), 1);
    NuFileReadInt(fh);
    short *tidtab = NuMemFileAddr(fh);
    gscene->texanims = NW_GSCN_NEW(struct nutexanim_s, gscene->numtexanims);

    for (int i = 0; i < gscene->numtexanims; i++) {
        struct nutexanim_s *tex = &gscene->texanims[i];
        NW_FILE_X(nutexanim_s, tex, src + i * sizeof(nw_ps2_nutexanim_s));
        tex->succ = 0;
        tex->prev = 0;
        tex->tids = tidtab + NW_FILE_IX(tex->tids);
        for (int j = 0; j < tex->numtids; j++) {
            tex->tids[j] = gscene->tids[tex->tids[j]];
        }
        tex->ntaname = gscene->nametable + NW_FILE_IX(tex->ntaname);
        char *scriptname = gscene->nametable + NW_FILE_IX(tex->scriptname);
        tex->mtl = gscene->mtls[NW_FILE_IX(tex->mtl)];
        tex->dynalloc = 0;
        struct nutexanimprog_s *prog = NuTexAnimProgFind(scriptname);
        union variptr_u buff;
        char *start = buff.charptr = (char *)(((uintptr_t)*buffer_freeptr + 0xF) & ~(uintptr_t)0xF);
        tex->env = NuTexAnimEnvCreate(&buff, tex->mtl, tex->tids, prog);
        GScnBufferAlloc(buff.charptr - start, 0x10);
    }

    for (int i = 0; i < gscene->numtexanims - 1; i++) {
        gscene->texanims[i].succ = &gscene->texanims[i] + 1;
        gscene->texanims[i + 1].prev = &gscene->texanims[i];
    }
    NuTexAnimAddList(gscene->texanims);
}

/* Read in place instead of retail's temp copy. */
void ReadSplineSet(int fh)
{
    gscene->numsplines = NuFileReadInt(fh);
    int size = NuFileReadInt(fh);
    gscene->splines = NW_GSCN_NEW(struct nugspline_s, gscene->numsplines);
    char *data = NuMemFileAddr(fh);
    NuMemFileSeek(fh, size, 1);
    for (int i = 0; i < gscene->numsplines; i++) {
        struct nugspline_s *spline = &gscene->splines[i];
        unsigned short len;
        int name;
        memcpy(&len, data, sizeof(len));
        memcpy(&name, data + 4, sizeof(name));
        data += 8;
        spline->len = len;
        spline->name = gscene->nametable + name;
        spline->ptsize = FindSplinePtSize(spline->name);
        spline->pts = GScnBufferAlloc((size_t)spline->ptsize * spline->len, 4);
        char *pts = spline->pts;
        for (int j = 0; j < spline->len; j++) {
            memcpy(pts, data, sizeof(struct nuvec_s));
            data += sizeof(struct nuvec_s);
            pts += spline->ptsize;
        }
    }
}

struct nugscn_s *NuGScnLoadFix(void)
{
    gscene = 0;
    timeUserReset();
    int size = gsc_load_context.size;
    char **buffer = gsc_load_context.buffer;
    char *filename = gsc_load_context.filename;
    if (size > 0) {
        char *data = GScnBufferAlloc(size, 0x10);
        if (data == 0) {
            NuErrorProlog(nugscn_srcfile, 0xF00)("GSC File too large, \"%s\" ", filename);
        }
        gscene = NW_GSCN_NEW(struct nugscn_s, 1);
        if (gscene != 0) {
            memset(gscene, 0, sizeof(struct nugscn_s));
            int fh = NuMemFileOpen(data, size, 0);
            if (fh != 0) {
                NuFileBeginBlkRead(fh, 0x3032554E);
                int version = NuFileReadInt(fh);
                NuFileReadInt(fh);
                if (version < NW_GSCN_VERSION) {
                    NuErrorProlog(nugscn_srcfile, 0xF0C)("GSC File out of date - need newer \"%s\" file", filename);
                }
                if (version > NW_GSCN_VERSION) {
                    NuErrorProlog(nugscn_srcfile, 0xF0F)("Loader out of date - need newer 3D");
                }
                int blk;
                while ((blk = NuFileBeginBlkRead(fh, 0)) != 0) {
                    DbgBlkType(blk);
                    switch (blk) {
                    case 0x32545354:
                    case 0x30545354:
                        ReadTextureSet(fh);
                        break;
                    case 0x3230534D:
                    case 0x3030534D:
                        ReadMaterialSet(fh);
                        break;
                    case 0x304A424F:
                        ReadObjSet(fh);
                        break;
                    case 0x54534E49: {
                        gscene->numinstance = NuFileReadInt(fh);
                        NuFileReadInt(fh);
                        char *src = NuMemFileAddr(fh);
                        NuMemFileSeek(fh, gscene->numinstance * sizeof(nw_ps2_nuinstance_s), 1);
                        gscene->instances = NW_GSCN_NEW(struct nuinstance_s, gscene->numinstance);
                        for (int i = 0; i < gscene->numinstance; i++) {
                            NW_FILE_X(nuinstance_s, &gscene->instances[i], src + i * sizeof(nw_ps2_nuinstance_s));
                            gscene->instances[i].flags.visitest = -1; /* the signed 1-bit field's set bit */
                        }
                        break;
                    }
                    case 0x4C42544E: {
                        int count = NuFileReadInt(fh);
                        gscene->nametable = NuMemFileAddr(fh);
                        NuMemFileSeek(fh, count, 1);
                        for (int j = 0; j < count; j++) {
                            char c = gscene->nametable[j];
                            gscene->nametable[j] = c >= 'A' && c <= 'Z' ? c + 0x20 : c;
                        }
                        break;
                    }
                    case 0x43455053: {
                        gscene->numspecial = NuFileReadInt(fh);
                        NuFileReadInt(fh);
                        char *src = NuMemFileAddr(fh);
                        NuMemFileSeek(fh, gscene->numspecial * sizeof(nw_ps2_nuspecial_s), 1);
                        gscene->specials = NW_GSCN_NEW(struct nuspecial_s, gscene->numspecial);
                        struct nuspecial_s *special = gscene->specials;
                        for (int k = 0; k < gscene->numspecial; k++, special++) {
                            NW_FILE_X(nuspecial_s, special, src + k * sizeof(nw_ps2_nuspecial_s));
                            int ix = NW_FILE_IX(special->instance);
                            special->mtx = gscene->instances[ix].mtx;
                            special->instance = &gscene->instances[ix];
                            special->name = gscene->nametable + NW_FILE_IX(special->name);
                            special->flags &= ~1;
                        }
                        break;
                    }
                    case 0x30545353:
                        ReadSplineSet(fh);
                        break;
                    case 0x4C424149: {
                        NuFileReadInt(fh);
                        NuFileReadInt(fh);
                        gscene->instanimblock = NuMemFileAddr(fh);
                        struct nuinstanim_s *anim = gscene->instanimblock;
                        if (gscene->instanimblock != 0) {
                            for (int m = 0; m < gscene->numinstance; m++) {
                                if (gscene->instances[m].anim != 0) {
                                    gscene->instances[m].anim = anim++;
                                    gscene->instances[m].anim->mtx = gscene->instances[m].mtx;
                                }
                            }
                        }
                        break;
                    }
                    case 0x42494C41:
                        ReadAnimationLibrary(fh);
                        break;
                    case 0x44494E49:
                        gscene->numinstanceixs = NuFileReadInt(fh);
                        NuFileReadInt(fh);
                        gscene->instanceixs = NuMemFileAddr(fh);
                        break;
                    case 0x30534154:
                        ReadTexAnimSet(fh);
                        break;
                    }
                    NuFileEndBlkRead(fh);
                }
                NuFileEndBlkRead(fh);
                NuMemFileClose(fh);
            }
            *buffer = (char *)(((uintptr_t)*buffer + 0xF) & ~(uintptr_t)0xF);
            NuRndrSplineSearch(gscene);
        }
    }
    return gscene;
}
