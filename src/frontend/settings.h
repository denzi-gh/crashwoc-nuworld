/* The player's settings in <data root>settings.toml, in the part of TOML the port writes: [table] headers,
   key = value lines and # comments. What the reader does not know keeps its value. */
#ifndef NUWORLD_FRONTEND_SETTINGS_H
#define NUWORLD_FRONTEND_SETTINGS_H

#include <stdbool.h>

typedef struct NwSettings {
    bool hardware_speed; /* [game]: the PS2's disc and sound load times instead of instant loads */
} NwSettings;

void nw_settings_defaults(NwSettings *s);
/* Root's settings.toml over s when there is one; false with an SDL error when it is there but cannot be read. */
bool nw_settings_load(NwSettings *s, const char *root);
/* Writes every setting to root's settings.toml; false with an SDL error. */
bool nw_settings_save(const NwSettings *s, const char *root);

#endif
