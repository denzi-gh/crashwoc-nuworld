/* NuGCutPC: the .TXT beside a .CSP and MOVIES.TXT (docs/csp-spec.md, paragraph 5), read into plain data.

   A configuration says how a cutscene is put together: its scenes and models, its sound, its timing, its look and
   what comes after it. It is data: whoever uses it reads the fields, nothing interprets the text again. A value the
   file does not give has the "not given" value written beside its field. */
#ifndef NUGCUTPC_CONFIG_H
#define NUGCUTPC_CONFIG_H

#include "port/gcutpc/nugcutpc.h"

#define NUGCUTPC_NOT_GIVEN (-1) /* an integer or colour the file does not give; a float the file does not give is NAN */

typedef enum NuGCutPCOnEnd {
    NUGCUTPC_END_NOT_GIVEN,
    NUGCUTPC_END_TITLE_MENU,
    NUGCUTPC_END_MOVIE,
    NUGCUTPC_END_NEXT,
} NuGCutPCOnEnd;

typedef enum NuGCutPCAlign {
    NUGCUTPC_ALIGN_CENTRE,
    NUGCUTPC_ALIGN_TOP,
    NUGCUTPC_ALIGN_TOP_RIGHT,
    NUGCUTPC_ALIGN_RIGHT,
    NUGCUTPC_ALIGN_BOTTOM_RIGHT,
    NUGCUTPC_ALIGN_BOTTOM,
    NUGCUTPC_ALIGN_BOTTOM_LEFT,
    NUGCUTPC_ALIGN_LEFT,
    NUGCUTPC_ALIGN_TOP_LEFT,
} NuGCutPCAlign;

typedef struct NUGCUTPCSCENEFILE_s {
    const char *name, *path;
} NUGCUTPCSCENEFILE_s;

typedef struct NUGCUTPCBESIDE_s {
    const char *cut;
    bool looping;
} NUGCUTPCBESIDE_s;

typedef struct NUGCUTPCMODEL_s {
    const char *path, *name;
} NUGCUTPCMODEL_s;

typedef struct NUGCUTPCSFX_s {
    int32_t id;
    float frame;  /* NAN: with the cutscene's start */
    float pos[3]; /* NAN: not placed */
    bool per_language;
} NUGCUTPCSFX_s;

typedef struct NUGCUTPCSETTINGS_s {
    float frame, farclip, nearclip, fognear, fogfar; /* NAN: not given */
    int16_t fog[4], top_back[3], bottom_back[3];     /* 0-255, NUGCUTPC_NOT_GIVEN: not given */
} NUGCUTPCSETTINGS_s;

typedef struct NUGCUTPCLIGHT_s {
    int32_t index;
    float dir[3], colour[3]; /* NAN: not given */
} NUGCUTPCLIGHT_s;

typedef struct NUGCUTPCTEXANIM_s {
    int32_t bit;
    bool on;
} NUGCUTPCTEXANIM_s;

typedef struct NUGCUTPCSUBTITLE_s {
    int32_t text_id; /* NUGCUTPC_NOT_GIVEN with a text */
    const char *text;
    float start_time, end_time, scale, fade_time; /* NAN: not given */
    float colour[4], pos[2];                      /* NAN: not given */
    int32_t align;                                /* an NuGCutPCAlign, NUGCUTPC_NOT_GIVEN: not given */
} NUGCUTPCSUBTITLE_s;

typedef struct NUGCUTPCEVENT_s {
    const char *name;
    float frame;
} NUGCUTPCEVENT_s;

typedef struct NUGCUTPCCONFIG_s {
    /* Time */
    float fpsec;             /* 30 when not given */
    bool looping;
    float resume_before_end; /* seconds, NAN: not given */
    bool skip_given, skip_on;
    float skip_to, skip_hint; /* NAN: not given */
    float fade_in;            /* NAN: not given */
    bool snap_out, wipe_out;
    /* Scenes and models */
    const NUGCUTPCSCENEFILE_s *scenes;
    uint32_t scene_count;
    const NUGCUTPCBESIDE_s *beside;
    uint32_t beside_count;
    const NUGCUTPCMODEL_s *characters;
    uint32_t character_count;
    /* Sound */
    const NUGCUTPCSFX_s *sfx;
    uint32_t sfx_count;
    int32_t music; /* NUGCUTPC_NOT_GIVEN: none */
    bool holdaudio;
    /* Look */
    float farclip, nearclip;               /* NAN: the game's */
    int16_t top_back[3], bottom_back[3];   /* NUGCUTPC_NOT_GIVEN: the game's */
    const NUGCUTPCSETTINGS_s *settings;
    uint32_t settings_count;
    float ambient[3];                      /* NAN: the game's lights */
    const NUGCUTPCLIGHT_s *lights;
    uint32_t light_count;
    bool super_widescreen;
    /* Effects */
    const char *ptl;
    const char **ptl_merge;
    uint32_t ptl_merge_count;
    const char **ats;
    uint32_t ats_count;
    const NUGCUTPCTEXANIM_s *tex_anim;
    uint32_t tex_anim_count;
    int32_t render_group; /* NUGCUTPC_NOT_GIVEN: the game's */
    /* Text */
    const NUGCUTPCSUBTITLE_s *subtitles;
    uint32_t subtitle_count;
    /* Direction */
    const NUGCUTPCEVENT_s *events;
    uint32_t event_count;
    const char *next_cut_scene;
    NuGCutPCOnEnd on_end;

    struct NUGCUTPCARENA_s *arena; /* where all of the above lives */
} NUGCUTPCCONFIG_s;

/* A .TXT's text (size bytes, not kept); where names the file in an error ("STATION.TXT:12: ..."). */
NUGCUTPCCONFIG_s *NuGCutPCConfigure(const char *text, size_t size, const char *where, NUGCUTPCERROR_s *error);
void NuGCutPCConfigFree(NUGCUTPCCONFIG_s *config);

/* MOVIES.TXT: the first cut of each movie by its number. */
typedef struct NUGCUTPCMOVIE_s {
    int32_t number;
    const char *cut;
} NUGCUTPCMOVIE_s;

typedef struct NUGCUTPCMOVIES_s {
    const NUGCUTPCMOVIE_s *movies;
    uint32_t count;
    struct NUGCUTPCARENA_s *arena;
} NUGCUTPCMOVIES_s;

NUGCUTPCMOVIES_s *NuGCutPCMoviesRead(const char *text, size_t size, const char *where, NUGCUTPCERROR_s *error);
void NuGCutPCMoviesFree(NUGCUTPCMOVIES_s *movies);
const char *NuGCutPCMovieCut(const NUGCUTPCMOVIES_s *movies, int32_t number); /* NULL: not a PC movie */

#endif /* NUGCUTPC_CONFIG_H */
