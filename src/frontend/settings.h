/* The player's settings in <data root>settings.toml, in the part of TOML the port writes: [table] headers,
   key = value lines and # comments. What the reader does not know keeps its value. */
#ifndef NUWORLD_FRONTEND_SETTINGS_H
#define NUWORLD_FRONTEND_SETTINGS_H

#include <stdbool.h>

typedef struct NwSettings {
    bool hardware_speed; /* [game]: the PS2's disc and sound load times instead of instant loads */
    int master_volume;   /* [audio], 0-100 over the game's own volumes: everything */
    int voice_volume;    /* the voice stream (cutscenes, hints) */
    float aspect;        /* [video]: the picture's width over its height, 0 = the display's */
    float hud;           /* the widest width over height the HUD spreads to, 0 = the picture's edges */
} NwSettings;

void nw_settings_defaults(NwSettings *s);
/* An aspect as settings.toml and --aspect write it: "auto" (0), "W:H" or a ratio, quoted or not; false when it is
   none of them. */
bool nw_settings_aspect(const char *value, float *out);
/* The aspect's name: auto, one of 4:3 16:9 16:10 21:9 32:9, else its ratio. */
const char *nw_settings_aspect_name(float aspect, char *buf, int size);
/* The HUD's reach as settings.toml writes it: "edges" (0) or an aspect, quoted or not; false when it is neither. */
bool nw_settings_hud(const char *value, float *out);
/* Its name: edges, else as nw_settings_aspect_name. */
const char *nw_settings_hud_name(float hud, char *buf, int size);
/* Root's settings.toml over s when there is one; false with an SDL error when it is there but cannot be read. */
bool nw_settings_load(NwSettings *s, const char *root);
/* Writes every setting to root's settings.toml; false with an SDL error. */
bool nw_settings_save(const NwSettings *s, const char *root);

#endif
