/* NuWorld's changes to how the game plays or looks, each behind a [game] setting; all off (zero) is the retail game that
   every check, test and recording runs, and only the windowed boot fills the profile from settings.toml. */
#ifndef NUWORLD_GAMEPLAY_GAMEPLAY_H
#define NUWORLD_GAMEPLAY_GAMEPLAY_H

#include <stdbool.h>

/* Crash's switches ([game.movement.crash]), false is retail. */
typedef struct NwCrashMovement {
    bool jump_fall;     /* shorter hang time and a faster fall, retail's height and reach */
    bool jump_distance; /* in a jump any stick tilt is a full run */
} NwCrashMovement;

/* N. Hanced Movement ([game.movement]), speeds in percent of retail's; 0, the zeroed profile's, is retail's too. */
typedef struct NwMovement {
    int monkey_bar_speed; /* Crash on the monkey bars */
    NwCrashMovement crash;
} NwMovement;

#define NW_MOVEMENT_RETAIL { .monkey_bar_speed = 100 }
#define NW_MOVEMENT_NHANCED { .monkey_bar_speed = 350, .crash = { .jump_fall = true, .jump_distance = true } }

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
