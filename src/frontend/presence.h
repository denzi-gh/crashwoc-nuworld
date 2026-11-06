/* Rich Presence: the game's state as plain values (filled by port/presence.c on the game thread) and the Discord
   activity for it. Art asset keys: level_00..level_34 (no bosses), nuworld. */
#ifndef NUWORLD_FRONTEND_PRESENCE_H
#define NUWORLD_FRONTEND_PRESENCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "platform/discord.h"

#define NW_PRESENCE_NAME 40

typedef struct NwPresenceSnap {
    bool valid; /* the game thread has swapped */
    int level;  /* Level */
    int hub;    /* Hub: the warp room the player is in, -1 the central hall */
    int game_mode; /* GameMode, 1 a cutscene */
    int cutmovie;  /* -1 none, 0 logos and title, 1-2 intro, 3-4 ending */
    int menu;      /* Cursor.menu, -1 none */
    int bonus;     /* Bonus: 1-3 on the way in, in and out of a bonus round */
    bool paused, loading, demo, time_trial, minigame;
    int minigame_score, minigame_best;
    unsigned level_flags; /* LData[level].flags, 2 a boss */
    unsigned collected;   /* Game.level[level].flags */
    int percent, crystals, gems, relics;
    char level_name[NW_PRESENCE_NAME]; /* LevelName[level][0], upper case as the game's */
    char room_name[NW_PRESENCE_NAME];  /* HubName[hub][0] */
    char warp_room[NW_PRESENCE_NAME];  /* tWARPROOM[0] */
} NwPresenceSnap;

/* version: the hover text's ("NuWorld <version>"); start_ms the elapsed timer's start. */
void nw_presence_activity(const NwPresenceSnap *s, const char *version, int64_t start_ms, NwActivity *out);
/* "RUMBLE IN THE ROKS" as "Rumble in the Roks". */
void nw_presence_title(const char *upper, char *out, size_t size);

#endif
