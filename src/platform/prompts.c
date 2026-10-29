#include "platform/prompts.h"

#include <SDL3/SDL.h>

/* By SDL_GamepadButton; NULL draws the button's name. The guide buttons are brand logos. */
static const char *const nw_prompts_xbox[SDL_GAMEPAD_BUTTON_COUNT] = {
    "xbox_button_a", "xbox_button_b", "xbox_button_x", "xbox_button_y", "xbox_button_view", NULL, "xbox_button_menu",
    "xbox_ls", "xbox_rs", "xbox_lb", "xbox_rb", "xbox_dpad_up", "xbox_dpad_down", "xbox_dpad_left", "xbox_dpad_right",
    "xbox_button_share", "xbox_elite_paddle_top_right", "xbox_elite_paddle_top_left", "xbox_elite_paddle_bottom_right",
    "xbox_elite_paddle_bottom_left",
};
static const char *const nw_prompts_nintendo[SDL_GAMEPAD_BUTTON_COUNT] = {
    "switch_button_b", "switch_button_a", "switch_button_y", "switch_button_x", "switch_button_minus",
    "switch_button_home", "switch_button_plus", "switch_stick_l_press", "switch_stick_r_press", "switch_button_l",
    "switch_button_r", "switch_dpad_up", "switch_dpad_down", "switch_dpad_left", "switch_dpad_right",
};
static const char *const nw_prompts_deck[SDL_GAMEPAD_BUTTON_COUNT] = {
    "steamdeck_button_a", "steamdeck_button_b", "steamdeck_button_x", "steamdeck_button_y",
    "steamdeck_button_view", NULL, "steamdeck_button_options", "steamdeck_stick_l_press", "steamdeck_stick_r_press",
    "steamdeck_button_l1", "steamdeck_button_r1", "steamdeck_dpad_up", "steamdeck_dpad_down", "steamdeck_dpad_left",
    "steamdeck_dpad_right", "steamdeck_button_quickaccess", "steamdeck_button_r4", "steamdeck_button_l4",
    "steamdeck_button_r5", "steamdeck_button_l5",
};
static const char *const nw_prompts_playstation[SDL_GAMEPAD_BUTTON_COUNT] = {
    "", "", "", "", "SELECT", "PS", "START", "L3", "R3", "L1", "R1", "UP", "DOWN", "LEFT", "RIGHT",
};
static const char nw_prompts_models[4] = { 'x', 'o', 's', 't' }; /* south, east, west, north */
static const char *const nw_prompts_triggers[][2] = {
    { "xbox_lt", "xbox_rt" },
    { "L2", "R2" },
    { "switch_button_zl", "switch_button_zr" },
    { "steamdeck_button_l2", "steamdeck_button_r2" },
};

static const struct {
    uint16_t key;
    const char *icon;
} nw_prompts_keys[] = {
    { SDL_SCANCODE_RETURN, "keyboard_enter" },
    { SDL_SCANCODE_ESCAPE, "keyboard_escape" },
    { SDL_SCANCODE_BACKSPACE, "keyboard_backspace" },
    { SDL_SCANCODE_TAB, "keyboard_tab" },
    { SDL_SCANCODE_SPACE, "keyboard_space" },
    { SDL_SCANCODE_MINUS, "keyboard_minus" },
    { SDL_SCANCODE_EQUALS, "keyboard_equals" },
    { SDL_SCANCODE_LEFTBRACKET, "keyboard_bracket_open" },
    { SDL_SCANCODE_RIGHTBRACKET, "keyboard_bracket_close" },
    { SDL_SCANCODE_BACKSLASH, "keyboard_slash_back" },
    { SDL_SCANCODE_SEMICOLON, "keyboard_semicolon" },
    { SDL_SCANCODE_APOSTROPHE, "keyboard_apostrophe" },
    { SDL_SCANCODE_GRAVE, "keyboard_tilde" },
    { SDL_SCANCODE_COMMA, "keyboard_comma" },
    { SDL_SCANCODE_PERIOD, "keyboard_period" },
    { SDL_SCANCODE_SLASH, "keyboard_slash_forward" },
    { SDL_SCANCODE_CAPSLOCK, "keyboard_capslock" },
    { SDL_SCANCODE_PRINTSCREEN, "keyboard_printscreen" },
    { SDL_SCANCODE_SCROLLLOCK, "keyboard_scroll_lock" },
    { SDL_SCANCODE_PAUSE, "keyboard_pause" },
    { SDL_SCANCODE_INSERT, "keyboard_insert" },
    { SDL_SCANCODE_HOME, "keyboard_home" },
    { SDL_SCANCODE_PAGEUP, "keyboard_page_up" },
    { SDL_SCANCODE_DELETE, "keyboard_delete" },
    { SDL_SCANCODE_END, "keyboard_end" },
    { SDL_SCANCODE_PAGEDOWN, "keyboard_page_down" },
    { SDL_SCANCODE_RIGHT, "keyboard_arrow_right" },
    { SDL_SCANCODE_LEFT, "keyboard_arrow_left" },
    { SDL_SCANCODE_DOWN, "keyboard_arrow_down" },
    { SDL_SCANCODE_UP, "keyboard_arrow_up" },
    { SDL_SCANCODE_NUMLOCKCLEAR, "keyboard_numlock" },
    { SDL_SCANCODE_KP_PLUS, "keyboard_numpad_plus" },
    { SDL_SCANCODE_KP_ENTER, "keyboard_numpad_enter" },
    { SDL_SCANCODE_LCTRL, "keyboard_ctrl" },
    { SDL_SCANCODE_LSHIFT, "keyboard_shift" },
    { SDL_SCANCODE_LALT, "keyboard_alt" },
    { SDL_SCANCODE_LGUI, "keyboard_win" },
    { SDL_SCANCODE_RCTRL, "keyboard_ctrl" },
    { SDL_SCANCODE_RSHIFT, "keyboard_shift" },
    { SDL_SCANCODE_RALT, "keyboard_alt" },
    { SDL_SCANCODE_RGUI, "keyboard_win" },
};

NwPromptStyle nw_prompts_style(NwPromptStyle style, const NwPromptDevice *d)
{
    if (style != NW_PROMPTS_AUTO) {
        return style;
    }
    if (d->keyboard) {
        return NW_PROMPTS_KEYBOARD;
    }
    if (d->vendor == 0x28DE && d->product == 0x1205) {
        return NW_PROMPTS_DECK;
    }
    switch (d->type) {
    case SDL_GAMEPAD_TYPE_PS3:
    case SDL_GAMEPAD_TYPE_PS4:
    case SDL_GAMEPAD_TYPE_PS5:
        return NW_PROMPTS_PLAYSTATION;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
        return NW_PROMPTS_NINTENDO;
    default:
        return d->deck ? NW_PROMPTS_DECK : NW_PROMPTS_XBOX;
    }
}

/* name in the font's letters: upper case, and only the characters it has. */
static void nw_prompts_text(NwPromptGlyph *g, const char *name)
{
    size_t n = 0;
    for (; *name && n + 1 < sizeof(g->text); name++) {
        char c = (char)SDL_toupper((unsigned char)*name);
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || SDL_strchr(" %/?!:.-+_<>(),='", c)) {
            g->text[n++] = c;
        }
    }
    g->text[n] = '\0';
}

static void nw_prompts_key(uint16_t key, NwPromptGlyph *g)
{
    if (key >= SDL_SCANCODE_A && key <= SDL_SCANCODE_Z) {
        SDL_snprintf(g->icon, sizeof(g->icon), "keyboard_%c", 'a' + (key - SDL_SCANCODE_A));
        return;
    }
    if (key >= SDL_SCANCODE_1 && key <= SDL_SCANCODE_0) {
        SDL_snprintf(g->icon, sizeof(g->icon), "keyboard_%d", (key - SDL_SCANCODE_1 + 1) % 10);
        return;
    }
    if (key >= SDL_SCANCODE_F1 && key <= SDL_SCANCODE_F12) {
        SDL_snprintf(g->icon, sizeof(g->icon), "keyboard_f%d", key - SDL_SCANCODE_F1 + 1);
        return;
    }
    for (size_t i = 0; i < SDL_arraysize(nw_prompts_keys); i++) {
        if (nw_prompts_keys[i].key == key) {
            SDL_strlcpy(g->icon, nw_prompts_keys[i].icon, sizeof(g->icon));
            return;
        }
    }
    const char *name = nw_controls_key_name(key);
    nw_prompts_text(g, name ? name : "");
}

static void nw_prompts_pad(NwPromptStyle style, uint16_t source, NwPromptGlyph *g)
{
    const char *const *icons = style == NW_PROMPTS_XBOX       ? nw_prompts_xbox
                               : style == NW_PROMPTS_NINTENDO ? nw_prompts_nintendo
                               : style == NW_PROMPTS_DECK     ? nw_prompts_deck
                                                              : nw_prompts_playstation;
    const char *name = NULL;
    if (source >= NW_CONTROL_TRIGGER) {
        int t = source - NW_CONTROL_TRIGGER - SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
        name = t == 0 || t == 1 ? nw_prompts_triggers[style - NW_PROMPTS_XBOX][t] : NULL;
    } else if (style == NW_PROMPTS_PLAYSTATION && source >= 1 && source <= 4) {
        g->model = nw_prompts_models[source - 1];
        return;
    } else if (source >= 1 && source <= SDL_GAMEPAD_BUTTON_COUNT) {
        name = icons[source - 1];
    }
    if (name && style != NW_PROMPTS_PLAYSTATION) {
        SDL_strlcpy(g->icon, name, sizeof(g->icon));
    } else {
        const char *label = nw_controls_pad_label(source);
        nw_prompts_text(g, name ? name : label ? label : "");
    }
}

void nw_prompts_build(NwPromptStyle style, const NwControls *c, NwPrompts *p)
{
    NwPrompts retail;
    nw_prompts_retail(&retail);
    SDL_zerop(p);
    for (int i = 0; i < NW_PROMPT_GLYPHS; i++) {
        NwPromptGlyph *g = &p->glyph[i];
        if (style == NW_PROMPTS_KEYBOARD && c->keys[i][0]) {
            nw_prompts_key(c->keys[i][0], g);
        } else if (style != NW_PROMPTS_KEYBOARD && style != NW_PROMPTS_AUTO && c->pad[i][0]) {
            nw_prompts_pad(style, c->pad[i][0], g);
        }
        if (!g->icon[0] && !g->text[0] && !g->model) {
            *g = retail.glyph[i];
        }
    }
}

bool nw_prompts_on_deck(void)
{
    const char *steam = SDL_getenv("SteamDeck");
    if (steam) {
        return SDL_strcmp(steam, "1") == 0;
    }
#ifdef SDL_PLATFORM_LINUX
    size_t size = 0;
    char *board = SDL_LoadFile("/sys/class/dmi/id/board_name", &size);
    bool deck = board && (SDL_strncmp(board, "Jupiter", 7) == 0 || SDL_strncmp(board, "Galileo", 7) == 0);
    SDL_free(board);
    return deck;
#else
    return false;
#endif
}
