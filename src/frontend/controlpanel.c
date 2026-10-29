#include "frontend/controlpanel.h"

#include <SDL3/SDL.h>

#include "frontend/debug.h"
#include "platform/input.h"

#define NW_CONTROL_PANEL_PAD 8 /* the gamepad column, in font sizes */

static int nw_control_panel_target = -1; /* listened for */

/* The sources' labels, "-" for none. */
static const char *nw_control_panel_names(const uint16_t *binds, char *buf, int size)
{
    buf[0] = '\0';
    for (int i = 0; i < NW_CONTROL_BINDS && binds[i]; i++) {
        SDL_strlcat(buf, i ? " / " : "", size);
        SDL_strlcat(buf, nw_input_pad_label(binds[i]), size);
    }
    return buf[0] ? buf : "-";
}

static void nw_control_panel_draw(void *user)
{
    NwControls *c = user;
    int target = nw_control_panel_target, heard = target >= 0 ? nw_input_heard() : -1;
    if (heard >= 0) {
        if (heard) {
            nw_controls_bind(c->pad, NW_CONTROL_BUTTONS, target, (uint16_t)heard);
        }
        nw_control_panel_target = target = -1;
    }
    const char *pad = nw_input_pad_name();
    nw_debug_text("Gamepad: %s", pad ? pad : "none");
    for (int i = 0; i < NW_CONTROL_BUTTONS; i++) {
        char names[96], label[128];
        nw_debug_label(nw_control_targets[i].label);
        nw_debug_same_line(NW_CONTROL_PANEL_PAD);
        SDL_snprintf(label, sizeof(label), "%s##pad%d",
                     i == target ? "..." : nw_control_panel_names(c->pad[i], names, sizeof(names)), i);
        if (nw_debug_button(label)) {
            nw_control_panel_target = i;
            nw_input_listen(NW_INPUT_LISTEN_PAD);
        }
    }
    if (target >= 0) {
        nw_debug_text("Press a button or trigger for %s; Esc cancels", nw_control_targets[target].label);
        if (nw_debug_button("Clear")) {
            nw_controls_bind(c->pad, NW_CONTROL_BUTTONS, target, 0);
            nw_control_panel_target = -1;
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
