#include "frontend/controlpanel.h"

#include <SDL3/SDL.h>

#include "frontend/debug.h"
#include "platform/input.h"

#define NW_CONTROL_PANEL_PAD 8   /* the gamepad column, in font sizes */
#define NW_CONTROL_PANEL_KEYS 20 /* the keyboard's */

static struct {
    int target; /* listened for, -1 none */
    bool keys;
} nw_control_panel = { -1, false };

/* The sources' labels, "-" for none. */
static const char *nw_control_panel_names(const uint16_t *binds, bool keys, char *buf, int size)
{
    buf[0] = '\0';
    for (int i = 0; i < NW_CONTROL_BINDS && binds[i]; i++) {
        const char *name = keys ? nw_controls_key_name(binds[i]) : nw_input_pad_label(binds[i]);
        SDL_strlcat(buf, i ? " / " : "", size);
        SDL_strlcat(buf, name ? name : "?", size);
    }
    return buf[0] ? buf : "-";
}

/* Target i's bindings in one column as a button that listens for a new one. */
static void nw_control_panel_cell(uint16_t (*binds)[NW_CONTROL_BINDS], int i, bool keys)
{
    char names[96], label[128];
    bool listening = nw_control_panel.target == i && nw_control_panel.keys == keys;
    nw_debug_same_line(keys ? NW_CONTROL_PANEL_KEYS : NW_CONTROL_PANEL_PAD);
    SDL_snprintf(label, sizeof(label), "%s##%c%d",
                 listening ? "..." : nw_control_panel_names(binds[i], keys, names, sizeof(names)), keys ? 'k' : 'p', i);
    if (nw_debug_button(label)) {
        nw_control_panel.target = i;
        nw_control_panel.keys = keys;
        nw_input_listen(keys ? NW_INPUT_LISTEN_KEYS : NW_INPUT_LISTEN_PAD);
    }
}

static void nw_control_panel_draw(void *user)
{
    NwControls *c = user;
    int target = nw_control_panel.target, heard = target >= 0 ? nw_input_heard() : -1;
    bool keys = nw_control_panel.keys;
    uint16_t(*binds)[NW_CONTROL_BINDS] = keys ? c->keys : c->pad;
    int count = keys ? NW_CONTROL_TARGETS : NW_CONTROL_BUTTONS;
    if (heard >= 0) {
        if (heard) {
            nw_controls_bind(binds, count, target, (uint16_t)heard);
        }
        nw_control_panel.target = target = -1;
    }
    NwPadInput in = nw_input_last();
    nw_debug_slider_int("Left stick deadzone %", &c->left_deadzone, 0, 90);
    nw_debug_slider_int("Right stick deadzone %", &c->right_deadzone, 0, 90);
    nw_debug_slider_int("Trigger press point %", &c->trigger_press, 1, 100);
    nw_debug_slider_int("Rumble %", &c->rumble, 0, 100);
    int prompts = (int)c->prompts;
    if (nw_debug_combo("Button prompts", &prompts, nw_control_prompt_labels, NW_PROMPT_STYLES)) {
        c->prompts = (NwPromptStyle)prompts;
    }
    if (c->prompts == NW_PROMPTS_AUTO) {
        nw_debug_text("prompts now %s", nw_control_prompt_labels[nw_input_prompt_style()]);
    }
    nw_debug_text("sticks now %+4d %+4d, %+4d %+4d; triggers %3d %3d", in.lx - 0x80, in.ly - 0x80, in.rx - 0x80,
                  in.ry - 0x80, in.l2, in.r2);
    const char *pad = nw_input_pad_name();
    nw_debug_label("");
    nw_debug_same_line(NW_CONTROL_PANEL_PAD);
    nw_debug_label(pad ? pad : "no gamepad");
    nw_debug_same_line(NW_CONTROL_PANEL_KEYS);
    nw_debug_label("Keyboard");
    for (int i = 0; i < NW_CONTROL_TARGETS; i++) {
        nw_debug_label(nw_control_targets[i].label);
        if (i < NW_CONTROL_BUTTONS) {
            nw_control_panel_cell(c->pad, i, false);
        }
        nw_control_panel_cell(c->keys, i, true);
    }
    if (target >= 0) {
        nw_debug_text("Press a %s for %s; Esc cancels", keys ? "key" : "button or trigger",
                      nw_control_targets[target].label);
        if (nw_debug_button("Clear")) {
            nw_controls_bind(binds, count, target, 0);
            nw_control_panel.target = -1;
            nw_input_listen(NW_INPUT_LISTEN_OFF);
        }
    }
    if (nw_debug_button("Defaults")) {
        nw_controls_defaults(c);
    }
}

void nw_control_panel_start(NwControls *c)
{
    nw_debug_panel("Input", "Controls", nw_control_panel_draw, c, false);
}
