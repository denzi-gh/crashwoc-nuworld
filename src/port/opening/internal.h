/* [prototype] The streamlined opening's state and the calls between its parts (src/port/opening/). Game thread.
   opening.c   the parameters, the state, and the game's hooks that drive the rest
   camera.c    the camera shown, the film earth's turn, the Universal text's ring, the title logo held
   world.c     the station's scene: its earth, clouds, planets, sky, passers-by; the cards at any aspect
   flyers.c    Oxide and the space levels' ships crossing the title's sky
   particles.c the particle frames of the cuts' PtlFrame locators
   title.c     the skip, the cross-dissolve, the title's SELECT, NEW GAME into the intro */
#ifndef NUWORLD_PORT_OPENING_INTERNAL_H
#define NUWORLD_PORT_OPENING_INTERNAL_H

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "nu_types.h"
#include "numath/numathtypes.h"
#include "numath/numtx.h"
#include "numath/nuquat.h"
#include "numath/nutrig.h"
#include "nu3d/nugobj.h"
#include "nu3d/nugscn.h"
#include "nu3d/numtl.h"
#include "nu3d/nurndr.h"
#include "nu3d/nuhgobj.h"
#include "nu3d/nuanim.h"
#include "nu3d/nutexanm.h"
#include "nu3d/nucamera.h"
#include "gamelib/gcutscn.h"
#include "gamelib/gcutscn.retail.h"
#include "nu3d/nugscn.retail.h"
#include "nu3d/nurndr.retail.h"
#include "game/cut.retail.h"
#include "game/main.retail.h"
#include "gamelib/debris.h"
#include "gamelib/debris.retail.h"
#include "gamelib/edptl.retail.h"
#include "game/gametypes.h"
#include "nups2/ps2ctl.h"
#include "gfx/packet.h"
#include "gfx/rndr.h"
#include "port/heap.h"
#include "port/opening.h"
#include "port/cutplay.h"
#include "port/nutexanm.h"
#include "port/wide.h"

#define NW_CUT_RATE 0.6f /* cut frames a game frame, as the game plays its cuts (LoadCutComponents) */

/* The game's, in no header. */
extern s32 cutmovie, cutworldix;
extern struct instNUGCUTSCENE_s *CutInst[];
extern struct NUGCUTSCENE_s *CutScene[];
extern s32 CutAudio[], cutdebgroups[];
extern struct nugscn_s *world_scene[];
extern union variptr_u superbuffer_ptr, superbuffer_end;
extern struct numtx_s cutscenecammtx;
extern struct nucamera_s *pNuCam;
extern struct rndrstream_s *rndrstream_ptl;
extern s32 next_cut_movie, fadeval, fade_rate, fadehack;
extern struct cursor_s Cursor;
extern struct nupad_s *Pad[];
int NuRndrGScnObj(struct nugobj_s *gobj, struct numtx_s *wm); /* nu3d/nurndr's */
int NuFileSize(char *name);
void AddVariableShotDebrisEffect(s32 type, struct nuvec_s *pos, s32 count, s16 rotz, s16 roty);
s32 LookupDebrisEffect(char *name);

/* Cut frames of the logos' cut (LOGOSR.CUT) unless noted; in the station's world.
   opening.txt sets them (params.def's names; a vector's values in a row). NUWORLD_OPENING can override a value
   while developing. */
typedef struct NwOpeningParams {
#define SCALAR(field) float field;
#define VECTOR(field, n) float field[n];
#include "port/opening/params.def"
#undef VECTOR
#undef SCALAR
} NwOpeningParams;

extern NwOpeningParams nw_opening;
void nw_opening_read(void);                 /* config.c: opening.txt */
extern const NwTexAnimIO nw_opening_io;     /* config.c: the opening's files, disc first, then built in */

extern char nw_station_cut[128];
extern bool nw_station_loop;
extern bool nw_new_game_dissolve;
extern bool nw_assets_only;


/* The opening's state; each LoadCutMovie resets most of it. */
typedef struct NwOpeningState {
    int on;                       /* -1 unread */
    float time;                   /* of this update */
    struct {
        float seconds, strike;                    /* the engine's signals: sky.seconds, bolt.first */
        const float *uis_in, *uis_out;            /* the ATS signals universal.in and universal.out */
        const float *earth_mix, *clouds;          /* earth.mix, clouds.form */
        const float *earth_start, *station_switch, *skip_open; /* events of earth.ats and logos.ats */
    } signals;
    int station;                  /* CutInst's index of station.cut, -1 none */
    struct nugscn_s *station_scene;
    bool switched;                /* the logos' scene gave way to the station's */
    float film_rot[3][4];         /* the film earth's map frame: world to it, rows (camera.c, each frame) */
    float film_a;                 /* the film earth's opacity over the tiles */
    float film_mix;               /* the halo's way from the film's (0) to the station earth's (1) */
    bool film_seen;               /* the logos' earth shows this frame */
    struct numtx_s uis_world;     /* the film UNIVERSAL's place in the world this frame */
    bool uis_seen;                /* and whether it shows */
    float uis_flat;               /* and how flat (1 in its hold; its shadow on the globe only then) */
    bool swapped;                 /* earth.start: the station's earth blending in, then standing for the other */
    uint32_t xf_id;               /* the cross-dissolve's, new at each switch */
    int xf_frame;                 /* game frames since the switch */
    struct numtx_s station_cam;
    bool have_station_cam;
    struct numtx_s real;          /* the camera shown, in the station's world */
    struct nugscn_s *logos_scene; /* the scene, while the logos play in it (NULL in the intro) */
    float saved_farclip;          /* the camera's, restored for the intro; 0 none saved */
    short sky[160];               /* SPACER.GSC's sky domes' instances (skyDome*) */
    int nsky;
    short cloud[21];              /* its cloud shells' (cloudIn*, then cloudOut*) */
    int ncloud, ncloud_in;
    int glow;                     /* its station earth's glow sprite's (stationGlow), -1 none */
    float jupiter[3];             /* where the title's Jupiter stands */
    int sky_frames;               /* the sky's clock, game frames */
    int skip_until;               /* the sky's clock the skip hint shows until */
    int passers_from;             /* the sky's clock when the station's world first showed */
    bool flyers_loaded;
    struct numtx_s cloud_mtx[21]; /* the cloud shells' instances as the scene placed them */
    struct nugscn_s *cloud_scene; /* whose they are */
} NwOpeningState;

extern NwOpeningState nw_op;

/* NEW GAME from the title on the station (title.c): no fade to black. Kept apart from nw_op. */
typedef enum { NW_GO_NONE, NW_GO_LEAVING, NW_GO_HOLDING, NW_GO_REVEALING } NwGoPhase;
typedef struct NwGo {
    NwGoPhase phase;
    int frame;
    uint32_t id;
} NwGo;
extern NwGo nw_go;

/* The intro after the opening's NEW GAME: its station shot carries the title's sky on (world.c's intro frame). */
typedef struct NwIntro {
    bool on;
    struct {
        char name[16];
        struct numtx_s mtx;
    } pose[4];       /* the title's station parts as it left them */
    int nposes;
    int frames;      /* game frames since the intro's first */
} NwIntro;
extern NwIntro nw_intro;

/* opening.txt's lists (config.c): the title's ships (anim: they play the flyers' animation), their ways (the title
   camera's view, 640 x 512: from x y depth, to x y depth) and its sky's passers-by (1 comet, 2 shooting star, 3 saucer;
   four at most, as the renderer's earth pass draws). */
typedef struct { int kind; float x0, y0, x1, y1, depth, size, secs, period, offset; } NwPasser;
typedef struct {
    struct { char path[128]; bool anim; } model[8];
    char anim[128];
    struct { float x0, y0, d0, x1, y1, d1; } way[8];
    NwPasser passer[4];
    int models, ways, passers;
} NwOpeningLists;
extern NwOpeningLists nw_lists;

/* What a cut's object is to the opening, by its name. */
typedef enum { NW_OP_OTHER, NW_OP_EARTH, NW_OP_UIS, NW_OP_ISLAND, NW_OP_LOGO } NwOpeningPart;

/* opening.c */
bool nw_opening_on(void);
bool nw_opening_active(void);
bool nw_opening_station_part(const char *name);
const struct numtx_s *nw_intro_pose(const char *name);
NwOpeningPart nw_opening_part(const char *name);
bool nw_opening_direction_load(void);       /* opening.c: the signals, the clips, the curve effects' clock */
void nw_opening_direction_restart(void);
void nw_opening_letters_follow(void);       /* the letters' clip takes the subtitle effect's start */
void nw_opening_live_check(void);           /* config.c: NUWORLD_OPENING_LIVE's files, reloaded when saved */
void nw_opening_sequence_sync(void);
bool nw_opening_sequence_camera(struct numtx_s *out);
/* Row-vector matrices, as the game's. */
float nw_smooth(float t, float t0, float t1);
void nw_mul(struct numtx_s *d, const struct numtx_s *a, const struct numtx_s *b); /* a then b */
void nw_identity(struct numtx_s *m);
void nw_translation(struct numtx_s *m, float x, float y, float z);
void nw_rotation_y(struct numtx_s *m, float degrees);
void nw_inverse_rigid(struct numtx_s *d, const struct numtx_s *m);
void nw_affine_inverse(struct numtx_s *out, const struct numtx_s *m);
float nw_len3(const float *v);
void nw_rot_only(struct numtx_s *d, const struct numtx_s *m);

/* camera.c */
void nw_opening_real(const struct numtx_s *cutcam);
bool nw_opening_logo_shown(void);

/* world.c */
void nw_opening_load_station(void);
void nw_opening_load_film_map(void);
void nw_opening_find_jupiter(struct nugscn_s *sc);
void nw_opening_view_point(const struct numtx_s *cam, float x, float y, float depth, float out[3]);
bool nw_opening_clouds_on(void);
void nw_opening_passers(NwClouds *k);
float nw_opening_wide(void);
void nw_opening_sky(void);
void nw_opening_intro_frame(void);

/* flyers.c */
void nw_opening_load_flyers(void);
void nw_opening_draw_flyers(void);

/* particles.c */
void nw_opening_hold_particles(void);

/* The title logo: its model (camera.c), its looks (looks.c) */
extern struct NUHGOBJ_s *nw_title_hobj; /* the logo's model (the logos cut's "title" character) */
void nw_opening_logo_frame(void);           /* looks.c: the logo model's materials, animated */
void nw_opening_looks_off(void);
const float *nw_opening_scene_look(const char *object, int slot); /* looks.c: an object's in the logos scene */
bool nw_opening_looks_load(void);           /* looks.c: the .looks files beside the logos' models */
const char *nw_opening_model(const char *path); /* config.c: a model's path as opening.txt replaces it */

/* title.c */
void nw_opening_swap(void);
void nw_opening_switch(void);
void nw_opening_dissolve(void);
void nw_opening_skip(struct instNUGCUTSCENE_s *scene);
void nw_opening_background(void);
void nw_opening_go_start(void);
void nw_opening_go(void);
void nw_opening_reveal(void);

#endif
