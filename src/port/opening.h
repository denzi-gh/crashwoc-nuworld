/* Frontend presentation and asset selection. */
#ifndef NUWORLD_PORT_OPENING_H
#define NUWORLD_PORT_OPENING_H

#include <stdbool.h>

/* A valid profile selects the frontend presentation and uses the game's own menu. */
bool nw_opening_enabled(void);

/* The title stands on the station: its menu shows two rows at a time and scrolls. */
bool nw_opening_title(void);
/* SELECT showed the background alone: the title's logo and menu are not drawn until a direction is pressed. */
bool nw_opening_background_only(void);
/* Whether the skip hint shows (port/menu.c): past the Konami card, before the title, once TRIANGLE or START was
   pressed; pressed again while it shows, the logos are skipped. */
bool nw_opening_skip_hint(void);
/* NEW GAME from the title runs on into the intro's first shot as a film's: no notices over it (port/menu.c). */
bool nw_opening_quiet(void);
/* A cut's port locator functions (PtlFrame:<effect>, the opening's particle frames) taken from the game's, before it
   looks its locator types up: the game neither finds nor runs them, the opening reads them (gameplay/gcutscn.c). */
struct NUGCUTSCENE_s;
void nw_opening_cut_fixup(struct NUGCUTSCENE_s *def);
/* Resolve a profile's replacement or return the original disc path. */
char *nw_opening_data(char *path);

#endif
