#ifndef NUWORLD_PLATFORM_DROPTARGET_H
#define NUWORLD_PLATFORM_DROPTARGET_H

#include <stdbool.h>

#include <SDL3/SDL.h>

typedef bool (*NwDropAccept)(const char *path);

bool nw_droptarget_start(SDL_Window *window, NwDropAccept accept, Uint32 event, Sint32 code);
void nw_droptarget_stop(void);
char *nw_droptarget_clipboard(SDL_Window *window, NwDropAccept accept);

#endif
