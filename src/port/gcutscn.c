/* gamelib/gcutscn's .cut loader (port/units.toml override): retail's NuGCutSceneLoad with host copies of the records
   it fixes in place, carved from the caller's heap after the file. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "nw_convert.h"
#include "port/xfile.h"

#define gcutscn_srcfile "..\\nu2crash.ps2\\gamelib\\gcutscn.c"
#define NW_CUT_REMAP 15 /* NUGCUTCAMSYS_s.remap: the rest of the file's 0x20-byte record */

/* Records the host reads in place from the file. */
_Static_assert(sizeof(struct NUGCUTCAM_s) == sizeof(nw_ps2_NUGCUTCAM_s), "NUGCUTCAM_s");
_Static_assert(sizeof(struct NUGCUTBBOX_s) == sizeof(nw_ps2_NUGCUTBBOX_s), "NUGCUTBBOX_s");
_Static_assert(sizeof(struct nuanimkey2_s) == sizeof(nw_ps2_nuanimkey2_s), "nuanimkey2_s");
_Static_assert(sizeof(union nuanimmask_u) == sizeof(nw_ps2_nuanimmask_u), "nuanimmask_u");

/* Declared by no header. */
int NuFileLoadBuffer(char *filename, void *mem, int buffsize);
void buildBitCountTable(void);
extern int D_0062EE18; /* bitcount_table_built */

typedef struct NwCutLoad {
    char *base;
    size_t size;
    uint32_t addr; /* the exporter's address of the file's first byte */
    char *file;
    union variptr_u *heap, *heapend;
} NwCutLoad;

static _Noreturn void nw_cut_bad(const NwCutLoad *ld, const char *what, uint32_t off)
{
    NuErrorProlog(gcutscn_srcfile, 0x151)("cutscene <%s>: %s at 0x%x outside the file", ld->file, what, off);
    for (;;) {
    }
}

/* size bytes of the image at the exporter address v, NULL for 0. */
static char *nw_cut_at(const NwCutLoad *ld, uint32_t v, size_t size)
{
    uint32_t off = v - ld->addr;
    if (v == 0) {
        return NULL;
    }
    if ((size_t)off > ld->size || size > ld->size - off) {
        nw_cut_bad(ld, "record", off);
    }
    return ld->base + off;
}

/* What a converted pointer field addresses. */
#define NW_CUT_PTR(ld, field, size) nw_cut_at((ld), (uint32_t)NW_FILE_IX(field), (size))

/* The string at off in the image, NUL-terminated inside it. */
static char *nw_cut_str(const NwCutLoad *ld, ptrdiff_t off)
{
    if (off < 0 || (size_t)off >= ld->size ||
        SDL_strnlen(ld->base + off, ld->size - (size_t)off) == ld->size - (size_t)off) {
        nw_cut_bad(ld, "name", (uint32_t)off);
    }
    return ld->base + off;
}

/* Zeroed host records off the caller's heap. */
static void *nw_cut_new(NwCutLoad *ld, size_t size, size_t align)
{
    char *p = (char *)(((uintptr_t)ld->heap->charptr + align - 1) & ~(uintptr_t)(align - 1));
    if (p > ld->heapend->charptr || size > (size_t)(ld->heapend->charptr - p)) {
        NuErrorProlog(gcutscn_srcfile, 0x14C)(
            "Could not read from cutscene <fname>, either file is not there or the buffer may be full");
    }
    memset(p, 0, size);
    ld->heap->charptr = p + size;
    return p;
}

#define NW_CUT_NEW(ld, type, n) ((type *)nw_cut_new((ld), (size_t)(n) * sizeof(type), _Alignof(type)))

/* NuAnimData2FixPtrs: curves become 8-byte unions, a constant where the curve's flag is 0. */
static struct nuanimdata2_s *nw_cut_anim2(NwCutLoad *ld, void *field)
{
    if (!D_0062EE18) {
        buildBitCountTable();
    }
    char *p = NW_CUT_PTR(ld, field, sizeof(nw_ps2_nuanimdata2_s));
    if (!p) {
        return NULL;
    }
    struct nuanimdata2_s *data = NW_CUT_NEW(ld, struct nuanimdata2_s, 1);
    NW_FILE_X(nuanimdata2_s, data, p);
    int count = data->nnodes * data->ncurves > 0 ? data->nnodes * data->ncurves : 0;
    int nchunks = data->nchunks > 0 ? data->nchunks : 0;
    char *curves = NW_CUT_PTR(ld, data->curves, (size_t)count * 4);
    data->curveflags = (signed char *)NW_CUT_PTR(ld, data->curveflags, (size_t)count);
    data->curvesetflags = NW_CUT_PTR(ld, data->curvesetflags, data->nnodes > 0 ? (size_t)data->nnodes : 0);
    data->curves = curves ? NW_CUT_NEW(ld, union nuanimcurve2_u, count) : NULL;
    if (count && (!curves || !data->curveflags)) {
        nw_cut_bad(ld, "anim curves", (uint32_t)(p - ld->base));
    }
    for (int i = 0; i < count; i++) {
        uint32_t v;
        memcpy(&v, curves + 4 * i, 4);
        if (!data->curveflags[i]) {
            memcpy(&data->curves[i].constant, &v, 4);
            continue;
        }
        char *c = nw_cut_at(ld, v, sizeof(nw_ps2_nuanimcurvedata2_s));
        if (!c) {
            nw_cut_bad(ld, "anim curve", (uint32_t)(curves + 4 * i - ld->base));
        }
        struct nuanimcurvedata2_s *cd = NW_CUT_NEW(ld, struct nuanimcurvedata2_s, 1);
        NW_FILE_X(nuanimcurvedata2_s, cd, c);
        cd->mask = (union nuanimmask_u *)NW_CUT_PTR(ld, cd->mask, (size_t)nchunks * 4);
        cd->key_ixs = (unsigned short *)NW_CUT_PTR(ld, cd->key_ixs, (size_t)nchunks * 2);
        cd->keys = (struct nuanimkey2_s *)NW_CUT_PTR(ld, cd->keys, 0);
        data->curves[i].curvedata = cd;
    }
    return data;
}

static struct NUSTATEANIM_s *nw_cut_stateanim(NwCutLoad *ld, void *field)
{
    char *p = NW_CUT_PTR(ld, field, sizeof(nw_ps2_NUSTATEANIM_s));
    if (!p) {
        return NULL;
    }
    struct NUSTATEANIM_s *anim = NW_CUT_NEW(ld, struct NUSTATEANIM_s, 1);
    NW_FILE_X(NUSTATEANIM_s, anim, p);
    size_t n = anim->count > 0 ? (size_t)anim->count : 0;
    anim->times = (float *)NW_CUT_PTR(ld, anim->times, n * 4);
    anim->values = (unsigned char *)NW_CUT_PTR(ld, anim->values, n);
    return anim;
}

/* A rigid, character or locator type name: a 1-based offset into the names. */
static char *nw_cut_name(const NwCutLoad *ld, const struct NUGCUTSCENE_s *def, char *field)
{
    if (!def->names) {
        nw_cut_bad(ld, "names", 0);
    }
    return nw_cut_str(ld, def->names - ld->base + NW_FILE_IX(field) - 1);
}

static void nw_cut_cams(NwCutLoad *ld, struct NUGCUTSCENE_s *def)
{
    char *p = NW_CUT_PTR(ld, def->camdef, offsetof(nw_ps2_NUGCUTCAMSYS_s, remap) + NW_CUT_REMAP);
    struct NUGCUTCAMSYS_s *camdef =
        nw_cut_new(ld, offsetof(struct NUGCUTCAMSYS_s, remap) + NW_CUT_REMAP, _Alignof(struct NUGCUTCAMSYS_s));
    NW_FILE_X(NUGCUTCAMSYS_s, camdef, p);
    memcpy(camdef->remap, p + offsetof(nw_ps2_NUGCUTCAMSYS_s, remap), NW_CUT_REMAP);
    def->camdef = camdef;
    camdef->cams = (struct NUGCUTCAM_s *)NW_CUT_PTR(ld, camdef->cams, camdef->count * sizeof(nw_ps2_NUGCUTCAM_s));
    if (camdef->anim) {
        camdef->anim = nw_cut_anim2(ld, camdef->anim);
    }
    camdef->stateanim = nw_cut_stateanim(ld, camdef->stateanim);
}

static void nw_cut_locators(NwCutLoad *ld, struct NUGCUTSCENE_s *def)
{
    struct NUGCUTLOCATORSYS_s *locatordef = NW_CUT_NEW(ld, struct NUGCUTLOCATORSYS_s, 1);
    NW_FILE_X(NUGCUTLOCATORSYS_s, locatordef, NW_CUT_PTR(ld, def->locatordef, sizeof(nw_ps2_NUGCUTLOCATORSYS_s)));
    def->locatordef = locatordef;
    char *p = NW_CUT_PTR(ld, locatordef->locators, locatordef->count * sizeof(nw_ps2_NUGCUTLOCATOR_s));
    locatordef->locators = p ? NW_CUT_NEW(ld, struct NUGCUTLOCATOR_s, locatordef->count) : NULL;
    for (int i = 0; p && i < locatordef->count; i++) {
        struct NUGCUTLOCATOR_s *locator = &locatordef->locators[i];
        NW_FILE_X(NUGCUTLOCATOR_s, locator, p + i * sizeof(nw_ps2_NUGCUTLOCATOR_s));
        if (locator->anim) {
            locator->anim = nw_cut_anim2(ld, locator->anim);
        }
    }
    p = NW_CUT_PTR(ld, locatordef->types, locatordef->ntypes * sizeof(nw_ps2_NUGCUTLOCATORTYPE_s));
    locatordef->types = p ? NW_CUT_NEW(ld, struct NUGCUTLOCATORTYPE_s, locatordef->ntypes) : NULL;
    for (int i = 0; p && i < locatordef->ntypes; i++) {
        struct NUGCUTLOCATORTYPE_s *type = &locatordef->types[i];
        NW_FILE_X(NUGCUTLOCATORTYPE_s, type, p + i * sizeof(nw_ps2_NUGCUTLOCATORTYPE_s));
        type->name = nw_cut_name(ld, def, type->name);
    }
}

/* locators stay the file's index (or junk) for NuGCutRigidSysFixUp. */
static void nw_cut_rigids(NwCutLoad *ld, struct NUGCUTSCENE_s *def)
{
    struct NUGCUTRIGIDSYS_s *rigiddef = NW_CUT_NEW(ld, struct NUGCUTRIGIDSYS_s, 1);
    NW_FILE_X(NUGCUTRIGIDSYS_s, rigiddef, NW_CUT_PTR(ld, def->rigiddef, sizeof(nw_ps2_NUGCUTRIGIDSYS_s)));
    def->rigiddef = rigiddef;
    char *p = NW_CUT_PTR(ld, rigiddef->rigids, rigiddef->count * sizeof(nw_ps2_NUGCUTRIGID_s));
    rigiddef->rigids = p ? NW_CUT_NEW(ld, struct NUGCUTRIGID_s, rigiddef->count) : NULL;
    for (int i = 0; p && i < rigiddef->count; i++) {
        struct NUGCUTRIGID_s *rigid = &rigiddef->rigids[i];
        NW_FILE_X(NUGCUTRIGID_s, rigid, p + i * sizeof(nw_ps2_NUGCUTRIGID_s));
        if (rigid->name) {
            rigid->name = nw_cut_name(ld, def, rigid->name);
        }
        if (rigid->anim) {
            rigid->anim = nw_cut_anim2(ld, rigid->anim);
        }
        rigid->stateanim = nw_cut_stateanim(ld, rigid->stateanim);
    }
}

/* locators as for the rigids, for the find-characters callback. */
static void nw_cut_chars(NwCutLoad *ld, struct NUGCUTSCENE_s *def)
{
    struct NUGCUTCHARSYS_s *chardef = NW_CUT_NEW(ld, struct NUGCUTCHARSYS_s, 1);
    NW_FILE_X(NUGCUTCHARSYS_s, chardef, NW_CUT_PTR(ld, def->chardef, sizeof(nw_ps2_NUGCUTCHARSYS_s)));
    def->chardef = chardef;
    char *p = NW_CUT_PTR(ld, chardef->chars, chardef->count * sizeof(nw_ps2_NUGCUTCHAR_s));
    chardef->chars = p ? NW_CUT_NEW(ld, struct NUGCUTCHAR_s, chardef->count) : NULL;
    for (int i = 0; p && i < chardef->count; i++) {
        struct NUGCUTCHAR_s *desc = &chardef->chars[i];
        NW_FILE_X(NUGCUTCHAR_s, desc, p + i * sizeof(nw_ps2_NUGCUTCHAR_s));
        if (desc->name) {
            desc->name = nw_cut_name(ld, def, desc->name);
        }
        if (desc->char_anim) {
            desc->char_anim = nw_cut_anim2(ld, desc->char_anim);
        }
        if (desc->joint_anim) {
            desc->joint_anim = nw_cut_anim2(ld, desc->joint_anim);
        }
        if (desc->face_anim) {
            desc->face_anim = nw_cut_anim2(ld, desc->face_anim);
        }
    }
}

/* Unlike the others, a trigger's name is an address. */
static void nw_cut_triggers(NwCutLoad *ld, struct NUGCUTSCENE_s *def)
{
    struct NUGCUTTRIGGERSYS_s *triggerdef = NW_CUT_NEW(ld, struct NUGCUTTRIGGERSYS_s, 1);
    NW_FILE_X(NUGCUTTRIGGERSYS_s, triggerdef, NW_CUT_PTR(ld, def->triggerdef, sizeof(nw_ps2_NUGCUTTRIGGERSYS_s)));
    def->triggerdef = triggerdef;
    int count = triggerdef->count > 0 ? triggerdef->count : 0;
    char *p = NW_CUT_PTR(ld, triggerdef->triggers, (size_t)count * sizeof(nw_ps2_NUGCUTTRIGGER_s));
    triggerdef->triggers = p ? NW_CUT_NEW(ld, struct NUGCUTTRIGGER_s, count) : NULL;
    for (int i = 0; p && i < count; i++) {
        struct NUGCUTTRIGGER_s *trigger = &triggerdef->triggers[i];
        NW_FILE_X(NUGCUTTRIGGER_s, trigger, p + i * sizeof(nw_ps2_NUGCUTTRIGGER_s));
        char *name = NW_CUT_PTR(ld, trigger->name, 1);
        trigger->name = name ? nw_cut_str(ld, name - ld->base) : NULL;
        trigger->anim = nw_cut_stateanim(ld, trigger->anim);
    }
}

/* base keeps the file's value: nothing reads it once loaded. */
struct NUGCUTSCENE_s *NuGCutSceneLoad(char *name, union variptr_u *heap, union variptr_u *heapend)
{
    heap->charptr = (char *)(((uintptr_t)heap->charptr + 15) & ~(uintptr_t)15);
    int size = NuFileLoadBuffer(name, heap->voidptr, (int)(heapend->charptr - heap->charptr));
    if (size == 0) {
        NuErrorProlog(gcutscn_srcfile, 0x14C)(
            "Could not read from cutscene <fname>, either file is not there or the buffer may be full");
    }
    NwCutLoad ld = { .base = heap->charptr, .size = (size_t)size, .file = name, .heap = heap, .heapend = heapend };
    heap->charptr += size;
    if (ld.size < sizeof(nw_ps2_NUGCUTSCENE_s)) {
        nw_cut_bad(&ld, "header", 0);
    }
    struct NUGCUTSCENE_s *def = NW_CUT_NEW(&ld, struct NUGCUTSCENE_s, 1);
    NW_FILE_X(NUGCUTSCENE_s, def, ld.base);
    if (def->version != 1) {
        NuErrorProlog(gcutscn_srcfile, 0x151)("Out of date version of cutscene <%s>, you have version <%d>", name,
                                              def->version);
    }
    ld.addr = (uint32_t)def->base;
    def->names = NW_CUT_PTR(&ld, def->names, 0);
    if (def->camdef) {
        nw_cut_cams(&ld, def);
    }
    if (def->locatordef) {
        nw_cut_locators(&ld, def);
    }
    if (def->rigiddef) {
        nw_cut_rigids(&ld, def);
    }
    if (def->chardef) {
        nw_cut_chars(&ld, def);
    }
    if (def->triggerdef) {
        nw_cut_triggers(&ld, def);
    }
    def->bounds = (struct NUGCUTBBOX_s *)NW_CUT_PTR(&ld, def->bounds, sizeof(nw_ps2_NUGCUTBBOX_s));
    return def;
}
