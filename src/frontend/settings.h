/* The player's settings in <data root>settings.toml, a TOML document (save/savedoc.h): a key the reader does not take
   keeps its value, and a save merges into the file, so keys and tables the port does not know stay. */
#ifndef NUWORLD_FRONTEND_SETTINGS_H
#define NUWORLD_FRONTEND_SETTINGS_H

#include <stdbool.h>

#include "gfx/backend.h"
#include "platform/display.h"

typedef struct NwSettings {
    bool hardware_speed; /* [game]: the PS2's disc and sound load times instead of instant loads */
    int master_volume;   /* [audio], 0-100 over the game's own volumes: everything */
    int voice_volume;    /* the voice stream (cutscenes, hints) */
    float aspect;        /* [video]: the picture's width over its height, 0 = the screen's or window's */
    float hud;           /* the widest width over height the HUD spreads to, 0 = the picture's edges */
    NwWindowMode window;
    NwResolution resolution; /* windowed the window's, fullscreen the display mode's; borderless the desktop's */
    NwVsync vsync;
    int scale; /* the internal resolution, NwRenderOptions' */
    int msaa;
    NwTexFilter filter;
} NwSettings;

void nw_settings_defaults(NwSettings *s);
bool nw_settings_equal(const NwSettings *a, const NwSettings *b);
/* An aspect as settings.toml and --aspect write it: "auto" (0), "W:H" or a ratio, quoted or not; false when it is
   none of them. */
bool nw_settings_aspect(const char *value, float *out);
/* The aspect's name: auto, one of 4:3 16:9 16:10 21:9 32:9, else its ratio. */
const char *nw_settings_aspect_name(float aspect, char *buf, int size);
/* The HUD's reach as settings.toml writes it: "edges" (0) or an aspect, quoted or not; false when it is neither. */
bool nw_settings_hud(const char *value, float *out);
/* Its name: edges, else as nw_settings_aspect_name. */
const char *nw_settings_hud_name(float hud, char *buf, int size);
/* A resolution as settings.toml writes it: "auto" (0 x 0) or "WxH" in pixels; false when it is neither. */
bool nw_settings_resolution(const char *value, NwResolution *out);
const char *nw_settings_resolution_name(NwResolution res, char *buf, int size);
/* Root's settings.toml over s when there is one. False with an SDL error when it cannot be read, or when it is not
   TOML: it is then moved to settings.toml.corrupt and s keeps its values. */
bool nw_settings_load(NwSettings *s, const char *root);
/* s merged into root's settings.toml through a .tmp file; false with an SDL error. */
bool nw_settings_save(const NwSettings *s, const char *root);

#endif
