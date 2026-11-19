/* The player's settings in <data root>settings.toml, a TOML document (save/savedoc.h): a key the reader does not take
   keeps its value, and a save merges into the file, so keys and tables the port does not know stay. */
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
    NW_MOVEMENT_PARTS,
} NwMovementPart;

#define NW_PARTS_ALL ((1u << NW_MOVEMENT_PARTS) - 1)

typedef struct NwSettings {
    bool hardware_speed; /* [game]: the PS2's disc and sound load times instead of instant loads */
    bool pal_60hz;       /* a PAL disc at 60 Hz with the US release's values */
    bool retail_menu;    /* the PS2's logos and title cut, from the next start */
    NwMovement movement; /* [game.movement], N. Hanced Movement */
    bool air_indicator;  /* a ring under Crash where he lands while he is in the air */
    bool boss_health;    /* phase lines on the boss bar */
    bool wombat;         /* the WOMBAT name opens everything in a new game */
    bool hub_minigame;   /* Crunch on the warp room's top floor starts the loading minigame */
    bool long_time_names; /* time trial records take names of up to 8 letters instead of 3 */
    int camera_stability; /* percent of a jump the game camera ignores, 0 the PS2's */
    bool hub_gem;
    bool first_boss_cut;
    bool vehicle_hints;
    int master_volume;   /* [audio], 0-100 over the game's own volumes: everything */
    int voice_volume;    /* the voice stream (cutscenes, hints) */
    bool discord;        /* [discord]: Rich Presence on the player's profile */
    bool updates;        /* [updates]: ask GitHub at the start for a newer NuWorld release */
    char log[64];
    bool gl_debug;
    bool minidump;
    float aspect;        /* [video]: the picture's width over its height, 0 = the screen's or window's */
    float hud;           /* the widest width over height the HUD spreads to, 0 = the picture's edges */
    NwWindowMode window;
    NwResolution resolution; /* windowed the window's, fullscreen the display mode's; borderless the desktop's */
    NwVsync vsync;
    int fps;   /* the most frames a second the window shows, 0 = the display's refresh */
    bool interpolation; /* frames between the game's blended from the two newest */
    int scale; /* the internal resolution, NwRenderOptions' */
    int msaa;
    NwTexFilter filter;
    bool clouds;         /* the intro island's sky live and its clouds volumetric, not the PS2's pictures */
    bool water;          /* water that refracts, reflects and foams, not the PS2's two passes */
    bool silhouettes;    /* the intro's heroes in the door as their models against the light, not a picture */
    bool gems; /* gems, crystals and relics as cut stone and polished metal */
    int draw_distance; /* percent of the PS2's */
    int fog;
    bool blur; 
    bool haze; 
    char gpu[NW_GPU_NAME];
    NwControls controls; /* [input], [pad], [keyboard] */
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

void nw_settings_profile_name(char *name);
char *nw_settings_profile_text(const NwMovement *m, unsigned parts, const char *name, size_t *size);
unsigned nw_settings_profile_read(const char *text, size_t size, NwMovement *m, char *name, size_t name_size);
void nw_settings_profile_apply(NwMovement *to, const NwMovement *from, unsigned parts);

#endif
