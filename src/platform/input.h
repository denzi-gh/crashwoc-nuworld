/* Host input as the game's DualShock 2 (src/shim/pad.c). Main thread only. */
#ifndef NUWORLD_PLATFORM_INPUT_H
#define NUWORLD_PLATFORM_INPUT_H

#include <SDL3/SDL.h>

#include "platform/controls.h"
#include "shim/shim.h"

/* Opens the first gamepad that connects; when it goes, the next one still connected. */
void nw_input_event(const SDL_Event *event);
/* Samples the gamepad and, unless keyboard is false, the keyboard, and hands both to the pad. */
void nw_input_poll(bool keyboard);
void nw_input_shutdown(void);

typedef enum NwInputListen {
    NW_INPUT_LISTEN_OFF,
    NW_INPUT_LISTEN_PAD,  /* a gamepad button or trigger */
    NW_INPUT_LISTEN_KEYS, /* a key */
} NwInputListen;

/* The controls the poll maps through. */
void nw_input_set_controls(const NwControls *c);
/* The next press is heard instead of played, the game's pad idles meanwhile; Escape cancels. */
void nw_input_listen(NwInputListen what);
/* -1 while listening or not, else (once) what was heard: a source, 0 for Escape. */
int nw_input_heard(void);
/* The open gamepad's name, NULL without one. */
const char *nw_input_pad_name(void);
/* Its label for a source: the face buttons' (Cross, A, ...), else nw_controls_pad_label's. */
const char *nw_input_pad_label(uint16_t source);

/* One gamepad's state as the pad, through c. */
void nw_input_gamepad(SDL_Gamepad *gamepad, const NwControls *c, NwPadInput *in);
/* Adds the held keys (SDL_GetKeyboardState) to in, through c; a stick's keys replace the axis they push. */
void nw_input_keyboard(const bool *keys, const NwControls *c, NwPadInput *in);

#endif
