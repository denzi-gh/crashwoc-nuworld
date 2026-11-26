#include "nu_types.h"
#include "gamelib/gcutscn.h"
#include "gamelib/gcutscn.retail.h"
#include "gameplay/titlemenu.h"
#include "port/opening.h"

void NuGCutSceneFixUp(struct NUGCUTSCENE_s *def, void *gscene, struct NUTRIGGERSYS_s *triggers)
{
    nw_opening_cut_fixup(def);
    NuGCutSceneFixUp__retail(def, gscene, triggers);
    nw_title_menu_fixup(def);
}

/* A cut that ends starts the one chained to it and marks it updated for this frame, without updating it: that frame
   is drawn with the new cut's objects as they stand and the old cut's camera (retail too: the intro's corridor seen
   from the station's camera for one frame). The new cut is brought up to its start here, as a paused update does
   (its cameras, triggers and rigids at its time, the time not advanced). */
void instNuGCutSceneUpdate(struct instNUGCUTSCENE_s *scene, int paused)
{
    struct instNUGCUTSCENE_s *chain = scene->chain;
    instNuGCutSceneUpdate__retail(scene, paused);
    if (chain && !scene->chain && (chain->flags & NUGCUTSCENE_STARTED)) {
        instNuGCutSceneUpdate__retail(chain, 1);
    }
}
