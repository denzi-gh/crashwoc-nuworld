#ifndef NUWORLD_FRONTEND_PROFILES_H
#define NUWORLD_FRONTEND_PROFILES_H

#include <stdbool.h>

#include <SDL3/SDL.h>

#include "gameplay/gameplay.h"

#ifdef __cplusplus
extern "C" {
#endif

void nw_profiles_start(SDL_Window *window, const char *root, NwMovement *movement);
void nw_profiles_stop(void);
void nw_profiles_event(const SDL_Event *e);
bool nw_profiles_copy(unsigned parts);

#ifdef __cplusplus
}
#endif

#endif
