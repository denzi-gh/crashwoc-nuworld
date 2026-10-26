/* Saves without the game: the TOML document, the records in it (lossless, keyed, what it does not know kept), the
   saves folder (.bak, .corrupt, delete, import) and PS2 card images (synthetic, mc_builder.h).
   Usage: test_savefile <scratch dir>. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "mc_builder.h"
#include "platform/paths.h"
#include "save/ps2card.h"
#include "save/savecodec.h"
#include "save/savedoc.h"
#include "save/savestore.h"

static int failures;
static char dir[1100];

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: CHECK(%s) failed; last error: %s\n", __FILE__, __LINE__, #cond, SDL_GetError()); \
            failures++; \
        } \
    } while (0)

#define SAVE_SIZE (NW_SAVE_RECORDS * NW_SAVE_GAME_SIZE)

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

/* A record of random bytes; every byte of game_s is a field, so any value has to come back. */
static void random_record(uint8_t *g)
{
    for (int i = 0; i < NW_SAVE_GAME_SIZE; i++) {
        g[i] = (uint8_t)rnd();
    }
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
        for (int s = 0; s < NW_SAVE_RECORDS; s++) {
            random_record(games + s * NW_SAVE_GAME_SIZE);
        }
        memset(games + 3 * NW_SAVE_GAME_SIZE, 0, NW_SAVE_GAME_SIZE); /* an empty slot of zeros */
        games[3 * NW_SAVE_GAME_SIZE + NW_SAVE_EMPTY] = 1;
        CHECK(round_trip(games, NW_SAVE_RECORDS, back, &text) && memcmp(games, back, SAVE_SIZE) == 0);
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
    for (int s = 1; s < NW_SAVE_RECORDS; s++) {
        games[s * NW_SAVE_GAME_SIZE + NW_SAVE_EMPTY] = 1;
    }
    CHECK(round_trip(games, NW_SAVE_RECORDS, back, &text) && memcmp(games, back, SAVE_SIZE) == 0);
    CHECK(starts(text, "format = 1\n\n[slot.0]\nempty = false\nname = \"CRASH   \"\nlives = 4\n"));
    CHECK(text && strstr(text, "powerbits = 0x00\ncutbits = 0x00000001\n"));
    CHECK(text && strstr(text, "\n[slot.0.options]\nvibration = 1\nsurround = 0\nsfx_volume = 100\n"));
    CHECK(text && strstr(text, "screen_x = -2\n"));
    CHECK(text && strstr(text, "\n[slot.0.hub.0]\nflags = 0x07\ncrystals = 5\n\n[slot.0.level.\"a/level_00/lev\"]\n"
                               "flags = 0x001f\ntime = [[\"1ST\", 25500], [\"\", 0], [\"\", 0]]\n\n[slot.1]\n"));
    CHECK(text && !strstr(text, "pad") && !strstr(text, "[slot.0.hub.1]") && !strstr(text, "level_01"));
    SDL_free(text);
    text = NULL;

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
    CHECK(nw_save_merge(&doc, &reg, games, NW_SAVE_RECORDS));
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
        CHECK(nw_save_decode(&doc, &reg, back, NW_SAVE_RECORDS) && memcmp(games, back, SAVE_SIZE) == 0);
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
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        CHECK(parse(&doc, bad[i]));
        if (!failures) {
            bool decoded = nw_save_decode(&doc, &reg, back, NW_SAVE_RECORDS);
            if (decoded) {
                fprintf(stderr, "decoded: %s", bad[i]);
            }
            CHECK(!decoded);
            nw_doc_free(&doc);
        }
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

#define MAIN NW_SAVE_CARD_NAME "slot00.toml"

static void test_store(const uint8_t *games, const uint8_t *other)
{
    uint8_t back[SAVE_SIZE];
    size_t size = 0;
    fresh();
    memset(back, 0xAA, sizeof(back));
    CHECK(nw_save_load(dir, 0, &reg, back, NW_SAVE_RECORDS) == NW_SAVE_NONE && back[0] == 0xAA);
    CHECK(nw_save_store(dir, 0, &reg, games, NW_SAVE_RECORDS) && there(MAIN) && !there(MAIN ".bak"));
    CHECK(!there(MAIN ".tmp") && nw_save_exists(dir, 0) && !nw_save_exists(dir, 1));
    CHECK(nw_save_load(dir, 0, &reg, back, NW_SAVE_RECORDS) == NW_SAVE_OK && memcmp(back, games, SAVE_SIZE) == 0);
    char *first = get(MAIN, &size);
    CHECK(nw_save_store(dir, 0, &reg, other, NW_SAVE_RECORDS));
    size_t bak_size = 0;
    char *bak = get(MAIN ".bak", &bak_size);
    CHECK(first && bak && size == bak_size && memcmp(first, bak, size) == 0);
    SDL_free(bak);

    /* fewer records than the file has: the others stay */
    CHECK(nw_save_store(dir, 0, &reg, games, 1));
    CHECK(nw_save_load(dir, 0, &reg, back, NW_SAVE_RECORDS) == NW_SAVE_OK);
    CHECK(memcmp(back, games, NW_SAVE_GAME_SIZE) == 0 &&
          memcmp(back + NW_SAVE_GAME_SIZE, other + NW_SAVE_GAME_SIZE, SAVE_SIZE - NW_SAVE_GAME_SIZE) == 0);
    CHECK(nw_save_load(dir, 0, &reg, back, 0) == NW_SAVE_FAILED);
    CHECK(nw_save_load(dir, 0, &reg, back, NW_SAVE_RECORDS + 1) == NW_SAVE_FAILED);
    CHECK(nw_save_load(dir, NW_SAVE_CARD_SLOTS, &reg, back, 1) == NW_SAVE_FAILED);

    /* a main file that does not load: the .bak is loaded, and the next save sets the file aside */
    CHECK(nw_save_store(dir, 0, &reg, games, NW_SAVE_RECORDS)); /* .bak: games + other */
    put(MAIN, "format = 1\n[slot.0\n", 19);
    CHECK(nw_save_load(dir, 0, &reg, back, NW_SAVE_RECORDS) == NW_SAVE_BACKUP && strstr(SDL_GetError(), "line 2"));
    CHECK(memcmp(back, games, NW_SAVE_GAME_SIZE) == 0);
    CHECK(nw_save_store(dir, 0, &reg, other, NW_SAVE_RECORDS) && there(MAIN ".corrupt"));
    CHECK(nw_save_load(dir, 0, &reg, back, NW_SAVE_RECORDS) == NW_SAVE_OK && memcmp(back, other, SAVE_SIZE) == 0);
    put(MAIN ".bak", "garbage", 7);
    put(MAIN, "format = 1\nlives = [\n", 21);
    CHECK(nw_save_load(dir, 0, &reg, back, NW_SAVE_RECORDS) == NW_SAVE_FAILED);

    /* a newer build's file is neither loaded nor overwritten */
    static const char newer[] = "format = 2\n[slot.0]\nlives = 9\n";
    put(MAIN, newer, sizeof(newer) - 1);
    CHECK(nw_save_load(dir, 0, &reg, back, NW_SAVE_RECORDS) == NW_SAVE_FAILED && strstr(SDL_GetError(), "newer"));
    CHECK(!nw_save_store(dir, 0, &reg, games, NW_SAVE_RECORDS));
    char *kept = get(MAIN, &size);
    CHECK(kept && size == sizeof(newer) - 1 && memcmp(kept, newer, size) == 0);
    SDL_free(kept);

    /* delete: no data afterwards, and no import over it */
    CHECK(nw_save_store(dir, 1, &reg, games, NW_SAVE_RECORDS) && there(NW_SAVE_CARD_NAME "slot01.toml"));
    CHECK(nw_save_remove(dir, 1) == NW_SAVE_OK && !nw_save_exists(dir, 1) && there(NW_SAVE_CARD_NAME "slot01.toml.bak"));
    CHECK(nw_save_remove(dir, 1) == NW_SAVE_NONE);
    put(NW_SAVE_CARD_NAME "slot01", games, SAVE_SIZE);
    CHECK(nw_save_load(dir, 1, &reg, back, NW_SAVE_RECORDS) == NW_SAVE_NONE && !nw_save_exists(dir, 1));
    SDL_free(first);
}

static uint8_t *card_with(const uint8_t *save, size_t save_size, bool ecc, uint32_t gap, size_t *size)
{
    static Mcb m;
    const char *names[2] = { "icon.sys", NW_SAVE_CARD_NAME "slot00" };
    const void *data[2] = { "icon", save };
    size_t lens[2] = { 4, save_size };
    mcb_build(&m, gap, NW_SAVE_CARD_NAME, save ? 2 : 1, names, data, lens);
    return mcb_image(&m, ecc, size);
}

static void test_card(const uint8_t *games)
{
    size_t size = 0, len = 0;
    bool missing = false;
    for (int variant = 0; variant < 4; variant++) {
        uint8_t *img = card_with(games, SAVE_SIZE, variant & 1, (uint32_t)variant / 2, &size);
        CHECK(nw_ps2card_is_card(img, size));
        uint8_t *file = nw_ps2card_file(img, size, NW_SAVE_CARD_NAME, NW_SAVE_CARD_NAME "slot00", &len, &missing);
        CHECK(file && len == SAVE_SIZE && memcmp(file, games, SAVE_SIZE) == 0 && !missing);
        SDL_free(file);
        CHECK(!nw_ps2card_file(img, size, NW_SAVE_CARD_NAME, "nothing", &len, &missing) && missing);
        CHECK(!nw_ps2card_file(img, size, "OTHER", NW_SAVE_CARD_NAME "slot00", &len, &missing) && missing);
        CHECK(!nw_ps2card_file(img, size - 1, NW_SAVE_CARD_NAME, NW_SAVE_CARD_NAME "slot00", &len, &missing) &&
              !missing);
        free(img);
    }

    /* a chain that loops back and a FAT outside the card */
    static Mcb m;
    const char *names[1] = { NW_SAVE_CARD_NAME "slot00" };
    const void *data[1] = { games };
    size_t lens[1] = { SAVE_SIZE };
    mcb_build(&m, 0, NW_SAVE_CARD_NAME, 1, names, data, lens);
    uint8_t *entry = mcb_entry_at(&m, mcb_rd32(mcb_entry_at(&m, 0, 2) + 0x10), 2);
    uint32_t first = mcb_rd32(entry + 0x10), last = first;
    while (mcb_rd32(mcb_fat(&m, last)) != MCB_END) {
        last = mcb_rd32(mcb_fat(&m, last)) & 0x7FFFFFFFu;
    }
    mcb_le32(mcb_fat(&m, last), 0x80000000u | first);
    mcb_le32(entry + 0x04, 0x100000); /* a size longer than the card */
    uint8_t *img = mcb_image(&m, true, &size);
    CHECK(!nw_ps2card_file(img, size, NW_SAVE_CARD_NAME, names[0], &len, &missing) && !missing);
    free(img);
    mcb_le32(entry + 0x04, MCB_CLUSTERS * MCB_CLUSTER + 1); /* more clusters than the card has */
    img = mcb_image(&m, true, &size);
    CHECK(!nw_ps2card_file(img, size, NW_SAVE_CARD_NAME, names[0], &len, &missing) && !missing &&
          strstr(SDL_GetError(), "loops"));
    free(img);
    mcb_le32(m.c[MCB_IFC], 0x7FFFFFF0u);
    img = mcb_image(&m, true, &size);
    CHECK(!nw_ps2card_file(img, size, NW_SAVE_CARD_NAME, names[0], &len, &missing) && !missing);
    free(img);
    mcb_le32(m.c[MCB_IFC], MCB_FAT);
    mcb_le16(m.c[0] + 0x2A, 0);
    img = mcb_image(&m, true, &size);
    CHECK(!nw_ps2card_file(img, size, NW_SAVE_CARD_NAME, names[0], &len, &missing) && !missing);
    free(img);

    uint8_t blank[4096];
    memset(blank, 0xFF, sizeof(blank));
    CHECK(!nw_ps2card_is_card(blank, sizeof(blank)));
    CHECK(!nw_ps2card_file(blank, sizeof(blank), "a", "b", &len, &missing) && !missing);
}

static void test_import(const uint8_t *games, const uint8_t *other)
{
    uint8_t back[SAVE_SIZE];
    size_t size = 0;

    /* a raw save in the folder, imported on the first load and left as it is */
    fresh();
    put(NW_SAVE_CARD_NAME "slot00", games, SAVE_SIZE);
    CHECK(nw_save_load(dir, 0, &reg, back, NW_SAVE_RECORDS) == NW_SAVE_IMPORTED && memcmp(back, games, SAVE_SIZE) == 0);
    CHECK(there(MAIN) && there(NW_SAVE_CARD_NAME "slot00"));
    CHECK(nw_save_load(dir, 0, &reg, back, NW_SAVE_RECORDS) == NW_SAVE_OK);

    /* card images, by name: the first that holds the save */
    fresh();
    uint8_t *with = card_with(games, SAVE_SIZE, true, 1, &size);
    put("b.ps2", with, size);
    free(with);
    uint8_t *without = card_with(NULL, 0, true, 0, &size);
    put("a.ps2", without, size);
    put("0.PS2", "not a card", 10);
    free(without);
    CHECK(nw_save_load(dir, 0, &reg, back, NW_SAVE_RECORDS) == NW_SAVE_IMPORTED && strstr(SDL_GetError(), "b.ps2"));
    CHECK(memcmp(back, games, SAVE_SIZE) == 0);

    /* nothing usable: no save, and no file made */
    fresh();
    put("x.ps2", "junk", 4);
    put(NW_SAVE_CARD_NAME "slot00", games, 100);
    CHECK(nw_save_load(dir, 0, &reg, back, NW_SAVE_RECORDS) == NW_SAVE_NONE && !there(MAIN));

    /* explicit: over an existing save, which is kept */
    fresh();
    CHECK(nw_save_store(dir, 0, &reg, other, NW_SAVE_RECORDS));
    with = card_with(games, SAVE_SIZE, false, 0, &size);
    put("card.ps2", with, size);
    free(with);
    CHECK(nw_save_import(dir, 0, path_of("card.ps2"), &reg) == NW_SAVE_IMPORTED);
    CHECK(nw_save_load(dir, 0, &reg, back, NW_SAVE_RECORDS) == NW_SAVE_OK && memcmp(back, games, SAVE_SIZE) == 0);
    CHECK(nw_save_remove(dir, 0) == NW_SAVE_OK);
    CHECK(nw_save_load(dir, 0, &reg, back, NW_SAVE_RECORDS) == NW_SAVE_NONE); /* the .bak holds the import now */
    without = card_with(NULL, 0, true, 0, &size);
    put("empty.ps2", without, size);
    free(without);
    CHECK(nw_save_import(dir, 0, path_of("empty.ps2"), &reg) == NW_SAVE_NONE);
    uint8_t blank[1024];
    memset(blank, 0xFF, sizeof(blank));
    put("blank.ps2", blank, sizeof(blank));
    CHECK(nw_save_import(dir, 0, path_of("blank.ps2"), &reg) == NW_SAVE_FAILED && strstr(SDL_GetError(), "card"));
    put("short", games, 16);
    CHECK(nw_save_import(dir, 0, path_of("short"), &reg) == NW_SAVE_FAILED);
    CHECK(nw_save_import(dir, 0, path_of("missing"), &reg) == NW_SAVE_FAILED);
    with = card_with(games, 100, true, 0, &size);
    put("odd.ps2", with, size);
    free(with);
    CHECK(nw_save_import(dir, 0, path_of("odd.ps2"), &reg) == NW_SAVE_FAILED);

    /* what tests/save_toml.py reads with Python's tomllib */
    CHECK(nw_save_store(dir, 0, &reg, games, NW_SAVE_RECORDS));
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
    for (int i = 0; i < SAVE_SIZE; i++) {
        games[i] = (uint8_t)rnd();
        other[i] = (uint8_t)rnd();
    }
    test_store(games, other);
    test_card(games);
    test_import(games, other);
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
