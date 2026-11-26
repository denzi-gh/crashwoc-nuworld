/* [prototype] The opening's world (src/port/opening/internal.h): station.cut loaded beside the logos (SPACER.GSC holds
   both, the title sky's planets placed in it), the station's earth, its clouds, the renderer's live space round the camera
   shown, the passers-by, and the intro's station shot after NEW GAME carrying that sky on. The cards' wide sides
   are SPACER.GSC's own (tools: scenery_data.py). */
#include "port/opening/internal.h"

NwIntro nw_intro;

/* SPACER.GSC's instances whose names (specials; lower case in the game) begin with prefix, in the file's order
   (tools: scenery_data.py). */
static int nw_space_named(const struct nugscn_s *sc, const char *prefix, short *out, int max)
{
    int n = 0;
    for (int k = 0; sc && k < sc->numspecial && n < max; k++) {
        const struct nuspecial_s *sp = &sc->specials[k];
        if (sp->name && sp->instance && !SDL_strncasecmp(sp->name, prefix, SDL_strlen(prefix)))
            out[n++] = (short)(sp->instance - sc->instances);
    }
    return n;
}

/* The title's station beside the logos' cuts, as LoadCutComponents loads one: STATIONT.CUT (station.cut's camera held
   at its first frame, its rings turning in a loop: tools: station_cuts.py), played from the start, looping. Its scene
   is the logos' own (SPACER.GSC holds both). */
void nw_opening_load_station(void)
{
    int ix = 0;
    while (ix < 31 && CutInst[ix]) {
        ix++;
    }
    struct nugscn_s *scn = world_scene[0];
    if (!scn) {
        return;
    }
    struct NUGCUTSCENE_s *def = NuGCutSceneLoad(nw_station_cut, &superbuffer_ptr, &superbuffer_end);
    if (!def) {
        return;
    }
    NuGCutSceneFixUp(def, scn, 0);
    struct instNUGCUTSCENE_s *inst = instNuGCutSceneCreate(def, 0, 0, "ro2\\station.cut", &superbuffer_ptr);
    if (!inst) {
        return;
    }
    CutScene[ix] = def;
    CutInst[ix] = inst;
    CutAudio[ix] = -1;
    cutdebgroups[ix] = 173;
    instNuGCutSceneStart(inst);
    inst->speed = NW_CUT_RATE;
    if (nw_station_loop) inst->flags |= NUGCUTSCENE_LOOP;
    nw_op.station = ix;
    nw_op.station_scene = scn;
    /* One world from the first frame: the logos' cuts and the station's play in one scene (the logos' instances are
       hidden in the file: their cuts show the ones they place). */
    nw_op.logos_scene = scn;
    nw_opening_find_jupiter(scn);
    /* the instances the opening moves: the sky domes' panels, the cloud shells (the inner layer first), the glow */
    nw_op.nsky = nw_space_named(scn, "skyDome", nw_op.sky, (int)SDL_arraysize(nw_op.sky));
    nw_op.ncloud_in = nw_space_named(scn, "cloudIn", nw_op.cloud, 21);
    nw_op.ncloud = nw_op.ncloud_in + nw_space_named(scn, "cloudOut", nw_op.cloud + nw_op.ncloud_in, 21 - nw_op.ncloud_in);
    short glow;
    nw_op.glow = nw_space_named(scn, "stationGlow", &glow, 1) ? glow : -1;
}

#define NW_FOCAL_X 610.0f /* the title camera's pixels per unit at unit depth, measured on the logos' earth */
#define NW_FOCAL_Y 650.0f

/* A point of the shown camera's view at a depth, in the world. */
void nw_opening_view_point(const struct numtx_s *cam, float x, float y, float depth, float out[3])
{
    float v[3] = { (x - 320) / NW_FOCAL_X * depth, -(y - 256) / NW_FOCAL_Y * depth, depth };
    out[0] = v[0] * cam->_00 + v[1] * cam->_10 + v[2] * cam->_20 + cam->_30;
    out[1] = v[0] * cam->_01 + v[1] * cam->_11 + v[2] * cam->_21 + cam->_31;
    out[2] = v[0] * cam->_02 + v[1] * cam->_12 + v[2] * cam->_22 + cam->_32;
}

/* Where the title's Jupiter stands (SPACER.GSC's instance 0, placed there for the title's camera): its sphere's centre,
round which the renderer's streaked dome lies. */
void nw_opening_find_jupiter(struct nugscn_s *sc)
{
    short ix;
    struct nuinstance_s *in = nw_space_named(sc, "jupiter", &ix, 1) ? &sc->instances[ix] : NULL;
    if (!in || in->objid < 0 || in->objid >= sc->numgobj || !sc->gobjs[in->objid]) {
        return;
    }
    const struct nugobj_s *g = sc->gobjs[in->objid];
    float c[3] = { (g->min.x + g->max.x) * 0.5f, (g->min.y + g->max.y) * 0.5f, (g->min.z + g->max.z) * 0.5f };
    const float *m = &in->mtx._00;
    for (int i = 0; i < 3; i++) {
        nw_op.jupiter[i] = c[0] * m[i] + c[1] * m[4 + i] + c[2] * m[8 + i] + m[12 + i];
    }
}

/* The renderer's clouds stand for the station earth's cloud shells once that earth shows: after the Universal globe
   gave way to it, in the title, in the intro's station shot. */
bool nw_opening_clouds_on(void)
{
    return (nw_opening.clouds != 0 || nw_opening.passers != 0) &&
           ((cutmovie == 0 && (nw_op.swapped || nw_op.switched)) || (nw_intro.on && cutmovie == 1 && cutworldix == 0));
}

/* The sky's passers-by (opening.txt's passer lines), each on its own round: where the title's camera (station.cut's
   first) shows its way across (x, y of 640 x 512, from and to), how far, how big, how long it takes and how often it
   comes (seconds). */

void nw_opening_passers(NwClouds *k)
{
    SDL_memset(k->fx, 0, sizeof(k->fx));
    if (nw_opening.passers == 0 || !nw_op.have_station_cam) {
        return;
    }
    float t = (float)(nw_op.sky_frames - nw_op.passers_from) / 60.0f; /* from the station's arrival */
    for (int i = 0; i < nw_lists.passers; i++) {
        const NwPasser *p = &nw_lists.passer[i];
        float u = SDL_fmodf(t - p->offset + p->period * 100, p->period) / p->secs;
        if (u < 0 || u > 1) {
            continue;
        }
        float a[3], b[3];
        nw_opening_view_point(&nw_op.station_cam, p->x0, p->y0, p->depth, a);
        nw_opening_view_point(&nw_op.station_cam, p->x1, p->y1, p->depth, b);
        float v[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
        float l = SDL_sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        float *f = k->fx[i];
        /* in and out softly: the streak grows in, then thins away along its track */
        float fade = nw_smooth(u, 0.0f, 0.15f) * (1.0f - nw_smooth(u, 0.55f, 1.0f));
        f[0] = (float)p->kind + 0.249f * fade;
        f[1] = p->size;
        for (int c = 0; c < 3; c++) {
            f[2 + c] = a[c] + v[c] * u;
            f[5 + c] = l > 0 ? v[c] / l : 0;
        }
    }
}

/* The picture's width over retail's 4:3 (1 at 4:3), as the session's aspect has it this frame. */
float nw_opening_wide(void)
{
    return (float)nw_wide_width() / 640.0f;
}

/* The renderer's sky from the camera shown (its rows: right, up, forward), made at the camera's update and given to the
   frame when the scene is drawn (NuGScnRndr3): a frame that does not draw it has no sky. */
static NwSky nw_sky_shown;

static void nw_opening_sky_packet(const struct numtx_s *cam)
{
    NwSky k = { 0 };
    k.on = nw_opening.sky_live > 0;
    float bl = 0;
    for (int i = 0; i < 3; i++) {
        k.band[i] = nw_op.jupiter[i] - (&cam->_30)[i]; /* the streaked dome lay round the title's Jupiter */
        bl += k.band[i] * k.band[i];
    }
    for (int i = 0; i < 3; i++) {
        k.band[i] = bl > 0 ? k.band[i] / SDL_sqrtf(bl) : (i == 0 ? 1.0f : 0.0f);
    }
    if (k.on) {
        const float *m = &cam->_00;
        for (int i = 0; i < 3; i++) {
            k.right[i] = m[i];
            k.up[i] = m[4 + i];
            k.fwd[i] = m[8 + i];
        }
        k.time = (float)nw_op.sky_frames / 30.0f;
    }
    nw_sky_shown = k;
}

/* The cloud shells turned round the earth's centre about world y by the sky's clock. */
static void nw_opening_clouds(void)
{
    struct nugscn_s *sc = nw_op.station_scene;
    if (!sc || nw_op.ncloud == 0) {
        return;
    }
    if (nw_op.cloud_scene != sc) {
        for (int i = 0; i < nw_op.ncloud; i++) {
            nw_op.cloud_mtx[i] = sc->instances[nw_op.cloud[i]].mtx;
        }
        nw_op.cloud_scene = sc;
    }
    const float *c = nw_opening.earth_c;
    for (int i = 0; i < nw_op.ncloud; i++) {
        float deg = (float)nw_op.sky_frames * (i < nw_op.ncloud_in ? nw_opening.cloud_in : nw_opening.cloud_out);
        struct numtx_s to, r, back, m = nw_op.cloud_mtx[i];
        nw_translation(&to, -c[0], -c[1], -c[2]);
        nw_rotation_y(&r, deg);
        nw_translation(&back, c[0], c[1], c[2]);
        nw_mul(&m, &m, &to);
        nw_mul(&m, &m, &r);
        nw_mul(&m, &m, &back);
        sc->instances[nw_op.cloud[i]].mtx = m;
        if (nw_opening.clouds != 0) {
            sc->instances[nw_op.cloud[i]].flagword &= ~0x1; /* the renderer draws the clouds */
        }
    }
}

/* space.gsc's sky domes scaled round the camera shown from station.cut's camera's place: from anywhere they look
   as they do from there. */
void nw_opening_sky(void)
{
    struct nugscn_s *sc = nw_op.station_scene;
    if (!sc || !nw_op.have_station_cam) {
        return;
    }
    struct numtx_s m, scale, t;
    nw_translation(&m, -nw_op.station_cam._30, -nw_op.station_cam._31, -nw_op.station_cam._32);
    nw_identity(&scale);
    scale._00 = scale._11 = scale._22 = nw_opening.sky_scale;
    nw_mul(&m, &m, &scale);
    nw_translation(&t, nw_op.real._30, nw_op.real._31, nw_op.real._32);
    nw_mul(&m, &m, &t);
    for (int i = 0; i < nw_op.nsky; i++) {
        sc->instances[nw_op.sky[i]].mtx = m;
        if (nw_opening.sky_live > 0) { /* hidden for the renderer's sky */
            sc->instances[nw_op.sky[i]].flagword &= ~0x1;
        } else {
            sc->instances[nw_op.sky[i]].flagword |= 0x1;
        }
    }
    nw_opening_sky_packet(&nw_op.real);
    nw_opening_clouds();
}

/* The opening's images (RGBA), read once from the disc; none without them (the globe falls back to the station
   earth's tiles, the halo stays). */
int NuFileLoadBuffer(char *filename, void *mem, int buffsize);
typedef struct NwImage {
    uint8_t *px;
    int w, h;
} NwImage;
static NwImage nw_globe_map, nw_globe_light, nw_text_mask;

/* A PNG of the disc's into img, RGBA; nothing without it. */
static void nw_opening_load_png(char *name, NwImage *img)
{
    int size = NuFileSize(name);
    void *file = size > 0 ? SDL_malloc((size_t)size) : NULL;
    if (!file || NuFileLoadBuffer(name, file, size) != size) {
        SDL_free(file);
        return;
    }
    SDL_Surface *png = SDL_LoadPNG_IO(SDL_IOFromConstMem(file, (size_t)size), true);
    SDL_Surface *rgba = png ? SDL_ConvertSurface(png, SDL_PIXELFORMAT_ABGR8888) : NULL; /* bytes R, G, B, A */
    if (rgba && (img->px = SDL_malloc((size_t)rgba->w * (size_t)rgba->h * 4)) != NULL) {
        for (int y = 0; y < rgba->h; y++) {
            SDL_memcpy(img->px + (size_t)y * (size_t)rgba->w * 4, (uint8_t *)rgba->pixels + (size_t)y * (size_t)rgba->pitch,
                       (size_t)rgba->w * 4);
        }
        img->w = rgba->w;
        img->h = rgba->h;
    }
    SDL_DestroySurface(rgba);
    SDL_DestroySurface(png);
    SDL_free(file);
}

/* The Universal globe's map (the 1997 remake's, its land in alpha, plain equirectangular: levels\b\logos\earthfm.png)
   and light (the Corporate logo's, baked as matcaps: globeco.png, tools: corp_bake2.py, matcap_atlas.py), the film
   UNIVERSAL's shadow mask (uishadow.png, tools: uis_shadow_mask.py), read once. */
void nw_opening_load_film_map(void)
{
    static bool tried;
    if (tried) {
        return;
    }
    tried = true;
    char map[] = "levels\\b\\logos\\earthfm.png", light[] = "levels\\b\\logos\\globeco.png";
    char mask[] = "levels\\b\\logos\\uishadow.png";
    nw_opening_load_png(map, &nw_globe_map);
    nw_opening_load_png(light, &nw_globe_light);
    nw_opening_load_png(mask, &nw_text_mask);
}

/* The earth pass's Universal globe and halo: its opacity and map frame (camera.c), its images, the camera's frame
   (its light is the camera's), the halo's strength and style, the film UNIVERSAL's shadow on it. */
static void nw_opening_film(NwClouds *k, bool earth)
{
    bool film = cutmovie == 0 && !nw_op.switched && nw_globe_map.px && nw_globe_light.px && nw_op.film_a > 0;
    k->film_a = film ? nw_op.film_a : 0;
    k->film_r = nw_opening.film_r;
    /* the station tiles' map is a plain equirectangular one turned (fitted to their vertices within a degree or two,
       tools: tile_frame_fit.py): the globe's map is read through that turn (rows: its direction = the tiles' . q) */
    static const float q[3][3] = { { -0.307381f, -0.833783f, 0.45861f },
                                   { -0.855782f, 0.452961f, 0.249928f },
                                   { -0.416118f, -0.315647f, -0.852768f } };
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            k->rot[i][j] = q[0][i] * nw_op.film_rot[0][j] + q[1][i] * nw_op.film_rot[1][j] + q[2][i] * nw_op.film_rot[2][j];
        }
        k->rot[i][3] = 0;
    }
    const struct numtx_s *cam = &nw_op.real;
    k->halo = earth ? nw_opening.halo : 0;
    k->halo_style = nw_op.switched || nw_intro.on ? 1.0f : nw_op.film_mix;
    k->map = nw_globe_map.px;
    k->map_w = nw_globe_map.w;
    k->map_h = nw_globe_map.h;
    k->look[0] = nw_opening.globe_bevel;
    k->look[1] = nw_opening.globe_coast;
    k->look[2] = 0.6875f; /* the map's u at the tiles' texture's u 0 (theirs Pacific-centred) */
    k->look[3] = 0;
    for (int i = 0; i < 3; i++) {
        float sg = i == 2 ? -1.0f : 1.0f; /* the camera's rows: right, up, forward; back is toward the viewer */
        for (int j = 0; j < 3; j++) {
            k->cam[i][j] = sg * (&cam->_00)[4 * i + j];
        }
        k->cam[i][3] = 0;
    }
    k->cap = nw_globe_light.px;
    k->cap_w = nw_globe_light.w;
    k->cap_h = nw_globe_light.h;
    struct numtx_s to_text;
    nw_affine_inverse(&to_text, &nw_op.uis_world);
    SDL_memcpy(k->text, &to_text._00, sizeof(k->text));
    SDL_memcpy(k->text_arc, nw_opening.text_arc, sizeof(k->text_arc));
    k->text_y[0] = nw_opening.text_y[0];
    k->text_y[1] = nw_opening.text_y[1];
    k->text_y[2] = film && nw_op.uis_seen ? nw_opening.text_shadow * nw_op.uis_flat : 0.0f;
    /* the film's shadow lies just behind the letters, a little left and hardly down (they stand 0.6 globe radii off
       its surface, so a light much above would drop it under them): its light nearer the camera's way */
    for (int i = 0; i < 3; i++) {
        k->text_key[i] = 0.12f * (&cam->_00)[i] + 0.06f * (&cam->_10)[i] - 1.0f * (&cam->_20)[i];
    }
    k->mask = nw_text_mask.px;
    k->mask_w = nw_text_mask.w;
    k->mask_h = nw_text_mask.h;
}

/* The scene (the station's and the logos'), Mars, the cards' sides, the flyers, the renderer's earth pass. */
void NuGScnRndr3(struct nugscn_s *scene)
{
    if (nw_opening_active() && scene == nw_op.station_scene) {
        nw_opening_go_start();
        nw_opening_logo_frame();
    }
    NuGScnRndr3__retail(scene);
    if (scene == nw_op.station_scene &&
        (nw_opening_active() || (nw_intro.on && cutmovie == 1 && cutworldix == 0))) {
        nw_packet_sky(&nw_sky_shown); /* the sky only with the scene: none on a frame that does not draw it */
    }
    if (scene == nw_op.station_scene && (cutmovie == 0 || (nw_intro.on && cutworldix == 0)) && nw_op.flyers_loaded) {
        nw_opening_draw_flyers();
    }
    bool earth = (cutmovie == 0 && (nw_op.film_seen || nw_op.swapped || nw_op.switched)) ||
                 (nw_intro.on && cutmovie == 1 && cutworldix == 0);
    if (scene == nw_op.station_scene && (nw_opening_clouds_on() || earth)) {
        /* the renderer's earth pass, drawn after the frame's opaque geometry (the particles' stream), in this view:
           the film earth over the station earth's tiles while the logos play, the halo, the clouds */
        NwClouds *k = &nw_packet_building()->clouds;
        k->on = true;
        nw_opening_film(k, earth);
        SDL_memcpy(k->centre, nw_opening.earth_c, sizeof(k->centre));
        k->r_earth = nw_opening.earth_r;
        k->r_in = nw_opening.cloud_r[0];
        k->r_out = nw_opening.cloud_r[1];
        SDL_memcpy(k->sun, nw_opening.sun, sizeof(k->sun));
        k->time = (float)nw_op.sky_frames / 30.0f;
        k->opacity = nw_opening.clouds == 0 || !nw_opening_clouds_on() ? 0.0f
                     : nw_intro.on ? 1.0f
                                   : *nw_op.signals.clouds; /* clouds.ats */
        nw_opening_passers(k);
        nw_rndr_push(rndrstream_ptl, nw_rndr_record(NW_CMD_CLOUDS, NULL, NULL, 0));
    }
}

/* Once a frame of the intro's station shot after the opening: the title's sky round the camera, the far clip. */
void nw_opening_intro_frame(void)
{
    struct nugscn_s *sc = nw_op.station_scene;
    if (cutworldix != 0) {
        if (sc && nw_op.glow >= 0 && nw_op.glow < sc->numinstance) {
            sc->instances[nw_op.glow].flagword |= 0x1; /* the glow sprite for the intro's own shots */
        }
        static const NwSky off = { 0 };
        nw_packet_sky(&off);
        nw_intro.on = false;
        if (nw_op.saved_farclip > 0 && pNuCam) {
            pNuCam->farclip = nw_op.saved_farclip;
            nw_op.saved_farclip = 0;
        }
        return;
    }
    nw_op.real = cutscenecammtx;
    nw_op.sky_frames++;
    nw_intro.frames++;
    if (sc && nw_op.glow >= 0 && nw_op.glow < sc->numinstance) {
        sc->instances[nw_op.glow].flagword &= ~0x1; /* the renderer's halo stands for the glow sprite */
    }
    nw_opening_sky();
    if (pNuCam && pNuCam->farclip < nw_opening.farclip) {
        nw_op.saved_farclip = pNuCam->farclip;
        pNuCam->farclip = nw_opening.farclip;
    }
}
