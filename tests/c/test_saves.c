/* The port's saves (port/saves.h) as the save menus use them: SaveSlot a page of 4, written and deleted at once, the
   result screens' frames, the saves from elsewhere converted; with "ps2", NUWORLD_TEST_CARD's games must come back
   byte for byte. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "game/gametypes.h"
#include "platform/paths.h"
#include "port/names.h"
#include "port/saves.h"
#include "port/autosave.h"
#include "port/menu.h"
#include "port/nameentry.h"
#include "port/savemenu.h"
#include "save/saveworker.h"
#include "nups2/ps2ctl.h"
#include "save/ps2card.h"

#define PAGE_SIZE ((int)sizeof(SaveSlot))
#define RAW NW_SAVE_PS2_NAME "slot00" /* a raw PS2 save */

extern struct game_s SaveSlot[2][2];
extern struct game_s *game;
extern struct game_s Game;
extern struct cursor_s Cursor;
extern struct panelcount_s plr_lives, plr_wumpas;
extern s32 Level, Hub, Demo, GameMode, editor_active, cutmovie, new_level, new_mode;
extern u16 new_lev_flags;
extern s32 fadeval, Paused, stick_bits, stick_bits_db, ForceRestart, i_nameinput;
void ProcMenu(struct cursor_s *cursor, struct nupad_s *pad);
void GetMenuInfo(struct cursor_s *cursor);

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

/* Storage assertions wait explicitly; gameplay uses the non-blocking result-screen poll. */
static void write_save(struct game_s *slot)
{
    nw_saves_write(slot);
    nw_saves_finish();
}

static void delete_save(struct game_s *slot)
{
    nw_saves_delete(slot);
    nw_saves_finish();
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
    nw_saves_finish();
    nw_autosave_finish();
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
    write_save(&SaveSlot[0][0]);
    CHECK(nw_saves_ok() && there("save001.toml") && !there("save002.toml") && nw_saves_any());
    CHECK(nw_saves_latest() == 0 && nw_saves_pages() == 2);
    CHECK(nw_saves_time(0)->year >= 2024 && nw_saves_time(1)->year == 0);
    CHECK(result_frames() == 0x32 - 1 && nw_saves_shown());
    write_save(&SaveSlot[1][0]);
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
    write_save(&SaveSlot[0][0]);
    CHECK(nw_saves_ok() && there("save001.toml.bak") && !there("save003.toml.bak"));
    static struct game_s elsewhere;
    elsewhere = SaveSlot[0][0];
    write_save(&elsewhere);
    CHECK(!nw_saves_ok());

    /* DELETE GAME's confirm: the slot emptied, its save to .bak */
    delete_save(&SaveSlot[0][0]);
    CHECK(nw_saves_ok() && SaveSlot[0][0].empty && !there("save001.toml") && there("save001.toml.bak"));
    CHECK(nw_saves_time(0)->year == 0 && there("save003.toml") && nw_saves_any());
    CHECK(result_frames() == 0x32 - 1);

    /* pages: the next one holds saves 5 to 8; the menus go up to the last save's page and one more */
    nw_saves_show(1);
    CHECK(nw_saves_page() == 1 && page_empty());
    fill(2);
    memcpy(later, SaveSlot, sizeof(later));
    write_save(&SaveSlot[0][0]);
    write_save(&SaveSlot[1][0]);
    CHECK(there("save005.toml") && there("save007.toml") && nw_saves_pages() == 3 && nw_saves_latest() / 4 == 1);
    nw_saves_show(0);
    CHECK(SaveSlot[0][0].empty && !SaveSlot[1][0].empty && memcmp(&SaveSlot[1][0], &expected[1][0], 0x40C) == 0);
    nw_saves_show(1);
    CHECK(same(&SaveSlot[0][0], &later[0][0]));
    nw_saves_show(NW_SAVES_PAGES);
    CHECK(nw_saves_page() == NW_SAVES_PAGES - 1 && page_empty());
    nw_saves_show(-1);
    CHECK(nw_saves_page() == 0);

    /* the game in play's longer names go with a save, and come back with a load */
    nw_saves_show(0);
    fill(9);
    strcpy(nw_names_game()->game, "NUWORLD TEAM");
    strcpy(nw_names_game()->time[2][0], "ROBERTA");
    write_save(&SaveSlot[0][0]);
    CHECK(nw_saves_ok() && strcmp(nw_names_slot(0)->game, "NUWORLD TEAM") == 0);
    SDL_zerop(nw_names_game());
    SDL_zerop(nw_names_slot(0));
    nw_saves_show(0);
    CHECK(strcmp(nw_names_slot(0)->game, "NUWORLD TEAM") == 0 && strcmp(nw_names_slot(0)->time[2][0], "ROBERTA") == 0);
    CHECK(!nw_names_slot(1)->game[0]);
    game = &SaveSlot[0][0];
    nw_saves_loaded();
    CHECK(strcmp(nw_names_game()->game, "NUWORLD TEAM") == 0 && strcmp(nw_names_game()->time[2][0], "ROBERTA") == 0);
    game = NULL;
    delete_save(&SaveSlot[0][0]);
    CHECK(nw_saves_ok() && !nw_names_slot(0)->game[0]);
    SDL_zerop(nw_names_game());

    /* GAME LOADED's frames */
    nw_saves_loaded();
    CHECK(result_frames() == 0x28 - 1);

    /* a save from a newer build: an empty slot, and not overwritten */
    static const char newer[] = "format = 2\n";
    CHECK(SDL_SaveFile(path_of("save001.toml"), newer, sizeof(newer) - 1));
    nw_saves_show(0);
    CHECK(SaveSlot[0][0].empty);
    fill(3);
    write_save(&SaveSlot[0][0]);
    CHECK(!nw_saves_ok());
    text = SDL_LoadFile(path_of("save001.toml"), &size);
    CHECK(text && size == sizeof(newer) - 1 && memcmp(text, newer, size) == 0);
    SDL_free(text);

    /* a saves folder that cannot be written; none at all */
    nw_saves_init(path_of("missing/folder/"));
    nw_saves_show(0);
    CHECK(page_empty() && !nw_saves_any());
    fill(4);
    write_save(&SaveSlot[0][0]);
    CHECK(!nw_saves_ok());
    nw_saves_init(NULL);
    nw_saves_show(0);
    CHECK(page_empty() && !nw_saves_any() && nw_saves_pages() == 1);
    fill(4);
    write_save(&SaveSlot[0][0]);
    delete_save(&SaveSlot[1][0]);
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
            write_save(g);
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

static void play(void)
{
    SDL_zero(Game);
    strcpy(Game.name, "TEST    ");
    Game.lives = 4;
    Game.language = 3;
    Game.music_volume = Game.sfx_volume = 75;
    Game.hub[0].flags = 1;
    SDL_zero(Cursor);
    Cursor.menu = -1;
    Level = 0x25;
    Hub = 0;
    Demo = GameMode = editor_active = new_lev_flags = fadeval = Paused = stick_bits = stick_bits_db = ForceRestart = 0;
    cutmovie = new_level = new_mode = -1;
    plr_lives.count = 4;
    plr_wumpas.count = 0;
    SDL_zerop(nw_names_game());
}

static void press(int button)
{
    struct nupad_s pad = { 0 };
    pad.paddata_db = (u32)button;
    Cursor.item_frame = 1;
    ProcMenu(&Cursor, &pad);
}

static void test_autosaves(void)
{
    fresh();
    nw_saves_init(dir);
    play();
    nw_autosave_new(true);
    CHECK(nw_autosave_enabled() && nw_autosave_status() == NW_AUTOSAVE_WRITING);
    uint64_t now = SDL_GetTicks();
    CHECK(nw_autosave_notice_visible(now));
    CHECK(!nw_autosave_notice_visible(now + 350));
    CHECK(nw_autosave_notice_visible(now + 700));
    nw_autosave_finish();
    CHECK(nw_autosave_status() == NW_AUTOSAVE_COMPLETE && there("autosave001.toml") && !there("save001.toml"));
    CHECK(nw_autosave_notice_visible(now) && nw_autosave_notice_visible(now + 350));
    const NwSaveRegistry *reg = nw_saves_levels();
    struct game_s saved;
    NwSaveMeta first, meta;
    CHECK(nw_save_load_meta(dir, NW_SAVE_GAMES, 0, reg, (uint8_t *)&saved, NULL, NULL, &first) == NW_SAVE_OK);
    CHECK(first.autosave && first.campaign[0] && saved.lives == 4);

    /* A completed level's rewards are durable even before the hub's award animation assigns them to Game. */
    Level = 0;
    new_lev_flags = 0x10;
    nw_autosave_checkpoint(true);
    nw_autosave_finish();
    CHECK(nw_save_load_meta(dir, NW_SAVE_GAMES, 0, reg, (uint8_t *)&saved, NULL, NULL, &meta) == NW_SAVE_OK);
    CHECK((saved.level[0].flags & 0x10) && !(Game.level[0].flags & 0x10));
    CHECK(meta.revision > first.revision);
    Level = 0x25;
    new_lev_flags = 0;
    Game.level[0].flags |= 0x10;
    nw_saves_show(0);
    SaveSlot[0][0] = Game;
    write_save(&SaveSlot[0][0]);
    CHECK(nw_saves_ok() && there("save001.toml"));
    Game.level[1].flags |= 8;
    nw_autosave_checkpoint(false);
    nw_autosave_finish();

    /* An older manual snapshot must not replace newer automatic progress without consent. */
    nw_saves_show(0);
    game = &SaveSlot[0][0];
    Game = *game;
    nw_saves_loaded();
    nw_autosave_finish();
    CHECK(nw_autosave_rollback());
    nw_autosave_poll();
    nw_autosave_finish();
    CHECK(nw_save_load_meta(dir, NW_SAVE_GAMES, 0, reg, (uint8_t *)&saved, NULL, NULL, &meta) == NW_SAVE_OK);
    CHECK(saved.level[1].flags & 8);
    CHECK(nw_autosave_set(false));
    nw_autosave_finish();
    CHECK(nw_save_load_meta(dir, NW_SAVE_GAMES, 0, reg, (uint8_t *)&saved, NULL, NULL, &meta) == NW_SAVE_OK);
    CHECK(!meta.autosave && (saved.level[1].flags & 8));
    CHECK(!nw_autosave_set(true));
    nw_autosave_accept_rollback(true);
    nw_autosave_finish();
    CHECK(nw_save_load_meta(dir, NW_SAVE_GAMES, 0, reg, (uint8_t *)&saved, NULL, NULL, &meta) == NW_SAVE_OK);
    CHECK(meta.autosave && !(saved.level[1].flags & 8));
    CHECK(nw_save_load_meta(dir, NW_SAVE_GAMES, 1, reg, (uint8_t *)&saved, NULL, NULL, NULL) == NW_SAVE_OK);
    CHECK(saved.level[1].flags & 8);

    /* Same name, new campaign, independent autosave. Manual write refuses the automatic bank. */
    nw_autosave_new(true);
    nw_autosave_finish();
    CHECK(there("autosave002.toml"));
    CHECK(strcmp(nw_autosave_meta()->campaign, first.campaign) != 0);
    nw_saves_autos(true);
    CHECK(nw_saves_automatic() && nw_saves_any() && !SaveSlot[0][0].empty);
    write_save(&SaveSlot[0][0]);
    CHECK(!nw_saves_ok());
    CHECK(nw_saves_previous(&SaveSlot[0][0], 1));
    CHECK(nw_saves_recovered(0));
    nw_saves_autos(false);
    CHECK(!nw_saves_automatic() && !SaveSlot[0][0].empty);

    /* Bank switching also works from CANCEL, including an empty manual bank. */
    Cursor.menu = 20;
    GetMenuInfo(&Cursor);
    nw_savemenu_prepare(&Cursor);
    nw_saves_autos(false);
    Cursor.y = Cursor.y_max;
    Cursor.item_frame = 1;
    struct nupad_s shoulder = { 0 };
    shoulder.paddata_db = 4;
    nw_savemenu_proc(&Cursor, &shoulder, Cursor.x);
    CHECK(nw_saves_automatic() && Cursor.y == 0);
    nw_saves_autos(false);

    /* The initial name confirmation must show the question without starting or saving the game. */
    nw_autosave_leave();
    play();
    Cursor.menu = 2;
    GetMenuInfo(&Cursor);
    Cursor.item_frame = 1;
    uint32_t taken;
    struct nupad_s pad = { 0 };
    (void)nw_nameentry_proc(&Cursor, &pad, &taken);
    for (const char *p = "NUWORLD TEAM"; *p; p++) nw_nameentry_type(*p);
    (void)nw_nameentry_proc(&Cursor, &pad, &taken);
    int length = i_nameinput;
    Cursor.y = Cursor.y_max - 1;
    press(0x40);
    CHECK(Cursor.menu == NW_MENU_AUTOSAVE_NEW && new_level == -1 && !nw_autosave_active());
    press(0x10); /* Back preserves the typed name and insertion point. */
    CHECK(Cursor.menu == 2 && i_nameinput == length && !strcmp(nw_nameentry_name(), "NUWORLD_TEAM____"));
    Cursor.y = Cursor.y_max - 1;
    press(0x40);
    CHECK(Cursor.menu == NW_MENU_AUTOSAVE_NEW);
    Cursor.y = 1;
    press(0x40);
    CHECK(new_level == 0x25 && !nw_autosave_enabled() && !strcmp(Game.name, "NUWORLD "));
    nw_autosave_finish();
    CHECK(!there("autosave003.toml"));

    /* Enabling through the actual guarded menu starts a separate third slot. First-frame confirm does nothing. */
    new_level = cutmovie = -1;
    Cursor.menu = NW_MENU_AUTOSAVE_NEW;
    GetMenuInfo(&Cursor);
    Cursor.y = 0;
    Cursor.item_frame = 0;
    pad.paddata_db = 0x40;
    ProcMenu(&Cursor, &pad);
    CHECK(Cursor.menu == NW_MENU_AUTOSAVE_NEW && new_level == -1);
    press(0x40);
    CHECK(nw_autosave_enabled() && new_level == 0x25);
    nw_autosave_finish();
    CHECK(there("autosave003.toml"));
    new_level = cutmovie = -1;
    Paused = 25;
    Cursor.menu = 4;
    GetMenuInfo(&Cursor);
    Cursor.y = Cursor.y_max - 1;
    press(0x40);
    CHECK(Cursor.menu == NW_MENU_AUTOSAVE_OPTIONS);
    Cursor.y = 1;
    press(0x40);
    CHECK(Cursor.menu == 4 && !nw_autosave_enabled());
    CHECK(there("autosave003.toml"));
    Cursor.y = Cursor.y_max - 1;
    press(0x40);
    CHECK(Cursor.menu == NW_MENU_AUTOSAVE_OPTIONS);
    Cursor.y = 0;
    press(0x40);
    CHECK(Cursor.menu == 4 && nw_autosave_enabled());
    nw_autosave_finish();
    CHECK(nw_save_load_meta(dir, NW_SAVE_GAMES + 2, 0, reg, (uint8_t *)&saved, NULL, NULL, &meta) == NW_SAVE_OK);
    CHECK(meta.autosave);
    nw_saves_init(path_of("missing/folder/"));
    play();
    nw_autosave_new(true);
    nw_autosave_finish();
    CHECK(nw_autosave_status() == NW_AUTOSAVE_FAILED);
    nw_saves_init(NULL);
}

typedef struct SlowDisk {
    SDL_Semaphore *entered, *release;
    SDL_AtomicInt released;
} SlowDisk;

static void slow_disk(void *context)
{
    SlowDisk *disk = context;
    SDL_SignalSemaphore(disk->entered);
    SDL_WaitSemaphore(disk->release);
}

static int SDLCALL release_disk(void *context)
{
    SlowDisk *disk = context;
    SDL_Delay(1500);
    SDL_SetAtomicInt(&disk->released, 1);
    SDL_SignalSemaphore(disk->release);
    return 0;
}

static void test_nonblocking_saves(void)
{
    nw_saves_init(NULL);
    fresh();
    nw_saves_init(dir);
    play();
    const NwSaveRegistry *reg = nw_saves_levels();
    for (int i = 0; i < 16; i++) {
        NwSaveMeta meta = { 0 };
        SDL_snprintf(meta.campaign, sizeof(meta.campaign), "%032x", i + 1);
        CHECK(nw_save_store_meta(dir, NW_SAVE_GAMES + i, reg, (const uint8_t *)&Game, NULL, NULL, &meta));
    }
    nw_saves_show(20); /* save081, distinct from automatic destinations */
    SaveSlot[0][0] = Game;
    SlowDisk disk = { .entered = SDL_CreateSemaphore(0), .release = SDL_CreateSemaphore(0) };
    NwSaveTask blocker = { 0 };
    CHECK(disk.entered && disk.release && nw_save_worker_submit(&blocker, slow_disk, &disk));
    SDL_WaitSemaphore(disk.entered);
    SDL_Thread *release = SDL_CreateThread(release_disk, "test_slow_disk", &disk);
    CHECK(release != NULL);
    if (!release) SDL_SignalSemaphore(disk.release);

    uint64_t frequency = SDL_GetPerformanceFrequency(), start = SDL_GetPerformanceCounter();
    nw_autosave_new(true); /* Includes first-destination allocation, which must also run off-thread. */
    nw_saves_write(&SaveSlot[0][0]);
    uint64_t submit = SDL_GetPerformanceCounter() - start, total = 0, worst = 0;
    for (int i = 0; i < 10000; i++) {
        start = SDL_GetPerformanceCounter();
        nw_autosave_poll();
        CHECK(!nw_saves_shown());
        (void)nw_saves_has_autos();
        uint64_t elapsed = SDL_GetPerformanceCounter() - start;
        total += elapsed;
        if (elapsed > worst) worst = elapsed;
    }
    /* All game-thread calls returned while the I/O worker was still stalled, not just on a fast local disk. */
    CHECK(!SDL_GetAtomicInt(&disk.released) && !nw_save_worker_done(&blocker));
    CHECK(nw_saves_pending() && nw_autosave_status() == NW_AUTOSAVE_WRITING);
    printf("save performance (worker stalled 1500 ms, 16 existing autosaves): submit %.3f ms; "
           "10000 polls average %.3f us, worst %.3f us\n", submit * 1000.0 / frequency,
           total * 1000000.0 / frequency / 10000, worst * 1000000.0 / frequency);
    if (release) SDL_WaitThread(release, NULL);
    nw_save_worker_wait(&blocker);
    nw_saves_finish();
    nw_autosave_finish();
    CHECK(nw_saves_ok() && there("save081.toml") && there("autosave017.toml"));
    CHECK(nw_autosave_status() == NW_AUTOSAVE_COMPLETE);
    SDL_DestroySemaphore(disk.entered);
    SDL_DestroySemaphore(disk.release);
    nw_saves_init(NULL);
}

static NwSavesImport run_import(NwSavesImported *r)
{
    NwSavesImport state = NW_SAVES_IMPORT_RUNNING;
    for (int frames = 0; frames < 10000; frames++) {
        nw_saves_import_poll();
        state = nw_saves_import_result(r);
        if (state == NW_SAVES_IMPORT_DONE || state == NW_SAVES_IMPORT_FAILED) {
            break;
        }
        SDL_Delay(1);
    }
    return state;
}

static void test_import(void)
{
    static uint8_t games[NW_SAVE_PS2_GAMES * NW_SAVE_GAME_SIZE];
    static struct game_s first, imported;
    NwSavesImported r;
    nw_saves_init(NULL);
    fresh();
    nw_saves_init(dir);
    play();
    nw_saves_show(0);
    fill(9);
    write_save(&SaveSlot[0][0]);
    CHECK(nw_saves_ok());
    first = SaveSlot[0][0];
    memcpy(games, SaveSlot, sizeof(games));

    Cursor.menu = 23;
    CHECK(nw_saves_import_games(games) && !nw_saves_import_games(games));
    for (int i = 0; i < 5; i++) {
        nw_saves_import_poll();
    }
    CHECK(nw_saves_screen() && nw_saves_import_result(&r) == NW_SAVES_IMPORT_WAITING && !there("save002.toml"));
    Cursor.menu = -1;
    CHECK(run_import(&r) == NW_SAVES_IMPORT_DONE && !nw_saves_screen());
    CHECK(r.already[0] == 0 && r.already[1] < 0 && r.already[2] < 0 && r.already[3] < 0);
    CHECK(r.saved[0] < 0 && r.saved[1] < 0 && r.saved[2] == 1 && r.saved[3] < 0);
    CHECK(there("save002.toml") && !there("save003.toml"));
    CHECK(run_import(&r) == NW_SAVES_IMPORT_DONE);

    CHECK(nw_saves_import_games(games) && run_import(&r) == NW_SAVES_IMPORT_DONE);
    CHECK(r.already[0] == 0 && r.already[2] == 1 && r.saved[0] < 0 && r.saved[2] < 0 && !there("save003.toml"));

    /* a grid opened meanwhile reloads */
    for (int i = 0; i < NW_SAVE_PS2_GAMES; i++) {
        games[i * NW_SAVE_GAME_SIZE + NW_SAVE_EMPTY] = 1;
    }
    fill(10);
    imported = SaveSlot[0][0];
    memcpy(games, &imported, sizeof(imported));
    nw_saves_show(0);
    CHECK(SaveSlot[1][0].empty);
    CHECK(nw_saves_import_games(games));
    nw_saves_import_poll();
    Cursor.menu = 20;
    CHECK(run_import(&r) == NW_SAVES_IMPORT_DONE && r.saved[0] == 2);
    CHECK(!SaveSlot[1][0].empty && memcmp(&SaveSlot[1][0], &imported, sizeof(imported)) == 0);

    /* a save into a slot shown empty that the import filled is refused */
    Cursor.menu = -1;
    nw_saves_show(0);
    CHECK(SaveSlot[1][1].empty);
    fill(11);
    imported = SaveSlot[0][0];
    memcpy(games, &imported, sizeof(imported));
    CHECK(nw_saves_import_games(games));
    nw_saves_import_poll();
    Cursor.menu = 24;
    CHECK(run_import(&r) == NW_SAVES_IMPORT_DONE && r.saved[0] == 3);
    SaveSlot[1][1] = first;
    write_save(&SaveSlot[1][1]);
    CHECK(!nw_saves_ok());
    struct game_s kept;
    CHECK(nw_save_load(dir, 3, nw_saves_levels(), (uint8_t *)&kept, NULL, NULL) == NW_SAVE_OK &&
          memcmp(&kept, &imported, sizeof(imported)) == 0);
    Cursor.menu = -1;
    nw_saves_show(0);
    CHECK(memcmp(&SaveSlot[1][1], &imported, sizeof(imported)) == 0);
    SaveSlot[1][1] = first;
    write_save(&SaveSlot[1][1]);
    CHECK(nw_saves_ok());
    nw_saves_init(NULL);
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
        test_autosaves();
        test_nonblocking_saves();
        test_import();
        /* what tests/save_toml.py reads with Python's tomllib */
        fresh();
        nw_saves_init(dir);
        nw_saves_show(0);
        fill(7);
        write_save(&SaveSlot[0][0]);
        write_save(&SaveSlot[1][0]);
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
