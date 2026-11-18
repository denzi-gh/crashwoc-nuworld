/* F1's Help: the data folder, a summary of the PC for bug reports and the GitHub issue forms. Main thread. */
#ifndef NUWORLD_FRONTEND_HELP_H
#define NUWORLD_FRONTEND_HELP_H

#include <SDL3/SDL.h>

/* root: the data folder (copied), or NULL. */
void nw_help_start(SDL_Window *window, const char *root);
void nw_help_stop(void);

#endif
