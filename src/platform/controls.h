/* The controls settings.toml keeps: which gamepad buttons and keys press each of the pad's buttons, the sticks'
   deadzones and the rumble. */
#ifndef NUWORLD_PLATFORM_CONTROLS_H
#define NUWORLD_PLATFORM_CONTROLS_H

#include <stdint.h>

#define NW_CONTROL_BUTTONS 16    /* the DualShock's buttons, the first targets */
#define NW_CONTROL_STICKS 16     /* then the left stick's up, down, left and right, then the right stick's */
#define NW_CONTROL_TARGETS 24
#define NW_CONTROL_BINDS 4       /* sources one target takes */
#define NW_CONTROL_TRIGGER 0x100 /* a gamepad source: NW_CONTROL_TRIGGER + an SDL trigger axis, else 1 + an SDL button */

/* Whose buttons the game's prompts show; auto follows the device used last. */
typedef enum NwPromptStyle {
    NW_PROMPTS_AUTO,
    NW_PROMPTS_XBOX,
    NW_PROMPTS_PLAYSTATION,
    NW_PROMPTS_NINTENDO,
    NW_PROMPTS_DECK,
    NW_PROMPTS_KEYBOARD,
    NW_PROMPT_STYLES
} NwPromptStyle;

typedef struct NwControls {
    uint16_t pad[NW_CONTROL_BUTTONS][NW_CONTROL_BINDS];  /* gamepad sources, 0 none */
    uint16_t keys[NW_CONTROL_TARGETS][NW_CONTROL_BINDS]; /* SDL_Scancodes, 0 none */
    int left_deadzone, right_deadzone; /* percent of a stick's reach that counts as centred */
    int trigger_press;                 /* percent of a trigger's travel that presses its button */
    int rumble;                        /* percent of the motors' strength */
    NwPromptStyle prompts;
} NwControls;

/* Target i's settings.toml key ("cross") and label ("Cross"). */
typedef struct NwControlTarget {
    const char *key, *label;
} NwControlTarget;

extern const NwControlTarget nw_control_targets[NW_CONTROL_TARGETS];
/* By NwPromptStyle: settings.toml's names ("xbox") and labels ("Xbox"). */
extern const char *const nw_control_prompt_keys[NW_PROMPT_STYLES];
extern const char *const nw_control_prompt_labels[NW_PROMPT_STYLES];

void nw_controls_defaults(NwControls *c);
/* A gamepad source's name ("south", "lefttrigger") and label ("South"), NULL for none. */
const char *nw_controls_pad_name(uint16_t source);
const char *nw_controls_pad_label(uint16_t source);
/* The source a name stands for, 0 for none. */
uint16_t nw_controls_pad_source(const char *name);
/* A key's name ("Left Shift"), NULL for none; the key of a name, 0 for none. */
const char *nw_controls_key_name(uint16_t source);
uint16_t nw_controls_key_source(const char *name);
/* source alone on target of binds (count targets), taken off the others; 0 clears target. */
void nw_controls_bind(uint16_t (*binds)[NW_CONTROL_BINDS], int count, int target, uint16_t source);

#endif
