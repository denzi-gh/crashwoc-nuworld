/* [game] title_menu: NuWorld's title instead of the PS2's logos and title cut. */
#ifndef NUWORLD_GAMEPLAY_TITLEMENU_H
#define NUWORLD_GAMEPLAY_TITLEMENU_H

#include <stdbool.h>

struct NUGCUTSCENE_s;
struct NUGCUTCHAR_s;
struct instNUGCUTSCENE_s;
struct instNUGCUTCHAR_s;

/* After NuGCutSceneFixUp: adds the mark to a title cut. */
void nw_title_menu_fixup(struct NUGCUTSCENE_s *def);

/* Instead of AppCutSceneCharacterRender; false to draw as retail. */
bool nw_title_menu_render(struct instNUGCUTSCENE_s *scene, struct NUGCUTSCENE_s *def, struct instNUGCUTCHAR_s *ichar,
                          struct NUGCUTCHAR_s *desc, float f);

/* Around StartCutMovie: whether the boot's logos cut starts, then its jump. */
bool nw_title_menu_starting(void);
void nw_title_menu_started(void);

/* After SetCutMovieRate (the cut's sound plays). */
void nw_title_menu_rate(void);

/* The title's particles play no sounds. */
bool nw_title_menu_quiet(void);

#endif
