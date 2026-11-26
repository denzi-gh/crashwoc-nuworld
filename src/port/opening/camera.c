/* [prototype] The opening's camera (src/port/opening/internal.h): the camera shown (LOGOSR.CUT's own, made in the
   station's world, tools: bake_cut.py; the station cut's once the title shows), and the cut's objects as the opening
   draws them: the film earth's turn on the real one, the Universal text's ring round it, the title's logo held where
   its cut's camera showed it. */
#include "port/opening/internal.h"

/* The logo's model (the logos cut's "title" character), kept when the cut places it. */
struct NUHGOBJ_s *nw_title_hobj;

/* The camera shown: the logos cut's own, then (the title) station.cut's. */
void nw_opening_real(const struct numtx_s *cutcam)
{
    nw_opening_sequence_sync();
    if (!nw_opening_sequence_camera(&nw_op.real)) nw_op.real = *cutcam;
}

/* Apply the presentation's material assignments, preserving its animated ring parameters. */
static void nw_opening_object_fx(struct NUGCUTRIGID_s *rigid, const float *direct, const float *ring)
{
    struct nuinstance_s *in = rigid->special.special ? rigid->special.special->instance : NULL;
    struct nugscn_s *sc = nw_op.logos_scene;
    if (!in || !sc || in->objid < 0 || in->objid >= sc->numgobj) return;
    for (struct nugobj_s *g = sc->gobjs[in->objid]; g; g = g->next_gobj) {
        for (struct nugeom_s *geom = g->geom; geom; geom = geom->next) {
            if (!geom->mtl) continue;
            if (direct) { nw_rndr_mtl_fx(geom->mtl, direct); continue; }
            int slot = -1;
            for (int i = 0; i < sc->nummtl; i++) if (geom->mtl == sc->mtls[i]) { slot = i; break; }
            const float *recipe = nw_opening_scene_look(rigid->name, slot);
            if (!recipe) continue;
            float fx[NW_MTL_FX_LEN];
            SDL_memcpy(fx, recipe, sizeof(fx));
            if (ring) SDL_memcpy(fx + 12, ring, sizeof(float) * 4);
            nw_rndr_mtl_fx(geom->mtl, fx);
        }
    }
}

/* An object flattened where it is: not seen. */
static void nw_flatten(struct numtx_s *m)
{
    m->_00 = m->_01 = m->_02 = m->_10 = m->_11 = m->_12 = m->_20 = m->_21 = m->_22 = 0;
}

void NuGCutRigidCalcMtx(struct NUGCUTRIGID_s *rigid, float time, struct numtx_s *mtx)
{
    NuGCutRigidCalcMtx__retail(rigid, time, mtx);
    if (!(nw_op.on > 0 && nw_assets_only) && rigid->name && SDL_strncmp(rigid->name, "fadePlane", 9) == 0 && cutmovie == 0) {
        /* the cards' fade steps (black, each its own opacity) as wide as the picture */
        float sc = nw_opening_wide() * 1.1f;
        for (int i = 0; i < 3; i++) {
            (&mtx->_00)[i] *= sc;
            (&mtx->_10)[i] *= sc;
            (&mtx->_20)[i] *= sc;
        }
    }
    if (nw_intro.on && cutmovie == 1 && cutworldix == 0 && nw_opening_station_part(rigid->name)) {
        /* the station goes on from the pose the title left it in: its intro cut's own motion since its first frame
           (STATIONR.CUT, on the game's clock: the cut holds its start until its audio plays) laid on that pose */
        const struct numtx_s *from = nw_intro_pose(rigid->name);
        if (from) {
            struct numtx_s m0, inv;
            NuGCutRigidCalcMtx__retail(rigid, 1, &m0);
            NuGCutRigidCalcMtx__retail(rigid, 1 + NW_CUT_RATE * (float)nw_intro.frames, mtx);
            nw_affine_inverse(&inv, &m0);
            nw_mul(mtx, mtx, &inv);
            nw_mul(mtx, mtx, from);
        }
        return;
    }
    if (!nw_opening_active() || nw_opening_station_part(rigid->name)) {
        return; /* the title's station: its own cut's (STATIONT.CUT, its rings in a loop) */
    }
    if (nw_op.switched) {
        nw_flatten(mtx); /* the logos' objects gone with their scene */
        return;
    }
    if (cutworldix != 0) {
        return;
    }
    NwOpeningPart part = nw_opening_part(rigid->name);
    if (nw_op.swapped && part == NW_OP_EARTH && *nw_op.signals.earth_mix >= 1) {
        /* earth.mix done: the station's earth stands there now (LOGOSR.CUT's "stationEarth", in its own place). */
        nw_op.film_a = 0;
        nw_op.film_mix = 1;
        nw_flatten(mtx);
        return;
    }
    switch (part) {
    case NW_OP_EARTH: {
        /* The earth is the station's: the logos' globe is not drawn. It gives the film earth over the station
           earth (the renderer's: as earth.mix rises it fades from it, the station earth carried with it by
           LOGOSR.CUT's "stationEarth", so the earth changes its look, not its place) its turn and whether it shows: the cut's own turn of it (tools:
           globe_carry_cut.py), the station earth's map frame carried onto the globe's now: from the film's pose in
           Universal's framing into station.cut's, spinning on its poles to a stop at the swap. */
        nw_op.film_seen = rigid->special.special && rigid->special.special->instance &&
                          (rigid->special.special->instance->flagword & 0x1);
        struct numtx_s carry;
        nw_rot_only(&carry, mtx);
        nw_flatten(mtx);
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                nw_op.film_rot[i][j] = (&carry._00)[4 * i + j];
            }
        }
        nw_op.film_mix = *nw_op.signals.earth_mix; /* earth.ats */
        nw_op.film_a = nw_op.film_seen ? 1 - nw_op.film_mix : 0;
        return;
    }
    case NW_OP_UIS: {
        /* The film's UNIVERSAL (its place, turn and size the cut's): its letters bent on a ring round the globe's
           vertical, out from behind its limb, the ring widening and the run straightening into the flat hold at the
           front; then on round behind it the same way. Never through the globe: the ring's inside stays off its
           surface. In the text's own units (the globe's radius logos_r / uis_scale): it stands uis_r0 in front of the
           axis, the ring's front starts and ends uis_back out. */
        const NwOpeningParams *p = &nw_opening;
        float in = *nw_op.signals.uis_in, out = *nw_op.signals.uis_out; /* universal.ats: in from behind it, out */
        float r0 = p->uis_r0, rb = p->uis_back * p->logos_r / p->uis_scale;
        float open = nw_smooth(in, 0, 0.8f) * (1 - nw_smooth(out, 0.2f, 1));       /* the ring's width */
        float bent = SDL_max(1 - nw_smooth(in, 0.5f, 1), nw_smooth(out, 0, 0.5f)); /* how far bent round */
        float run = SDL_min(nw_smooth(in, 0, 0.45f), 1 - nw_smooth(out, 0.4f, 1)); /* its run, unsqueezed */
        float d = rb + (r0 - rb) * open;
        float ring[4] = { d, bent / d, r0, p->uis_squeeze + (1 - p->uis_squeeze) * run };
        nw_opening_object_fx(rigid, NULL, ring);
        nw_op.uis_flat = 1 - bent;
        nw_op.uis_world = *mtx; /* the film earth's shader casts its shadow */
        nw_op.uis_seen = rigid->special.special && rigid->special.special->instance &&
                         (rigid->special.special->instance->flagword & 0x1);
        return;
    }
    case NW_OP_ISLAND:
        if (nw_opening.island_fade[0] != nw_opening.island_fade[1]) {
            /* the Traveller's Tales block's rock column, down into the logos' black, fades out (its local y): against
               the nebula it would end in a hard dark column */
            float fx[NW_MTL_FX_LEN] = { NW_MTL_FX_FADE, 0, 0, 0, nw_opening.island_fade[0], nw_opening.island_fade[1] };
            nw_opening_object_fx(rigid, fx, NULL);
        }
        return;
    default:
        return;
    }
}

void NuGCutCharAnimProcess(struct NUGCUTCHAR_s *desc, float time, struct numtx_s *mtx, int *is_visible,
                           unsigned int *animix, float *animrate, float *blendframes)
{
    NuGCutCharAnimProcess__retail(desc, time, mtx, is_visible, animix, animrate, blendframes);
    if (!nw_opening_active() || !is_visible || !*is_visible) {
        return;
    }
    if (cutworldix == 0 && desc->char_anim) {
        /* a root anim has no scale: the file matrix's kept (the TT block's character, made larger) */
        for (int i = 0; i < 3; i++) {
            float l = nw_len3(&desc->mtx._00 + 4 * i);
            if (SDL_fabsf(l - 1) > 1e-3f) {
                for (int j = 0; j < 3; j++) {
                    (&mtx->_00)[4 * i + j] *= l;
                }
            }
        }
    }
    NwOpeningPart part = nw_opening_part(desc->name);
    if (part == NW_OP_LOGO) {
        nw_title_hobj = (struct NUHGOBJ_s *)desc->character;
    }
    if (part == NW_OP_LOGO && !nw_opening_logo_shown()) {
        *is_visible = 0;
    }
}

/* Whether the title logo is drawn: not with SELECT's background alone, nor once NEW GAME has it dissolving out (the
   frame kept with it fades: live, it is no longer drawn). */
bool nw_opening_logo_shown(void)
{
    return !nw_opening_background_only() &&
           !(nw_go.phase == NW_GO_LEAVING && nw_go.frame > (int)(nw_opening.go_text + nw_opening.go_wait)) &&
           !(nw_go.phase > NW_GO_LEAVING);
}
