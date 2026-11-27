#ifndef NUWORLD_PORT_SUBTITLES_H
#define NUWORLD_PORT_SUBTITLES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Same order as Game.language and the original voice tables. */
enum {
    NW_SUBTITLE_EN, NW_SUBTITLE_FR, NW_SUBTITLE_DE, NW_SUBTITLE_IT, NW_SUBTITLE_ES, NW_SUBTITLE_NL,
    NW_SUBTITLE_LANGUAGES,
};

typedef struct NwSubtitleCue {
    uint32_t start, end; /* milliseconds in the voice sample, not display frames */
    const char *text;
} NwSubtitleCue;

typedef struct NwSubtitleTrack {
    const char *voice;
    int language;
    uint32_t duration;
    const NwSubtitleCue *cues;
    size_t count;
} NwSubtitleTrack;

#define NW_SUBTITLE_LINES 3
#define NW_SUBTITLE_LINE_SIZE 256
typedef struct NwSubtitleLayout {
    char line[NW_SUBTITLE_LINES][NW_SUBTITLE_LINE_SIZE];
    int count, speaker_length; /* the first line, including the colon */
    float scale;
} NwSubtitleLayout;

/* Immutable UTF-8, identified by the original voice filename. */
const char *nw_subtitles_at(const char *voice, int language, uint32_t ms);
/* Uppercase Text3D glyphs, word wrapping and scaling. */
bool nw_subtitles_layout(const char *utf8, NwSubtitleLayout *out);
/* The active game-thread profile. */
bool nw_subtitles_enabled(bool title);

/* Baseline of the topmost HUD line drawn in the current panel. */
void nw_subtitles_hud_line(float y);

#endif
