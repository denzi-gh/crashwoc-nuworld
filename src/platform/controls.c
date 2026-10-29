#include "platform/controls.h"

#include <SDL3/SDL.h>

const NwControlTarget nw_control_targets[NW_CONTROL_BUTTONS] = {
    { "cross", "Cross" },   { "circle", "Circle" }, { "square", "Square" },   { "triangle", "Triangle" },
    { "l1", "L1" },         { "r1", "R1" },         { "l2", "L2" },           { "r2", "R2" },
    { "l3", "L3" },         { "r3", "R3" },         { "select", "Select" },   { "start", "Start" },
    { "up", "D-pad up" },   { "down", "D-pad down" }, { "left", "D-pad left" }, { "right", "D-pad right" },
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

void nw_controls_defaults(NwControls *c)
{
    SDL_zerop(c);
    for (int i = 0; i < NW_CONTROL_BUTTONS; i++) {
        c->pad[i][0] = nw_controls_pad_defaults[i];
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
