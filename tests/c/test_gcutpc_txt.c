/* The .TXT and MOVIES.TXT readers (src/port/gcutpc/config.c, movies.c) against docs/csp-spec.md: the same text and
   values as crashWoCEditor/tests/test_csp_txt.py, the same refusals, and the files the port ships.

     nuworld_test_gcutpc_txt <assets/levels> */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port/gcutpc/nugcutpc_config.h"

static int failures;

#define CHECK(x)                                                                                                     \
    do {                                                                                                             \
        if (!(x)) {                                                                                                  \
            fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #x);                                                 \
            failures++;                                                                                              \
        }                                                                                                            \
    } while (0)

static const char STATION[] =
    ";; a comment\n"
    "fpsec           30\n"
    "scene           main    levels\\b\\intro\\spacer.gsc\n"
    "play_beside     levels\\b\\intro2\\stationt    looping\n"
    "sfx             173     per_language\n"
    "sfx             12      frame 40    pos 1 2 -3\n"
    "holdaudio\n"
    "light           0   dir -.5 -1 -.5   colour .4 .8 .4\n"
    "settings {\r\n"
    "    frame   690\n"
    "    fogr    49\n"
    "}\n"
    "subtitle {\n"
    "    text        \"Hello; world\"\n"
    "    start_time  10\n"
    "    end_time    40.5\n"
    "    align       bottom\n"
    "}\n"
    "event           station.arrive  40\n"
    "next_cut_scene  levels\\b\\intro2\\corridor\n"
    "on_end          next\n";

static NUGCUTPCCONFIG_s *configure(const char *text, NUGCUTPCERROR_s *error)
{
    return NuGCutPCConfigure(text, strlen(text), "STATION.TXT", error);
}

static void test_read(void)
{
    NUGCUTPCERROR_s error;
    NUGCUTPCCONFIG_s *c = configure(STATION, &error);
    CHECK(c != NULL);
    if (!c) {
        fprintf(stderr, "  %s\n", error.text);
        return;
    }
    CHECK(c->fpsec == 30);
    CHECK(c->scene_count == 1 && strcmp(c->scenes[0].name, "main") == 0 &&
          strcmp(c->scenes[0].path, "levels\\b\\intro\\spacer.gsc") == 0);
    CHECK(c->beside_count == 1 && c->beside[0].looping);
    CHECK(c->sfx_count == 2);
    CHECK(c->sfx[0].id == 173 && c->sfx[0].per_language && isnan(c->sfx[0].frame) && isnan(c->sfx[0].pos[0]));
    CHECK(c->sfx[1].id == 12 && c->sfx[1].frame == 40 && c->sfx[1].pos[2] == -3 && !c->sfx[1].per_language);
    CHECK(c->holdaudio && !c->looping);
    CHECK(c->light_count == 1 && c->lights[0].dir[0] == -0.5f && c->lights[0].colour[1] == 0.8f);
    CHECK(c->settings_count == 1 && c->settings[0].frame == 690 && c->settings[0].fog[0] == 49);
    CHECK(c->settings[0].fog[1] == NUGCUTPC_NOT_GIVEN && isnan(c->settings[0].farclip));
    CHECK(c->subtitle_count == 1 && strcmp(c->subtitles[0].text, "Hello; world") == 0);
    CHECK(c->subtitles[0].end_time == 40.5f && c->subtitles[0].align == NUGCUTPC_ALIGN_BOTTOM);
    CHECK(c->subtitles[0].text_id == NUGCUTPC_NOT_GIVEN && isnan(c->subtitles[0].colour[0]));
    CHECK(c->event_count == 1 && strcmp(c->events[0].name, "station.arrive") == 0 && c->events[0].frame == 40);
    CHECK(strcmp(c->next_cut_scene, "levels\\b\\intro2\\corridor") == 0 && c->on_end == NUGCUTPC_END_NEXT);
    /* not given */
    CHECK(c->music == NUGCUTPC_NOT_GIVEN && c->render_group == NUGCUTPC_NOT_GIVEN && isnan(c->farclip));
    CHECK(!c->skip_given && isnan(c->ambient[0]) && c->top_back[0] == NUGCUTPC_NOT_GIVEN && c->ptl == NULL);
    NuGCutPCConfigFree(c);
}

static void test_defaults(void)
{
    NUGCUTPCCONFIG_s *c = configure("", NULL);
    CHECK(c && c->fpsec == 30 && c->scene_count == 0 && c->on_end == NUGCUTPC_END_NOT_GIVEN);
    NuGCutPCConfigFree(c);
    c = configure("skip on to 800 hint 4\nats a.ats b.ats\ntex_anim 2 on\ntex_anim 4 off\n", NULL);
    CHECK(c && c->skip_given && c->skip_on && c->skip_to == 800 && c->skip_hint == 4);
    CHECK(c && c->ats_count == 2 && strcmp(c->ats[1], "b.ats") == 0);
    CHECK(c && c->tex_anim_count == 2 && c->tex_anim[0].on && !c->tex_anim[1].on && c->tex_anim[1].bit == 4);
    NuGCutPCConfigFree(c);
}

static void test_many(void)
{
    char text[64 * 40] = "";
    for (int i = 0; i < 64; i++) {
        char line[40];
        snprintf(line, sizeof(line), "character model%d.ghg name%d\n", i, i);
        strcat(text, line);
    }
    NUGCUTPCCONFIG_s *c = configure(text, NULL);
    CHECK(c && c->character_count == 64 && strcmp(c->characters[63].name, "name63") == 0);
    CHECK(c && strcmp(c->characters[0].path, "model0.ghg") == 0);
    NuGCutPCConfigFree(c);
}

/* The same refusals as test_csp_txt.py: the reason names the file and the line. */
static void test_refused(void)
{
    static const struct {
        const char *text, *reason;
    } cases[] = {
        { "fpsec", "STATION.TXT:1: fpsec needs" },
        { "fpsec fast", "not a number" },
        { "colour 1 2 3", "unknown word 'colour'" },
        { "on_end later", "not one of its choices" },
        { "music 1\nmusic 2", ":2: music twice" },
        { "sfx 1 loud", "unexpected 'loud'" },
        { "settings {\nframe 1", "not closed" },
        { "}", "without a block" },
        { "subtitle {\nalign middle\n}", "not one of its choices" },
        { "top_backr 300", "not 0-255" },
        { "next_cut_scene \"open", "not closed" },
        { "settings", "opens a block" },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
        NUGCUTPCERROR_s error;
        NUGCUTPCCONFIG_s *c = configure(cases[i].text, &error);
        if (c || !strstr(error.text, cases[i].reason)) {
            fprintf(stderr, "test_refused: \"%s\": %s (expected \"%s\")\n", cases[i].text, c ? "read" : error.text,
                    cases[i].reason);
            failures++;
        }
        NuGCutPCConfigFree(c);
    }
}

static void test_movies(void)
{
    const char *text = "movie 0 levels\\b\\logos\\logosr\nmovie 1 levels\\b\\intro2\\stationr\n";
    NUGCUTPCERROR_s error;
    NUGCUTPCMOVIES_s *m = NuGCutPCMoviesRead(text, strlen(text), NULL, &error);
    CHECK(m && m->count == 2 && strcmp(NuGCutPCMovieCut(m, 1), "levels\\b\\intro2\\stationr") == 0);
    CHECK(m && NuGCutPCMovieCut(m, 3) == NULL);
    NuGCutPCMoviesFree(m);
    const char *twice = "movie 0 a\nmovie 0 b\n";
    CHECK(!NuGCutPCMoviesRead(twice, strlen(twice), NULL, &error) && strstr(error.text, "movie 0 twice"));
}

/* The files the port ships. */

static char *read_text(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *text = malloc((size_t)(n > 0 ? n : 0) + 1);
    *size = text ? fread(text, 1, (size_t)n, f) : 0;
    fclose(f);
    if (text) {
        text[*size] = '\0';
    }
    return text;
}

static void test_shipped(const char *levels)
{
    static const char *const configs[] = { "b/logos/logosr.txt", "b/logos/logos2r.txt", "b/intro2/stationt.txt",
                                           "b/intro2/stationr.txt" };
    char path[1024];
    size_t size;
    for (size_t i = 0; i < sizeof(configs) / sizeof(*configs); i++) {
        snprintf(path, sizeof(path), "%s/%s", levels, configs[i]);
        char *text = read_text(path, &size);
        NUGCUTPCERROR_s error = { "no file" };
        NUGCUTPCCONFIG_s *c = text ? NuGCutPCConfigure(text, size, configs[i], &error) : NULL;
        if (!c) {
            fprintf(stderr, "test_shipped: %s: %s\n", configs[i], error.text);
            failures++;
        }
        CHECK(!c || (c->fpsec == 30 && c->scene_count == 1));
        NuGCutPCConfigFree(c);
        free(text);
    }
    snprintf(path, sizeof(path), "%s/b/movies.txt", levels);
    char *text = read_text(path, &size);
    NUGCUTPCMOVIES_s *m = text ? NuGCutPCMoviesRead(text, size, "movies.txt", NULL) : NULL;
    CHECK(m && strcmp(NuGCutPCMovieCut(m, 0), "levels\\b\\logos\\logosr") == 0);
    NuGCutPCMoviesFree(m);
    free(text);
}

int main(int argc, char **argv)
{
    test_read();
    test_defaults();
    test_many();
    test_refused();
    test_movies();
    if (argc > 1) {
        test_shipped(argv[1]);
    }
    if (failures) {
        fprintf(stderr, "gcutpc-txt: %d checks failed\n", failures);
        return 1;
    }
    printf("gcutpc-txt: all checks passed\n");
    return 0;
}
