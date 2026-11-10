/* NEW GAME's name typed on the keyboard (port/nameentry.h) as game/game's menu 2 runs it: retail's InputNewLetter for
   each typed letter, Backspace as its '<', Enter as DONE and Escape as TRIANGLE pressed for one frame. */
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "game/gametypes.h"
#include "nups2/ps2ctl.h"
#include "port/nameentry.h"

extern struct game_s Game;
extern s32 i_nameinput;

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

/* Retail's menu 2 as NewMenu opens it. */
static void open_entry(void)
{
    memset(&cursor, 0, sizeof(cursor));
    cursor.menu = 2;
    cursor.x_max = 6;
    cursor.y_max = 5;
    cursor.item_frame = 1;
    Game.language = 0;
    strcpy(Game.name, "________");
    i_nameinput = 0;
}

/* One menu frame: the entry, retail's ProcMenu (here: what it would see), the entry's buttons released. */
static uint32_t frame(void)
{
    uint32_t pressed = nw_nameentry_proc(&cursor, &pad);
    seen = pad.paddata_db;
    nw_nameentry_done(&pad, pressed);
    return pressed;
}

static void type(const char *text)
{
    for (; *text; text++) {
        nw_nameentry_type((unsigned char)*text);
    }
}

int main(void)
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

    /* letters (lower case too), digits and spaces; anything else is dropped */
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

    /* past the last letter, as retail: the last one is replaced */
    type("ABCDEFGHI");
    frame();
    CHECK(strcmp(Game.name, "ABCDEFGI") == 0 && i_nameinput == 8);

    /* Enter: retail's CROSS on DONE for one frame; what was typed after waits for the next */
    open_entry();
    frame();
    type("AKU");
    nw_nameentry_type(NW_NAMEENTRY_DONE);
    type("X");
    CHECK(frame() == 0x40 && (seen & 0x40) && cursor.y == cursor.y_max - 1 && (pad.paddata_db & 0x40) == 0);
    CHECK(strcmp(Game.name, "AKU_____") == 0);
    frame();
    CHECK(strcmp(Game.name, "AKUX____") == 0);

    /* Escape: retail's TRIANGLE; a button the pad presses itself stays its own */
    nw_nameentry_type(NW_NAMEENTRY_QUIT);
    CHECK(frame() == 0x10 && (seen & 0x10) && (pad.paddata_db & 0x10) == 0);
    pad.paddata_db = 0x40;
    nw_nameentry_type(NW_NAMEENTRY_DONE);
    CHECK(frame() == 0 && pad.paddata_db == 0x40);
    pad.paddata_db = 0;

    /* nothing is taken on the menu's first frame (retail takes no button then) */
    open_entry();
    frame();
    cursor.item_frame = 0;
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
    CHECK(!nw_nameentry_wanted());
    open_entry();
    frame();
    frame();
    CHECK(strcmp(Game.name, "________") == 0);
    Game.language = 'c';
    frame();
    CHECK(!nw_nameentry_wanted());
    Game.language = 0;

    /* the device used last decides the table; retail draws it while the controller is */
    open_entry();
    frame();
    nw_nameentry_set_keyboard(false);
    CHECK(!nw_nameentry_keyboard() && !nw_nameentry_draw(&cursor));
    nw_nameentry_set_keyboard(true);
    CHECK(nw_nameentry_keyboard());
    cursor.menu = 0;
    frame();
    CHECK(!nw_nameentry_draw(&cursor));

    if (failures) {
        fprintf(stderr, "nameentry: %d checks failed\n", failures);
        return 1;
    }
    printf("nameentry: all checks passed\n");
    return 0;
}
