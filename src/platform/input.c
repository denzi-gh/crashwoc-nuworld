#include "platform/input.h"

/* Trigger pressure (0-255) from which a trigger presses its button. */
#define NW_INPUT_TRIGGER_PRESSED 0x40

/* By target (platform/controls.h). */
static const uint16_t nw_input_bits[NW_CONTROL_BUTTONS] = {
    NW_PAD_CROSS, NW_PAD_CIRCLE, NW_PAD_SQUARE, NW_PAD_TRIANGLE, NW_PAD_L1,     NW_PAD_R1,   NW_PAD_L2,   NW_PAD_R2,
    NW_PAD_L3,    NW_PAD_R3,     NW_PAD_SELECT, NW_PAD_START,    NW_PAD_UP,     NW_PAD_DOWN, NW_PAD_LEFT, NW_PAD_RIGHT,
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

/* A trigger's travel that a listen hears. */
#define NW_INPUT_TRIGGER_HEARD 16384

static SDL_Gamepad *nw_input_pad;
static NwControls nw_input_controls;
static NwInputListen nw_input_listening;
static int nw_input_heard_source = -1;

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

/* A source's pressure: a button's 0x00 or 0xFF, a trigger's travel. */
static uint8_t nw_input_source(SDL_Gamepad *gamepad, uint16_t source)
{
    if (source >= NW_CONTROL_TRIGGER) {
        return nw_input_trigger(gamepad, (SDL_GamepadAxis)(source - NW_CONTROL_TRIGGER));
    }
    return source && SDL_GetGamepadButton(gamepad, (SDL_GamepadButton)(source - 1)) ? 0xFF : 0x00;
}

void nw_input_gamepad(SDL_Gamepad *gamepad, const NwControls *c, NwPadInput *in)
{
    in->buttons = 0;
    for (int i = 0; i < NW_CONTROL_BUTTONS; i++) {
        uint8_t most = 0;
        for (int j = 0; j < NW_CONTROL_BINDS; j++) {
            uint8_t v = nw_input_source(gamepad, c->pad[i][j]);
            most = v > most ? v : most;
        }
        if (most >= NW_INPUT_TRIGGER_PRESSED) {
            in->buttons |= nw_input_bits[i];
        }
        if (nw_input_bits[i] == NW_PAD_L2) {
            in->l2 = most;
        } else if (nw_input_bits[i] == NW_PAD_R2) {
            in->r2 = most;
        }
    }
    in->lx = nw_input_axis(gamepad, SDL_GAMEPAD_AXIS_LEFTX);
    in->ly = nw_input_axis(gamepad, SDL_GAMEPAD_AXIS_LEFTY);
    in->rx = nw_input_axis(gamepad, SDL_GAMEPAD_AXIS_RIGHTX);
    in->ry = nw_input_axis(gamepad, SDL_GAMEPAD_AXIS_RIGHTY);
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

void nw_input_set_controls(const NwControls *c)
{
    nw_input_controls = *c;
}

void nw_input_listen(NwInputListen what)
{
    nw_input_listening = what;
    nw_input_heard_source = -1;
}

int nw_input_heard(void)
{
    int heard = nw_input_heard_source;
    nw_input_heard_source = -1;
    return heard;
}

const char *nw_input_pad_name(void)
{
    return nw_input_pad ? SDL_GetGamepadName(nw_input_pad) : NULL;
}

const char *nw_input_pad_label(uint16_t source)
{
    static const char *const faces[] = { NULL, "A", "B", "X", "Y", "Cross", "Circle", "Square", "Triangle" };
    if (nw_input_pad && source >= 1 && source <= 4) {
        SDL_GamepadButtonLabel label = SDL_GetGamepadButtonLabel(nw_input_pad, (SDL_GamepadButton)(source - 1));
        if (label > SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN && (size_t)label < SDL_arraysize(faces)) {
            return faces[label];
        }
    }
    return nw_controls_pad_label(source);
}

/* What a listen hears in event, -1 nothing. */
static int nw_input_hear(const SDL_Event *event)
{
    SDL_JoystickID pad = nw_input_pad ? SDL_GetGamepadID(nw_input_pad) : 0;
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.scancode == SDL_SCANCODE_ESCAPE) {
        return 0;
    }
    if (event->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && event->gbutton.which == pad) {
        return 1 + event->gbutton.button;
    }
    if (event->type == SDL_EVENT_GAMEPAD_AXIS_MOTION && event->gaxis.which == pad &&
        (event->gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || event->gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) &&
        event->gaxis.value >= NW_INPUT_TRIGGER_HEARD) {
        return NW_CONTROL_TRIGGER + event->gaxis.axis;
    }
    return -1;
}

void nw_input_event(const SDL_Event *event)
{
    int heard = nw_input_listening ? nw_input_hear(event) : -1;
    if (heard >= 0) {
        nw_input_heard_source = heard;
        nw_input_listening = NW_INPUT_LISTEN_OFF;
    }
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
    if (nw_input_pad && !nw_input_listening) {
        nw_input_gamepad(nw_input_pad, &nw_input_controls, &in);
    }
    if (keyboard && !nw_input_listening) {
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
