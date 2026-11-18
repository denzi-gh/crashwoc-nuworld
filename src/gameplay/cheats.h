#ifndef NUWORLD_GAMEPLAY_CHEATS_H
#define NUWORLD_GAMEPLAY_CHEATS_H

#include <stdbool.h>
#include <stdint.h>

#define NW_CHEATS_LEVELS 35
#define NW_CHEATS_POWERS 6

enum {
    NW_CHEAT_SAPPHIRE = 0x1,
    NW_CHEAT_GOLD = 0x3,
    NW_CHEAT_PLATINUM = 0x7,
    NW_CHEAT_RELIC = 0x7,
    NW_CHEAT_CRYSTAL = 0x8,
    NW_CHEAT_CRATE_GEM = 0x10,
    NW_CHEAT_BONUS_GEM = 0x20,
    NW_CHEAT_RED_GEM = 0x40,
    NW_CHEAT_GREEN_GEM = 0x80,
    NW_CHEAT_BLUE_GEM = 0x100,
    NW_CHEAT_YELLOW_GEM = 0x200,
    NW_CHEAT_PURPLE_GEM = 0x400,
    NW_CHEAT_BOSS = 0x800,
};

typedef struct NwCheatsView {
    bool valid;
    bool invincible;
    uint8_t powers;
    int level;
    uint16_t items[NW_CHEATS_LEVELS];
    uint16_t flags[NW_CHEATS_LEVELS];
    int percent, crystals, gems, relics;
} NwCheatsView;

void nw_cheats_view(NwCheatsView *out);
void nw_cheats_invincible(bool on);
void nw_cheats_powers(uint8_t mask, uint8_t value);
void nw_cheats_level(int level, uint16_t mask, uint16_t value);

void nw_cheats_swap(void);

#endif
