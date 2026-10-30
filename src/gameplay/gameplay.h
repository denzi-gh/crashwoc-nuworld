/* NuWorld's changes to how the game plays or looks, each behind a [game] setting; all off (zero) is the retail game that
   every check, test and recording runs, and only the windowed boot fills the profile from settings.toml. */
#ifndef NUWORLD_GAMEPLAY_GAMEPLAY_H
#define NUWORLD_GAMEPLAY_GAMEPLAY_H

#include <stdbool.h>

/* Crash's options ([game.movement.crash]) in panel order: SW(field, label, help) a switch, off is retail;
   FLAG(field, label, default, help) and VAL(field, label, default, lo, hi, help) tune the switch above them. */
#define NW_CRASH_MOVEMENT(SW, FLAG, VAL) \
    SW(jump_fall, "Crash 4 jump arc", "a Crash 4 style jump: quick launch, soft top, a fall that builds up") \
    VAL(jump_float, "Apex float %", 100, 0, 200, "how long the jump floats at the top") \
    VAL(jump_weight, "Fall weight %", 100, 100, 200, "how hard the fall builds up") \
    VAL(jump_height, "Jump height %", 100, 100, 130, "percent of the PS2's jump height") \
    SW(jump_distance, "Predictable jump distance", "in a jump any stick tilt is a full run") \
    VAL(air_accel, "Air acceleration %", 200, 100, 400, "percent of the PS2's") \
    SW(air_momentum, "Air momentum", "mid-air with the stick let go he keeps some momentum") \
    VAL(air_drift, "Momentum kept %", 75, 0, 100, "how much of the PS2's slow-down he skips") \
    SW(dash_jump, "Crash Dash jump momentum", "a jump out of a Crash Dash keeps its speed") \
    VAL(dash_kept, "Dash speed kept %", 100, 0, 100, "share of the dash's extra speed kept to the ground") \
    SW(run_speed, "Faster run", "a faster run and Crash Dash, quicker to speed") \
    VAL(run_top, "Top speed %", 120, 100, 150, "percent of the PS2's") \
    VAL(run_accel, "Acceleration %", 150, 100, 300, "percent of the PS2's") \
    SW(grip, "Grip", "less slippery stops and turns; ice stays slippery") \
    VAL(grip_turn, "Turn grip %", 300, 100, 500, "percent of the PS2's") \
    VAL(grip_brake, "Braking %", 200, 100, 400, "percent of the PS2's") \
    SW(long_spin, "Longer spin", "the spin attack lasts longer") \
    VAL(spin_ms, "Spin length ms", 400, 320, 600, "the PS2's is 320") \
    SW(edge_grip, "Edge grip", "a beveled platform edge holds him instead of sliding him off") \
    VAL(edge_reach, "Edge reach %", 75, 45, 90, "how far past an edge he stands, percent of his radius; the PS2's 45") \
    SW(line_2d, "Hold 2D line", "in side-on sections he stays on the line he came in on") \
    VAL(line_pull, "Line pull %", 100, 0, 100, "how hard he is pulled back to the line") \
    SW(hazard_hitboxes, "Fair hazard hitboxes", "what can kill him reaches about as far as it is drawn") \
    VAL(hazard_enemy, "Enemy reach %", 80, 50, 100, "percent of the PS2's") \
    VAL(hazard_fire, "Fire and steam reach %", 50, 25, 100, "share of his radius that counts against them") \
    SW(air_jump, "Double jump at any height", "the double jump at any fall speed") \
    FLAG(air_jump_ledge, "Off ledges and bars", true, "also after walking off a ledge or dropping off the bars") \
    FLAG(air_jump_free, "Before it is earned", false, "also before the power is earned") \
    VAL(air_jump_fall, "Max fall speed", 12, 5, 12, "units a second it still works at; 5 is the PS2's, 12 any")

#define NW_CRASH_BOOL(f, ...) bool f;
#define NW_CRASH_INT(f, ...) int f;
typedef struct NwCrashMovement {
    NW_CRASH_MOVEMENT(NW_CRASH_BOOL, NW_CRASH_BOOL, NW_CRASH_INT)
} NwCrashMovement;

/* N. Hanced Movement ([game.movement]), speeds in percent of retail's; 0, the zeroed profile's, is retail's too. */
typedef struct NwMovement {
    int monkey_bar_speed; /* Crash on the monkey bars */
    NwCrashMovement crash;
} NwMovement;

#define NW_CRASH_NONE(...)
#define NW_CRASH_ON(f, ...) .f = true,
#define NW_CRASH_DEFAULT(f, label, def, ...) .f = def,
#define NW_MOVEMENT_RETAIL \
    { .monkey_bar_speed = 100, .crash = { NW_CRASH_MOVEMENT(NW_CRASH_NONE, NW_CRASH_DEFAULT, NW_CRASH_DEFAULT) } }
#define NW_MOVEMENT_NHANCED \
    { .monkey_bar_speed = 350, .crash = { NW_CRASH_MOVEMENT(NW_CRASH_ON, NW_CRASH_DEFAULT, NW_CRASH_DEFAULT) } }

/* Every value at retail's is the retail profile, every one at N. Hanced's the N. Hanced profile, anything else Custom. */
typedef enum NwMovementProfile {
    NW_MOVEMENT_PROFILE_RETAIL,
    NW_MOVEMENT_PROFILE_NHANCED,
    NW_MOVEMENT_PROFILE_CUSTOM,
} NwMovementProfile;

NwMovementProfile nw_movement_profile(const NwMovement *m);

typedef struct NwProfile {
    bool unused_title; /* the unused title menu (TITLES' own level) instead of the logos and the title cut */
    NwMovement movement;
    bool air_indicator; /* a ring on the floor under Crash while he is in the air */
} NwProfile;

extern NwProfile nw_profile;

/* The windowed boot, before the game thread starts. */
void nw_profile_apply(const NwProfile *p);
/* Asks the game thread for p's movement and air indicator at its next swap */
void nw_profile_request(const NwProfile *p);

/* Marks where a feature changes the game; reached with its NwProfile switch off (every retail run), the game stops. */
#define NW_DEVIATION(sw) (nw_profile.sw ? (void)0 : nw_deviation_off(#sw))
_Noreturn void nw_deviation_off(const char *sw);

/* The game thread at each swap, before the port's level poll (src/port/ps2video.c). */
void nw_gameplay_swap(void);

#endif
