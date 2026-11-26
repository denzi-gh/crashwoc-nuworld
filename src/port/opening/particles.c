/* [prototype] The opening's particles (src/port/opening/internal.h): the TT block's sparkles drawn in the frame the
   cut gives them. The cuts' other effects are let off where the cuts put them, as the game does: the satellite's
   exhaust, the logo's and the title's exhaust ("logosmoke"); Oxide's in the title is INTROR.PTL's "exhaustOX", let off
   by flyers.c. */
#include "port/opening/internal.h"

/* The particle frames: where an effect's particles are drawn, as the cut says with its locators of the port's function
   "PtlFrame:<effect>" (tools: logos_cut.py, title_anchor_cut.py), shown from frame to frame as any locator (its
   visible channel, or its flag without an anim). param1 the frame: a camera of the cut (0 up), or none (-1: drawn
   where it is); param2 1 draws it only while the logo shows.
   The TT block moves across the camera shown as the original's camera saw it pass; its sparkles, let off in the
   original's places (logos1.cut's), are drawn through LOGOSR.CUT's camera 2 (logos1's camera itself): that camera's
   inverse times the camera shown, the block's own carrying, so they stay on it. Their effect ("sparkleL") emits at any
   distance: its emitters are not where it shows. The title's exhaust has a PtlFrame of -1, param2 1: hidden with the
   logo (SELECT's background, NEW GAME). */
#define NW_PTL_FRAME "PtlFrame:"
#define NW_LOCATOR_FUNC 0x2  /* gcutscn.c's NUGCUTLOCATOR_FUNC */
#define NW_LOCATOR_PORT 0x80 /* a type of the port's: the game neither looks it up nor runs it */

static struct numtx_s nw_ptl_frame[4];

static bool nw_ptl_frame_type(const struct NUGCUTLOCATORTYPE_s *type)
{
    return type->name && !SDL_strncmp(type->name, NW_PTL_FRAME, sizeof NW_PTL_FRAME - 1);
}

void nw_opening_cut_fixup(struct NUGCUTSCENE_s *def)
{
    struct NUGCUTLOCATORSYS_s *sys = def ? def->locatordef : NULL;
    for (unsigned int i = 0; sys && i < sys->ntypes; i++) {
        struct NUGCUTLOCATORTYPE_s *type = &sys->types[i];
        if ((type->flags & NW_LOCATOR_FUNC) && nw_ptl_frame_type(type)) {
            type->flags = NW_LOCATOR_PORT;
            type->ix = 0;
        }
    }
}

static bool nw_cut_camera(struct NUGCUTSCENE_s *cut, unsigned int cam, float time, struct numtx_s *m)
{
    struct NUGCUTCAMSYS_s *def = cut ? cut->camdef : NULL;
    if (!def || cam >= def->ncutcams || !(def->cutcams[cam].flags & 1) || !def->camanim ||
        def->cutcams[cam].anim_ix >= def->camanim->nnodes) {
        return false;
    }
    struct nuanimdata2_s *anim = def->camanim;
    int ix = def->cutcams[cam].anim_ix;
    struct nuanimtime_s atime;
    NuAnimData2CalcTime(anim, time, &atime);
    NuAnimCurve2SetApplyToMatrix(&anim->curves[anim->ncurves * ix], &anim->curveflags[anim->ncurves * ix],
                                 anim->curvesetflags[ix], &atime, m);
    return true;
}

/* Each frame, at the cut camera's update: the frames the cut's PtlFrame locators show. */
void nw_opening_hold_particles(void)
{
    for (int k = 0; k < 4; k++) {
        nw_rndr_particle_own[k] = NULL;
        nw_rndr_particle_own_xform[k] = NULL;
        nw_rndr_particle_own_hidden[k] = false;
    }
    struct NUGCUTSCENE_s *cut = CutScene[cutworldix];
    struct NUGCUTLOCATORSYS_s *sys = cut ? cut->locatordef : NULL;
    if (!nw_opening_active() || !sys) {
        return;
    }
    int n = 0;
    for (unsigned int i = 0; i < sys->count && n < 4; i++) {
        struct NUGCUTLOCATOR_s *loc = &sys->locators[i];
        struct NUGCUTLOCATORTYPE_s *type = loc->locator_type < sys->ntypes ? &sys->types[loc->locator_type] : NULL;
        if (!type || type->flags != NW_LOCATOR_PORT || !nw_ptl_frame_type(type)) {
            continue;
        }
        struct nuanimtime_s atime = { 0 };
        if (loc->anim) {
            NuAnimData2CalcTime(loc->anim, nw_op.time, &atime);
        }
        int camera = (int)loc->param1;
        if (!NuGCutLocatorIsVisble(loc, nw_op.time, &atime, NULL)) {
            continue;
        }
        int ix = LookupDebrisEffect(type->name + sizeof NW_PTL_FRAME - 1);
        struct numtx_s cam, inv;
        if (ix <= 0 || ix >= 128 || !debtab[ix] || !debtab[ix]->DmaDebTypePointer) {
            continue;
        }
        if (loc->param2 == 1.0f && !nw_opening_logo_shown()) {
            nw_rndr_particle_own[n] = debtab[ix]->DmaDebTypePointer;
            nw_rndr_particle_own_hidden[n++] = true;
            continue;
        }
        if (camera >= 0 && nw_cut_camera(cut, (unsigned int)camera, nw_op.time, &cam)) {
            nw_inverse_rigid(&inv, &cam);
            nw_mul(&nw_ptl_frame[n], &inv, &nw_op.real);
        } else {
            continue;
        }
        nw_rndr_particle_own[n] = debtab[ix]->DmaDebTypePointer;
        nw_rndr_particle_own_xform[n] = &nw_ptl_frame[n]._00;
        n++;
    }
}
