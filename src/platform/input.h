/* Host input as the game's DualShock 2 (src/shim/pad.c). Main thread only. */
#ifndef NUWORLD_PLATFORM_INPUT_H
#define NUWORLD_PLATFORM_INPUT_H

#include <SDL3/SDL.h>

#include "shim/shim.h"

/* Opens the first gamepad that connects; when it goes, the next one still connected. */
void nw_input_event(const SDL_Event *event);
/* Samples the input and hands it to the pad. */
void nw_input_poll(void);
void nw_input_shutdown(void);

/* One gamepad's state as the pad: buttons by position (south is cross). */
void nw_input_gamepad(SDL_Gamepad *gamepad, NwPadInput *in);

#endif
