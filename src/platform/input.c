#include "platform/input.h"

/* Trigger pressure (0-255) from which L2/R2 count as pressed. */
#define NW_INPUT_TRIGGER_PRESSED 0x40

static const struct {
    SDL_GamepadButton button;
    uint16_t bit;
} nw_input_buttons[] = {
    { SDL_GAMEPAD_BUTTON_SOUTH, NW_PAD_CROSS },
    { SDL_GAMEPAD_BUTTON_EAST, NW_PAD_CIRCLE },
    { SDL_GAMEPAD_BUTTON_WEST, NW_PAD_SQUARE },
    { SDL_GAMEPAD_BUTTON_NORTH, NW_PAD_TRIANGLE },
    { SDL_GAMEPAD_BUTTON_BACK, NW_PAD_SELECT },
    { SDL_GAMEPAD_BUTTON_START, NW_PAD_START },
    { SDL_GAMEPAD_BUTTON_LEFT_STICK, NW_PAD_L3 },
    { SDL_GAMEPAD_BUTTON_RIGHT_STICK, NW_PAD_R3 },
    { SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, NW_PAD_L1 },
    { SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, NW_PAD_R1 },
    { SDL_GAMEPAD_BUTTON_DPAD_UP, NW_PAD_UP },
    { SDL_GAMEPAD_BUTTON_DPAD_DOWN, NW_PAD_DOWN },
    { SDL_GAMEPAD_BUTTON_DPAD_LEFT, NW_PAD_LEFT },
    { SDL_GAMEPAD_BUTTON_DPAD_RIGHT, NW_PAD_RIGHT },
};

/* WASD is the left stick. */
static const struct {
    SDL_Scancode key;
    uint16_t bit;
} nw_input_keys[] = {
    { SDL_SCANCODE_UP, NW_PAD_UP },
    { SDL_SCANCODE_DOWN, NW_PAD_DOWN },
    { SDL_SCANCODE_LEFT, NW_PAD_LEFT },
    { SDL_SCANCODE_RIGHT, NW_PAD_RIGHT },
    { SDL_SCANCODE_SPACE, NW_PAD_CROSS },
    { SDL_SCANCODE_E, NW_PAD_SQUARE },
    { SDL_SCANCODE_J, NW_PAD_SQUARE },
    { SDL_SCANCODE_C, NW_PAD_CIRCLE },
    { SDL_SCANCODE_K, NW_PAD_CIRCLE },
    { SDL_SCANCODE_F, NW_PAD_TRIANGLE },
    { SDL_SCANCODE_I, NW_PAD_TRIANGLE },
    { SDL_SCANCODE_Q, NW_PAD_L1 },
    { SDL_SCANCODE_LSHIFT, NW_PAD_R1 },
    { SDL_SCANCODE_1, NW_PAD_L2 },
    { SDL_SCANCODE_3, NW_PAD_R2 },
    { SDL_SCANCODE_RETURN, NW_PAD_START },
    { SDL_SCANCODE_ESCAPE, NW_PAD_START },
    { SDL_SCANCODE_TAB, NW_PAD_SELECT },
};

static SDL_Gamepad *nw_input_pad;

/* -32768..32767 as 0x00..0xFF, 0 as 0x80. */
static uint8_t nw_input_axis(SDL_Gamepad *gamepad, SDL_GamepadAxis axis)
{
    return (uint8_t)((SDL_GetGamepadAxis(gamepad, axis) + 32768) >> 8);
}

/* 0..32767 as 0x00..0xFF. */
static uint8_t nw_input_trigger(SDL_Gamepad *gamepad, SDL_GamepadAxis axis)
{
    int v = SDL_GetGamepadAxis(gamepad, axis);
    return (uint8_t)(v > 0 ? v >> 7 : 0);
}

void nw_input_gamepad(SDL_Gamepad *gamepad, NwPadInput *in)
{
    in->buttons = 0;
    for (size_t i = 0; i < SDL_arraysize(nw_input_buttons); i++) {
        if (SDL_GetGamepadButton(gamepad, nw_input_buttons[i].button)) {
            in->buttons |= nw_input_buttons[i].bit;
        }
    }
    in->lx = nw_input_axis(gamepad, SDL_GAMEPAD_AXIS_LEFTX);
    in->ly = nw_input_axis(gamepad, SDL_GAMEPAD_AXIS_LEFTY);
    in->rx = nw_input_axis(gamepad, SDL_GAMEPAD_AXIS_RIGHTX);
    in->ry = nw_input_axis(gamepad, SDL_GAMEPAD_AXIS_RIGHTY);
    in->l2 = nw_input_trigger(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
    in->r2 = nw_input_trigger(gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
    if (in->l2 >= NW_INPUT_TRIGGER_PRESSED) {
        in->buttons |= NW_PAD_L2;
    }
    if (in->r2 >= NW_INPUT_TRIGGER_PRESSED) {
        in->buttons |= NW_PAD_R2;
    }
}

/* -1, 0 or 1 as 0x00, keep or 0xFF. */
static void nw_input_key_axis(int dir, uint8_t *axis)
{
    if (dir) {
        *axis = dir > 0 ? 0xFF : 0x00;
    }
}

void nw_input_keyboard(const bool *keys, NwPadInput *in)
{
    for (size_t i = 0; i < SDL_arraysize(nw_input_keys); i++) {
        if (keys[nw_input_keys[i].key]) {
            in->buttons |= nw_input_keys[i].bit;
        }
    }
    if (in->buttons & NW_PAD_L2) {
        in->l2 = 0xFF;
    }
    if (in->buttons & NW_PAD_R2) {
        in->r2 = 0xFF;
    }
    nw_input_key_axis(keys[SDL_SCANCODE_D] - keys[SDL_SCANCODE_A], &in->lx);
    nw_input_key_axis(keys[SDL_SCANCODE_S] - keys[SDL_SCANCODE_W], &in->ly);
}

static void nw_input_open_next(void)
{
    int count = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    for (int i = 0; ids && i < count && !nw_input_pad; i++) {
        nw_input_pad = SDL_OpenGamepad(ids[i]);
    }
    SDL_free(ids);
}

void nw_input_event(const SDL_Event *event)
{
    if (event->type == SDL_EVENT_GAMEPAD_ADDED && !nw_input_pad) {
        nw_input_pad = SDL_OpenGamepad(event->gdevice.which);
    } else if (event->type == SDL_EVENT_GAMEPAD_REMOVED && nw_input_pad &&
               SDL_GetGamepadID(nw_input_pad) == event->gdevice.which) {
        SDL_CloseGamepad(nw_input_pad);
        nw_input_pad = NULL;
        nw_input_open_next();
    }
}

void nw_input_poll(bool keyboard)
{
    NwPadInput in = { 0, 0x80, 0x80, 0x80, 0x80, 0, 0 };
    if (nw_input_pad) {
        nw_input_gamepad(nw_input_pad, &in);
    }
    if (keyboard) {
        nw_input_keyboard(SDL_GetKeyboardState(NULL), &in);
    }
    nw_pad_set(&in);
}

void nw_input_shutdown(void)
{
    if (nw_input_pad) {
        SDL_CloseGamepad(nw_input_pad);
        nw_input_pad = NULL;
    }
}
