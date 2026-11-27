#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "audio/spu2.h"
#include "gameplay/gameplay.h"
#include "port/subtitles.h"

int RemapAccentedCharacter(char *c);

#include "port/subtitles.inc"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%d: %s\n", __LINE__, #c); failures++; } } while (0)

static void catalog(void)
{
    CHECK(nw_subtitle_track_count == NW_SUBTITLE_LANGUAGES * 54);
    int languages[NW_SUBTITLE_LANGUAGES] = { 0 };
    for (size_t i = 0; i < nw_subtitle_track_count; i++) {
        const NwSubtitleTrack *track = &nw_subtitle_tracks[i];
        if ((unsigned)track->language >= NW_SUBTITLE_LANGUAGES) {
            CHECK(false);
            continue;
        }
        languages[track->language]++;
        CHECK(track->count);
        int questions = 0, exclamations = 0;
        for (size_t k = 0; k < i; k++) {
            const NwSubtitleTrack *other = &nw_subtitle_tracks[k];
            CHECK(strcmp(track->voice, other->voice) || track->language != other->language);
        }
        for (size_t j = 0; j < track->count; j++) {
            const NwSubtitleCue *cue = &track->cues[j];
            CHECK(cue->end > cue->start && cue->end <= track->duration);
            CHECK(!j || cue->start >= track->cues[j - 1].end);
            CHECK(cue->text[0] && strlen(cue->text) < 512);
            const char *dialogue = strchr(cue->text, ':');
            CHECK(dialogue && !strchr(dialogue + 1, ':'));
            CHECK(dialogue && dialogue[1] == ' ' && dialogue[2]);
            CHECK(!strchr(cue->text, '"'));
            NwSubtitleLayout layout;
            CHECK(nw_subtitles_layout(cue->text, &layout));
            CHECK(layout.count > 0 && layout.count <= NW_SUBTITLE_LINES);
            const char *colon = strchr(layout.line[0], ':');
            CHECK(colon && layout.speaker_length == colon - layout.line[0] + 1);
            CHECK(layout.scale > 0.25f && layout.scale <= 0.34f);
            size_t expected = 0, drawn = 0;
            const char *p = cue->text;
            while (*p) {
                Uint32 c = SDL_StepUTF8(&p, NULL);
                CHECK(c != 0xfffd);
                if (track->language == NW_SUBTITLE_ES) {
                    questions += c == 0xbf ? 1 : c == '?' ? -1 : 0;
                    exclamations += c == 0xa1 ? 1 : c == '!' ? -1 : 0;
                    CHECK(questions >= 0 && exclamations >= 0);
                }
                expected += c == ' ' ? 0 : c == 0xdf ? 2 : 1;
            }
            for (int line = 0; line < layout.count; line++) {
                for (char *s = layout.line[line]; *s; s++) {
                    CHECK(*s < 'a' || *s > 'z'); /* plain letters cannot become button models */
                    drawn += *s != ' ';
                    if ((unsigned char)*s >= 0x80 && *s != (char)0xa8 && *s != (char)0xad) {
                        char c = *s;
                        CHECK(RemapAccentedCharacter(&c) >= 0);
                    }
                }
            }
            CHECK(drawn == expected); /* wrapping and case conversion never discard text */
            const char *first = nw_subtitles_at(track->voice, track->language, cue->start);
            const char *last = nw_subtitles_at(track->voice, track->language, cue->end - 1);
            CHECK(first && last && !strcmp(first, cue->text) && !strcmp(last, cue->text));
            if (j + 1 == track->count || track->cues[j + 1].start > cue->end) {
                CHECK(!nw_subtitles_at(track->voice, track->language, cue->end));
            }
        }
        /* A sentence may continue in the next cue, but its Spanish signs must close. */
        CHECK(questions == 0 && exclamations == 0);
    }
    for (int i = NW_SUBTITLE_EN; i < NW_SUBTITLE_LANGUAGES; i++) {
        CHECK(languages[i] == 54);
    }
    CHECK(!nw_subtitles_at(NULL, NW_SUBTITLE_EN, 10000));
    CHECK(!nw_subtitles_at("MUSIC.VAG", NW_SUBTITLE_EN, 10000));
    CHECK(!nw_subtitles_at("INTRO1.VAG", -1, 10000));
    CHECK(!nw_subtitles_at("INTRO1.VAG", 999, 10000));
    CHECK(!nw_subtitles_at("INTRO1.VAG", NW_SUBTITLE_EN, 0));
    CHECK(!nw_subtitles_at("INTRO1.VAG", NW_SUBTITLE_EN, UINT32_MAX));
    CHECK(nw_subtitles_at("intro1.vag", NW_SUBTITLE_EN, 10000));
    CHECK(!nw_subtitles_at("FRINTRO1.VAG", NW_SUBTITLE_EN, 10000));
}

static void playback(void)
{
    nw_profile_apply(&(NwProfile){ .title_subtitles = true });
    CHECK(!nw_subtitles_enabled(false) && !nw_subtitles_enabled(true));
    nw_profile_request(&(NwProfile){ .subtitles = true });
    CHECK(!nw_subtitles_enabled(false));
    nw_gameplay_swap();
    CHECK(nw_subtitles_enabled(false) && !nw_subtitles_enabled(true));
    nw_profile_request(&(NwProfile){ .subtitles = true, .title_subtitles = true });
    nw_gameplay_swap();
    CHECK(nw_subtitles_enabled(false) && nw_subtitles_enabled(true));

    NwSubtitleLayout layout;
    CHECK(nw_subtitles_layout("x o s t q u / Straße", &layout));
    CHECK(!strcmp(layout.line[0], "X O S T Q U / STRASSE"));
    CHECK(layout.speaker_length == 0);
    CHECK(!nw_subtitles_layout("", &layout));
    CHECK(!nw_subtitles_layout("   ", &layout));
    CHECK(!nw_subtitles_layout("#o", &layout));
    CHECK(!nw_subtitles_layout("🙂", &layout));
    CHECK(nw_subtitles_layout("Lo-Lo : C'est donc ça, le héros que je dois balayer ?", &layout));
    CHECK(layout.count == 2 && !strcmp(layout.line[1], "BALAYER ?"));
    CHECK(nw_subtitles_layout("Cortex: Do you hear me?", &layout));
    float normal = layout.scale;
    CHECK(nw_subtitles_layout("Cortex: DO YOU HEAR ME?", &layout));
    CHECK(layout.scale > normal);

    nw_spu2_init();
    uint64_t cycle = nw_spu2_cycle();
    nw_spu2_set_param(NW_SD_VP_PITCH | (4 << 1), 0x1000);
    nw_spu2_run_to(cycle + 768 * 100);
    CHECK(nw_spu2_voice_samples(4) == 100);
    nw_spu2_set_param(NW_SD_VP_PITCH | (4 << 1), 0); /* the existing audio pause */
    nw_spu2_run_to(cycle + 768 * 200);
    CHECK(nw_spu2_voice_samples(4) == 100);
    nw_spu2_set_param(NW_SD_VP_PITCH | (4 << 1), 0x1000);
    nw_spu2_set_switch(NW_SD_S_KON, 1u << 4);
    nw_spu2_run_to(cycle + 768 * 210);
    CHECK(nw_spu2_voice_samples(4) == 10);
    CHECK(!nw_spu2_voice_samples(48));
}

int main(void)
{
    catalog();
    playback();
    return failures ? 1 : 0;
}
