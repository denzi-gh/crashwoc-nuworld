/* The port's gamelib/saveload under the decomp's own loadsaveCallEachFrame and UpdateSaveSlots, frame by frame as
   ProcMenu runs them: the first card check, load, save, delete and format, their statuses and the menus' flags; the
   sync calls; no card; a save that cannot be written or comes from a newer build. With "card", the PS2 card image
   NUWORLD_TEST_CARD (exit 77 without it) is imported and must come back byte for byte, through the file and through
   the game. Usage: test_saveload <scratch dir> [card]. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "game/gametypes.h"
#include "platform/paths.h"
#include "port/saveload.h"
#include "save/ps2card.h"

#define SAVE_SIZE ((int)sizeof(SaveSlot))

extern struct game_s SaveSlot[2][2];
extern s32 saveload_status, saveload_error, saveload_cardtype, saveload_freespace, saveload_cardformatted,
    saveload_cardchanged;
extern s32 memcard_gamesavailable, memcard_loadattempted, memcard_loadcompleted, memcard_loadfailed,
    memcard_saveneeded, memcard_savestarted, memcard_savefailed, memcard_deleteneeded, memcard_deletestarted,
    memcard_deletefailed, memcard_formatme, memcard_formatting, memcard_formatfailed;
void loadsaveCallEachFrame(void);
void UpdateSaveSlots(struct cursor_s *cursor);
s32 saveloadCheckCardPresent(void);
s32 saveloadGetDirectory(void);
s32 saveloadSaveSlot(s32 slot, void *buf, s32 size);
s32 saveloadDeleteSlot(s32 slot);
s32 saveloadCheckCardType(void);
s32 saveloadCheckCardFormatted(void);
s32 saveloadCheckCardFreeSpace(s32 size);
s32 saveloadFormatCard(void);
s32 saveloadUnFormatCard(void);
s32 saveloadCheckSlotsUsed(void);
s32 saveloadLoadSlot(s32 slot, void *buf, s32 size);

static int failures;
static char dir[1100];

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: CHECK(%s) failed; last error: %s\n", __FILE__, __LINE__, #cond, SDL_GetError()); \
            failures++; \
        } \
    } while (0)

static unsigned seen;   /* bit n: status n was seen after a frame */
static bool changed;    /* saveload_cardchanged was set */

/* One menu frame: what ProcMenu's load and save menus run. */
static void frame(void)
{
    loadsaveCallEachFrame();
    UpdateSaveSlots(NULL);
    if (saveload_status >= 0 && saveload_status < 32) {
        seen |= 1u << saveload_status;
    }
    changed = changed || saveload_cardchanged;
}

#define RUN_UNTIL(cond) \
    do { \
        for (int frames_ = 0; frames_ < 60 && !(cond); frames_++) { \
            frame(); \
        } \
        CHECK(cond); \
    } while (0)

/* The card as the game finds it at boot, the menus' flags cleared. */
static void boot(const char *card)
{
    nw_saveload_init(card);
    saveload_status = 0;
    saveload_error = 0;
    saveload_cardtype = saveload_freespace = saveload_cardformatted = -1;
    saveload_cardchanged = 0;
    memcard_gamesavailable = memcard_loadattempted = memcard_loadcompleted = memcard_loadfailed = 0;
    memcard_saveneeded = memcard_savestarted = memcard_savefailed = 0;
    memcard_deleteneeded = memcard_deletestarted = memcard_deletefailed = 0;
    memcard_formatme = memcard_formatting = memcard_formatfailed = 0;
    seen = 0;
    changed = false;
}

static char *path_of(const char *name)
{
    static char path[1200];
    SDL_snprintf(path, sizeof(path), "%s%s", dir, name);
    return path;
}

static bool there(const char *name)
{
    return SDL_GetPathInfo(path_of(name), NULL);
}

static void fill(uint32_t seed)
{
    uint8_t *p = (uint8_t *)SaveSlot;
    for (int i = 0; i < SAVE_SIZE; i++) {
        seed = seed * 1103515245u + 12345u;
        p[i] = (uint8_t)(seed >> 16);
    }
    SaveSlot[0][0].empty = 0;
    SaveSlot[0][1].empty = 1;
    SaveSlot[1][0].empty = 0;
    memset(&SaveSlot[1][1], 0, sizeof(SaveSlot[1][1]));
    SaveSlot[1][1].empty = 1;
}

/* The game's load on the next menu frames, as NewMenu 19 asks for it. */
static void reload(void)
{
    memcard_loadattempted = 0;
    RUN_UNTIL(memcard_loadattempted && memcard_loadcompleted);
}

static void save(void)
{
    memcard_saveneeded = 1;
    RUN_UNTIL(!memcard_saveneeded && !memcard_savestarted);
}

static void test_game(void)
{
    static struct game_s expected[2][2];
    CHECK(nw_paths_remove_tree(dir) && SDL_CreateDirectory(dir));
    boot(dir);

    /* the first check, then the load the menu starts at once: no save */
    frame();
    CHECK(saveload_status == 3 && saveload_cardtype == 2 && saveload_cardformatted == 1 &&
          saveload_freespace >= 0x42);
    frame();
    CHECK(saveload_status == 5 && memcard_loadattempted && !memcard_loadcompleted);
    frame();
    CHECK(saveload_status == 1 && saveload_error == 5 && memcard_loadcompleted);
    CHECK(!memcard_gamesavailable && !memcard_loadfailed);

    fill(1);
    memcpy(expected, SaveSlot, sizeof(expected));
    seen = 0;
    save();
    CHECK(!memcard_savefailed && memcard_gamesavailable && there(NW_SAVE_CARD_NAME "slot00.toml"));
    CHECK((seen & (1u << 9 | 1u << 3)) == (1u << 9 | 1u << 3));

    memset(SaveSlot, 0x55, sizeof(SaveSlot));
    reload();
    CHECK(memcard_gamesavailable && !memcard_loadfailed && memcmp(SaveSlot, expected, sizeof(expected)) == 0);

    /* idle: retail's card check every 5 frames, never a card change */
    int checks = 0;
    for (int i = 0; i < 30; i++) {
        frame();
        checks += saveload_status == 2;
    }
    CHECK(checks == 5 && !changed);

    /* the LData keys */
    size_t size = 0;
    char *text = SDL_LoadFile(path_of(NW_SAVE_CARD_NAME "slot00.toml"), &size);
    CHECK(text && strstr(text, "[slot.0.level.\"a/cold_a/cold_a\"]") && strstr(text, "\"a/seashell/seashell\"]"));
    SDL_free(text);

    /* delete: no data afterwards, and no import over it */
    memcard_deleteneeded = 1;
    RUN_UNTIL(!memcard_deleteneeded && !memcard_deletestarted);
    CHECK(!memcard_deletefailed && !there(NW_SAVE_CARD_NAME "slot00.toml"));
    CHECK(there(NW_SAVE_CARD_NAME "slot00.toml.bak"));
    CHECK(SDL_SaveFile(path_of(NW_SAVE_CARD_NAME "slot00"), expected, sizeof(expected)));
    reload();
    CHECK(!memcard_gamesavailable && !memcard_loadfailed && saveload_error == 5);
    memcard_deleteneeded = 1;
    RUN_UNTIL(!memcard_deleteneeded && !memcard_deletestarted);
    CHECK(memcard_deletefailed);

    /* format: done at status 3, and a new load follows */
    seen = 0;
    memcard_formatme = 1;
    RUN_UNTIL(!memcard_formatme && !memcard_formatting);
    CHECK(!memcard_formatfailed && (seen & 1u << 4) && (seen & 1u << 3));
    RUN_UNTIL(memcard_loadattempted && memcard_loadcompleted);

    /* a save from a newer build: fails to load, and is not overwritten */
    static const char newer[] = "format = 2\n";
    CHECK(SDL_SaveFile(path_of(NW_SAVE_CARD_NAME "slot00.toml"), newer, sizeof(newer) - 1));
    reload();
    CHECK(memcard_loadfailed && !memcard_gamesavailable);
    fill(2);
    save();
    CHECK(memcard_savefailed);
    text = SDL_LoadFile(path_of(NW_SAVE_CARD_NAME "slot00.toml"), &size);
    CHECK(text && size == sizeof(newer) - 1 && memcmp(text, newer, size) == 0);
    SDL_free(text);

    /* a card folder that cannot be written */
    boot(path_of("missing/folder/"));
    RUN_UNTIL(memcard_loadattempted && memcard_loadcompleted);
    CHECK(!memcard_gamesavailable && !memcard_loadfailed);
    save();
    CHECK(memcard_savefailed && !changed);

    /* no card: no load is tried */
    boot(NULL);
    for (int i = 0; i < 10; i++) {
        frame();
    }
    CHECK(saveload_status == 1 && saveload_cardtype == -1 && !memcard_loadattempted && !changed);
    CHECK(!saveloadCheckCardPresent() && !saveloadCheckCardType() && saveloadGetDirectory() == -1);
    CHECK(!saveloadCheckCardFreeSpace(0) && !saveloadSaveSlot(0, SaveSlot, SAVE_SIZE));
}

static void test_sync(void)
{
    static struct game_s back[2][2];
    char text[32] = "debug menu text";
    CHECK(nw_paths_remove_tree(dir) && SDL_CreateDirectory(dir));
    boot(dir);
    fill(3);
    CHECK(saveloadCheckCardPresent() && saveloadCheckCardType() && saveloadCheckCardFormatted());
    CHECK(saveloadCheckCardFreeSpace(0) == 8000 && saveloadCheckCardFreeSpace(0x42) == 1 &&
          saveloadCheckCardFreeSpace(9000) == 0);
    CHECK(saveloadGetDirectory() == 0 && saveloadCheckSlotsUsed() == 0 && !saveloadLoadSlot(0, back, SAVE_SIZE));
    CHECK(saveloadSaveSlot(0, SaveSlot, SAVE_SIZE) && saveloadGetDirectory() == 2 && saveloadCheckSlotsUsed() == 1);
    CHECK(saveloadLoadSlot(0, back, SAVE_SIZE) && memcmp(back, SaveSlot, sizeof(back)) == 0);
    CHECK(!saveloadSaveSlot(1, text, sizeof(text)) && !saveloadLoadSlot(1, text, sizeof(text)));
    CHECK(!saveloadSaveSlot(4, SaveSlot, SAVE_SIZE) && !saveloadLoadSlot(4, back, SAVE_SIZE));
    CHECK(saveloadSaveSlot(2, SaveSlot, (int)sizeof(struct game_s)) && saveloadGetDirectory() == 3);
    CHECK(saveloadDeleteSlot(0) && !saveloadDeleteSlot(0) && !saveloadLoadSlot(0, back, SAVE_SIZE));
    CHECK(saveloadDeleteSlot(2) && saveloadGetDirectory() == 0);
    CHECK(saveloadFormatCard() && !saveloadUnFormatCard());
}

/* NUWORLD_TEST_CARD's save: imported, then loaded from the file and by the game, then saved by the game. */
static int test_card(void)
{
    const char *card = SDL_getenv("NUWORLD_TEST_CARD");
    if (!card || !*card) {
        printf("saveload-card: NUWORLD_TEST_CARD is not set; skipped\n");
        return 77;
    }
    size_t size = 0, len = 0;
    bool missing = false;
    uint8_t *image = SDL_LoadFile(card, &size);
    uint8_t *raw = image ? nw_ps2card_file(image, size, NW_SAVE_CARD_NAME, NW_SAVE_CARD_NAME "slot00", &len, &missing)
                         : NULL;
    CHECK(raw && len == (size_t)SAVE_SIZE);
    if (!raw || len != (size_t)SAVE_SIZE) {
        SDL_free(image);
        return 1;
    }
    CHECK(nw_paths_remove_tree(dir) && SDL_CreateDirectory(dir));
    CHECK(nw_saveload_import(dir, card) == NW_SAVE_IMPORTED);

    static struct game_s back[2][2];
    boot(dir);
    CHECK(saveloadLoadSlot(0, back, SAVE_SIZE) && memcmp(back, raw, len) == 0);
    memset(SaveSlot, 0x55, sizeof(SaveSlot));
    RUN_UNTIL(memcard_loadattempted && memcard_loadcompleted);
    CHECK(memcard_gamesavailable && memcmp(SaveSlot, raw, len) == 0);
    save();
    CHECK(!memcard_savefailed);
    memset(SaveSlot, 0x55, sizeof(SaveSlot));
    reload();
    CHECK(memcard_gamesavailable && memcmp(SaveSlot, raw, len) == 0);
    for (int s = 0; s < 4; s++) {
        const struct game_s *g = &SaveSlot[s / 2][s % 2];
        printf("saveload-card: slot %d: %s, %.8s, %d%%\n", s, g->empty ? "empty" : "game", g->name, g->percent);
    }
    SDL_free(raw);
    SDL_free(image);
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc < 2 || argc > 3 || (argc == 3 && strcmp(argv[2], "card") != 0)) {
        fprintf(stderr, "usage: test_saveload <scratch dir> [card]\n");
        return 2;
    }
    SDL_snprintf(dir, sizeof(dir), "%s/", argv[1]);
    int code = 0;
    if (argc == 3) {
        code = test_card();
    } else {
        test_game();
        test_sync();
        /* what tests/save_toml.py reads with Python's tomllib */
        CHECK(nw_paths_remove_tree(dir) && SDL_CreateDirectory(dir));
        boot(dir);
        fill(4);
        RUN_UNTIL(memcard_loadattempted && memcard_loadcompleted);
        save();
    }
    nw_saveload_init(NULL);
    if (code == 77) {
        return code;
    }
    if (failures) {
        fprintf(stderr, "saveload: %d checks failed\n", failures);
        return 1;
    }
    printf("saveload: all checks passed\n");
    return 0;
}
