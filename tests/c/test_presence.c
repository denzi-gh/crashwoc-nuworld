/* frontend/presence.c: each screen's details, images and state, the state's order, the names' title case. */
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "frontend/presence.h"

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

#define DOT " \xC2\xB7 "

static NwPresenceSnap snap(int level, const char *name)
{
    NwPresenceSnap s;
    SDL_zero(s);
    s.valid = true;
    s.level = level;
    s.hub = -1;
    s.cutmovie = -1;
    s.menu = -1;
    s.vehicle = -1;
    SDL_strlcpy(s.level_name, name, sizeof(s.level_name));
    SDL_strlcpy(s.warp_room, "WARP ROOM", sizeof(s.warp_room));
    return s;
}

/* s's activity; every field fits Discord's 2-128 or is left out, and the whole fits a frame. */
static NwActivity act(const NwPresenceSnap *s)
{
    NwActivity a;
    char json[4096];
    nw_presence_activity(s, "0.7.0 BETA abc1234", 1762400000000LL, &a);
    const char *fields[] = { a.details, a.state, a.large_image, a.large_text, a.small_image, a.small_text };
    for (size_t i = 0; i < SDL_arraysize(fields); i++) {
        size_t n = strlen(fields[i]);
        CHECK(n == 0 || (n >= 2 && n < NW_DISCORD_TEXT));
    }
    CHECK(a.details[0]);
    CHECK(nw_discord_activity(json, sizeof(json), &a, 1, 1));
    return a;
}

static bool is(const char *got, const char *want)
{
    if (strcmp(got, want) != 0) {
        fprintf(stderr, "  got \"%s\", want \"%s\"\n", got, want);
        return false;
    }
    return true;
}

static void test_title(void)
{
    static const char *const cases[][2] = {
        { "RUMBLE IN THE ROKS", "Rumble in the Roks" },
        { "WIZARDS AND LIZARDS", "Wizards and Lizards" },
        { "THE GAUNTLET", "The Gauntlet" },
        { "H2 OH NO", "H2 Oh No" },
        { "CRATE BALLS OF FIRE", "Crate Balls of Fire" },
        { "CRASHES TO ASHES", "Crashes to Ashes" },
        { "SMOKEY AND THE BANDICOOT", "Smokey and the Bandicoot" },
        { "ICE STATION BANDICOOT", "Ice Station Bandicoot" },
        { "WARP ROOM", "Warp Room" },
        { "", "" },
    };
    char out[NW_PRESENCE_NAME];
    for (size_t i = 0; i < SDL_arraysize(cases); i++) {
        nw_presence_title(cases[i][0], out, sizeof(out));
        CHECK(is(out, cases[i][1]));
    }
    nw_presence_title("ARCTIC ANTICS", out, 7);
    CHECK(is(out, "Arctic"));
}

static void test_screens(void)
{
    NwPresenceSnap s;
    SDL_zero(s);
    NwActivity a = act(&s);
    CHECK(is(a.details, "Starting Up") && is(a.state, "") && is(a.large_image, "nuworld"));
    CHECK(is(a.large_text, "NuWorld 0.7.0 BETA abc1234") && a.start_ms == 1762400000000LL);
    CHECK(a.nbuttons == 2 && is(a.buttons[0].label, "Get NuWorld") &&
          is(a.buttons[0].url, "https://github.com/denzi-gh/crashwoc-nuworld") &&
          is(a.buttons[1].label, "Join the Discord") && is(a.buttons[1].url, "https://discord.gg/kmCPpW4KvJ"));

    /* The PS2's title (the logos' cut) and NuWorld's (TITLES' own level). */
    s = snap(0x25, "FRONT END");
    s.cutmovie = 0;
    a = act(&s);
    CHECK(is(a.details, "Main Menu") && is(a.state, "") && is(a.large_image, "nuworld") && is(a.small_image, ""));
    s = snap(0x23, "TITLES");
    a = act(&s);
    CHECK(is(a.details, "Main Menu") && is(a.state, ""));
    s.menu = 2;
    CHECK(is(act(&s).state, "New Game"));
    s.menu = 21;
    CHECK(is(act(&s).state, "Loading a Save"));

    s = snap(0x25, "FRONT END");
    s.percent = 42;
    s.crystals = 12;
    s.gems = 8;
    s.relics = 5;
    a = act(&s);
    CHECK(is(a.details, "Warp Room") && is(a.large_image, "warproom"));
    CHECK(is(a.state, "42%" DOT "12 Crystals" DOT "8 Gems" DOT "5 Relics"));
    CHECK(is(a.small_image, "crash") && is(a.small_text, "Crash"));
    s.hub = 0;
    SDL_strlcpy(s.room_name, "EARTH", sizeof(s.room_name));
    s.percent = 2;
    s.crystals = 1;
    s.gems = 0;
    s.relics = 1;
    a = act(&s);
    CHECK(is(a.details, "Warp Room: Earth") && is(a.large_image, "room_0"));
    CHECK(is(a.state, "2%" DOT "1 Crystal" DOT "1 Relic"));
    s.paused = true;
    CHECK(is(act(&s).state, "2%" DOT "1 Crystal" DOT "1 Relic"));
    s.menu = 19;
    CHECK(is(act(&s).state, "Save Point"));
    s.menu = 24;
    CHECK(is(act(&s).state, "Saving"));
    s.menu = 27;
    CHECK(is(act(&s).state, "Deleting a Save"));
    s.game_mode = 1;
    CHECK(is(act(&s).state, "Cutscene"));
    s = snap(0x25, "FRONT END");
    CHECK(is(act(&s).state, ""));

    s = snap(0x11, "WIZARDS AND LIZARDS");
    a = act(&s);
    CHECK(is(a.details, "Wizards and Lizards") && is(a.large_image, "level_17") && is(a.state, ""));
    CHECK(is(a.small_image, "crash") && is(a.small_text, "Crash"));
    s.collected = 0x8 | 0x10 | 0x40 | 0x3;
    CHECK(is(act(&s).state, "Crystal" DOT "2 Gems" DOT "Gold Relic"));
    s.collected = 0x20 | 0x7;
    CHECK(is(act(&s).state, "1 Gem" DOT "Platinum Relic"));
    s.collected = 0x1;
    CHECK(is(act(&s).state, "Sapphire Relic"));
    s.time_trial = true;
    CHECK(is(act(&s).state, "Time Trial"));
    s.paused = true;
    CHECK(is(act(&s).state, "Paused"));
    s.menu = 15;
    CHECK(is(act(&s).state, "New Record"));

    s = snap(5, "AVALANCHE");
    s.character = 1;
    s.vehicle = 0xA0;
    a = act(&s);
    CHECK(is(a.details, "Avalanche") && is(a.large_image, "level_05"));
    CHECK(is(a.small_image, "coco") && is(a.small_text, "Coco" DOT "Snowboard"));

    s = snap(21, "RUMBLE IN THE ROKS");
    s.level_flags = 2;
    a = act(&s);
    CHECK(is(a.details, "Rumble in the Roks") && is(a.large_image, "level_21") && is(a.state, "Boss Fight"));
    s.bonus = 2;
    CHECK(is(act(&s).state, "Bonus Round"));
    s.bonus = 4;
    CHECK(is(act(&s).state, "Boss Fight"));
    s.loading = true;
    CHECK(is(act(&s).state, "Loading"));

    s = snap(7, "ARCTIC ANTICS");
    s.loading = true;
    s.minigame = true;
    s.minigame_score = 12;
    s.minigame_best = 40;
    CHECK(is(act(&s).state, "Minigame" DOT "12 Wumpa" DOT "Best 40"));
    s.minigame_best = 0;
    CHECK(is(act(&s).state, "Minigame" DOT "12 Wumpa"));

    s = snap(0x11, "WIZARDS AND LIZARDS");
    s.demo = true;
    s.loading = true;
    a = act(&s);
    CHECK(is(a.details, "Main Menu") && is(a.state, "Demo: Wizards and Lizards") && is(a.large_image, "level_17"));
    CHECK(is(a.small_image, ""));

    s = snap(0x25, "FRONT END");
    s.cutmovie = 1;
    a = act(&s);
    CHECK(is(a.details, "Cutscene") && is(a.state, "Intro") && is(a.large_image, "nuworld") && is(a.small_image, ""));
    s.cutmovie = 4;
    CHECK(is(act(&s).state, "Ending"));
    s = snap(0x29, "OUTRO");
    CHECK(is(act(&s).state, "Ending"));
    s = snap(0x27, "INTRO");
    CHECK(is(act(&s).state, "Intro"));
    s = snap(0x28, "SPACE");
    s.game_mode = 1;
    a = act(&s);
    CHECK(is(a.details, "Cutscene") && is(a.state, ""));

    s = snap(0x26, "GAME OVER");
    a = act(&s);
    CHECK(is(a.details, "Game Over") && is(a.state, "") && is(a.large_image, "nuworld") && is(a.small_image, ""));
    s = snap(0x2B, "CREDITS");
    CHECK(is(act(&s).details, "Credits"));
    s = snap(0x24, "AIRSHIP");
    a = act(&s);
    CHECK(is(a.details, "Airship") && is(a.state, "Unused Level") && is(a.large_image, "nuworld"));
    CHECK(is(a.small_image, "crash"));
    s = snap(0x2A, "");
    CHECK(is(act(&s).details, "Level 42"));
}

int main(void)
{
    test_title();
    test_screens();
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    return 0;
}
