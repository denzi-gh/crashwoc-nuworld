#include "platform/controls.h"

#include <SDL3/SDL.h>

const NwControlTarget nw_control_targets[NW_CONTROL_TARGETS] = {
    { "cross", "Cross" },   { "circle", "Circle" }, { "square", "Square" },   { "triangle", "Triangle" },
    { "l1", "L1" },         { "r1", "R1" },         { "l2", "L2" },           { "r2", "R2" },
    { "l3", "L3" },         { "r3", "R3" },         { "select", "Select" },   { "start", "Start" },
    { "up", "D-pad up" },   { "down", "D-pad down" }, { "left", "D-pad left" }, { "right", "D-pad right" },
    { "left_stick_up", "Left stick up" },     { "left_stick_down", "Left stick down" },
    { "left_stick_left", "Left stick left" }, { "left_stick_right", "Left stick right" },
    { "right_stick_up", "Right stick up" },     { "right_stick_down", "Right stick down" },
    { "right_stick_left", "Right stick left" }, { "right_stick_right", "Right stick right" },
};

/* By SDL_GamepadButton. */
static const NwControlTarget nw_controls_buttons[SDL_GAMEPAD_BUTTON_COUNT] = {
    { "south", "South" },
    { "east", "East" },
    { "west", "West" },
    { "north", "North" },
    { "back", "Back" },
    { "guide", "Guide" },
    { "start", "Start" },
    { "leftstick", "Left stick" },
    { "rightstick", "Right stick" },
    { "leftshoulder", "Left shoulder" },
    { "rightshoulder", "Right shoulder" },
    { "dpup", "D-pad up" },
    { "dpdown", "D-pad down" },
    { "dpleft", "D-pad left" },
    { "dpright", "D-pad right" },
    { "misc1", "Misc 1" },
    { "rightpaddle1", "Right paddle 1" },
    { "leftpaddle1", "Left paddle 1" },
    { "rightpaddle2", "Right paddle 2" },
    { "leftpaddle2", "Left paddle 2" },
    { "touchpad", "Touchpad" },
    { "misc2", "Misc 2" },
    { "misc3", "Misc 3" },
    { "misc4", "Misc 4" },
    { "misc5", "Misc 5" },
    { "misc6", "Misc 6" },
};

static const NwControlTarget nw_controls_triggers[] = { { "lefttrigger", "Left trigger" },
                                                        { "righttrigger", "Right trigger" } };

/* By target. */
static const uint16_t nw_controls_pad_defaults[NW_CONTROL_BUTTONS] = {
    1 + SDL_GAMEPAD_BUTTON_SOUTH,
    1 + SDL_GAMEPAD_BUTTON_EAST,
    1 + SDL_GAMEPAD_BUTTON_WEST,
    1 + SDL_GAMEPAD_BUTTON_NORTH,
    1 + SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,
    1 + SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
    NW_CONTROL_TRIGGER + SDL_GAMEPAD_AXIS_LEFT_TRIGGER,
    NW_CONTROL_TRIGGER + SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,
    1 + SDL_GAMEPAD_BUTTON_LEFT_STICK,
    1 + SDL_GAMEPAD_BUTTON_RIGHT_STICK,
    1 + SDL_GAMEPAD_BUTTON_BACK,
    1 + SDL_GAMEPAD_BUTTON_START,
    1 + SDL_GAMEPAD_BUTTON_DPAD_UP,
    1 + SDL_GAMEPAD_BUTTON_DPAD_DOWN,
    1 + SDL_GAMEPAD_BUTTON_DPAD_LEFT,
    1 + SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
};

/* By target; WASD is the left stick, the right stick has none. */
static const uint16_t nw_controls_key_defaults[NW_CONTROL_TARGETS][2] = {
    { SDL_SCANCODE_SPACE },
    { SDL_SCANCODE_C, SDL_SCANCODE_K },
    { SDL_SCANCODE_E, SDL_SCANCODE_J },
    { SDL_SCANCODE_F, SDL_SCANCODE_I },
    { SDL_SCANCODE_Q },
    { SDL_SCANCODE_LSHIFT },
    { SDL_SCANCODE_1 },
    { SDL_SCANCODE_3 },
    { 0 },
    { 0 },
    { SDL_SCANCODE_TAB },
    { SDL_SCANCODE_RETURN, SDL_SCANCODE_ESCAPE },
    { SDL_SCANCODE_UP },
    { SDL_SCANCODE_DOWN },
    { SDL_SCANCODE_LEFT },
    { SDL_SCANCODE_RIGHT },
    { SDL_SCANCODE_W },
    { SDL_SCANCODE_S },
    { SDL_SCANCODE_A },
    { SDL_SCANCODE_D },
};

void nw_controls_defaults(NwControls *c)
{
    SDL_zerop(c);
    for (int i = 0; i < NW_CONTROL_BUTTONS; i++) {
        c->pad[i][0] = nw_controls_pad_defaults[i];
    }
    for (int i = 0; i < NW_CONTROL_TARGETS; i++) {
        c->keys[i][0] = nw_controls_key_defaults[i][0];
        c->keys[i][1] = nw_controls_key_defaults[i][1];
    }
}

static const NwControlTarget *nw_controls_pad(uint16_t source)
{
    if (source >= 1 && source <= SDL_GAMEPAD_BUTTON_COUNT) {
        return &nw_controls_buttons[source - 1];
    }
    if (source == NW_CONTROL_TRIGGER + SDL_GAMEPAD_AXIS_LEFT_TRIGGER ||
        source == NW_CONTROL_TRIGGER + SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
        return &nw_controls_triggers[source - NW_CONTROL_TRIGGER - SDL_GAMEPAD_AXIS_LEFT_TRIGGER];
    }
    return NULL;
}

const char *nw_controls_pad_name(uint16_t source)
{
    const NwControlTarget *t = nw_controls_pad(source);
    return t ? t->key : NULL;
}

const char *nw_controls_pad_label(uint16_t source)
{
    const NwControlTarget *t = nw_controls_pad(source);
    return t ? t->label : NULL;
}

uint16_t nw_controls_pad_source(const char *name)
{
    for (int i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; i++) {
        if (SDL_strcmp(name, nw_controls_buttons[i].key) == 0) {
            return (uint16_t)(1 + i);
        }
    }
    for (int i = 0; i < 2; i++) {
        if (SDL_strcmp(name, nw_controls_triggers[i].key) == 0) {
            return (uint16_t)(NW_CONTROL_TRIGGER + SDL_GAMEPAD_AXIS_LEFT_TRIGGER + i);
        }
    }
    return 0;
}

const char *nw_controls_key_name(uint16_t source)
{
    const char *name = source ? SDL_GetScancodeName((SDL_Scancode)source) : "";
    return *name ? name : NULL;
}

uint16_t nw_controls_key_source(const char *name)
{
    return (uint16_t)SDL_GetScancodeFromName(name);
}

void nw_controls_bind(uint16_t (*binds)[NW_CONTROL_BINDS], int count, int target, uint16_t source)
{
    for (int i = 0; source && i < count; i++) {
        int n = 0;
        for (int j = 0; j < NW_CONTROL_BINDS; j++) {
            if (binds[i][j] != source) {
                binds[i][n++] = binds[i][j];
            }
        }
        while (n < NW_CONTROL_BINDS) {
            binds[i][n++] = 0;
        }
    }
    SDL_memset(binds[target], 0, sizeof(binds[target]));
    binds[target][0] = source;
}
