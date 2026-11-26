/* The title's looks: the .looks files beside its models (port/lookfile.h), worn by the logo, its per-joint materials
   (the meter's LEDs) and UNIVERSAL, their drives following the signals each frame. TITLE.LOOKS for the logo, the
   logos scene's for UNIVERSAL. */
#include "port/opening/internal.h"
#include "port/lookfile.h"
#include "port/nuhgobj.h"

/* The logos cut's models, before opening.txt's replacements. */
#define LOGO_MODEL "levels\\b\\logos\\title.ghg"
#define SCENE_MODEL "levels\\b\\logos\\logos.gsc"

static NwLookFile *logo, *scene;

/* The looks beside a model, as opening.txt replaces it: *out stays NULL with no file; false if it does not read. */
static bool read_beside(const char *model, NwLookFile **out)
{
    char path[160];
    const char *target = nw_opening_model(model), *dot = SDL_strrchr(target, '.');
    *out = NULL;
    if (!dot || SDL_snprintf(path, sizeof(path), "%.*s.looks", (int)(dot - target), target) >= (int)sizeof(path))
        return SDL_SetError("%s: path too long", target);
    NwTexAnimFile file;
    if (!nw_opening_io.read(NULL, path, &file)) return true; /* none: the model's own materials */
    *out = nw_lookfile_read(path, file.bytes, file.size);
    nw_opening_io.release(NULL, &file);
    return *out != NULL;
}

bool nw_opening_looks_load(void)
{
    NwLookFile *l = NULL, *s = NULL;
    if (!read_beside(LOGO_MODEL, &l) || !read_beside(SCENE_MODEL, &s)) {
        nw_lookfile_free(l);
        return false;
    }
    nw_lookfile_free(logo);
    nw_lookfile_free(scene);
    logo = l;
    scene = s;
    return true;
}

/* An object's materials in the logos scene (camera.c): the recipe its slot wears, animated; NULL for none. */
const float *nw_opening_scene_look(const char *object, int slot)
{
    return nw_lookfile_frame(scene, nw_lookfile_slot(scene, object, slot));
}

/* The logo file's splits (the meter's LEDs): each joint's own copy of a material, made once a model, wearing the
   recipe its group's count picks. Up to 4 splits of 32 joints. */
static struct {
    const struct NUHGOBJ_s *model;
    struct numtl_s *mtl[32];
} copies[4];

static void split_frame(struct NUHGOBJ_s *h)
{
    NwLookSplit s;
    for (int k = 0; k < (int)SDL_arraysize(copies) && nw_lookfile_split(logo, k, &s); k++) {
        int n = s.last - s.first + 1;
        if (n > (int)SDL_arraysize(copies[k].mtl)) continue;
        if (copies[k].model != h) {
            copies[k].model = h;
            if (nw_hgobj_own_material(h, s.slot, s.prefix, s.first, s.last, copies[k].mtl) < 0)
                SDL_Log("opening: no joints %s%d..%d for %s", s.prefix, s.first, s.last, s.group);
        }
        for (int i = 0; i < n; i++) {
            const float *fx = copies[k].mtl[i] ? nw_lookfile_frame(logo, nw_lookfile_counted(logo, s.group, i + 1)) : NULL;
            if (fx) nw_rndr_mtl_fx(copies[k].mtl[i], fx);
        }
    }
}

/* The logo materials wearing a look this cut, to take the looks off with it. */
static struct numtl_s *worn[64];
static size_t worn_count;

void nw_opening_looks_off(void)
{
    for (size_t i = 0; i < worn_count; i++) nw_rndr_mtl_fx(worn[i], NULL);
    worn_count = 0;
}

void nw_opening_logo_frame(void)
{
    struct NUHGOBJ_s *h = nw_title_hobj;
    if (!h) return;
    if (nw_opening.logo_look <= 0 || !h->mtls) { nw_opening_looks_off(); return; }
    nw_opening_sequence_sync();
    split_frame(h);
    worn_count = 0;
    for (int slot = 0; slot < h->nummtl && worn_count < SDL_arraysize(worn); slot++) {
        const float *fx = h->mtls[slot] ? nw_lookfile_frame(logo, nw_lookfile_slot(logo, NULL, slot)) : NULL;
        if (!fx) continue;
        nw_rndr_mtl_fx(h->mtls[slot], fx);
        worn[worn_count++] = h->mtls[slot];
    }
}
