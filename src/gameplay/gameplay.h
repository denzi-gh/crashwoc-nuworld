/* The game layer: NuWorld's changes to how the game plays or what it shows, each behind a [game] setting. With every
   switch off the profile is the retail game, which every check, test and recording run plays; only the windowed boot
   fills it from settings.toml, before the game thread starts. */
#ifndef NUWORLD_GAMEPLAY_GAMEPLAY_H
#define NUWORLD_GAMEPLAY_GAMEPLAY_H

#include <stdbool.h>

typedef struct NwProfile {
    bool unused_title; /* the unused title menu (TITLES' own level) instead of the logos and the title cut */
} NwProfile;

extern NwProfile nw_profile;

/* The windowed boot, before the game thread starts. */
void nw_profile_apply(const NwProfile *p);

/* Marks where a feature changes the game, by its NwProfile switch. Reached with that switch off (every retail run
   included), the feature runs without it: the game stops. */
#define NW_DEVIATION(sw) (nw_profile.sw ? (void)0 : nw_deviation_off(#sw))
_Noreturn void nw_deviation_off(const char *sw);

/* The game thread at each swap, before the port's level poll (src/port/ps2video.c). */
void nw_gameplay_swap(void);

#endif
