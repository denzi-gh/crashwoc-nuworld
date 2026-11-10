/* Native menu actions, autosave consent, previous versions and save notifications. */
#ifndef NUWORLD_PORT_MENU_H
#define NUWORLD_PORT_MENU_H

#include <stdbool.h>

struct cursor_s;

#define NW_MENU_AUTOSAVE_NEW 36
#define NW_MENU_AUTOSAVE_ROLLBACK 37
#define NW_MENU_AUTOSAVE_OPTIONS 38
#define NW_MENU_SAVE_PREVIOUS 39

/* Called after retail's navigation and input guards; consumes native actions and dialogs. */
bool nw_menu_quit(struct cursor_s *cursor, int confirm, int cancel);
/* After loading, ask before overwriting a newer autosave. */
void nw_menu_autosave_loaded(struct cursor_s *cursor);
void nw_menu_autosave_notice(void);

#endif
