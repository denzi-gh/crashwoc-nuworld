/* The glyphs Text3D draws for the pad's buttons: the bound button's or key's icon (assets/prompts), its name in the
   font's letters, or one of retail's cross, circle, square and triangle. */
#ifndef NUWORLD_PLATFORM_PROMPTS_H
#define NUWORLD_PLATFORM_PROMPTS_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "platform/controls.h"

/* The targets with a glyph code in game text: cross x, circle o, square s, triangle t, and the port's l1 q, r1 r,
   l2 u, r2 v. */
#define NW_PROMPT_GLYPHS 8

typedef struct NwPromptGlyph {
    char icon[32]; /* an icon's name, "" none */
    char text[16]; /* else these letters, "" none */
    char model;    /* else this retail glyph: x, o, s or t */
} NwPromptGlyph;

typedef struct NwPrompts {
    NwPromptGlyph glyph[NW_PROMPT_GLYPHS];
} NwPrompts;

typedef struct NwPromptDevice {
    bool keyboard; /* used last, or no gamepad */
    int type;      /* the gamepad's SDL_GamepadType */
    uint16_t vendor, product;
    bool deck; /* nw_prompts_on_deck */
} NwPromptDevice;

/* The style auto stands for on d; any other is itself. */
NwPromptStyle nw_prompts_style(NwPromptStyle style, const NwPromptDevice *d);
/* Every glyph in style (not auto) through the first source bound to its target; retail's without one. */
void nw_prompts_build(NwPromptStyle style, const NwControls *c, NwPrompts *p);
/* A PlayStation pad's: retail's models and letters. */
static inline void nw_prompts_retail(NwPrompts *p)
{
    static const char *const shoulders[4] = { "L1", "R1", "L2", "R2" };
    memset(p, 0, sizeof(*p));
    for (int i = 0; i < 4; i++) {
        p->glyph[i].model = "xost"[i];
        strcpy(p->glyph[4 + i].text, shoulders[i]);
    }
}
/* Steam sets SteamDeck=1; elsewhere on Linux the DMI board name is Jupiter (LCD) or Galileo (OLED). */
bool nw_prompts_on_deck(void);

#endif
