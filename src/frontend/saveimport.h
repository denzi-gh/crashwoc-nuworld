/* F1's Saves > Import from PS2 card. Main thread. */
#ifndef NUWORLD_FRONTEND_SAVEIMPORT_H
#define NUWORLD_FRONTEND_SAVEIMPORT_H

#include <SDL3/SDL.h>

void nw_saveimport_start(SDL_Window *window, const char *dir);
void nw_saveimport_stop(void);

#endif
