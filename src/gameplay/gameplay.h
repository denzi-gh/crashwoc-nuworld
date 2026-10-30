/* NuWorld's changes to how the game plays or looks, each behind a [game] setting; all off is the retail game that every
   check, test and recording runs, and only the windowed boot fills the profile from settings.toml. */
#ifndef NUWORLD_GAMEPLAY_GAMEPLAY_H
#define NUWORLD_GAMEPLAY_GAMEPLAY_H

#include <stdbool.h>

typedef struct NwProfile {
    bool unused_title; /* the unused title menu (TITLES' own level) instead of the logos and the title cut */
    /* Enhanced movement */
    bool fast_monkey_bars; /* Crash climbs the monkey bars twice as fast */
} NwProfile;

extern NwProfile nw_profile;

/* The windowed boot, before the game thread starts. */
void nw_profile_apply(const NwProfile *p);

/* Marks where a feature changes the game; reached with its NwProfile switch off (every retail run), the game stops. */
#define NW_DEVIATION(sw) (nw_profile.sw ? (void)0 : nw_deviation_off(#sw))
_Noreturn void nw_deviation_off(const char *sw);

/* The game thread at each swap, before the port's level poll (src/port/ps2video.c). */
void nw_gameplay_swap(void);

#endif
