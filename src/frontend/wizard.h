/* First-run wizard: imports the user's disc from a file dialog, a file dropped onto the window or the
   <root>import/ folder (for the Steam Deck, where Game Mode has neither). Draws with nw_ui. */
#ifndef NUWORLD_FRONTEND_WIZARD_H
#define NUWORLD_FRONTEND_WIZARD_H

#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum NwWizardResult {
    NW_WIZARD_OPEN,   /* keep drawing */
    NW_WIZARD_CLOSED, /* a disc is imported, go on */
    NW_WIZARD_QUIT,
} NwWizardResult;

typedef struct NwWizard NwWizard;

/* root is the data folder (with its import/ folder already created). */
NwWizard *nw_wizard_create(SDL_Window *window, const char *root);

/* Cancels a running import and waits for it. */
void nw_wizard_destroy(NwWizard *w);

/* Handles file drops and file dialog results. */
void nw_wizard_event(NwWizard *w, const SDL_Event *e);

/* Draws one frame between nw_ui_begin and nw_ui_end. */
NwWizardResult nw_wizard_draw(NwWizard *w);

#ifdef __cplusplus
}
#endif

#endif
