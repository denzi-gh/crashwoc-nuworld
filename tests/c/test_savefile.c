/* Saves without the game: the TOML document, the records in it, the saves folder and synthetic PS2 card images. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "ps2card_builder.h"
#include "platform/paths.h"
#include "save/ps2card.h"
#include "save/savecodec.h"
#include "save/savedoc.h"
#include "save/savestore.h"
#include "save/saveio.h"

static int failures;
static char dir[1100];

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: CHECK(%s) failed; last error: %s\n", __FILE__, __LINE__, #cond, SDL_GetError()); \
            failures++; \
        } \
    } while (0)

#define SAVE_SIZE (NW_SAVE_PS2_GAMES * NW_SAVE_GAME_SIZE)

static NwSaveRegistry reg;
static uint32_t seed = 0x12345678;

static uint32_t rnd(void)
{
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
}

static bool parse(NwDoc *doc, const char *text)
{
    return nw_doc_init(doc) && (nw_doc_parse(doc, text, strlen(text)) || (nw_doc_free(doc), false));
}

/* Whether text fails to parse with an error on that line. */
static bool rejects(const char *text, int line)
{
    NwDoc doc;
    char want[16];
    if (parse(&doc, text)) {
        nw_doc_free(&doc);
        return false;
    }
    SDL_snprintf(want, sizeof(want), "line %d:", line);
    return strncmp(SDL_GetError(), want, strlen(want)) == 0;
}

static bool starts(const char *s, const char *prefix)
{
    return s && strncmp(s, prefix, strlen(prefix)) == 0;
}

static const NwDocTable *table2(const NwDoc *doc, const char *a, const char *b)
{
    const char *path[2] = { a, b };
    return nw_doc_table(doc, path, b ? 2 : 1);
}

static void test_doc(void)
{
    static const char text[] = "\xEF\xBB\xBF# a save written by hand\r\n"
                               "format = 1 # trailing comment\r\n"
                               "big = -9223372036854775808\n"
                               "under = 1_000\n"
                               "\n"
                               "[ a . \"b c\" . 'd' ]\n"
                               "s = \"q\\\"\\\\\\t\\u00e9\\U000000FF\"\n"
                               "lit = 'C:\\path'\n"
                               "utf8 = \"\xC3\xA9\xC2\xA0\"\n"
                               "hex = 0x00ff\n"
                               "neg = -5\n"
                               "yes = true\n"
                               "arr = [\n"
                               "  [\"x\", 1], # first\n"
                               "  [],\n"
                               "]\n"
                               "\"key with space\" = false\n"
                               "[empty]\n";
    NwDoc doc;
    CHECK(parse(&doc, text));
    if (failures) {
        return;
    }
    const NwDocValue *v = nw_doc_get(doc.tables[0], "format");
    CHECK(v && v->type == NW_DOC_INT && v->i == 1 && v->hex == 0);
    CHECK((v = nw_doc_get(doc.tables[0], "big")) && v->i == INT64_MIN);
    CHECK((v = nw_doc_get(doc.tables[0], "under")) && v->i == 1000);
    const char *path[3] = { "a", "b c", "d" };
    const NwDocTable *t = nw_doc_table(&doc, path, 3);
    CHECK(t && t->count == 8);
    if (t) {
        CHECK((v = nw_doc_get(t, "s")) && v->type == NW_DOC_STR && v->len == 6 &&
              memcmp(v->bytes, "q\"\\\t\xE9\xFF", 6) == 0);
        CHECK((v = nw_doc_get(t, "lit")) && v->len == 7 && memcmp(v->bytes, "C:\\path", 7) == 0);
        CHECK((v = nw_doc_get(t, "utf8")) && v->len == 2 && memcmp(v->bytes, "\xE9\xA0", 2) == 0);
        CHECK((v = nw_doc_get(t, "hex")) && v->i == 255 && v->hex == 4);
        CHECK((v = nw_doc_get(t, "neg")) && v->i == -5);
        CHECK((v = nw_doc_get(t, "yes")) && v->type == NW_DOC_BOOL && v->i == 1);
        CHECK((v = nw_doc_get(t, "arr")) && v->type == NW_DOC_ARRAY && v->len == 2 && v->items[0].len == 2 &&
              v->items[0].items[1].i == 1 && v->items[1].type == NW_DOC_ARRAY && v->items[1].len == 0);
        CHECK((v = nw_doc_get(t, "key with space")) && v->type == NW_DOC_BOOL && v->i == 0);
    }
    CHECK(table2(&doc, "empty", NULL) != NULL);

    /* written, read back and written again: the same text */
    size_t n1 = 0, n2 = 0;
    char *w1 = nw_doc_write(&doc, "comment\n\nsecond", &n1), *w2 = NULL;
    NwDoc again;
    bool reparsed = w1 && parse(&again, w1);
    CHECK(reparsed);
    if (reparsed) {
        w2 = nw_doc_write(&again, "comment\n\nsecond", &n2);
        nw_doc_free(&again);
    }
    CHECK(w2 && n1 == n2 && memcmp(w1, w2, n1) == 0);
    CHECK(starts(w1, "# comment\n#\n# second\n\nformat = 1\n"));
    CHECK(w1 && strstr(w1, "[a.\"b c\".d]\ns = \"q\\\"\\\\\\u0009\\u00E9\\u00FF\"\n"));
    CHECK(w1 && strstr(w1, "arr = [[\"x\", 1], []]\n"));
    SDL_free(w1);
    SDL_free(w2);

    /* a new table goes after the last one it shares the most with */
    const char *slot0[3] = { "a", "b c", "z" };
    NwDocTable *z = nw_doc_add_table(&doc, slot0, 3);
    CHECK(z && doc.count == 4 && doc.tables[2] == z);
    CHECK(nw_doc_add_table(&doc, slot0, 3) == z);
    const char *other[1] = { "zz" };
    CHECK(nw_doc_add_table(&doc, other, 1) == doc.tables[4]);
    nw_doc_drop_table(&doc, z);
    CHECK(doc.count == 4 && nw_doc_table(&doc, slot0, 3) == NULL);
    /* a key and a table cannot share a name */
    NwDocValue one = nw_doc_int(1, 0);
    CHECK(!nw_doc_set(&doc, doc.tables[0], "a", &one));
    const char *under_key[2] = { "a", "b c" };
    NwDocTable *ab = nw_doc_add_table(&doc, under_key, 2);
    one = nw_doc_int(1, 0);
    CHECK(ab && !nw_doc_set(&doc, ab, "d", &one));
    nw_doc_unset(doc.tables[0], "big");
    CHECK(!nw_doc_get(doc.tables[0], "big") && nw_doc_get(doc.tables[0], "under"));
    nw_doc_free(&doc);

    /* an empty string still has its bytes (the codec copies from them) */
    CHECK(parse(&doc, "a = \"\"\nb = ''\n"));
    if (!failures) {
        CHECK((v = nw_doc_get(doc.tables[0], "a")) && v->type == NW_DOC_STR && v->len == 0 && v->bytes);
        CHECK((v = nw_doc_get(doc.tables[0], "b")) && v->type == NW_DOC_STR && v->len == 0 && v->bytes);
        nw_doc_free(&doc);
    }

    /* more hex digits than an int64 has are written as 16 */
    CHECK(parse(&doc, "a = 0x00000000000000000000000000000000000000000000000001\n"));
    w1 = failures ? NULL : nw_doc_write(&doc, NULL, NULL);
    CHECK(w1 && strcmp(w1, "a = 0x0000000000000001\n") == 0);
    SDL_free(w1);
    if (!failures) {
        nw_doc_free(&doc);
    }

    CHECK(rejects("a = 1\na = 2\n", 2));
    CHECK(rejects("[t]\n[t]\n", 2));
    CHECK(rejects("t = 1\n[t]\n", 2));
    CHECK(rejects("[t.u]\n[t]\nu = 1\n", 3));
    CHECK(rejects("[[t]]\n", 1));
    CHECK(rejects("a = { b = 1 }\n", 1));
    CHECK(rejects("a = 1.5\n", 1));
    CHECK(rejects("a = 1979-05-27\n", 1));
    CHECK(rejects("a = \"\"\"x\"\"\"\n", 1));
    CHECK(rejects("\n\na = \"\xE2\x82\xAC\"\n", 3));
    CHECK(rejects("a = \"\\u20AC\"\n", 1));
    CHECK(rejects("a.b = 1\n", 1));
    CHECK(rejects("a = \"open\n", 1));
    CHECK(rejects("a =\n", 1));
    CHECK(rejects("a = 1 2\n", 1));
    CHECK(rejects("a = 9223372036854775808\n", 1));
    CHECK(rejects("a = 007\n", 1));
    CHECK(rejects("a = 0x\n", 1));
    CHECK(rejects("a = [1, 2\n", 2));
    CHECK(rejects("a = \"\x01\"\n", 1));
    CHECK(rejects("[a\nb = 1\n", 1));
    CHECK(rejects("a = inf\n", 1));
    CHECK(rejects("x\n", 1));
    CHECK(rejects("a = 1\r\rb = 2\n", 1));
    CHECK(rejects("a = \"\\x41\"\n", 1));
}

/* A record of random bytes in what the game writes (texts end in a NUL, options in their ranges); every byte of game_s
   is a field, so any such value has to come back. */
static void random_record(uint8_t *g)
{
    for (int i = 0; i < NW_SAVE_GAME_SIZE; i++) {
        g[i] = (uint8_t)rnd();
    }
    g[NW_SAVE_NAME + NW_SAVE_NAME_SIZE - 1] = 0;
    for (int l = 0; l < NW_SAVE_LEVELS; l++) {
        for (int i = 0; i < NW_SAVE_TIMES; i++) {
            g[NW_SAVE_LEVEL + l * NW_SAVE_LEVEL_SIZE + NW_SAVE_LEVEL_TIME + i * NW_SAVE_TIME_SIZE + 3] = 0;
        }
    }
    g[NW_SAVE_VIBRATION] &= 1;
    g[NW_SAVE_SURROUND] &= 1;
    g[NW_SAVE_SFX_VOLUME] %= NW_SAVE_VOLUME_MAX + 1;
    g[NW_SAVE_MUSIC_VOLUME] %= NW_SAVE_VOLUME_MAX + 1;
    g[NW_SAVE_LANGUAGE] %= NW_SAVE_LANGUAGES;
    if (rnd() & 1) { /* a name with a NUL inside and trailing NULs */
        memcpy(g + NW_SAVE_NAME, "AB\0\xE9z\0\0\0\0", 9);
    }
    for (int l = 0; l < NW_SAVE_LEVELS; l += 3) { /* records that are all zero */
        memset(g + NW_SAVE_LEVEL + l * NW_SAVE_LEVEL_SIZE, 0, NW_SAVE_LEVEL_SIZE);
    }
    memset(g + NW_SAVE_HUB + 2 * NW_SAVE_HUB_SIZE, 0, NW_SAVE_HUB_SIZE);
}

/* records -> TOML text -> records */
static bool round_trip(const uint8_t *games, size_t count, uint8_t *out, char **text)
{
    NwDoc doc, back;
    size_t size = 0;
    bool ok = nw_doc_init(&doc) && nw_save_merge(&doc, &reg, games, count) && (*text = nw_doc_write(&doc, NULL, &size));
    nw_doc_free(&doc);
    ok = ok && parse(&back, *text);
    if (ok) {
        ok = nw_save_decode(&back, &reg, out, count);
        nw_doc_free(&back);
    }
    return ok;
}

static void test_registry(void)
{
    const char *paths[NW_SAVE_LEVELS];
    char names[NW_SAVE_LEVELS][32];
    for (int i = 0; i < NW_SAVE_LEVELS; i++) {
        SDL_snprintf(names[i], sizeof(names[i]), "A\\Level_%02d\\Lev", i);
        paths[i] = names[i];
    }
    paths[3] = NULL;
    paths[4] = "";
    paths[5] = names[6];
    NwSaveRegistry r;
    CHECK(nw_save_registry_init(&r, paths, NW_SAVE_LEVELS, NW_SAVE_HUBS));
    CHECK(strcmp(r.levels[0], "a/level_00/lev") == 0);
    CHECK(strcmp(r.levels[3], "#3") == 0 && strcmp(r.levels[4], "#4") == 0);
    CHECK(strcmp(r.levels[5], "a/level_06/lev") == 0 && strcmp(r.levels[6], "#6") == 0);
    CHECK(strcmp(r.hubs[0], "0") == 0 && strcmp(r.hubs[5], "5") == 0);
    nw_save_registry_free(&r);

    paths[3] = names[3];
    paths[4] = names[4];
    paths[5] = names[5];
    CHECK(nw_save_registry_init(&reg, paths, NW_SAVE_LEVELS, NW_SAVE_HUBS));
    NwDoc doc;
    uint8_t g[NW_SAVE_GAME_SIZE] = { 0 };
    CHECK(nw_save_registry_init(&r, paths, NW_SAVE_LEVELS - 1, NW_SAVE_HUBS));
    CHECK(nw_doc_init(&doc) && !nw_save_merge(&doc, &r, g, 1));
    nw_doc_free(&doc);
    nw_save_registry_free(&r);
}

static void test_codec(void)
{
    uint8_t games[SAVE_SIZE], back[SAVE_SIZE];
    char *text = NULL;
    for (int round = 0; round < 50; round++) {
        for (int s = 0; s < NW_SAVE_PS2_GAMES; s++) {
            random_record(games + s * NW_SAVE_GAME_SIZE);
        }
        memset(games + 3 * NW_SAVE_GAME_SIZE, 0, NW_SAVE_GAME_SIZE); /* an empty slot of zeros */
        games[3 * NW_SAVE_GAME_SIZE + NW_SAVE_EMPTY] = 1;
        CHECK(round_trip(games, NW_SAVE_PS2_GAMES, back, &text) && memcmp(games, back, SAVE_SIZE) == 0);
        SDL_free(text);
        text = NULL;
    }

    /* what a real save looks like */
    uint8_t *g = games;
    memset(games, 0, SAVE_SIZE);
    memcpy(g + NW_SAVE_NAME, "CRASH   ", 8);
    g[NW_SAVE_VIBRATION] = 1;
    g[NW_SAVE_SFX_VOLUME] = 100;
    g[NW_SAVE_SCREEN_X] = 0xFE;
    g[NW_SAVE_LIVES] = 4;
    g[NW_SAVE_HUB] = 0x07;
    g[NW_SAVE_HUB + 1] = 5;
    g[NW_SAVE_LEVEL] = 0x1F;
    memcpy(g + NW_SAVE_LEVEL + NW_SAVE_LEVEL_TIME, "1ST", 3);
    g[NW_SAVE_LEVEL + NW_SAVE_LEVEL_TIME + 4] = 0x9C;
    g[NW_SAVE_LEVEL + NW_SAVE_LEVEL_TIME + 5] = 0x63;
    g[NW_SAVE_CUTBITS] = 1;
    for (int s = 1; s < NW_SAVE_PS2_GAMES; s++) {
        games[s * NW_SAVE_GAME_SIZE + NW_SAVE_EMPTY] = 1;
    }
    CHECK(round_trip(games, NW_SAVE_PS2_GAMES, back, &text) && memcmp(games, back, SAVE_SIZE) == 0);
    CHECK(starts(text, "format = 1\n\n[slot.0]\nempty = false\nname = \"CRASH   \"\nlives = 4\n"));
    CHECK(text && strstr(text, "powerbits = 0x00\ncutbits = 0x00000001\n"));
    CHECK(text && strstr(text, "\n[slot.0.options]\nvibration = 1\nsurround = 0\nsfx_volume = 100\n"));
    CHECK(text && strstr(text, "screen_x = -2\n"));
    CHECK(text && strstr(text, "\n[slot.0.hub.0]\nflags = 0x07\ncrystals = 5\n\n[slot.0.level.\"a/level_00/lev\"]\n"
                               "flags = 0x001f\ntime = [[\"1ST\", 25500], [\"\", 0], [\"\", 0]]\n\n[slot.1]\n"));
    CHECK(text && !strstr(text, "pad") && !strstr(text, "[slot.0.hub.1]") && !strstr(text, "level_01"));
    CHECK(text && !strstr(text, "minigame_best"));
    SDL_free(text);
    text = NULL;

    /* the minigame best: a key of the slot, not the top floor hub's pad */
    g[NW_SAVE_MINIGAME_BEST] = 37;
    CHECK(round_trip(games, NW_SAVE_PS2_GAMES, back, &text) && memcmp(games, back, SAVE_SIZE) == 0);
    CHECK(text && strstr(text, "gembits = 0x00\nminigame_best = 37\n"));
    CHECK(text && !strstr(text, "pad") && !strstr(text, "[slot.0.hub.5]"));
    SDL_free(text);
    text = NULL;
    g[NW_SAVE_MINIGAME_BEST] = 0;

    /* merged into a document that holds more than the records */
    static const char more[] = "format = 1\n"
                               "note = \"kept\"\n"
                               "[mod.\"some.mod\"]\n"
                               "data = [1, 2]\n"
                               "[slot.0]\n"
                               "extra = 5\n"
                               "lives = 99\n"
                               "[slot.0.level.\"custom/level\"]\n"
                               "flags = 0x0010\n"
                               "[slot.0.level.\"a/level_01/lev\"]\n"
                               "flags = 0x0001\n"
                               "mine = true\n"
                               "[slot.0.hub.1]\n"
                               "flags = 0x01\n"
                               "[slot.4]\n"
                               "lives = 3\n";
    NwDoc doc;
    CHECK(parse(&doc, more));
    if (failures) {
        return;
    }
    CHECK(nw_save_merge(&doc, &reg, games, NW_SAVE_PS2_GAMES));
    char *merged = nw_doc_write(&doc, NULL, NULL);
    nw_doc_free(&doc);
    CHECK(merged && parse(&doc, merged));
    if (!failures) {
        const NwDocValue *v;
        CHECK((v = nw_doc_get(doc.tables[0], "note")) && v->type == NW_DOC_STR);
        CHECK((v = nw_doc_get(table2(&doc, "mod", "some.mod"), "data")) && v->len == 2);
        const NwDocTable *s0 = table2(&doc, "slot", "0");
        CHECK(s0 && (v = nw_doc_get(s0, "extra")) && v->i == 5);
        CHECK(s0 && (v = nw_doc_get(s0, "lives")) && v->i == 4);
        CHECK(s0 && s0->keys[0].name && strcmp(s0->keys[0].name, "extra") == 0); /* in its place */
        const char *custom[4] = { "slot", "0", "level", "custom/level" };
        CHECK((v = nw_doc_get(nw_doc_table(&doc, custom, 4), "flags")) && v->i == 0x10);
        const char *level1[4] = { "slot", "0", "level", "a/level_01/lev" };
        const NwDocTable *l1 = nw_doc_table(&doc, level1, 4);
        CHECK(l1 && l1->count == 1 && nw_doc_get(l1, "mine")); /* zero now: only what the codec does not know */
        const char *hub1[4] = { "slot", "0", "hub", "1" };
        CHECK(nw_doc_table(&doc, hub1, 4) == NULL);
        CHECK((v = nw_doc_get(table2(&doc, "slot", "4"), "lives")) && v->i == 3);
        CHECK(nw_save_decode(&doc, &reg, back, NW_SAVE_PS2_GAMES) && memcmp(games, back, SAVE_SIZE) == 0);
        nw_doc_free(&doc);
    }
    SDL_free(merged);

    /* a missing slot is empty, a missing field zero */
    CHECK(parse(&doc, "format = 1\n[slot.1]\nlives = 7\n"));
    if (!failures) {
        CHECK(nw_save_decode(&doc, &reg, back, 2));
        CHECK(back[NW_SAVE_EMPTY] == 1 && back[NW_SAVE_LIVES] == 0);
        CHECK(back[NW_SAVE_GAME_SIZE + NW_SAVE_EMPTY] == 0 && back[NW_SAVE_GAME_SIZE + NW_SAVE_LIVES] == 7);
        nw_doc_free(&doc);
    }

    /* refused */
    static const char *const bad[] = {
        "[slot.0]\nlives = 1\n",
        "format = 2\n",
        "format = \"1\"\n",
        "format = 1\n[slot.0]\nlives = 256\n",
        "format = 1\n[slot.0]\nlives = \"4\"\n",
        "format = 1\n[slot.0.options]\nscreen_x = -129\n",
        "format = 1\n[slot.0]\nname = \"0123456789\"\n",
        "format = 1\n[slot.0.level.\"a/level_00/lev\"]\ntime = [[\"1ST\", 1], [\"2ND\", 2], [\"3RD\", 3], [\"4TH\", 4]]\n",
        "format = 1\n[slot.0.level.\"a/level_00/lev\"]\ntime = [[\"1STX\", 1, 2]]\n",
        "format = 1\n[slot.0.level.\"a/level_00/lev\"]\ntime = [[\"FIFTH\", 1]]\n",
        "format = 1\n[slot.0.level.\"a/level_00/lev\"]\ntime = [[\"1ST\", -1]]\n",
        "format = 1\n[slot.0.hub.0]\npad = 0x10000\n",
        "format = 1\n[slot.0]\nempty = 300\n",
        /* in the field's width, not in what the game writes (retail indexes and prints with these) */
        "format = 1\n[slot.0]\nname = \"012345678\"\n",
        "format = 1\n[slot.0.level.\"a/level_00/lev\"]\ntime = [[\"ABCD\", 1]]\n",
        "format = 1\n[slot.0.options]\nlanguage = 6\n",
        "format = 1\n[slot.0.options]\nmusic_volume = 101\n",
        "format = 1\n[slot.0.options]\nvibration = 2\n",
        "format = 1\n[slot.3.options]\nsurround = 255\n",
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        CHECK(parse(&doc, bad[i]));
        if (!failures) {
            bool decoded = nw_save_decode(&doc, &reg, back, NW_SAVE_PS2_GAMES);
            if (decoded) {
                fprintf(stderr, "decoded: %s", bad[i]);
            }
            CHECK(!decoded);
            nw_doc_free(&doc);
        }
    }
    CHECK(parse(&doc, "format = 1\n[slot.0]\nname = \"12345678\"\n[slot.0.options]\nlanguage = 5\nsfx_volume = 100\n"));
    if (!failures) {
        CHECK(nw_save_decode(&doc, &reg, back, NW_SAVE_PS2_GAMES) && back[NW_SAVE_LANGUAGE] == 5);
        nw_doc_free(&doc);
    }
    CHECK(parse(&doc, "format = 2\n"));
    if (!failures) {
        CHECK(!nw_save_merge(&doc, &reg, games, 1) && strstr(SDL_GetError(), "newer"));
        nw_doc_free(&doc);
    }
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

static void put(const char *name, const void *data, size_t size)
{
    CHECK(SDL_SaveFile(path_of(name), data, size));
}

static void *get(const char *name, size_t *size)
{
    return SDL_LoadFile(path_of(name), size);
}

static void fresh(void)
{
    CHECK(nw_paths_remove_tree(dir) && SDL_CreateDirectory(dir));
}

#define OLDER NW_SAVE_PS2_NAME "slot00.toml" /* an older NuWorld build's file of 4 games */
#define RAW NW_SAVE_PS2_NAME "slot00"         /* a raw PS2 save */
#define GAME NW_SAVE_GAME_SIZE

static uint8_t *card_with(const uint8_t *save, size_t save_size, bool ecc, uint32_t gap, size_t *size)
{
    static Pcb m;
    const char *names[2] = { "icon.sys", RAW };
    const void *data[2] = { "icon", save };
    size_t lens[2] = { 4, save_size };
    pcb_build(&m, gap, NW_SAVE_PS2_NAME, save ? 2 : 1, names, data, lens);
    return pcb_image(&m, ecc, size);
}

static void test_card(const uint8_t *games)
{
    size_t size = 0, len = 0;
    bool missing = false;
    for (int variant = 0; variant < 4; variant++) {
        uint8_t *img = card_with(games, SAVE_SIZE, variant & 1, (uint32_t)variant / 2, &size);
        CHECK(nw_ps2card_is_card(img, size));
        uint8_t *file = nw_ps2card_file(img, size, NW_SAVE_PS2_NAME, RAW, &len, &missing);
        CHECK(file && len == SAVE_SIZE && memcmp(file, games, SAVE_SIZE) == 0 && !missing);
        SDL_free(file);
        CHECK(!nw_ps2card_file(img, size, NW_SAVE_PS2_NAME, "nothing", &len, &missing) && missing);
        CHECK(!nw_ps2card_file(img, size, "OTHER", RAW, &len, &missing) && missing);
        CHECK(!nw_ps2card_file(img, size - 1, NW_SAVE_PS2_NAME, RAW, &len, &missing) &&
              !missing);
        free(img);
    }

    /* a chain that loops back and a FAT outside the card */
    static Pcb m;
    const char *names[1] = { RAW };
    const void *data[1] = { games };
    size_t lens[1] = { SAVE_SIZE };
    pcb_build(&m, 0, NW_SAVE_PS2_NAME, 1, names, data, lens);
    uint8_t *entry = pcb_entry_at(&m, pcb_rd32(pcb_entry_at(&m, 0, 2) + 0x10), 2);
    uint32_t first = pcb_rd32(entry + 0x10), last = first;
    while (pcb_rd32(pcb_fat(&m, last)) != PCB_END) {
        last = pcb_rd32(pcb_fat(&m, last)) & 0x7FFFFFFFu;
    }
    pcb_le32(pcb_fat(&m, last), 0x80000000u | first);
    pcb_le32(entry + 0x04, 0x100000); /* a size longer than the card */
    uint8_t *img = pcb_image(&m, true, &size);
    CHECK(!nw_ps2card_file(img, size, NW_SAVE_PS2_NAME, names[0], &len, &missing) && !missing);
    free(img);
    pcb_le32(entry + 0x04, PCB_CLUSTERS * PCB_CLUSTER + 1); /* more clusters than the card has */
    img = pcb_image(&m, true, &size);
    CHECK(!nw_ps2card_file(img, size, NW_SAVE_PS2_NAME, names[0], &len, &missing) && !missing &&
          strstr(SDL_GetError(), "loops"));
    free(img);
    pcb_le32(m.c[PCB_IFC], 0x7FFFFFF0u);
    img = pcb_image(&m, true, &size);
    CHECK(!nw_ps2card_file(img, size, NW_SAVE_PS2_NAME, names[0], &len, &missing) && !missing);
    free(img);
    pcb_le32(m.c[PCB_IFC], PCB_FAT);
    pcb_le16(m.c[0] + 0x2A, 0);
    img = pcb_image(&m, true, &size);
    CHECK(!nw_ps2card_file(img, size, NW_SAVE_PS2_NAME, names[0], &len, &missing) && !missing);
    free(img);

    uint8_t blank[4096];
    memset(blank, 0xFF, sizeof(blank));
    CHECK(!nw_ps2card_is_card(blank, sizeof(blank)));
    CHECK(!nw_ps2card_file(blank, sizeof(blank), "a", "b", &len, &missing) && !missing);
}

/* A PS2 save's 4 games from games: 0, 2 and 3 games, 1 empty. */
static void as_card(uint8_t *card, const uint8_t *games)
{
    memcpy(card, games, SAVE_SIZE);
    for (int i = 0; i < NW_SAVE_PS2_GAMES; i++) {
        card[i * GAME + NW_SAVE_EMPTY] = i == 1;
    }
}

/* The file an older NuWorld build wrote: the codec's 4 records. */
static void put_older(const uint8_t *card, const char *name)
{
    NwDoc doc;
    size_t size = 0;
    char *text = NULL;
    CHECK(nw_doc_init(&doc) && nw_save_merge(&doc, &reg, card, NW_SAVE_PS2_GAMES) &&
          (text = nw_doc_write(&doc, NULL, &size)));
    if (text) {
        put(name, text, size);
    }
    SDL_free(text);
    nw_doc_free(&doc);
}

/* A game per file with its time, kept as .bak when replaced or deleted. */
static void test_saves(const uint8_t *games, const uint8_t *other)
{
    uint8_t g[GAME], o[GAME], e[GAME], back[GAME];
    NwSaveTime when = { 2026, 9, 24, 23, 28, 5 }, t;
    size_t size = 0;
    memcpy(g, games, GAME);
    g[NW_SAVE_EMPTY] = 0;
    memcpy(o, other, GAME);
    o[NW_SAVE_EMPTY] = 0;
    memcpy(e, g, GAME);
    e[NW_SAVE_EMPTY] = 1;

    fresh();
    memset(back, 0xAA, sizeof(back));
    CHECK(!nw_save_started(dir) && nw_save_load(dir, 0, &reg, back, NULL, &t) == NW_SAVE_NONE && back[0] == 0xAA);
    CHECK(nw_save_scan(dir, NULL) == -1);
    CHECK(nw_save_store(dir, 0, &reg, g, NULL, &when) && there("save001.toml") && nw_save_exists(dir, 0));
    CHECK(nw_save_started(dir) && !nw_save_exists(dir, 1) && !there("save001.toml.bak"));
    CHECK(nw_save_load(dir, 0, &reg, back, NULL, &t) == NW_SAVE_OK && memcmp(back, g, GAME) == 0);
    CHECK(t.year == 2026 && t.month == 9 && t.day == 24 && t.hour == 23 && t.minute == 28 && t.second == 5);
    char *text = get("save001.toml", &size);
    CHECK(text && strstr(text, "saved = \"2026-09-24T23:28:05\"") && strstr(text, "[slot.0]") &&
          !strstr(text, "[slot.1]"));
    SDL_free(text);

    /* saved now; out of range; an empty game is no save */
    CHECK(nw_save_store(dir, NW_SAVE_GAMES - 1, &reg, o, NULL, NULL) && there("save100.toml"));
    CHECK(nw_save_load(dir, NW_SAVE_GAMES - 1, &reg, back, NULL, &t) == NW_SAVE_OK && memcmp(back, o, GAME) == 0);
    CHECK(t.year >= 2024 && t.month >= 1);
    int last = 0;
    CHECK(nw_save_scan(dir, &last) >= 0 && last == NW_SAVE_GAMES - 1);
    CHECK(!nw_save_store(dir, NW_SAVE_GAMES, &reg, g, NULL, NULL) && !nw_save_store(dir, -1, &reg, g, NULL, NULL));
    CHECK(nw_save_load(dir, NW_SAVE_GAMES, &reg, back, NULL, NULL) == NW_SAVE_FAILED);
    CHECK(!nw_save_store(dir, 1, &reg, e, NULL, NULL) && !there("save002.toml"));
    put("save002.toml", "format = 1\n[slot.0]\nempty = 1\n", 30);
    CHECK(nw_save_load(dir, 1, &reg, back, NULL, NULL) == NW_SAVE_NONE);
    put("save003.toml", "format = 1\n[slot.0]\nlives = 3\n", 30);
    CHECK(nw_save_load(dir, 2, &reg, back, NULL, &t) == NW_SAVE_OK && back[NW_SAVE_LIVES] == 3 && t.year == 0);

    /* replaced: the last one kept as .bak, loaded when the file does not, the next save sets the file aside */
    CHECK(nw_save_store(dir, 0, &reg, o, NULL, &when) && there("save001.toml.bak"));
    CHECK(nw_save_load(dir, 0, &reg, back, NULL, NULL) == NW_SAVE_OK && memcmp(back, o, GAME) == 0);
    put("save001.toml", "format = 1\n[slot.0\n", 19);
    CHECK(nw_save_load(dir, 0, &reg, back, NULL, &t) == NW_SAVE_BACKUP && strstr(SDL_GetError(), "line 2"));
    CHECK(memcmp(back, g, GAME) == 0 && t.year == 2026);
    CHECK(nw_save_store(dir, 0, &reg, o, NULL, NULL) && there("save001.toml.corrupt"));
    CHECK(nw_save_load(dir, 0, &reg, back, NULL, NULL) == NW_SAVE_OK && memcmp(back, o, GAME) == 0);

    /* a newer build's file is neither loaded nor overwritten */
    static const char newer[] = "format = 2\n[slot.0]\nlives = 9\n";
    put("save004.toml", newer, sizeof(newer) - 1);
    CHECK(nw_save_load(dir, 3, &reg, back, NULL, NULL) == NW_SAVE_FAILED && strstr(SDL_GetError(), "newer"));
    CHECK(!nw_save_store(dir, 3, &reg, g, NULL, NULL));
    char *kept = get("save004.toml", &size);
    CHECK(kept && size == sizeof(newer) - 1 && memcmp(kept, newer, size) == 0);
    SDL_free(kept);

    /* NuWorld's longer names beside the record; without them their keys go; one the codec does not keep is empty */
    static NwSaveNames names, names_back;
    strcpy(names.game, "NUWORLD TEAM");
    strcpy(names.time[3][1], "ROBERTA");
    CHECK(nw_save_store(dir, 7, &reg, g, &names, NULL));
    memset(&names_back, 0x55, sizeof(names_back));
    CHECK(nw_save_load(dir, 7, &reg, back, &names_back, NULL) == NW_SAVE_OK);
    CHECK(memcmp(&names, &names_back, sizeof(names)) == 0);
    text = get("save008.toml", &size);
    CHECK(text && strstr(text, "long_name = \"NUWORLD TEAM\"") && strstr(text, "long_names") &&
          strstr(text, "\"ROBERTA\""));
    SDL_free(text);
    CHECK(nw_save_store(dir, 7, &reg, g, NULL, NULL));
    CHECK(nw_save_load(dir, 7, &reg, back, &names_back, NULL) == NW_SAVE_OK && !names_back.game[0] &&
          !names_back.time[3][1][0]);
    text = get("save008.toml", &size);
    CHECK(text && !strstr(text, "long_name"));
    SDL_free(text);
    static const char odd[] = "format = 1\n[slot.0]\nlives = 3\nlong_name = \"lower case\"\n";
    put("save009.toml", odd, sizeof(odd) - 1);
    CHECK(nw_save_load(dir, 8, &reg, back, &names_back, NULL) == NW_SAVE_OK && !names_back.game[0]);
    CHECK(nw_save_name_ok("AB 9", 4, 4) && !nw_save_name_ok("AB 9", 4, 3) && !nw_save_name_ok("ab", 2, 8));

    /* deleted to .bak: no save afterwards, and the folder has had saves */
    CHECK(nw_save_remove(dir, 0) == NW_SAVE_OK && !nw_save_exists(dir, 0) && there("save001.toml.bak"));
    CHECK(nw_save_remove(dir, 0) == NW_SAVE_NONE && nw_save_load(dir, 0, &reg, back, NULL, NULL) == NW_SAVE_NONE);
    for (int n = 1; n < NW_SAVE_GAMES; n++) {
        CHECK(nw_save_remove(dir, n) != NW_SAVE_FAILED);
    }
    CHECK(nw_save_started(dir) && nw_save_scan(dir, &last) == -1 && last == -1);
}

/* Before the first save, once: an older NuWorld file, else the folder's PS2 save, by the games' places. */
static void test_convert(const uint8_t *games)
{
    uint8_t card[SAVE_SIZE], back[GAME];
    NwSaveTime t;
    size_t size = 0;
    as_card(card, games);

    fresh();
    CHECK(nw_save_convert(dir, &reg) == 0 && !nw_save_started(dir));

    /* an older NuWorld build's file: renamed once converted, at its own time */
    fresh();
    put_older(card, OLDER);
    CHECK(nw_save_convert(dir, &reg) == 3);
    CHECK(there("save001.toml") && !there("save002.toml") && there("save003.toml") && there("save004.toml"));
    CHECK(!there(OLDER) && there(OLDER ".converted"));
    CHECK(nw_save_load(dir, 2, &reg, back, NULL, &t) == NW_SAVE_OK && memcmp(back, card + 2 * GAME, GAME) == 0);
    CHECK(t.year >= 2024);
    put_older(card, OLDER);
    CHECK(nw_save_convert(dir, &reg) == 0 && there(OLDER)); /* the folder has saves: never again */

    /* its .bak when the file does not load; a newer build's is kept and not converted */
    fresh();
    put_older(card, OLDER ".bak");
    put(OLDER, "format = 1\n[slot.0\n", 19);
    CHECK(nw_save_convert(dir, &reg) == 3 && there("save004.toml"));
    fresh();
    put(OLDER, "format = 2\n", 11);
    CHECK(nw_save_convert(dir, &reg) == -1 && strstr(SDL_GetError(), "newer") && there(OLDER));
    CHECK(!nw_save_started(dir));

    /* the folder's raw PS2 save, left as it is */
    fresh();
    put(RAW, card, SAVE_SIZE);
    CHECK(nw_save_convert(dir, &reg) == 3 && there("save004.toml") && there(RAW));
    CHECK(nw_save_load(dir, 3, &reg, back, NULL, NULL) == NW_SAVE_OK && memcmp(back, card + 3 * GAME, GAME) == 0);

    /* card images, by name: the first that holds the save; a stray file is passed over */
    fresh();
    uint8_t *with = card_with(card, SAVE_SIZE, true, 1, &size);
    put("b.ps2", with, size);
    free(with);
    uint8_t *without = card_with(NULL, 0, true, 0, &size);
    put("a.ps2", without, size);
    free(without);
    put("0.PS2", "not a card", 10);
    CHECK(nw_save_convert(dir, &reg) == 3);
    CHECK(nw_save_load(dir, 0, &reg, back, NULL, NULL) == NW_SAVE_OK && memcmp(back, card, GAME) == 0);

    /* nothing usable: no save, and no file made */
    fresh();
    put("x.ps2", "junk", 4);
    put(RAW, card, 100);
    CHECK(nw_save_convert(dir, &reg) == 0 && !nw_save_started(dir));
}

/* A PS2 save's games into the first free saves, in their order; nothing written when one does not convert. */
static void test_import(const uint8_t *games, const uint8_t *other)
{
    uint8_t card[SAVE_SIZE], g[GAME], o[GAME], back[GAME];
    size_t size = 0;
    as_card(card, games);
    memcpy(g, games, GAME);
    g[NW_SAVE_EMPTY] = 0;
    memcpy(o, other, GAME);
    o[NW_SAVE_EMPTY] = 0;

    fresh();
    CHECK(nw_save_store(dir, 0, &reg, g, NULL, NULL) && nw_save_store(dir, 2, &reg, o, NULL, NULL));
    uint8_t *with = card_with(card, SAVE_SIZE, false, 0, &size);
    put("card.ps2", with, size);
    free(with);
    CHECK(nw_save_import(dir, path_of("card.ps2"), &reg) == 3);
    CHECK(nw_save_load(dir, 0, &reg, back, NULL, NULL) == NW_SAVE_OK && memcmp(back, g, GAME) == 0);
    CHECK(nw_save_load(dir, 1, &reg, back, NULL, NULL) == NW_SAVE_OK && memcmp(back, card, GAME) == 0);
    CHECK(nw_save_load(dir, 3, &reg, back, NULL, NULL) == NW_SAVE_OK && memcmp(back, card + 2 * GAME, GAME) == 0);
    CHECK(nw_save_load(dir, 4, &reg, back, NULL, NULL) == NW_SAVE_OK && memcmp(back, card + 3 * GAME, GAME) == 0);
    CHECK(!nw_save_exists(dir, 5));
    put(RAW, card, SAVE_SIZE);
    CHECK(nw_save_import(dir, path_of(RAW), &reg) == 3 && nw_save_exists(dir, 7) && !nw_save_exists(dir, 8));

    uint8_t *without = card_with(NULL, 0, true, 0, &size);
    put("empty.ps2", without, size);
    free(without);
    CHECK(nw_save_import(dir, path_of("empty.ps2"), &reg) == 0);
    uint8_t blank[1024];
    memset(blank, 0xFF, sizeof(blank));
    put("blank.ps2", blank, sizeof(blank));
    CHECK(nw_save_import(dir, path_of("blank.ps2"), &reg) == -1 && strstr(SDL_GetError(), "card"));
    put("short", games, 16);
    CHECK(nw_save_import(dir, path_of("short"), &reg) == -1 && strstr(SDL_GetError(), "short"));
    CHECK(nw_save_import(dir, path_of("missing"), &reg) == -1);
    with = card_with(card, 100, true, 0, &size);
    put("odd.ps2", with, size);
    free(with);
    CHECK(nw_save_import(dir, path_of("odd.ps2"), &reg) == -1);

    /* crafted saves (a time name without its NUL, a language past the tables) are refused before anything is written */
    fresh();
    uint8_t *crafted = malloc(SAVE_SIZE);
    memcpy(crafted, card, SAVE_SIZE);
    memcpy(crafted + 2 * GAME + NW_SAVE_LEVEL + NW_SAVE_LEVEL_TIME, "ABCD", 4);
    put("crafted", crafted, SAVE_SIZE);
    CHECK(nw_save_import(dir, path_of("crafted"), &reg) == -1 && strstr(SDL_GetError(), "slot.2"));
    CHECK(!nw_save_started(dir));
    crafted[2 * GAME + NW_SAVE_LEVEL + NW_SAVE_LEVEL_TIME + 3] = 0;
    crafted[NW_SAVE_LANGUAGE] = 200;
    with = card_with(crafted, SAVE_SIZE, true, 0, &size);
    put("crafted.ps2", with, size);
    free(with);
    CHECK(nw_save_import(dir, path_of("crafted.ps2"), &reg) == -1 && strstr(SDL_GetError(), "language"));
    CHECK(!nw_save_started(dir));
    free(crafted);

    /* no room: nothing written */
    for (int n = 0; n < NW_SAVE_GAMES - 1; n++) {
        CHECK(nw_save_store(dir, n, &reg, g, NULL, NULL));
    }
    with = card_with(card, SAVE_SIZE, false, 0, &size);
    put("card.ps2", with, size);
    free(with);
    CHECK(nw_save_import(dir, path_of("card.ps2"), &reg) == -1 && strstr(SDL_GetError(), "free saves"));
    CHECK(!nw_save_exists(dir, NW_SAVE_GAMES - 1));

    /* what tests/save_toml.py reads with Python's tomllib */
    fresh();
    CHECK(nw_save_store(dir, 0, &reg, g, NULL, NULL) && nw_save_store(dir, 1, &reg, o, NULL, NULL));
}

/* The card's Hamming code of 128 bytes, as the PS2 card manager checks it. */
static void ecc_of(const uint8_t *chunk, uint8_t out[3])
{
    static const uint8_t columns[7] = { 0x55, 0x33, 0x0F, 0x00, 0xAA, 0xCC, 0xF0 };
    uint8_t cp = 0x77, lp0 = 0x7F, lp1 = 0x7F;
    for (int i = 0; i < 128; i++) {
        uint8_t b = chunk[i], mask = 0;
        for (int k = 0; k < 7; k++) {
            mask |= (uint8_t)((uint8_t)(__builtin_popcount(b & columns[k]) & 1) << k);
        }
        cp ^= mask;
        if (__builtin_popcount(b) & 1) {
            lp0 ^= (uint8_t)~i;
            lp1 ^= (uint8_t)i;
        }
    }
    out[0] = cp;
    out[1] = lp0 & 0x7F;
    out[2] = lp1;
}

/* Pages with a spare that is not all zero (the builder's) carry their ECC; how many do. */
static int ecc_pages(const uint8_t *img, size_t size)
{
    int written = 0;
    for (size_t at = 0; at + 528 <= size; at += 528) {
        const uint8_t *spare = img + at + 512;
        bool any = false;
        for (int i = 0; i < 16; i++) {
            any = any || spare[i];
        }
        if (!any) {
            continue;
        }
        written++;
        for (int k = 0; k < 4; k++) {
            uint8_t want[3];
            ecc_of(img + at + 128 * k, want);
            if (memcmp(spare + 3 * k, want, 3) != 0) {
                return -1;
            }
        }
    }
    return written;
}

/* The builder's card's free file clusters (its one FAT cluster), in an image with ECC. */
static int free_clusters(const uint8_t *img)
{
    int free = 0;
    for (uint32_t n = 0; n < PCB_CLUSTERS - PCB_ALLOC; n++) {
        uint32_t at = 4 * n, page = PCB_FAT * PCB_PPC + at / PCB_PAGE;
        free += !(pcb_rd32(img + (size_t)page * 528 + at % PCB_PAGE) & 0x80000000u);
    }
    return free;
}

/* A file of the card at path, SDL_malloc'd. */
static uint8_t *card_file(const char *card, const char *folder, const char *name, size_t *len)
{
    size_t size = 0;
    bool missing = false;
    uint8_t *img = get(card, &size);
    uint8_t *file = img ? nw_ps2card_file(img, size, folder, name, len, &missing) : NULL;
    SDL_free(img);
    return file;
}

/* The card writer itself: files made, grown, shrunk, kept; another folder untouched; the card full. */
static void test_card_put(void)
{
    static Pcb m;
    size_t size = 0, len = 0;
    bool missing = false;
    static uint8_t big[5000], small[300], other[700];
    for (size_t i = 0; i < sizeof(big); i++) {
        big[i] = (uint8_t)rnd();
    }
    memset(small, 0x5A, sizeof(small));
    memset(other, 0x33, sizeof(other));
    const char *names[1] = { "OTHERFILE" };
    const void *data[1] = { other };
    size_t lens[1] = { sizeof(other) };
    pcb_build(&m, 1, "BESLUS-OTHER", 1, names, data, lens);
    uint8_t *img = pcb_image(&m, true, &size);

    NwPs2CardFile files[2] = { { "A", big, sizeof(big), false }, { "B", small, sizeof(small), false } };
    int before = free_clusters(img);
    CHECK(nw_ps2card_put(img, size, "NEWDIR", files, 2, 0));
    uint8_t *a = nw_ps2card_file(img, size, "NEWDIR", "A", &len, &missing);
    CHECK(a && len == sizeof(big) && memcmp(a, big, len) == 0);
    SDL_free(a);
    int made = free_clusters(img);
    CHECK(before - made == 5 + 1 + 2); /* A, B, the folder's 4 entries */
    /* shrunk (its clusters freed), then grown again; kept */
    files[0].len = 100;
    files[1].keep = true;
    files[1].data = big;
    CHECK(nw_ps2card_put(img, size, "NEWDIR", files, 2, 0));
    CHECK(free_clusters(img) == made + 4);
    a = nw_ps2card_file(img, size, "NEWDIR", "A", &len, &missing);
    CHECK(a && len == 100 && memcmp(a, big, len) == 0);
    SDL_free(a);
    uint8_t *b = nw_ps2card_file(img, size, "NEWDIR", "B", &len, &missing);
    CHECK(b && len == sizeof(small) && memcmp(b, small, len) == 0);
    SDL_free(b);
    files[0].len = sizeof(big);
    CHECK(nw_ps2card_put(img, size, "NEWDIR", files, 1, 0) && free_clusters(img) == made);
    a = nw_ps2card_file(img, size, "NEWDIR", "A", &len, &missing);
    CHECK(a && len == sizeof(big) && memcmp(a, big, len) == 0);
    SDL_free(a);
    uint8_t *o = nw_ps2card_file(img, size, "BESLUS-OTHER", "OTHERFILE", &len, &missing);
    CHECK(o && len == sizeof(other) && memcmp(o, other, len) == 0);
    SDL_free(o);
    CHECK(ecc_pages(img, size) > 0);

    /* a name too long; the card full */
    CHECK(!nw_ps2card_put(img, size, "0123456789012345678901234567890123", files, 1, 0) &&
          strstr(SDL_GetError(), "folder"));
    static uint8_t huge[PCB_CLUSTERS * PCB_CLUSTER];
    NwPs2CardFile full = { "HUGE", huge, sizeof(huge), false };
    CHECK(!nw_ps2card_put(img, size, "NEWDIR", &full, 1, 0) && strstr(SDL_GetError(), "full"));
    free(img);

    /* without ECC, the pages are only the data */
    img = pcb_image(&m, false, &size);
    CHECK(nw_ps2card_put(img, size, "NEWDIR", files, 2, 0));
    a = nw_ps2card_file(img, size, "NEWDIR", "A", &len, &missing);
    CHECK(a && len == sizeof(big) && memcmp(a, big, len) == 0);
    SDL_free(a);
    free(img);
}

/* NuWorld's saves back into a card: each PS2 slot a save, the card's game or empty; other saves stay; read back by
   the import as they went. */
static void test_export(const uint8_t *games, const uint8_t *other)
{
    uint8_t g[GAME], o[GAME], back[SAVE_SIZE], card_games[SAVE_SIZE], icon[3000];
    size_t size = 0, len = 0;
    for (size_t i = 0; i < sizeof(icon); i++) {
        icon[i] = (uint8_t)rnd();
    }
    memcpy(g, games, GAME);
    g[NW_SAVE_EMPTY] = 0;
    memcpy(o, other, GAME);
    o[NW_SAVE_EMPTY] = 0;
    memcpy(g, "NUWORLD", 8); /* "NUWORLD TEAM" typed: the record keeps its 8 letters */
    static NwSaveNames names;
    strcpy(names.game, "NUWORLD TEAM");
    strcpy(names.time[3][0], "ROBERTA");

    fresh();
    CHECK(nw_save_store(dir, 0, &reg, g, &names, NULL) && nw_save_store(dir, 4, &reg, o, NULL, NULL));
    /* a card with another game's save, none of this one */
    static Pcb m;
    const char *names_other[1] = { "OTHERFILE" };
    const void *data_other[1] = { "other game" };
    size_t lens_other[1] = { 10 };
    pcb_build(&m, 1, "BASLUS-20238", 1, names_other, data_other, lens_other);
    uint8_t *img = pcb_image(&m, true, &size);
    put("card.ps2", img, size);
    CHECK(nw_save_card(path_of("card.ps2"), &reg, card_games) == NW_SAVE_CARD_NONE);

    int picks[4] = { 0, NW_SAVE_PS2_KEEP, NW_SAVE_PS2_EMPTY, 4 };
    CHECK(nw_save_export(dir, &reg, path_of("card.ps2"), picks, icon, sizeof(icon)));
    CHECK(nw_save_card(path_of("card.ps2"), &reg, card_games) == NW_SAVE_CARD_SAVE);
    CHECK(memcmp(card_games, g, GAME) == 0 && memcmp(card_games + 3 * GAME, o, GAME) == 0);
    CHECK(card_games[GAME + NW_SAVE_EMPTY] && card_games[2 * GAME + NW_SAVE_EMPTY]);
    uint8_t *before = get("card.ps2.bak", &len);
    CHECK(before && len == size && memcmp(before, img, size) == 0 && !there("card.ps2.tmp"));
    SDL_free(before);
    free(img);
    uint8_t *file = card_file("card.ps2", NW_SAVE_PS2_NAME, "crash.ico", &len);
    CHECK(file && len == sizeof(icon) && memcmp(file, icon, len) == 0);
    SDL_free(file);
    file = card_file("card.ps2", NW_SAVE_PS2_NAME, "icon.sys", &len);
    CHECK(file && len == 0x3C4 && memcmp(file, "PS2D", 4) == 0 && file[6] == 30 && file[0xC0] == 0x82 &&
          file[0xC1] == 0x62 && strcmp((char *)file + 0x104, "crash.ico") == 0);
    SDL_free(file);
    file = card_file("card.ps2", NW_SAVE_PS2_NAME, NW_SAVE_PS2_NAME, &len);
    CHECK(file && len == 4);
    SDL_free(file);
    file = card_file("card.ps2", "BASLUS-20238", "OTHERFILE", &len);
    CHECK(file && len == 10 && memcmp(file, "other game", 10) == 0);
    SDL_free(file);
    img = get("card.ps2", &size);
    CHECK(img && ecc_pages(img, size) > 0);
    SDL_free(img);

    /* again over the card's save, without the icon (it has one): kept slots stay */
    int again[4] = { NW_SAVE_PS2_KEEP, 4, NW_SAVE_PS2_KEEP, NW_SAVE_PS2_EMPTY };
    CHECK(nw_save_export(dir, &reg, path_of("card.ps2"), again, NULL, 0));
    CHECK(nw_save_card(path_of("card.ps2"), &reg, back) == NW_SAVE_CARD_SAVE);
    CHECK(memcmp(back, g, GAME) == 0 && memcmp(back + GAME, o, GAME) == 0 && back[2 * GAME + NW_SAVE_EMPTY] &&
          back[3 * GAME + NW_SAVE_EMPTY] && memcmp(back + 3 * GAME + 1, o + 1, 16) == 0);

    /* imported back: the games as they went, the game's own names */
    char *keep = SDL_strdup(dir);
    SDL_snprintf(dir, sizeof(dir), "%sback/", keep);
    CHECK(SDL_CreateDirectory(dir));
    CHECK(nw_save_import(dir, path_of("../card.ps2"), &reg) == 2);
    uint8_t one[GAME];
    static NwSaveNames none;
    CHECK(nw_save_load(dir, 0, &reg, one, &none, NULL) == NW_SAVE_OK && memcmp(one, g, GAME) == 0 && !none.game[0] &&
          !none.time[3][0][0]);
    CHECK(nw_save_load(dir, 1, &reg, one, NULL, NULL) == NW_SAVE_OK && memcmp(one, o, GAME) == 0);
    SDL_strlcpy(dir, keep, sizeof(dir));
    SDL_free(keep);

    /* refused, the card unchanged: a save that is not there, no icon for a new save, a blank card, not a card */
    img = pcb_image(&m, true, &size);
    put("other.ps2", img, size);
    int missing_save[4] = { 7, NW_SAVE_PS2_KEEP, NW_SAVE_PS2_KEEP, NW_SAVE_PS2_KEEP };
    CHECK(!nw_save_export(dir, &reg, path_of("other.ps2"), missing_save, icon, sizeof(icon)) &&
          strstr(SDL_GetError(), "save 8"));
    CHECK(!nw_save_export(dir, &reg, path_of("other.ps2"), picks, NULL, 0) && strstr(SDL_GetError(), "icon"));
    uint8_t *now = get("other.ps2", &len);
    CHECK(now && len == size && memcmp(now, img, size) == 0 && !there("other.ps2.bak"));
    SDL_free(now);
    free(img);
    size = 528 * 256;
    uint8_t *blank = malloc(size);
    memset(blank, 0xFF, size);
    put("blank.ps2", blank, size);
    CHECK(nw_save_card(path_of("blank.ps2"), &reg, back) == NW_SAVE_CARD_BLANK && strstr(SDL_GetError(), "format"));
    CHECK(!nw_save_export(dir, &reg, path_of("blank.ps2"), picks, icon, sizeof(icon)) &&
          strstr(SDL_GetError(), "not formatted"));
    now = get("blank.ps2", &len);
    CHECK(now && len == size && memcmp(now, blank, size) == 0 && !there("blank.ps2.bak"));
    SDL_free(now);
    blank[7] = 1;
    put("junk.ps2", blank, size);
    CHECK(nw_save_card(path_of("junk.ps2"), &reg, back) == NW_SAVE_CARD_FAILED && strstr(SDL_GetError(), "not a PS2"));
    free(blank);
    CHECK(nw_save_card(path_of("nothing.ps2"), &reg, back) == NW_SAVE_CARD_FAILED);

    /* a card save that does not read: kept slots refuse, a full set of slots replaces it */
    uint8_t card[SAVE_SIZE];
    as_card(card, games);
    card[NW_SAVE_LANGUAGE] = 200;
    img = card_with(card, SAVE_SIZE, true, 0, &size);
    put("bad.ps2", img, size);
    free(img);
    CHECK(nw_save_card(path_of("bad.ps2"), &reg, back) == NW_SAVE_CARD_FAILED && strstr(SDL_GetError(), "language"));
    CHECK(!nw_save_export(dir, &reg, path_of("bad.ps2"), picks, icon, sizeof(icon)));
    int all[4] = { 0, NW_SAVE_PS2_EMPTY, NW_SAVE_PS2_EMPTY, 4 };
    CHECK(nw_save_export(dir, &reg, path_of("bad.ps2"), all, icon, sizeof(icon)));
    CHECK(nw_save_card(path_of("bad.ps2"), &reg, back) == NW_SAVE_CARD_SAVE && memcmp(back, g, GAME) == 0);
}

/* Interrupt every durable I/O stage, for both save kinds. A failed update must leave the old main readable.
   Also cover parseable truncation, two-generation recovery, stale temporaries and deliberate deletion. */
static void test_save_safety(const uint8_t *games)
{
    uint8_t first[GAME], second[GAME], third[GAME], back[GAME];
    memcpy(first, games, GAME);
    memcpy(second, games, GAME);
    memcpy(third, games, GAME);
    first[NW_SAVE_EMPTY] = second[NW_SAVE_EMPTY] = third[NW_SAVE_EMPTY] = 0;
    first[NW_SAVE_LIVES] = 3;
    second[NW_SAVE_LIVES] = 4;
    third[NW_SAVE_LIVES] = 5;
    NwSaveMeta meta = { .campaign = "0123456789abcdef0123456789abcdef", .autosave = true, .revision = 1 }, got;
    for (int automatic = 0; automatic <= 1; automatic++) {
        meta.revision = 1;
        int n = automatic ? NW_SAVE_GAMES : 0;
        const char *base = automatic ? "autosave001.toml" : "save001.toml";
        fresh();
        CHECK(nw_save_store_meta(dir, n, &reg, first, NULL, NULL, &meta));
        meta.revision = 2;
        CHECK(nw_save_store_meta(dir, n, &reg, second, NULL, NULL, &meta));
        meta.revision = 3;
        CHECK(nw_save_store_meta(dir, n, &reg, third, NULL, NULL, &meta));
        CHECK(nw_save_load_meta(dir, n, 1, &reg, back, NULL, NULL, &got) == NW_SAVE_OK);
        CHECK(back[NW_SAVE_LIVES] == 4 && got.revision == 2 && got.autosave && !strcmp(got.campaign, meta.campaign));
        CHECK(nw_save_load_meta(dir, n, 2, &reg, back, NULL, NULL, &got) == NW_SAVE_OK);
        CHECK(back[NW_SAVE_LIVES] == 3 && got.revision == 1);
        CHECK(SDL_RemovePath(path_of(base)));
        CHECK(nw_save_load_meta(dir, n, 0, &reg, back, NULL, NULL, NULL) == NW_SAVE_BACKUP);
        CHECK(back[NW_SAVE_LIVES] == 4 && nw_save_exists(dir, n));
        CHECK(nw_save_store_meta(dir, n, &reg, third, NULL, NULL, &meta));
        size_t size = 0;
        char *text = get(base, &size);
        char *boundary = text ? strstr(text, "[slot.0.options]") : NULL;
        CHECK(boundary != NULL);
        if (boundary) {
            put(base, text, (size_t)(boundary - text)); /* still valid TOML, but incomplete */
            CHECK(nw_save_load_meta(dir, n, 0, &reg, back, NULL, NULL, NULL) == NW_SAVE_BACKUP);
            CHECK(back[NW_SAVE_LIVES] == 4);
        }
        char backup[64], temporary[64];
        SDL_snprintf(backup, sizeof(backup), "%s.bak", base);
        SDL_snprintf(temporary, sizeof(temporary), "%s.tmp", base);
        put(backup, "# NuWorld integrity 1 99 1234\nformat = 1\n", 38);
        CHECK(nw_save_load_meta(dir, n, 0, &reg, back, NULL, NULL, NULL) == NW_SAVE_BACKUP);
        CHECK(back[NW_SAVE_LIVES] == 3);
        if (text) put(temporary, text, size); /* a completed but uncommitted write is never promoted */
        CHECK(nw_save_load_meta(dir, n, 0, &reg, back, NULL, NULL, NULL) == NW_SAVE_BACKUP && back[NW_SAVE_LIVES] == 3);
        CHECK(nw_save_store_meta(dir, n, &reg, second, NULL, NULL, &meta));
        CHECK(nw_save_load_meta(dir, n, 0, &reg, back, NULL, NULL, NULL) == NW_SAVE_OK && back[NW_SAVE_LIVES] == 4);
        CHECK(nw_save_remove(dir, n) == NW_SAVE_OK);
        CHECK(nw_save_load_meta(dir, n, 0, &reg, back, NULL, NULL, NULL) == NW_SAVE_NONE);
        CHECK(nw_save_load_meta(dir, n, 1, &reg, back, NULL, NULL, NULL) == NW_SAVE_OK); /* explicit recovery works */
        CHECK(nw_save_store_meta(dir, n, &reg, third, NULL, NULL, &meta));
        CHECK(nw_save_load_meta(dir, n, 0, &reg, back, NULL, NULL, NULL) == NW_SAVE_OK && back[NW_SAVE_LIVES] == 5);
        SDL_free(text);

        bool succeeded = false;
        for (int fault = 0; fault < 16; fault++) {
            fresh();
            CHECK(nw_save_store_meta(dir, n, &reg, first, NULL, NULL, &meta));
            CHECK(nw_save_store_meta(dir, n, &reg, second, NULL, NULL, &meta));
            nw_saveio_fail_after(fault);
            bool ok = nw_save_store_meta(dir, n, &reg, third, NULL, NULL, &meta);
            nw_saveio_fail_after(-1);
            CHECK(nw_save_load_meta(dir, n, 0, &reg, back, NULL, NULL, NULL) == NW_SAVE_OK);
            CHECK(back[NW_SAVE_LIVES] == (ok ? 5 : 4));
            CHECK(!there(temporary));
            succeeded = succeeded || ok;
        }
        CHECK(succeeded);
    }
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "usage: test_savefile <scratch dir>\n");
        return 2;
    }
    SDL_snprintf(dir, sizeof(dir), "%s/", argv[1]);
    test_doc();
    test_registry();
    test_codec();

    uint8_t *games = malloc(SAVE_SIZE), *other = malloc(SAVE_SIZE);
    for (int s = 0; s < NW_SAVE_PS2_GAMES; s++) {
        random_record(games + s * NW_SAVE_GAME_SIZE);
        random_record(other + s * NW_SAVE_GAME_SIZE);
    }
    test_saves(games, other);
    test_save_safety(games);
    test_card(games);
    test_convert(games);
    test_card_put();
    test_export(games, other);
    test_import(games, other); /* last: tests/save_toml.py reads what it leaves */
    free(games);
    free(other);
    nw_save_registry_free(&reg);

    if (failures) {
        fprintf(stderr, "savefile: %d checks failed\n", failures);
        return 1;
    }
    printf("savefile: all checks passed\n");
    return 0;
}
