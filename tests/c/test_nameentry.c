/* The names typed in game/game's name entries (port/nameentry.h), as its menus 2 (NEW GAME) and 15 (a time trial
   record) run them: retail's InputNewLetter into NuWorld's longer name (port/names.h), the game's own name taking its
   first letters; Backspace as '<', Enter as DONE and Escape as TRIANGLE pressed for one frame; a table letter taken
   from retail. */
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "game/gametypes.h"
#include "gameplay/gameplay.h"
#include "nups2/ps2ctl.h"
#include "port/nameentry.h"
#include "port/names.h"

extern struct game_s Game;
extern struct game_s SaveSlot[2][2];
extern s32 i_nameinput, Level, newleveltime_slot;

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

static struct cursor_s cursor;
static struct nupad_s pad;
static uint32_t seen; /* the pad's buttons retail's ProcMenu saw */

static uint32_t frame(void);

/* Any entry open closes first: in the game a menu opens from another one. */
static void leave(void)
{
    memset(&cursor, 0, sizeof(cursor));
    cursor.item_frame = 1;
    frame();
}

/* Retail's menu 2 as NewMenu opens it. */
static void open_entry(void)
{
    leave();
    memset(&cursor, 0, sizeof(cursor));
    cursor.menu = 2;
    cursor.x_max = 6;
    cursor.y_max = 5;
    cursor.item_frame = 1;
    Game.language = 0;
    strcpy(Game.name, "________");
    i_nameinput = 0;
}

/* Retail's menu 15 as NewLevelTime and NewMenu leave it for record slot of the level. */
static void open_record(int level, int slot)
{
    leave();
    memset(&cursor, 0, sizeof(cursor));
    cursor.menu = 15;
    cursor.x_max = 6;
    cursor.y_max = 4;
    cursor.item_frame = 1;
    Level = level;
    newleveltime_slot = slot;
    strcpy(Game.level[level].time[slot].name, "___");
    i_nameinput = 0;
}

/* One menu frame: the entry, retail's ProcMenu (here: what it would see), the entry's buttons handed back. */
static uint32_t frame(void)
{
    uint32_t taken = 0, pressed = nw_nameentry_proc(&cursor, &pad, &taken);
    seen = pad.paddata_db;
    nw_nameentry_done(&pad, pressed, taken);
    return pressed;
}

static void type(const char *text)
{
    for (; *text; text++) {
        nw_nameentry_type((unsigned char)*text);
    }
}

static const char *longer(void)
{
    return nw_names_game()->game;
}

static void test_game_name(void)
{
    /* closed: nothing is taken */
    memset(&cursor, 0, sizeof(cursor));
    cursor.item_frame = 1;
    frame();
    CHECK(!nw_nameentry_wanted());
    type("A");
    open_entry();
    frame();
    CHECK(nw_nameentry_wanted() && strcmp(Game.name, "________") == 0 && i_nameinput == 0);
    CHECK(strcmp(nw_nameentry_name(), "________________") == 0);

    /* letters (lower case too), digits and spaces; anything else is dropped; the game's own takes 8 */
    type("cr4sh!\xc3\xa9");
    CHECK(frame() == 0 && strcmp(Game.name, "CR4SH___") == 0 && i_nameinput == 5);
    nw_nameentry_type(NW_NAMEENTRY_BACK);
    nw_nameentry_type(NW_NAMEENTRY_BACK);
    type(" ");
    frame();
    CHECK(strcmp(Game.name, "CR4_____") == 0 && i_nameinput == 4);
    for (int i = 0; i < 6; i++) {
        nw_nameentry_type(NW_NAMEENTRY_BACK);
    }
    frame();
    CHECK(strcmp(Game.name, "________") == 0 && i_nameinput == 0);

    /* up to 16 letters; past the last, as retail, the last one is replaced */
    type("NUWORLDTEAMCRASH");
    frame();
    CHECK(strcmp(nw_nameentry_name(), "NUWORLDTEAMCRASH") == 0 && strcmp(Game.name, "NUWORLDT") == 0);
    type("Y");
    frame();
    CHECK(strcmp(nw_nameentry_name(), "NUWORLDTEAMCRASY") == 0 && i_nameinput == 16);

    /* a table letter: the longer name takes it, retail does not see the CROSS; full, the cursor goes to DONE */
    open_entry();
    frame();
    cursor.x = 2; /* C */
    pad.paddata_db = 0x40;
    CHECK(frame() == 0 && (seen & 0x40) == 0 && pad.paddata_db == 0x40);
    pad.paddata_db = 0;
    CHECK(strcmp(Game.name, "C_______") == 0 && i_nameinput == 1);
    cursor.y = 3;
    cursor.x = 6; /* '<' */
    pad.paddata_db = 0x40;
    frame();
    pad.paddata_db = 0;
    CHECK(strcmp(Game.name, "________") == 0 && i_nameinput == 0);
    type("ABCDEFGHIJKLMNO");
    frame();
    cursor.y = 0;
    cursor.x = 0;
    pad.paddata_db = 0x40;
    frame();
    pad.paddata_db = 0;
    CHECK(cursor.y == cursor.y_max - 1 && strcmp(nw_nameentry_name(), "ABCDEFGHIJKLMNOA") == 0);

    /* Enter: retail's CROSS on DONE for one frame; what was typed after waits for the next */
    open_entry();
    frame();
    type("NUWORLD TEAM");
    nw_nameentry_type(NW_NAMEENTRY_DONE);
    type("X");
    CHECK(frame() == 0x40 && (seen & 0x40) && cursor.y == cursor.y_max - 1 && (pad.paddata_db & 0x40) == 0);
    CHECK(strcmp(Game.name, "NUWORLD_") == 0);
    frame();
    CHECK(strcmp(nw_nameentry_name(), "NUWORLD_TEAMX___") == 0);

    /* closed (retail cleaned its own name): the longer one stands, trimmed; shown in place of the game's own */
    strcpy(Game.name, "NUWORLD ");
    cursor.menu = 0;
    frame();
    CHECK(!nw_nameentry_wanted() && strcmp(longer(), "NUWORLD TEAMX") == 0);
    CHECK(strcmp(nw_names_of(&Game), "NUWORLD TEAMX") == 0);
    strcpy(Game.name, "CRASH   "); /* a name the game set itself wins */
    CHECK(strcmp(nw_names_of(&Game), "CRASH   ") == 0);

    /* saved to a slot, loaded back */
    strcpy(Game.name, "NUWORLD ");
    SaveSlot[0][1] = Game;
    nw_names_save(1);
    CHECK(strcmp(nw_names_of(&SaveSlot[0][1]), "NUWORLD TEAMX") == 0);
    strcpy(nw_names_game()->game, "OTHER");
    nw_names_load(1);
    CHECK(strcmp(nw_names_of(&Game), "NUWORLD TEAMX") == 0);

    /* Escape: retail's TRIANGLE; a button the pad presses itself stays its own */
    open_entry();
    frame();
    nw_nameentry_type(NW_NAMEENTRY_QUIT);
    CHECK(frame() == 0x10 && (seen & 0x10) && (pad.paddata_db & 0x10) == 0);
    pad.paddata_db = 0x10;
    nw_nameentry_type(NW_NAMEENTRY_QUIT);
    CHECK(frame() == 0 && pad.paddata_db == 0x10);
    pad.paddata_db = 0;

    /* nothing is taken on the menu's first frame (retail takes no button then) */
    open_entry();
    cursor.menu = 0;
    frame();
    open_entry();
    cursor.item_frame = 0;
    frame();
    type("Z");
    frame();
    CHECK(strcmp(Game.name, "________") == 0);
    cursor.item_frame = 1;
    frame();
    CHECK(strcmp(Game.name, "Z_______") == 0);

    /* leaving the entry drops what was typed; the Japanese build's alphabets stay retail's */
    type("Q");
    cursor.menu = 0;
    frame();
    open_entry();
    frame();
    frame();
    CHECK(strcmp(Game.name, "________") == 0);
    Game.language = 'c';
    frame();
    CHECK(!nw_nameentry_wanted());
    Game.language = 0;

    /* the device used last decides the table; retail draws it while the controller is */
    cursor.menu = 0;
    frame();
    nw_nameentry_set_keyboard(false);
    CHECK(!nw_nameentry_keyboard() && !nw_nameentry_draw(&cursor));
    nw_nameentry_set_keyboard(true);
    CHECK(nw_nameentry_keyboard());
}

static void test_record_name(void)
{
    /* the original's 3 letters */
    nw_profile.long_time_names = false;
    open_record(4, 1);
    frame();
    CHECK(nw_nameentry_wanted() && strcmp(nw_nameentry_name(), "___") == 0);
    type("BOBBY");
    frame();
    CHECK(strcmp(Game.level[4].time[1].name, "BOY") == 0 && i_nameinput == 3);
    nw_nameentry_type(NW_NAMEENTRY_QUIT); /* a record's name has no way back */
    CHECK(frame() == 0);
    nw_nameentry_type(NW_NAMEENTRY_DONE);
    CHECK(frame() == 0x40 && cursor.y == cursor.y_max);

    /* up to 8 when asked: the record keeps its 3, its line shows the longer one */
    nw_profile.long_time_names = true;
    cursor.menu = 0;
    frame();
    open_record(5, 0);
    frame();
    CHECK(strcmp(nw_nameentry_name(), "________") == 0);
    type("ROBERTA");
    frame();
    CHECK(strcmp(Game.level[5].time[0].name, "ROB") == 0);
    CHECK(strcmp(nw_names_time_text(&Game.level[5].time[0]), "ROBERTA_") == 0);
    cursor.menu = 0;
    frame();
    CHECK(strcmp(nw_names_game()->time[5][0], "ROBERTA") == 0);
    CHECK(strcmp(nw_names_time_text(&Game.level[5].time[0]), "ROBERTA") == 0);
    CHECK(strcmp(nw_names_time_text(&Game.level[5].time[1]), Game.level[5].time[1].name) == 0);
    strcpy(Game.level[5].time[0].name, "1ST"); /* a placeholder the game set wins */
    CHECK(strcmp(nw_names_time_text(&Game.level[5].time[0]), "1ST") == 0);
    nw_profile.long_time_names = false;
}

int main(void)
{
    test_game_name();
    test_record_name();
    if (failures) {
        fprintf(stderr, "nameentry: %d checks failed\n", failures);
        return 1;
    }
    printf("nameentry: all checks passed\n");
    return 0;
}
