/* The button prompts' glyphs: the style auto stands for, each style's icons by the bound button's position, the
   PlayStation's retail models and letters, keys as icons or their names, retail's without a binding, the Deck. */
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "platform/prompts.h"

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

static NwPromptStyle style_of(bool keyboard, SDL_GamepadType type, uint16_t vendor, uint16_t product, bool deck)
{
    NwPromptDevice d = { keyboard, type, vendor, product, deck };
    return nw_prompts_style(NW_PROMPTS_AUTO, &d);
}

static void test_style(void)
{
    NwPromptDevice d = { true, SDL_GAMEPAD_TYPE_PS5, 0, 0, false };
    CHECK(nw_prompts_style(NW_PROMPTS_NINTENDO, &d) == NW_PROMPTS_NINTENDO);
    CHECK(style_of(true, SDL_GAMEPAD_TYPE_PS5, 0, 0, false) == NW_PROMPTS_KEYBOARD);
    CHECK(style_of(false, SDL_GAMEPAD_TYPE_PS4, 0, 0, true) == NW_PROMPTS_PLAYSTATION);
    CHECK(style_of(false, SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO, 0, 0, false) == NW_PROMPTS_NINTENDO);
    CHECK(style_of(false, SDL_GAMEPAD_TYPE_XBOXONE, 0, 0, false) == NW_PROMPTS_XBOX);
    CHECK(style_of(false, SDL_GAMEPAD_TYPE_STANDARD, 0, 0, false) == NW_PROMPTS_XBOX);
    /* Steam Input's virtual pad on a Deck, and the Deck's own controller anywhere. */
    CHECK(style_of(false, SDL_GAMEPAD_TYPE_XBOX360, 0, 0, true) == NW_PROMPTS_DECK);
    CHECK(style_of(false, SDL_GAMEPAD_TYPE_UNKNOWN, 0x28DE, 0x1205, false) == NW_PROMPTS_DECK);
}

static void test_glyphs(void)
{
    NwControls c;
    NwPrompts p, retail;
    nw_controls_defaults(&c);
    nw_prompts_retail(&retail);
    CHECK(retail.glyph[0].model == 'x' && retail.glyph[3].model == 't' && strcmp(retail.glyph[4].text, "L1") == 0 &&
          strcmp(retail.glyph[7].text, "R2") == 0);

    nw_prompts_build(NW_PROMPTS_PLAYSTATION, &c, &p);
    CHECK(memcmp(&p, &retail, sizeof(p)) == 0);
    nw_prompts_build(NW_PROMPTS_XBOX, &c, &p);
    CHECK(strcmp(p.glyph[0].icon, "xbox_button_a") == 0 && strcmp(p.glyph[1].icon, "xbox_button_b") == 0);
    CHECK(strcmp(p.glyph[4].icon, "xbox_lb") == 0 && strcmp(p.glyph[7].icon, "xbox_rt") == 0);
    /* Nintendo's labels by position: south is B. */
    nw_prompts_build(NW_PROMPTS_NINTENDO, &c, &p);
    CHECK(strcmp(p.glyph[0].icon, "switch_button_b") == 0 && strcmp(p.glyph[3].icon, "switch_button_x") == 0);
    CHECK(strcmp(p.glyph[6].icon, "switch_button_zl") == 0);
    nw_prompts_build(NW_PROMPTS_DECK, &c, &p);
    CHECK(strcmp(p.glyph[2].icon, "steamdeck_button_x") == 0 && strcmp(p.glyph[5].icon, "steamdeck_button_r1") == 0);

    /* Remapped: cross on the right trigger, L1 on east, circle unbound; the PlayStation draws circle for L1. */
    nw_controls_bind(c.pad, NW_CONTROL_BUTTONS, 0, NW_CONTROL_TRIGGER + SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
    nw_controls_bind(c.pad, NW_CONTROL_BUTTONS, 4, 1 + SDL_GAMEPAD_BUTTON_EAST);
    nw_prompts_build(NW_PROMPTS_XBOX, &c, &p);
    CHECK(strcmp(p.glyph[0].icon, "xbox_rt") == 0 && strcmp(p.glyph[4].icon, "xbox_button_b") == 0);
    CHECK(memcmp(&p.glyph[1], &retail.glyph[1], sizeof(p.glyph[1])) == 0);
    nw_prompts_build(NW_PROMPTS_PLAYSTATION, &c, &p);
    CHECK(strcmp(p.glyph[0].text, "R2") == 0 && p.glyph[4].model == 'o' && !p.glyph[4].text[0]);
    /* A button without an icon: its name in the font's letters. */
    nw_controls_bind(c.pad, NW_CONTROL_BUTTONS, 2, 1 + SDL_GAMEPAD_BUTTON_MISC2);
    nw_prompts_build(NW_PROMPTS_XBOX, &c, &p);
    CHECK(!p.glyph[2].icon[0] && strcmp(p.glyph[2].text, "MISC 2") == 0);

    /* Keys: letters, digits and named keys as icons, others by name; no key, retail's. */
    nw_controls_defaults(&c);
    c.keys[3][0] = SDL_SCANCODE_KP_5;
    c.keys[5][0] = 0;
    nw_prompts_build(NW_PROMPTS_KEYBOARD, &c, &p);
    CHECK(strcmp(p.glyph[0].icon, "keyboard_space") == 0 && strcmp(p.glyph[1].icon, "keyboard_c") == 0);
    CHECK(strcmp(p.glyph[4].icon, "keyboard_q") == 0 && strcmp(p.glyph[6].icon, "keyboard_1") == 0);
    CHECK(!p.glyph[3].icon[0] && strcmp(p.glyph[3].text, "KEYPAD 5") == 0);
    CHECK(memcmp(&p.glyph[5], &retail.glyph[5], sizeof(p.glyph[5])) == 0);
    c.keys[1][0] = SDL_SCANCODE_0;
    c.keys[2][0] = SDL_SCANCODE_F12;
    c.keys[4][0] = SDL_SCANCODE_RSHIFT;
    nw_prompts_build(NW_PROMPTS_KEYBOARD, &c, &p);
    CHECK(strcmp(p.glyph[1].icon, "keyboard_0") == 0 && strcmp(p.glyph[2].icon, "keyboard_f12") == 0 &&
          strcmp(p.glyph[4].icon, "keyboard_shift") == 0);
}

static void test_deck(void)
{
    CHECK(SDL_setenv_unsafe("SteamDeck", "1", 1) == 0);
    CHECK(nw_prompts_on_deck());
    CHECK(SDL_setenv_unsafe("SteamDeck", "0", 1) == 0);
    CHECK(!nw_prompts_on_deck());
}

int main(void)
{
    test_style();
    test_glyphs();
    test_deck();
    if (failures) {
        fprintf(stderr, "test_prompts: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_prompts: ok\n");
    return 0;
}
