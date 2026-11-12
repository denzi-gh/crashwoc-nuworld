/* Desktop exit through the real menu navigation and confirmation guards, without game assets. */
#include <stdio.h>

#include <SDL3/SDL.h>

#include "game/gametypes.h"
#include "nups2/ps2ctl.h"

extern struct game_s Game;
extern s32 LANGUAGEOPTION, TimeTrial, Level, Paused, fadeval, editor_active, ForceRestart;
extern s32 cutmovie, new_mode, new_level, stick_bits, stick_bits_db, pause_dir;
void GetMenuInfo(struct cursor_s *cursor);
void ProcMenu(struct cursor_s *cursor, struct nupad_s *pad);

static int failures;
#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

static bool quit_event(void)
{
    /* Peep directly: no window or video event pump is needed. */
    SDL_Event event;
    return SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_EVENT_QUIT, SDL_EVENT_QUIT) == 1;
}

static struct cursor_s open_menu(int menu)
{
    struct cursor_s cursor = { 0 };
    cursor.menu = menu;
    cursor.item_frame = 1;
    GetMenuInfo(&cursor);
    cursor.y = cursor.y_max;
    fadeval = editor_active = ForceRestart = stick_bits = stick_bits_db = 0;
    cutmovie = new_mode = new_level = -1;
    Paused = menu == 3 ? 25 : 0;
    Level = 1;
    Game.language = 3;
    SDL_FlushEvent(SDL_EVENT_QUIT);
    return cursor;
}

int main(void)
{
    CHECK(SDL_Init(SDL_INIT_EVENTS));
    struct nupad_s pad = { 0 };
    pad.paddata_db = 0x40; /* confirm */

    for (int language = 0; language <= 1; language++) {
        LANGUAGEOPTION = language;
        for (int trial = 0; trial <= 1; trial++) {
            TimeTrial = trial;
            for (int menu = 0; menu <= 3; menu += 3) {
                struct cursor_s cursor = open_menu(menu);
                CHECK(cursor.y_max == (menu == 0 ? 2 + language : 3 + trial));
                /* Recomputing bounds must not append the row twice. */
                GetMenuInfo(&cursor);
                CHECK(cursor.y_max == (menu == 0 ? 2 + language : 3 + trial));
                ProcMenu(&cursor, &pad);
                CHECK(!quit_event());
                CHECK(cursor.menu == (menu == 0 ? 34 : 35) && cursor.y == 1);
                CHECK(new_level == -1 && new_mode == -1);
                /* No is the default; confirming it restores the exit row. */
                cursor.item_frame = 1;
                ProcMenu(&cursor, &pad);
                CHECK(cursor.menu == menu && cursor.y == cursor.y_max && !quit_event());
                cursor.item_frame = 1;
                ProcMenu(&cursor, &pad);
                cursor.item_frame = 1;
                cursor.y = 0;
                ProcMenu(&cursor, &pad);
                CHECK(quit_event() && !quit_event());
            }
        }
    }

    LANGUAGEOPTION = 0;
    TimeTrial = 0;
    struct cursor_s cursor = open_menu(0);
    /* The former language row is now exit: left/right must not change the language. */
    pad.paddata_db = 0x2000;
    ProcMenu(&cursor, &pad);
    CHECK(Game.language == 3 && !quit_event());

    /* Wrap from the first row to exit, with confirm in the same input frame. */
    cursor = open_menu(0);
    cursor.y = 0;
    pad.paddata_db = 0x1040;
    ProcMenu(&cursor, &pad);
    CHECK(cursor.menu == 34 && cursor.y == 1 && !quit_event());

    /* Moving off exit while confirming still performs the destination action. */
    cursor = open_menu(3);
    pause_dir = 0;
    pad.paddata_db = 0x4040;
    ProcMenu(&cursor, &pad);
    CHECK(cursor.y == 0 && pause_dir == 2 && !quit_event());

    pad.paddata_db = 0x40;
    cursor = open_menu(0);
    cursor.item_frame = 0;
    ProcMenu(&cursor, &pad);
    CHECK(!quit_event());
    cursor = open_menu(0);
    cursor.button_lock = 2;
    ProcMenu(&cursor, &pad);
    CHECK(!quit_event());
    cursor = open_menu(0);
    cursor.wait = 2;
    ProcMenu(&cursor, &pad);
    CHECK(!quit_event());
    cursor = open_menu(0);
    fadeval = 1;
    ProcMenu(&cursor, &pad);
    CHECK(!quit_event());
    cursor = open_menu(3);
    Paused = 24;
    ProcMenu(&cursor, &pad);
    CHECK(!quit_event());
    cursor = open_menu(0);
    ProcMenu(&cursor, NULL);
    CHECK(!quit_event());

    /* All supported languages: cancel/back returns to the originating menu. */
    for (int language = 0; language < 6; language++) {
        for (int menu = 0; menu <= 3; menu += 3) {
            cursor = open_menu(menu);
            Game.language = language;
            pad.paddata_db = 0x40;
            ProcMenu(&cursor, &pad);
            CHECK(cursor.menu == (menu == 0 ? 34 : 35) && !quit_event());
            cursor.item_frame = 1;
            pad.paddata_db = 0x10;
            ProcMenu(&cursor, &pad);
            CHECK(cursor.menu == menu && cursor.y == cursor.y_max && !quit_event());
        }
    }

    /* Options and unrelated menus keep their retail bounds. */
    cursor = open_menu(4);
    CHECK(cursor.y_max == 4); /* autosave before DONE */
    cursor = open_menu(8);
    CHECK(cursor.y_max == 1);
    SDL_Quit();
    return failures ? 1 : 0;
}
