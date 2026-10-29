#include "platform/input.h"

/* By target (platform/controls.h). */
static const uint16_t nw_input_bits[NW_CONTROL_BUTTONS] = {
    NW_PAD_CROSS, NW_PAD_CIRCLE, NW_PAD_SQUARE, NW_PAD_TRIANGLE, NW_PAD_L1,     NW_PAD_R1,   NW_PAD_L2,   NW_PAD_R2,
    NW_PAD_L3,    NW_PAD_R3,     NW_PAD_SELECT, NW_PAD_START,    NW_PAD_UP,     NW_PAD_DOWN, NW_PAD_LEFT, NW_PAD_RIGHT,
};

/* A trigger's travel that a listen hears. */
#define NW_INPUT_TRIGGER_HEARD 16384
/* How long one rumble call lasts; the poll renews it while the motors run. */
#define NW_INPUT_RUMBLE_MS 250

static SDL_Gamepad *nw_input_pad;
static NwControls nw_input_controls;
static NwInputListen nw_input_listening;
static int nw_input_heard_source = -1;
static NwPadInput nw_input_sent = { 0, 0x80, 0x80, 0x80, 0x80, 0, 0 };
static struct {
    uint16_t low, high;
    uint64_t at;
} nw_input_rumbling;

/* -32768..32767 as 0x00..0xFF, 0 as 0x80. */
static uint8_t nw_input_axis(int v)
{
    return (uint8_t)((v + 32768) >> 8);
}

/* A stick from its x axis on, with deadzone percent of its reach centred and the rest stretched over all of it. */
static void nw_input_stick(SDL_Gamepad *gamepad, SDL_GamepadAxis x, int deadzone, uint8_t *px, uint8_t *py)
{
    int ix = SDL_GetGamepadAxis(gamepad, x), iy = SDL_GetGamepadAxis(gamepad, (SDL_GamepadAxis)(x + 1));
    if (deadzone > 0) {
        float fx = (float)ix / 32767.0f, fy = (float)iy / 32767.0f, r = SDL_sqrtf(fx * fx + fy * fy);
        float dz = (float)deadzone / 100.0f, k = r > dz ? (r - dz) / (1.0f - dz) / r : 0.0f;
        ix = (int)SDL_lroundf(SDL_clamp(fx * k, -1.0f, 1.0f) * 32767.0f);
        iy = (int)SDL_lroundf(SDL_clamp(fy * k, -1.0f, 1.0f) * 32767.0f);
    }
    *px = nw_input_axis(ix);
    *py = nw_input_axis(iy);
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
    int press = (c->trigger_press * 0xFF + 50) / 100;
    in->buttons = 0;
    for (int i = 0; i < NW_CONTROL_BUTTONS; i++) {
        uint8_t most = 0;
        for (int j = 0; j < NW_CONTROL_BINDS; j++) {
            uint8_t v = nw_input_source(gamepad, c->pad[i][j]);
            most = v > most ? v : most;
        }
        if (most && most >= press) {
            in->buttons |= nw_input_bits[i];
        }
        if (nw_input_bits[i] == NW_PAD_L2) {
            in->l2 = most;
        } else if (nw_input_bits[i] == NW_PAD_R2) {
            in->r2 = most;
        }
    }
    nw_input_stick(gamepad, SDL_GAMEPAD_AXIS_LEFTX, c->left_deadzone, &in->lx, &in->ly);
    nw_input_stick(gamepad, SDL_GAMEPAD_AXIS_RIGHTX, c->right_deadzone, &in->rx, &in->ry);
}

/* -1, 0 or 1 as 0x00, keep or 0xFF. */
static void nw_input_key_axis(int dir, uint8_t *axis)
{
    if (dir) {
        *axis = dir > 0 ? 0xFF : 0x00;
    }
}

void nw_input_keyboard(const bool *keys, const NwControls *c, NwPadInput *in)
{
    bool held[NW_CONTROL_TARGETS];
    for (int i = 0; i < NW_CONTROL_TARGETS; i++) {
        held[i] = false;
        for (int j = 0; j < NW_CONTROL_BINDS; j++) {
            uint16_t key = c->keys[i][j];
            held[i] = held[i] || (key && key < SDL_SCANCODE_COUNT && keys[key]);
        }
        if (held[i] && i < NW_CONTROL_BUTTONS) {
            in->buttons |= nw_input_bits[i];
        }
    }
    if (in->buttons & NW_PAD_L2) {
        in->l2 = 0xFF;
    }
    if (in->buttons & NW_PAD_R2) {
        in->r2 = 0xFF;
    }
    const bool *stick = held + NW_CONTROL_STICKS; /* up, down, left, right */
    nw_input_key_axis(stick[3] - stick[2], &in->lx);
    nw_input_key_axis(stick[1] - stick[0], &in->ly);
    nw_input_key_axis(stick[7] - stick[6], &in->rx);
    nw_input_key_axis(stick[5] - stick[4], &in->ry);
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

NwPadInput nw_input_last(void)
{
    return nw_input_sent;
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
    bool keys = nw_input_listening == NW_INPUT_LISTEN_KEYS;
    SDL_JoystickID pad = nw_input_pad && !keys ? SDL_GetGamepadID(nw_input_pad) : 0;
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.scancode == SDL_SCANCODE_ESCAPE) {
        return 0;
    }
    if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat && keys && event->key.scancode > SDL_SCANCODE_UNKNOWN &&
        event->key.scancode < SDL_SCANCODE_COUNT) {
        return event->key.scancode;
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
        nw_input_rumbling.low = nw_input_rumbling.high = 0;
        nw_input_open_next();
    }
}

/* The pad's motors on the gamepad: the big one is the low frequency rumble, the small one the high. */
static void nw_input_rumble(void)
{
    uint8_t small, big;
    nw_pad_motors(&small, &big);
    uint16_t low = (uint16_t)(big * 0x101 * nw_input_controls.rumble / 100);
    uint16_t high = (uint16_t)(small ? 0xFFFF * nw_input_controls.rumble / 100 : 0);
    uint64_t now = SDL_GetTicks();
    if (low != nw_input_rumbling.low || high != nw_input_rumbling.high ||
        ((low || high) && now - nw_input_rumbling.at >= NW_INPUT_RUMBLE_MS / 2)) {
        SDL_RumbleGamepad(nw_input_pad, low, high, NW_INPUT_RUMBLE_MS);
        nw_input_rumbling.low = low;
        nw_input_rumbling.high = high;
        nw_input_rumbling.at = now;
    }
}

void nw_input_poll(bool keyboard)
{
    NwPadInput in = { 0, 0x80, 0x80, 0x80, 0x80, 0, 0 };
    if (nw_input_pad && !nw_input_listening) {
        nw_input_gamepad(nw_input_pad, &nw_input_controls, &in);
    }
    if (keyboard && !nw_input_listening) {
        nw_input_keyboard(SDL_GetKeyboardState(NULL), &nw_input_controls, &in);
    }
    nw_input_sent = in;
    nw_pad_set(&in);
    if (nw_input_pad) {
        nw_input_rumble();
    }
}

void nw_input_shutdown(void)
{
    if (nw_input_pad) {
        SDL_CloseGamepad(nw_input_pad);
        nw_input_pad = NULL;
    }
}
