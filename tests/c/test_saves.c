/* The port's saves (port/saves.h) as the save menus use them: SaveSlot a page of 4, written and deleted at once, the
   result screens' frames, the saves from elsewhere converted; with "ps2", NUWORLD_TEST_CARD's games must come back
   byte for byte. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "game/gametypes.h"
#include "platform/paths.h"
#include "port/saves.h"
#include "save/ps2card.h"

#define PAGE_SIZE ((int)sizeof(SaveSlot))
#define RAW NW_SAVE_PS2_NAME "slot00" /* a raw PS2 save */

extern struct game_s SaveSlot[2][2];

static int failures;
static char dir[1100];

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: CHECK(%s) failed; last error: %s\n", __FILE__, __LINE__, #cond, SDL_GetError()); \
            failures++; \
        } \
    } while (0)

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

static void fresh(void)
{
    CHECK(nw_paths_remove_tree(dir) && SDL_CreateDirectory(dir));
}

/* SaveSlot as the game would fill it: games in [0][0] and [1][0], [0][1] and [1][1] empty. */
static void fill(uint32_t seed)
{
    uint8_t *p = (uint8_t *)SaveSlot;
    for (int i = 0; i < PAGE_SIZE; i++) {
        seed = seed * 1103515245u + 12345u;
        p[i] = (uint8_t)(seed >> 16);
    }
    for (struct game_s *g = &SaveSlot[0][0]; g < &SaveSlot[0][0] + 4; g++) { /* what the game writes */
        g->name[8] = 0;
        for (int l = 0; l < 35; l++) {
            for (int i = 0; i < 3; i++) {
                g->level[l].time[i].name[3] = 0;
            }
        }
        g->vibration &= 1;
        g->surround &= 1;
        g->sfx_volume %= 101;
        g->music_volume %= 101;
        g->language %= 6;
    }
    SaveSlot[0][0].empty = 0;
    SaveSlot[0][1].empty = 1;
    SaveSlot[1][0].empty = 0;
    memset(&SaveSlot[1][1], 0, sizeof(SaveSlot[1][1]));
    SaveSlot[1][1].empty = 1;
}

/* Same games: the empty flags, and the records of the games. */
static bool same(const struct game_s *a, const struct game_s *b)
{
    for (int i = 0; i < 4; i++) {
        if (!a[i].empty != !b[i].empty || (!a[i].empty && memcmp(&a[i], &b[i], sizeof(a[i])) != 0)) {
            return false;
        }
    }
    return true;
}

static bool page_empty(void)
{
    return SaveSlot[0][0].empty && SaveSlot[0][1].empty && SaveSlot[1][0].empty && SaveSlot[1][1].empty;
}

/* A result screen's frames before nw_saves_shown says it is over. */
static int result_frames(void)
{
    int frames = 0;
    while (!nw_saves_shown() && frames < 1000) {
        frames++;
    }
    return frames;
}

static void test_menus(void)
{
    static struct game_s expected[2][2], later[2][2];
    fresh();
    nw_saves_init(dir);
    CHECK(page_empty());
    nw_saves_show(0);
    CHECK(!nw_saves_any() && nw_saves_latest() == -1 && nw_saves_pages() == 1 && page_empty());

    /* SAVE GAME's confirm: the slot's game written as its save, at once */
    fill(1);
    memcpy(expected, SaveSlot, sizeof(expected));
    nw_saves_write(&SaveSlot[0][0]);
    CHECK(nw_saves_ok() && there("save001.toml") && !there("save002.toml") && nw_saves_any());
    CHECK(nw_saves_latest() == 0 && nw_saves_pages() == 2);
    CHECK(nw_saves_time(0)->year >= 2024 && nw_saves_time(1)->year == 0);
    CHECK(result_frames() == 0x32 - 1 && nw_saves_shown());
    nw_saves_write(&SaveSlot[1][0]);
    CHECK(nw_saves_ok() && there("save003.toml") && nw_saves_latest() == 2);

    memset(SaveSlot, 0x55, sizeof(SaveSlot));
    nw_saves_show(0);
    CHECK(same(&SaveSlot[0][0], &expected[0][0]) && nw_saves_time(2)->year >= 2024);

    /* the LData keys and the time */
    size_t size = 0;
    char *text = SDL_LoadFile(path_of("save001.toml"), &size);
    CHECK(text && strstr(text, "[slot.0.level.\"a/cold_a/cold_a\"]") && strstr(text, "\"a/seashell/seashell\"]"));
    CHECK(text && strstr(text, "saved = \""));
    SDL_free(text);

    /* only the slot written is: the other keeps its file */
    nw_saves_write(&SaveSlot[0][0]);
    CHECK(nw_saves_ok() && there("save001.toml.bak") && !there("save003.toml.bak"));
    static struct game_s elsewhere;
    elsewhere = SaveSlot[0][0];
    nw_saves_write(&elsewhere);
    CHECK(!nw_saves_ok());

    /* DELETE GAME's confirm: the slot emptied, its save to .bak */
    nw_saves_delete(&SaveSlot[0][0]);
    CHECK(nw_saves_ok() && SaveSlot[0][0].empty && !there("save001.toml") && there("save001.toml.bak"));
    CHECK(nw_saves_time(0)->year == 0 && there("save003.toml") && nw_saves_any());
    CHECK(result_frames() == 0x32 - 1);

    /* pages: the next one holds saves 5 to 8; the menus go up to the last save's page and one more */
    nw_saves_show(1);
    CHECK(nw_saves_page() == 1 && page_empty());
    fill(2);
    memcpy(later, SaveSlot, sizeof(later));
    nw_saves_write(&SaveSlot[0][0]);
    nw_saves_write(&SaveSlot[1][0]);
    CHECK(there("save005.toml") && there("save007.toml") && nw_saves_pages() == 3 && nw_saves_latest() / 4 == 1);
    nw_saves_show(0);
    CHECK(SaveSlot[0][0].empty && !SaveSlot[1][0].empty && memcmp(&SaveSlot[1][0], &expected[1][0], 0x40C) == 0);
    nw_saves_show(1);
    CHECK(same(&SaveSlot[0][0], &later[0][0]));
    nw_saves_show(NW_SAVES_PAGES);
    CHECK(nw_saves_page() == NW_SAVES_PAGES - 1 && page_empty());
    nw_saves_show(-1);
    CHECK(nw_saves_page() == 0);

    /* GAME LOADED's frames */
    nw_saves_loaded();
    CHECK(result_frames() == 0x28 - 1);

    /* a save from a newer build: an empty slot, and not overwritten */
    static const char newer[] = "format = 2\n";
    CHECK(SDL_SaveFile(path_of("save001.toml"), newer, sizeof(newer) - 1));
    nw_saves_show(0);
    CHECK(SaveSlot[0][0].empty);
    fill(3);
    nw_saves_write(&SaveSlot[0][0]);
    CHECK(!nw_saves_ok());
    text = SDL_LoadFile(path_of("save001.toml"), &size);
    CHECK(text && size == sizeof(newer) - 1 && memcmp(text, newer, size) == 0);
    SDL_free(text);

    /* a saves folder that cannot be written; none at all */
    nw_saves_init(path_of("missing/folder/"));
    nw_saves_show(0);
    CHECK(page_empty() && !nw_saves_any());
    fill(4);
    nw_saves_write(&SaveSlot[0][0]);
    CHECK(!nw_saves_ok());
    nw_saves_init(NULL);
    nw_saves_show(0);
    CHECK(page_empty() && !nw_saves_any() && nw_saves_pages() == 1);
    fill(4);
    nw_saves_write(&SaveSlot[0][0]);
    nw_saves_delete(&SaveSlot[1][0]);
    CHECK(!nw_saves_ok());

    /* the folder's PS2 save, converted on first use by its places; imported after, into the free saves */
    fresh();
    fill(5);
    memcpy(expected, SaveSlot, sizeof(expected));
    CHECK(SDL_SaveFile(path_of(RAW), SaveSlot, PAGE_SIZE));
    nw_saves_init(dir);
    memset(SaveSlot, 0x55, sizeof(SaveSlot));
    nw_saves_show(0);
    CHECK(nw_saves_any() && same(&SaveSlot[0][0], &expected[0][0]));
    CHECK(there("save001.toml") && there("save003.toml") && !there("save002.toml"));
    CHECK(nw_saves_import(dir, path_of(RAW)) == 2);
    CHECK(there("save002.toml") && there("save004.toml") && !there("save005.toml"));
}

/* NUWORLD_TEST_CARD's save: converted from the folder, shown by the menus, then saved again by them. */
static int test_ps2(void)
{
    const char *card = SDL_getenv("NUWORLD_TEST_CARD");
    if (!card || !*card) {
        printf("saves-ps2: NUWORLD_TEST_CARD is not set; skipped\n");
        return 77;
    }
    size_t size = 0, len = 0;
    bool missing = false;
    uint8_t *image = SDL_LoadFile(card, &size);
    uint8_t *raw = image ? nw_ps2card_file(image, size, NW_SAVE_PS2_NAME, RAW, &len, &missing) : NULL;
    CHECK(raw && len == (size_t)PAGE_SIZE);
    if (!raw || len != (size_t)PAGE_SIZE) {
        SDL_free(image);
        return 1;
    }
    fresh();
    CHECK(SDL_SaveFile(path_of("card.ps2"), image, size));

    nw_saves_init(dir);
    memset(SaveSlot, 0x55, sizeof(SaveSlot));
    nw_saves_show(0);
    CHECK(nw_saves_any() && same(&SaveSlot[0][0], (const struct game_s *)raw));
    for (int s = 0; s < 4; s++) {
        struct game_s *g = &SaveSlot[s / 2][s % 2];
        if (!g->empty) {
            nw_saves_write(g);
            CHECK(nw_saves_ok());
        }
    }
    memset(SaveSlot, 0x55, sizeof(SaveSlot));
    nw_saves_show(0);
    CHECK(same(&SaveSlot[0][0], (const struct game_s *)raw));
    for (int s = 0; s < 4; s++) {
        const struct game_s *g = &SaveSlot[s / 2][s % 2];
        printf("saves-ps2: slot %d: %s, %.8s, %d%%\n", s, g->empty ? "empty" : "game", g->name, g->percent);
    }
    SDL_free(raw);
    SDL_free(image);
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc < 2 || argc > 3 || (argc == 3 && strcmp(argv[2], "ps2") != 0)) {
        fprintf(stderr, "usage: test_saves <scratch dir> [ps2]\n");
        return 2;
    }
    SDL_snprintf(dir, sizeof(dir), "%s/", argv[1]);
    int code = 0;
    if (argc == 3) {
        code = test_ps2();
    } else {
        test_menus();
        /* what tests/save_toml.py reads with Python's tomllib */
        fresh();
        nw_saves_init(dir);
        nw_saves_show(0);
        fill(7);
        nw_saves_write(&SaveSlot[0][0]);
        nw_saves_write(&SaveSlot[1][0]);
    }
    nw_saves_init(NULL);
    if (code == 77) {
        return code;
    }
    if (failures) {
        fprintf(stderr, "saves: %d checks failed\n", failures);
        return 1;
    }
    printf("saves: all checks passed\n");
    return 0;
}
