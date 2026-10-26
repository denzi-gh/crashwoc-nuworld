/* Host input as the game's DualShock 2 (src/shim/pad.c). Main thread only. */
#ifndef NUWORLD_PLATFORM_INPUT_H
#define NUWORLD_PLATFORM_INPUT_H

#include <SDL3/SDL.h>

#include "shim/shim.h"

/* Opens the first gamepad that connects; when it goes, the next one still connected. */
void nw_input_event(const SDL_Event *event);
/* Samples the gamepad and, unless keyboard is false, the keyboard, and hands both to the pad. */
void nw_input_poll(bool keyboard);
void nw_input_shutdown(void);

/* One gamepad's state as the pad: buttons by position (south is cross). */
void nw_input_gamepad(SDL_Gamepad *gamepad, NwPadInput *in);
/* Adds the held keys (SDL_GetKeyboardState) to in; WASD replaces the left stick's axis it pushes. */
void nw_input_keyboard(const bool *keys, NwPadInput *in);

#endif
