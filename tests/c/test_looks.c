/* .looks files (port/lookfile.h) and the named signals (port/signals.h): the shipped files read into the recipes the
   opening wore in C, the drives, the slots, and the reader's errors with their lines; the shipped ATS files read, and
   start from events they declare. Arguments: assets/levels, assets/opening. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "nu3d/nutexanm.h"
#include "port/lookfile.h"
#include "port/nutexanm.h"
#include "port/signals.h"

static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s (%s)\n", __LINE__, #x, SDL_GetError()); failures++; } } while (0)

/* The engine's and the ATS tracks' signals the shipped files read. */
static float seconds, strike, elec = 1, wobble = 1;

static void make_signals(void)
{
    CHECK(nw_signal_bind("sky.seconds", &seconds) && nw_signal_bind("bolt.first", &strike));
    CHECK(nw_signal_bind("elec", &elec) && nw_signal_bind("wobble", &wobble));
    static const char *const tracks[] = {"title.formed", "meter.flash", "meter.level", "subtitle.reveal", "subtitle.flash",
                                         "button.glow"};
    for (size_t i = 0; i < SDL_arraysize(tracks); i++) CHECK(nw_signal(tracks[i]) != NULL);
}

static NwLookFile *read_file(const char *dir, const char *name)
{
    char *path = NULL;
    size_t size = 0;
    void *text = SDL_asprintf(&path, "%s/%s", dir, name) >= 0 ? SDL_LoadFile(path, &size) : NULL;
    NwLookFile *file = text ? nw_lookfile_read(path, text, size) : NULL;
    SDL_free(text);
    SDL_free(path);
    return file;
}

static bool same(const float *a, const float *b)
{
    return a && !memcmp(a, b, 16 * sizeof(float));
}

/* The shipped files give the recipes src/port/opening/looks.c held, bit for bit. */
static void test_shipped(const char *dir)
{
    static const float crash[16] = {3, .24f, .3f, 22, .97f, .95f, .06f, .08f, .96f, .8f, .03f, -2, .7f, .55f, .2f, 0};
    static const float subtitle_back[16] = {3, 1, .25f, 24, .28f, .44f, .5f, .06f, .1f, .19f, .21f, -1, 0, 1, 0, 0};
    static const float ring[16] = {2, 1, 0, 0, 1, 1, 1, .05f, 0, 0, 0, 0, 0, 0, 0, .025f};
    static const float button[16] = {3, 0, .4f, 30, .95f, .1f, .05f, .06f, .35f, 0, 0, .15f, .6f, .05f, .02f, .035f};
    static const float water[16] = {1, 0, 0, 1, 0, -.317f, .955f, .675f, 1, 1, .8f, 1, 1, 0, 0, 0};
    static const float led_15[16] = {3, 0, .3f, 24, 1, .15000004f, .08f, .05f, .45f, .06750001f, .035999995f, .55f,
                                     .5f, .07500002f, .04f, .012f};
    static const float bevel[16] = {3, 0, .45f, 22, 1, .95f, .92f, .12f, .78f, .64f, .56f, -3, 0, 0, 0, 0};
    NwLookFile *title = read_file(dir, "b/logos/title.looks"), *spacer = read_file(dir, "b/intro/spacer.looks");
    CHECK(title && spacer);
    if (!title || !spacer) return;

    /* With every signal at zero and the switches off, a recipe is its own values. */
    elec = wobble = 0;
    CHECK(same(nw_lookfile_frame(title, nw_lookfile_slot(title, NULL, 0)), crash));
    CHECK(same(nw_lookfile_frame(title, nw_lookfile_slot(title, NULL, 3)), subtitle_back) == false); /* flash: 0 */
    CHECK(same(nw_lookfile_frame(title, nw_lookfile_slot(title, NULL, 5)), ring));
    CHECK(same(nw_lookfile_frame(title, nw_lookfile_find(title, "led-15")), led_15));
    CHECK(same(nw_lookfile_frame(spacer, nw_lookfile_slot(spacer, "UIS", 51)), bevel));
    CHECK(nw_lookfile_slot(spacer, "UIS", 3) == nw_lookfile_find(spacer, "universal-face"));
    CHECK(nw_lookfile_slot(spacer, NULL, 3) < 0 && nw_lookfile_slot(title, NULL, 10) < 0);
    /* The meter: Dot_1..15 each their own copy of material 2; LED n lit while meter.level is above n - 0.5. */
    NwLookSplit split;
    CHECK(nw_lookfile_split(title, 0, &split) && split.slot == 2 && !strcmp(split.prefix, "Dot_") &&
          split.first == 1 && split.last == 15 && !strcmp(split.group, "meter") && !nw_lookfile_split(title, 1, &split));
    float *level = nw_signal("meter.level");
    int off = nw_lookfile_find(title, "led-off");
    *level = 2.49f;
    CHECK(nw_lookfile_counted(title, "meter", 1) == nw_lookfile_find(title, "led-01"));
    CHECK(nw_lookfile_counted(title, "meter", 2) == nw_lookfile_find(title, "led-02"));
    CHECK(nw_lookfile_counted(title, "meter", 3) == off);
    *level = 2.51f;
    CHECK(nw_lookfile_counted(title, "meter", 3) == nw_lookfile_find(title, "led-03"));
    *level = 99;
    CHECK(nw_lookfile_counted(title, "meter", 15) == nw_lookfile_find(title, "led-15"));
    CHECK(nw_lookfile_counted(title, "meter", 16) == off && nw_lookfile_counted(title, "other", 1) < 0);
    *level = 0;

    /* The drives, as the C table had them. */
    *nw_signal("subtitle.flash") = 1;
    CHECK(same(nw_lookfile_frame(title, nw_lookfile_slot(title, NULL, 3)), subtitle_back));
    float glow = .5f;
    *nw_signal("button.glow") = glow;
    float lit_button[16];
    memcpy(lit_button, button, sizeof(lit_button));
    lit_button[11] = glow;
    for (int k = 12; k < 15; k++) lit_button[k] = button[k] * glow;
    CHECK(same(nw_lookfile_frame(title, nw_lookfile_slot(title, NULL, 7)), lit_button));
    seconds = 2.5f;
    strike = 7;
    *nw_signal("title.formed") = .25f;
    float formed[16];
    memcpy(formed, water, sizeof(formed));
    formed[1] = 2.5f;
    formed[2] = .25f;
    formed[13] = 7;
    CHECK(same(nw_lookfile_frame(title, nw_lookfile_slot(title, NULL, 4)), formed));
    const float *motion = nw_lookfile_frame(title, nw_lookfile_slot(title, NULL, 0));
    CHECK(motion && motion[15] == 0); /* wobble off */
    wobble = 1;
    motion = nw_lookfile_frame(title, nw_lookfile_slot(title, NULL, 0));
    CHECK(motion && motion[15] == 3.5f);
    elec = 1;
    const float *electric = nw_lookfile_frame(title, nw_lookfile_slot(title, NULL, 5));
    CHECK(electric && electric[8] == 2.5f && electric[9] == 1 && electric[10] == 7);
    seconds = NAN;
    CHECK(!nw_lookfile_frame(title, nw_lookfile_slot(title, NULL, 0)));
    seconds = 0;
    nw_lookfile_free(title);
    nw_lookfile_free(spacer);
}

/* Each wrong file is refused, naming its line. */
static void test_errors(void)
{
    static const struct { const char *text, *error; } cases[] = {
        {"material a gloss {\n}\n", "x:1: the file starts with: looks 1"},
        {"looks 2\n", "x:1: the file starts with"},
        {"looks 1\nmaterial a glass {\n}\n", "x:2: unknown shader glass"},
        {"looks 1\nmaterial a gloss {\n\tshine 1\n}\n", "x:3: gloss has no field shine"},
        {"looks 1\nmaterial a gloss {\n\tlit 1 1\n}\n", "x:3: lit expects 3 number(s)"},
        {"looks 1\nmaterial a gloss {\n\tlit 1 1 1 1\n}\n", "x:3: lit expects 3"},
        {"looks 1\nmaterial a gloss {\n\trim 1e39\n}\n", "x:3: rim expects 1"},
        {"looks 1\nmaterial a gloss {\n\tglow 1 1 1\n\tsides 1 1 1\n}\n", "x:4: sides overlaps"},
        {"looks 1\nmaterial a gloss {\n\tmode shiny\n}\n", "x:3: mode: unknown value"},
        {"looks 1\nmaterial a gloss {\n\tmode gradient\n}\n", "x:4: a gradient needs a positive height"},
        {"looks 1\nmaterial a gloss {\n\tdrive rim nobody.writes\n}\n", "x:3: drive: no one writes the signal nobody.writes"},
        {"looks 1\nmaterial a gloss {\n\tdrive mode sky.seconds\n}\n", "x:3: drive: gloss has no numeric field mode"},
        {"looks 1\nmaterial a gloss {\n\tdrive rim sky.seconds twice\n}\n", "x:3: drive: expected scale"},
        {"looks 1\nmaterial a gloss {\n", "x:2: a block is missing its }"},
        {"looks 1\nmaterial a gloss {\n}\nmaterial a metal {\n}\n", "x:4: material a already defined"},
        {"looks 1\nslot 2 nothing\n", "x:2: no material nothing"},
        {"looks 1\nmaterial a gloss {\n}\nslot 2 a\nslot 2 a\n", "x:5: slot 2 given twice"},
        {"looks 1\nobject UIS {\n\tmaterial a gloss {\n}\n", "x:3: an object's block holds slot and default lines"},
        {"looks 1\n}\n", "x:2: } without a block"},
        {"looks 1\nsplit 2 Dot_\n", "x:2: expected split SLOT JOINTa..b GROUP"},
        {"looks 1\nsplit 2 Dot_5..1 meter\n", "x:2: expected split SLOT"},
        {"looks 1\ncount meter meter.level off a lit b1..2\n", "x:2: no material a"},
        {"looks 1\nmaterial a gloss {\n}\nmaterial b1 gloss {\n}\ncount meter meter.level off a lit b1..2\n",
         "x:6: no material b2"},
        {"looks 1\ncount meter nobody.writes off a lit b1..2\n", "x:2: count: no one writes the signal nobody.writes"},
        {"looks 1\nshine\n", "x:2: unknown line shine"},
    };
    for (size_t i = 0; i < SDL_arraysize(cases); i++) {
        NwLookFile *file = nw_lookfile_read("x", cases[i].text, strlen(cases[i].text));
        bool refused = !file && strstr(SDL_GetError(), cases[i].error) != NULL;
        if (!refused) fprintf(stderr, "case %zu: got '%s'\n", i, file ? "(read)" : SDL_GetError());
        CHECK(refused);
        nw_lookfile_free(file);
    }
}

/* Names: bound storage, made on first use, a name taken elsewhere refused, curve players writing theirs. */
static void test_signals(void)
{
    static float mine, other;
    CHECK(nw_signal_bind("test.mine", &mine) && nw_signal_bind("test.mine", &mine));
    CHECK(!nw_signal_bind("test.mine", &other));
    CHECK(nw_signal_find("test.mine") == &mine && !nw_signal_find("test.none"));
    float *made = nw_signal("test.made");
    CHECK(made && nw_signal("test.made") == made && !nw_signal_bind("test.made", &other));
    char long_name[80];
    memset(long_name, 'a', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = 0;
    CHECK(!nw_signal(long_name));
    NwCurveKey keys[] = {{0, 0, true, false}, {1, 2, false, false}};
    NwCurve curve = {.name = "level", .keys = keys, .key_count = 2, .rate_num = 1, .rate_den = 1,
                     .event = "testfx.full", .event_at = 1, .event_after = .25f};
    NwCurvePlayer player = {0};
    CHECK(nw_signal_player(&player, (NwCurveSet){.effect = "testfx", .curves = &curve, .count = 1}));
    const float *full = nw_signal_find("testfx.full");
    CHECK(full && *full == 1e9f); /* the curve's event, declared by its player */
    nw_curve_player_write(&player, (NwCurveTime){.seconds = .5f, .units = 0});
    const float *level = nw_signal_find("testfx.level");
    CHECK(level && *level == 1);
    nw_signal_events_update(10);
    CHECK(full && *full == 10.25f);
    nw_curve_player_destroy(&player);
}

/* Events: not yet until the source reaches its value, then the host's time plus the delay, once; kept when declared
   again, not yet again after a reset. */
static void test_events(void)
{
    static float source;
    CHECK(nw_signal_event("test.event", NW_EVENT_REACH, &source, 1, .5f));
    const float *when = nw_signal_find("test.event");
    nw_signal_events_update(2);
    CHECK(when && *when == 1e9f);
    if (!when) return;
    source = 1;
    nw_signal_events_update(3);
    nw_signal_events_update(4);
    CHECK(*when == 3.5f);
    CHECK(nw_signal_event("test.event", NW_EVENT_REACH, &source, 1, .5f) && *when == 3.5f);
    nw_signal_events_reset();
    CHECK(*when == 1e9f);
    nw_signal_events_update(5);
    CHECK(*when == 5.5f);

    /* BELOW: once its source has reached the value, the first fall under it. */
    static float level;
    CHECK(nw_signal_event("test.low", NW_EVENT_BELOW, &level, .5f, 0));
    const float *low = nw_signal_find("test.low");
    nw_signal_events_update(6);
    CHECK(!nw_signal_happened(low)); /* 0 from the start is not a fall */
    level = 1;
    nw_signal_events_update(7);
    level = .25f;
    nw_signal_events_update(8);
    CHECK(nw_signal_happened(low) && *low == 8);

    /* AFTER, declared before the event it follows: both in one update, at the first's time plus the delay. */
    static float trigger;
    CHECK(nw_signal_event("test.second", NW_EVENT_AFTER, nw_signal("test.first"), 0, .5f));
    CHECK(nw_signal_event("test.first", NW_EVENT_REACH, &trigger, 1, .25f));
    const float *first = nw_signal_find("test.first"), *second = nw_signal_find("test.second");
    nw_signal_events_update(9);
    CHECK(*second == 1e9f);
    trigger = 1;
    nw_signal_events_update(10);
    CHECK(*first == 10.25f && *second == 10.75f && !nw_signal_happened(first)); /* not yet by the clock */
    nw_signal_events_update(11);
    CHECK(nw_signal_happened(first) && nw_signal_happened(second) && !nw_signal_happened(nw_signal("test.made")));

    /* A curve's own start: its event and delay, not the set's; one no one writes is an error. */
    NwCurveKey keys[] = {{0, 0, true, false}, {1, 2, false, false}};
    NwCurve late = {.name = "late", .keys = keys, .key_count = 2, .rate_num = 1, .rate_den = 1,
                    .start = "test.first", .start_after = .5f};
    NwCurvePlayer player = {0};
    CHECK(nw_signal_player(&player, (NwCurveSet){.effect = "testfx", .curves = &late, .count = 1, .start = "test.low"}));
    CHECK(nw_curve_player_sample(&player, 660, 60) && *nw_signal("testfx.late") == .5f); /* 11 - 10.25 - .5 */
    SDL_strlcpy(late.start, "test.nobody", sizeof(late.start));
    CHECK(!nw_signal_player(&player, (NwCurveSet){.effect = "testfx", .curves = &late, .count = 1}) &&
          SDL_strstr(player.error, "test.nobody"));
    nw_curve_player_destroy(&player);
}

/* The ATS files ats.txt lists: each reads, and every start, and every event the opening waits for, names an event one
   of them declares. */
static void test_shipped_ats(const char *dir)
{
    char *path = NULL, *list = NULL, events[32][32];
    size_t size = 0;
    int read = 0, event_count = 0, start_count = 3, missing = 0;
    struct nutexanimprog_s *programs[32];
    const char *starts[40] = {"earth.start", "station.switch", "skip.open"}; /* the opening's own */
    if (SDL_asprintf(&path, "%s/ats.txt", dir) >= 0) list = SDL_LoadFile(path, &size);
    SDL_free(path);
    CHECK(list != NULL);
    for (char *line = list ? strtok(list, "\r\n") : NULL; line && read < 32; line = strtok(NULL, "\r\n")) {
        if (line[0] != ' ' && line[0] != '\t') continue; /* sections and comments */
        char *name = line + strspn(line, " \t");
        name[strcspn(name, " \t;")] = 0;
        const char *file = strchr(name, '/') ? strchr(name, '/') + 1 : name;
        char *text = NULL;
        size_t text_size = 0;
        if (SDL_asprintf(&path, "%s/%s", dir, file) >= 0) text = SDL_LoadFile(path, &text_size);
        SDL_free(path);
        struct nutexanimprog_s *p = text ? nw_texanim_from_memory(name, text, text_size) : NULL;
        SDL_free(text);
        if (!p) fprintf(stderr, "%s: %s\n", name, nw_texanim_error()->message);
        CHECK(p != NULL);
        if (!p) continue;
        programs[read++] = p;
        NwCurveSet set;
        if (!nw_texanim_curves(p, &set)) continue;
        if (set.start && start_count < 40) starts[start_count++] = set.start;
        for (size_t i = 0; i < set.count && event_count < 32; i++) {
            if (set.curves[i].event[0]) SDL_strlcpy(events[event_count++], set.curves[i].event, sizeof(events[0]));
            if (set.curves[i].start[0] && start_count < 40) starts[start_count++] = set.curves[i].start;
        }
        for (size_t i = 0; i < set.event_count && event_count < 32; i++)
            SDL_strlcpy(events[event_count++], set.events[i].name, sizeof(events[0]));
    }
    for (int i = 0; i < start_count; i++) {
        int k = 0;
        while (k < event_count && strcmp(events[k], starts[i])) k++;
        if (k == event_count) {
            fprintf(stderr, "no listed file declares the event %s\n", starts[i]);
            missing++;
        }
    }
    CHECK(read >= 8 && start_count >= 6 && !missing);
    for (int i = 0; i < read; i++) NuTexAnimProgDestroy(programs[i]);
    SDL_free(list);
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: test_looks ASSETS/LEVELS ASSETS/OPENING\n");
        return 2;
    }
    make_signals();
    test_shipped(argv[1]);
    test_errors();
    test_signals();
    test_events();
    test_shipped_ats(argv[2]);
    if (!failures) puts("looks: shipped recipes, drives, slots, errors, signals, events and shipped ATS checked");
    return failures ? 1 : 0;
}
