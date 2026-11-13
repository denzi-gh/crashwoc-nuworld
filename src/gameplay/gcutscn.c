/* Wraps of gamelib/gcutscn: title cuts get the NuWorld mark (gameplay/titlemenu.c). */
#include "nu_types.h"
#include "gamelib/gcutscn.h"
#include "gamelib/gcutscn.retail.h"
#include "gameplay/titlemenu.h"

void NuGCutSceneFixUp(struct NUGCUTSCENE_s *def, void *gscene, struct NUTRIGGERSYS_s *triggers)
{
    NuGCutSceneFixUp__retail(def, gscene, triggers);
    nw_title_menu_fixup(def);
}
