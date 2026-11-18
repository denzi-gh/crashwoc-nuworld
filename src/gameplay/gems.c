#include "gameplay/gems.h"

#include <stdbool.h>

#include <SDL3/SDL.h>

#include "data/image_tables.h"
#include "game/creature.h"
#include "game/gametypes.h"
#include "gfx/gem.h"
#include "nu3d/nugobj.h"
#include "nu3d/nugscn.h"
#include "nu3d/nuhgobj.h"
#include "nu3d/numtl.h"

/* Declared by no header. */
extern struct objtab_s ObjTab[201];

/* CData's d\items. */
static const struct {
    const char *file;
    NwGemKind kind;
} nw_gems_items[] = {
    { "crystal", NW_GEM_CRYSTAL }, { "crategem", NW_GEM_CLEAR }, { "bonusgem", NW_GEM_CLEAR },
    { "red", NW_GEM_COLOUR },      { "green", NW_GEM_COLOUR },  { "blue", NW_GEM_COLOUR },
    { "yellow", NW_GEM_COLOUR },   { "purple", NW_GEM_COLOUR },
};

/* ObjTab: relic_p, relic_g, relic_s (wumpa.gsc), then the hub's pedestal copies crystal_h .. yellow_gem_h. */
#define NW_GEMS_RELICS 1
#define NW_GEMS_PEDESTALS 0xC1

static void nw_gems_add(const struct numtl_s *m, NwGemKind kind, NwGemRole role)
{
    const float tint[3] = { m->diffuse.r, m->diffuse.g, m->diffuse.b };
    nw_gem_add(m, kind, role, tint);
}

static bool nw_gems_glass(const struct numtl_s *m)
{
    const NwVuProg *mpg = (const NwVuProg *)m->rs.mpg;
    return mpg && SDL_strcmp(mpg->name, "vu_load_glass_mpg") == 0;
}

void nw_gems_models(void)
{
    nw_gem_reset();
    for (int c = 0; c < (int)SDL_arraysize(CData); c++) {
        if (CRemap[c] < 0 || !CData[c].path || SDL_strcmp(CData[c].path, "d\\items") != 0) {
            continue;
        }
        const struct NUHGOBJ_s *o = CModel[CRemap[c]].obj;
        for (size_t i = 0; o && i < SDL_arraysize(nw_gems_items); i++) {
            if (SDL_strcmp(CData[c].file, nw_gems_items[i].file) != 0) {
                continue;
            }
            /* The glass is the stone; the crystal's and blue gem's blended layer goes. */
            for (int k = 0; k < o->nummtl; k++) {
                const struct numtl_s *m = o->mtls[k];
                nw_gems_add(m, nw_gems_items[i].kind,
                            nw_gems_glass(m) ? NW_GEM_BODY : m->attrib.alpha ? NW_GEM_HIDE : NW_GEM_KEEP);
            }
        }
    }
}

static void nw_gems_object(int o, NwGemKind kind)
{
    const struct objtab_s *t = &ObjTab[o];
    if (!t->obj.special || !t->obj.scene) {
        return;
    }
    for (const struct nugobj_s *g = t->obj.scene->gobjs[t->obj.special->instance->objid]; g; g = g->next_gobj) {
        for (const struct nugeom_s *geom = g->geom; geom; geom = geom->next) {
            const struct numtl_s *m = geom->mtl;
            if (kind != NW_GEM_RELIC) {
                nw_gems_add(m, kind, NW_GEM_BODY);
            } else if (nw_gems_glass(m)) {
                nw_gems_add(m, NW_GEM_COLOUR, NW_GEM_BODY); /* the red stone */
            } else if (m->attrib.alpha) {
                nw_gems_add(m, kind, m->tid ? NW_GEM_KEEP : NW_GEM_HIDE); /* the shine stays */
            } else {
                nw_gems_add(m, kind, NW_GEM_METAL);
            }
        }
    }
}

void nw_gems_objects(void)
{
    for (int i = 0; i < 3; i++) {
        nw_gems_object(NW_GEMS_RELICS + i, NW_GEM_RELIC);
    }
    static const NwGemKind pedestals[] = { NW_GEM_CRYSTAL, NW_GEM_CLEAR,  NW_GEM_CLEAR,  NW_GEM_COLOUR,
                                           NW_GEM_COLOUR,  NW_GEM_COLOUR, NW_GEM_COLOUR, NW_GEM_COLOUR };
    for (int i = 0; i < (int)SDL_arraysize(pedestals); i++) {
        nw_gems_object(NW_GEMS_PEDESTALS + i, pedestals[i]);
    }
}
