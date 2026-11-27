/* A level asked for from any thread is entered at a game-thread swap, as retail's debug level select, no fade out. */
#ifndef NUWORLD_PORT_LEVEL_H
#define NUWORLD_PORT_LEVEL_H

#include <stdbool.h>
#include <stddef.h>

#define NW_LEVEL_HUB 0x25
#define NW_LEVEL_GAMEOVER 0x26
#define NW_LEVEL_SPACE 0x28

int nw_level_count(void);
/* The English name, NULL for a level the disc lacks. */
const char *nw_level_name(int level);
/* Game order: the warp rooms' levels, each room's boss last (slot 5), then the rest in index order with room 0. */
int nw_level_at(int i, int *room, int *slot);
int nw_level_current(void);
int nw_level_pending(void);
void nw_level_load(int level);

int nw_cutscene_count(void);
const char *nw_cutscene_name(int i, const char **group);
int nw_cutscene_current(void);
int nw_cutscene_pending(void);
void nw_cutscene_play(int i);
bool nw_cutscene_route(int next);

/* --level: LData indices or file names, comma-separated ("intro" first is NEW GAME); false when the disc lacks one. */
bool nw_level_start(const char *names);
int nw_level_final(void);
int nw_level_arrival(void);
/* Loaded, faded in, its arrival cut skipped. */
bool nw_level_arrived(void);

typedef struct NwLevelArrival {
    int level;
    size_t gpu; /* the scene GPU objects alive (src/port/heap.h) when it ran */
} NwLevelArrival;
int nw_level_arrivals(const NwLevelArrival **out);

/* The game thread, at each swap. */
void nw_level_poll(void);

#endif
