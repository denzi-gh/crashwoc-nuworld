/* Frontend loader hooks, the opening's direction and camera update. opening.txt (config.c) selects authored playback
   or the enhanced choreography that runs from the logos through the station title into New Game. */
#include "port/opening/internal.h"
#include "port/cutplay.h"
#include "port/nutexanm.h"
#include "port/signals.h"

NwOpeningState nw_op = { .on = -1, .station = -1, .glow = -1 };

bool nw_opening_enabled(void)
{
    if (nw_op.on < 0) {
        nw_opening_read();
    }
    return nw_op.on;
}

/* ---- Direction, on the sky's clock (sky_frames at 60 a second) ----
   The curve effects (ats.txt) are TexAnim's scene effects, on the sky's clock the direction gives it. */

/* THE WRATH OF CORTEX's letters grow in with the subtitle (nw_opening_letters_follow); the station's own camera once
   the title switches. */
static const char *const letter_joints[] = {"The_*", "Wrath_*", "Of_*", "Cortex_*", "Dot_*"};
static NwMotion panel_letters = {
    .id = "panel-letters", .source = "logos", .character = "title", .joints = letter_joints, .joint_count = 5,
    .first = 918, .last = 951, .rate = 36, .enabled = &nw_opening.meter_wait, .before_source = "logos", .before = 918,
    .camera = -1, .skeletal = true, .preserve_root = true,
};
static const NwMotion station_view = {.id = "station-view", .source = "station", .first = 1, .last = 1, .camera = -1};
static NwCutPlayback playback;
static bool camera_played;

static struct instNUGCUTSCENE_s *motion_scene(void *context, const char *id)
{
    (void)context;
    if (!SDL_strcmp(id, "logos")) return CutInst[0];
    if (!SDL_strcmp(id, "station")) return nw_op.station >= 0 ? CutInst[nw_op.station] : NULL;
    return NULL;
}
static struct instNUGCUTSCENE_s *motion_current(void *context) { (void)context; return CutInst[cutworldix]; }
static bool motion_active(void *context) { (void)context; return nw_opening_active(); }
static void motion_prepare(void *context) { (void)context; nw_opening_sequence_sync(); }

/* The letters start when the subtitle effect does: its event, its delay (none while it is not loaded). */
void nw_opening_letters_follow(void)
{
    NwCurveSet set;
    struct nutexanimprog_s *subtitle = NuTexAnimProgFind("subtitle");
    bool follows = subtitle && nw_texanim_curves(subtitle, &set) && set.start;
    panel_letters.origin = follows ? nw_signal_find(set.start) : NULL;
    panel_letters.delay = follows ? set.after : NULL;
}

/* Starts the direction. The engine's signals go in the registry by name (bolt.first is vortex.ats's event, kept where
   the letters' clip reads it); UNIVERSAL's ramps come out of it. TexAnim plays the curve effects
   on the sky's clock. */
bool nw_opening_direction_load(void)
{
    if (!nw_signal_bind("sky.seconds", &nw_op.signals.seconds) || !nw_signal_bind("bolt.first", &nw_op.signals.strike) ||
        !(nw_op.signals.uis_in = nw_signal("universal.in")) ||
        !(nw_op.signals.uis_out = nw_signal("universal.out")) || !(nw_op.signals.earth_mix = nw_signal("earth.mix")) ||
        !(nw_op.signals.clouds = nw_signal("clouds.form")) || !(nw_op.signals.earth_start = nw_signal("earth.start")) ||
        !(nw_op.signals.station_switch = nw_signal("station.switch")) ||
        !(nw_op.signals.skip_open = nw_signal("skip.open")))
        return false;
    nw_cutplay_init(&playback, (NwCutPlaybackHost){.scene = motion_scene, .current = motion_current,
        .active = motion_active, .prepare = motion_prepare, .multiply = nw_mul, .inverse = nw_affine_inverse});
    nw_cutplay_install(&playback);
    nw_texanim_scene_clock(&nw_op.sky_frames, 60);
    nw_opening_direction_restart();
    return true;
}

/* A new cut: its events have not happened yet (the vortex forms again), the letters' clip waits for its bolt, the
   station camera for its switch. */
void nw_opening_direction_restart(void)
{
    nw_signal_events_reset();
    nw_opening_letters_follow();
    camera_played = false;
    nw_cutplay_clear(&playback);
    nw_cutplay_play(&playback, &panel_letters, 0);
}

bool nw_opening_sequence_camera(struct numtx_s *out) { return nw_cutplay_camera(&playback, out); }

/* The sky's clock, then the clips that follow it. */
void nw_opening_sequence_sync(void)
{
    nw_op.signals.seconds = (float)nw_op.sky_frames / 60.0f;
    if (nw_op.switched && !camera_played) {
        camera_played = nw_cutplay_play(&playback, &station_view, nw_op.signals.seconds);
    }
    if (!nw_cutplay_update(&playback, nw_op.signals.seconds)) {
        SDL_Log("opening: %s", playback.error);
        nw_op.on = false;
    }
}

bool nw_opening_on(void)
{
    return nw_opening_enabled() && !nw_assets_only;
}

bool nw_opening_active(void)
{
    return nw_opening_on() && cutmovie == 0 && (cutworldix == 0 || cutworldix == 1);
}

bool nw_opening_station_part(const char *name)
{
    return name && (SDL_strcmp(name, "station1") == 0 || SDL_strcmp(name, "lower_ring") == 0 ||
                    SDL_strcmp(name, "top_ring") == 0 || SDL_strcmp(name, "stationEarth") == 0);
}

struct nugscn_s *NuGScnRead(char **buffer, char *bufend, char *filename)
{
    return NuGScnRead__retail(buffer, bufend, nw_opening_data(filename));
}

int edppLoadEffects(char *filename, signed char list)
{
    return edppLoadEffects__retail(nw_opening_data(filename), list);
}

NwOpeningPart nw_opening_part(const char *name)
{
    if (!name) {
        return NW_OP_OTHER;
    }
    if (SDL_strcmp(name, "earth") == 0) {
        return NW_OP_EARTH;
    }
    if (SDL_strcmp(name, "UIS") == 0) {
        return NW_OP_UIS;
    }
    if (SDL_strcmp(name, "title") == 0) {
        return NW_OP_LOGO;
    }
    if (SDL_strcmp(name, "island") == 0) {
        return NW_OP_ISLAND;
    }
    return NW_OP_OTHER;
}

/* The title's station as it stands (its rings where its loop has them), kept for the intro to go on from. */
static void nw_opening_keep_station(void)
{
    struct NUGCUTRIGIDSYS_s *rd = nw_op.station >= 0 && CutScene[nw_op.station] ? CutScene[nw_op.station]->rigiddef : NULL;
    if (!rd) {
        return;
    }
    nw_intro.nposes = 0;
    for (int i = 0; i < (int)rd->count && nw_intro.nposes < (int)SDL_arraysize(nw_intro.pose); i++) {
        struct NUGCUTRIGID_s *r = &rd->rigids[i];
        if (nw_opening_station_part(r->name) && r->special.special && r->special.special->instance) {
            SDL_strlcpy(nw_intro.pose[nw_intro.nposes].name, r->name, sizeof(nw_intro.pose[0].name));
            nw_intro.pose[nw_intro.nposes++].mtx = r->special.special->instance->mtx;
        }
    }
}

const struct numtx_s *nw_intro_pose(const char *name)
{
    for (int i = 0; name && i < nw_intro.nposes; i++) {
        if (SDL_strcmp(nw_intro.pose[i].name, name) == 0) {
            return &nw_intro.pose[i].mtx;
        }
    }
    return NULL;
}

s32 LoadCutMovie(s32 movie)
{
    static bool roomy;
    if (!roomy && nw_opening_enabled()) {
        /* the opening's scenes are larger (finer textures, new models): the superbuffer's slack opened for them */
        superbuffer_end.charptr += nw_superbuffer_open_slack();
        roomy = true;
    }
    struct numtx_s title_cam = nw_op.station_cam;
    bool had_cam = nw_op.have_station_cam;
    nw_opening_keep_station();
    s32 rv = LoadCutMovie__retail(movie);
    nw_intro.on = false;
    nw_op.station = -1;
    nw_op.station_scene = NULL;
    nw_op.logos_scene = NULL;
    nw_op.swapped = false;
    nw_op.film_seen = false;
    nw_op.uis_seen = false;
    nw_op.film_a = 0;
    nw_op.film_mix = 0;
    nw_op.switched = false;
    nw_op.have_station_cam = false;
    nw_op.flyers_loaded = false;
    nw_title_hobj = NULL; /* a new cut's characters: the logo's model, its looks and direction start over */
    nw_opening_looks_off();
    nw_opening_direction_restart();
    if (movie == 0 && rv && nw_opening_on()) {
        nw_opening_load_station();
        nw_opening_load_film_map();
        nw_opening_load_flyers();
        nw_op.flyers_loaded = true;
    }
    if (movie == 1 && rv && nw_opening_on() && nw_go.phase != NW_GO_NONE && had_cam && world_scene[0]) {
        nw_op.station_scene = world_scene[0];
        nw_op.station_cam = title_cam;
        nw_op.have_station_cam = true;
        nw_opening_find_jupiter(world_scene[0]);
        nw_intro.on = true;
        nw_op.film_mix = 1; /* the station earth's halo */
        nw_intro.frames = -1;
        nw_op.logos_scene = NULL;
        nw_opening_load_flyers();
        nw_op.flyers_loaded = true;
    }
    return rv;
}

/* Once a game frame, each cut's camera: the opening's clock. The camera shown replaces the cut's (cutscenecammtx) and
   the rest follows it: the sky, the particles, the skip, the switch to the station, the title's ways out. */
void instNuGCutCamSysUpdate(struct instNUGCUTSCENE_s *scene, float time)
{
    instNuGCutCamSysUpdate__retail(scene, time);
    nw_texanim_scene_cut(scene); /* the scene effects on its time (universal.ats), before its objects are placed */
    if (nw_go.phase >= NW_GO_HOLDING && cutmovie == 1 && scene == CutInst[cutworldix]) {
        nw_opening_reveal();
    }
    if (nw_intro.on && cutmovie == 1 && scene == CutInst[cutworldix]) {
        nw_opening_intro_frame();
    }
    if (cutmovie != 0 && !nw_intro.on) {
        static const NwSky off = { 0 };
        nw_packet_sky(&off);
    }
    if (cutmovie != 0 && !nw_intro.on && nw_op.saved_farclip > 0 && pNuCam) {
        pNuCam->farclip = nw_op.saved_farclip;
        nw_op.saved_farclip = 0;
    }
    if (!nw_opening_active()) {
        nw_opening_hold_particles();
        return;
    }
    if (nw_op.station >= 0 && scene == CutInst[nw_op.station]) {
        nw_op.station_cam = cutscenecammtx;
        nw_op.have_station_cam = true;
        return;
    }
    if (scene != CutInst[cutworldix]) {
        return;
    }
    nw_op.time = time;
    if (cutworldix == 0) {
        nw_opening_skip(scene);
    }
    if (!nw_op.swapped && cutworldix == 0 && nw_op.station >= 0 && nw_op.have_station_cam &&
        nw_signal_happened(nw_op.signals.earth_start)) { /* earth.ats */
        nw_opening_swap();
    }
    if (!nw_op.switched && cutworldix == 0 && nw_op.station >= 0 && nw_signal_happened(nw_op.signals.station_switch)) {
        nw_opening_switch();
    }
    if (pNuCam && pNuCam->farclip < nw_opening.farclip) {
        nw_op.saved_farclip = pNuCam->farclip;
        pNuCam->farclip = nw_opening.farclip;
    }
    nw_op.sky_frames++;
    nw_opening_live_check();
    nw_opening_real(&cutscenecammtx);
    nw_opening_sky();
    if (nw_op.switched) {
        nw_opening_dissolve();
        nw_opening_go();
        nw_opening_background();
    }
    cutscenecammtx = nw_op.real;
    nw_opening_hold_particles();
}

/* Row-vector matrices, as the game's. */
float nw_smooth(float t, float t0, float t1)
{
    float u = t1 > t0 ? (t - t0) / (t1 - t0) : (t >= t1);
    u = u < 0 ? 0 : u > 1 ? 1 : u;
    return u * u * (3 - 2 * u);
}

/* a then b */
void nw_mul(struct numtx_s *d, const struct numtx_s *a, const struct numtx_s *b)
{
    const float *x = &a->_00, *y = &b->_00;
    float r[16];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            r[i * 4 + j] = x[i * 4] * y[j] + x[i * 4 + 1] * y[4 + j] + x[i * 4 + 2] * y[8 + j] + x[i * 4 + 3] * y[12 + j];
        }
    }
    SDL_memcpy(d, r, sizeof r);
}

void nw_identity(struct numtx_s *m)
{
    SDL_zerop(m);
    m->_00 = m->_11 = m->_22 = m->_33 = 1;
}

void nw_translation(struct numtx_s *m, float x, float y, float z)
{
    nw_identity(m);
    m->_30 = x;
    m->_31 = y;
    m->_32 = z;
}

void nw_rotation_y(struct numtx_s *m, float degrees)
{
    float a = degrees * (float)M_PI / 180, c = cosf(a), s = sinf(a);
    nw_identity(m);
    m->_00 = c;
    m->_02 = -s;
    m->_20 = s;
    m->_22 = c;
}

/* A camera's inverse: its rotation rows orthonormal. */
void nw_inverse_rigid(struct numtx_s *d, const struct numtx_s *m)
{
    struct numtx_s r;
    nw_identity(&r);
    r._00 = m->_00, r._01 = m->_10, r._02 = m->_20;
    r._10 = m->_01, r._11 = m->_11, r._12 = m->_21;
    r._20 = m->_02, r._21 = m->_12, r._22 = m->_22;
    r._30 = -(m->_30 * r._00 + m->_31 * r._10 + m->_32 * r._20);
    r._31 = -(m->_30 * r._01 + m->_31 * r._11 + m->_32 * r._21);
    r._32 = -(m->_30 * r._02 + m->_31 * r._12 + m->_32 * r._22);
    *d = r;
}

/* The inverse of an affine row-vector matrix. */
void nw_affine_inverse(struct numtx_s *out, const struct numtx_s *m)
{
    const float *a = &m->_00;
    float c00 = a[5] * a[10] - a[6] * a[9], c01 = a[2] * a[9] - a[1] * a[10], c02 = a[1] * a[6] - a[2] * a[5];
    float c10 = a[6] * a[8] - a[4] * a[10], c11 = a[0] * a[10] - a[2] * a[8], c12 = a[2] * a[4] - a[0] * a[6];
    float c20 = a[4] * a[9] - a[5] * a[8], c21 = a[1] * a[8] - a[0] * a[9], c22 = a[0] * a[5] - a[1] * a[4];
    float det = a[0] * c00 + a[1] * c10 + a[2] * c20;
    float k = fabsf(det) > 1e-12f ? 1.0f / det : 0.0f;
    float r[12] = { c00 * k, c01 * k, c02 * k, 0, c10 * k, c11 * k, c12 * k, 0, c20 * k, c21 * k, c22 * k, 0 };
    float *o = &out->_00;
    SDL_memcpy(o, r, sizeof(r));
    for (int j = 0; j < 3; j++) {
        o[12 + j] = -(a[12] * r[j] + a[13] * r[4 + j] + a[14] * r[8 + j]);
    }
    o[15] = 1.0f;
}

float nw_len3(const float *v)
{
    return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

void nw_rot_only(struct numtx_s *d, const struct numtx_s *m)
{
    *d = *m;
    d->_30 = d->_31 = d->_32 = 0;
}
