/* A save merges into settings.toml, so keys and tables the port does not know stay. */
#ifndef NUWORLD_FRONTEND_SETTINGS_H
#define NUWORLD_FRONTEND_SETTINGS_H

#include <stdbool.h>
#include <stddef.h>

#include "gameplay/gameplay.h"
#include "gfx/backend.h"
#include "platform/controls.h"
#include "platform/display.h"
#include "platform/gpu.h"

#define NW_FPS_MAX 1000
#define NW_DRAW_DISTANCE_MAX 400
#define NW_PROFILE_NAME 48

typedef enum NwMovementPart {
    NW_PART_CRASH,
    NW_PART_COCO,
    NW_PART_MECH,
    NW_PART_SUBMARINE,
    NW_PART_JEEP,
    NW_PART_ATLASPHERE,
    NW_PART_FIREFLY,
    NW_PART_PLANE,
    NW_PART_SPACEFIGHTER,
    NW_PART_TIME_TRIAL,
    NW_MOVEMENT_PARTS,
} NwMovementPart;

#define NW_PARTS_ALL ((1u << NW_MOVEMENT_PARTS) - 1)

typedef struct NwSettings {
    bool subtitles, title_subtitles;
    bool hardware_speed;
    bool retail_menu;
    NwMovement movement;
    bool air_indicator;
    bool boss_health;
    bool wombat;         /* the WOMBAT name opens everything in a new game */
    bool hub_minigame;
    bool long_time_names; /* up to 8 letters instead of 3 */
    bool relic_spin;
    int camera_stability; /* % of a jump the camera ignores; 0 = retail */
    bool hub_gem;
    bool first_boss_cut;
    bool vehicle_hints;
    bool bug_fixes;
    int master_volume;   /* 0-100 over the game's own volumes */
    int voice_volume;
    bool discord;
    bool updates;
    char log[64];
    bool gl_debug;
    bool minidump;
    float aspect;        /* width over height, 0 = the screen's or window's */
    float hud;           /* the widest width over height the HUD spreads to, 0 = the picture's edges */
    NwWindowMode window;
    NwResolution resolution; /* windowed the window's, fullscreen the display mode's; borderless the desktop's */
    NwVsync vsync;
    int fps;   /* 0 = the display's refresh */
    bool interpolation;
    int scale;
    int msaa;
    NwTexFilter filter;
    bool gems;
    int draw_distance; /* percent of the PS2's */
    int fog;
    bool blur; 
    bool haze; 
    char gpu[NW_GPU_NAME];
    NwControls controls;
} NwSettings;

void nw_settings_defaults(NwSettings *s);
bool nw_settings_equal(const NwSettings *a, const NwSettings *b);
/* Smokey and the Bandicoot: its sky is black, only full fog hides it. */
#define NW_SETTINGS_SKYLESS_LEVEL 3
/* Entering that level sets fog to 100 and keeps the player's in *kept (-1 none); leaving puts it back. */
void nw_settings_level_fog(NwSettings *s, int level, int *kept);
/* "auto" (0), "W:H" or a ratio, quoted or not. */
bool nw_settings_aspect(const char *value, float *out);
const char *nw_settings_aspect_name(float aspect, char *buf, int size);
/* "edges" (0) or an aspect, quoted or not. */
bool nw_settings_hud(const char *value, float *out);
const char *nw_settings_hud_name(float hud, char *buf, int size);
/* "auto" (0 x 0) or "WxH" in pixels. */
bool nw_settings_resolution(const char *value, NwResolution *out);
const char *nw_settings_resolution_name(NwResolution res, char *buf, int size);
/* A file that is not TOML is moved to settings.toml.corrupt and s keeps its values. */
bool nw_settings_load(NwSettings *s, const char *root);
bool nw_settings_save(const NwSettings *s, const char *root);

void nw_settings_profile_name(char *name);
char *nw_settings_profile_text(const NwMovement *m, unsigned parts, const char *name, size_t *size);
unsigned nw_settings_profile_read(const char *text, size_t size, NwMovement *m, char *name, size_t name_size);
void nw_settings_profile_apply(NwMovement *to, const NwMovement *from, unsigned parts);

#endif
