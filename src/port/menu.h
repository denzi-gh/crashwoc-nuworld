/* Native menu actions, autosave consent, previous versions and save notifications. */
#ifndef NUWORLD_PORT_MENU_H
#define NUWORLD_PORT_MENU_H

#include <stdbool.h>
#include <stdint.h>

struct cursor_s;

#define NW_MENU_AUTOSAVE_NEW 36
#define NW_MENU_AUTOSAVE_ROLLBACK 37
#define NW_MENU_AUTOSAVE_OPTIONS 38
#define NW_MENU_SAVE_PREVIOUS 39

/* Before retail's ProcMenu: the buttons pressed this frame, 0 while retail ignores the pad. */
void nw_menu_buttons(uint32_t pressed);
/* Called after retail's navigation and input guards; consumes native actions and dialogs. */
bool nw_menu_quit(struct cursor_s *cursor, int confirm, int cancel);
/* After loading, ask before overwriting a newer autosave. */
void nw_menu_autosave_loaded(struct cursor_s *cursor);
void nw_menu_autosave_notice(void);

#endif
