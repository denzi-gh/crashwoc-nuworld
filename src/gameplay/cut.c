#include "nu_types.h"
#include "gamelib/gcutscn.h"
#include "game/cut.retail.h"
#include "gameplay/bosscut.h"
#include "gameplay/titlemenu.h"
#include "port/cutstart.h"
#include "port/level.h"
#include "port/silhouette.h"

extern u16 new_lev_flags;
extern s32 cutmovie, GameMode;

void StartCutMovie(void)
{
    if (cutmovie == 0) {
        GameMode = 0;
    }
    bool menu = nw_title_menu_starting();
    nw_cut_start_movie();
    if (menu) {
        nw_title_menu_started();
    }
}

void GetSpaceCut(s32 last, s32 next)
{
    u16 flags = new_lev_flags;
    new_lev_flags |= nw_boss_cut_flags(last, next);
    if (!nw_cutscene_route(next)) {
        GetSpaceCut__retail(last, next);
    }
    new_lev_flags = flags;
}

void SetCutMovieRate(void)
{
    SetCutMovieRate__retail();
    nw_title_menu_rate();
}

void AppCutSceneCharacterRender(struct instNUGCUTSCENE_s *icutscene, struct NUGCUTSCENE_s *cutscene,
                                struct instNUGCUTCHAR_s *icutchar, struct NUGCUTCHAR_s *cutchar, f32 current_frame)
{
    if (!nw_title_menu_render(icutscene, cutscene, icutchar, cutchar, current_frame) &&
        !nw_silhouette_render(icutscene, cutscene, icutchar, cutchar, current_frame)) {
        AppCutSceneCharacterRender__retail(icutscene, cutscene, icutchar, cutchar, current_frame);
    }
}
