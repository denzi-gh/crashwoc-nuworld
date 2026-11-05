/* Wraps of game/main's MiniGame*: BGLoad still calls them and still moves the character by mg_player_pos, tilt and
   spin, and the level loop still pays mg_wumpatot into the wumpa count. */
#include "gameplay/minigame.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>

#include <SDL3/SDL.h>

#include "game/gametypes.h"
#include "game/main.retail.h"
#include "nu3d/nugscn.h"
#include "nu_port.h"
#include "numath/numtx.h"
#include "numath/nurand.h"
#include "nups2/ps2ctl.h"
#include "port/level.h"

/* Retail's own, in main.c. */
struct mg_wumpa_s {
    struct nuvec_s pos;
    s32 type;
};
struct vec2_s;

/* Declared by no header. */
extern struct game_s Game;
extern struct ldata_s LData[44];
extern struct nugscn_s *wumpa_scene;
extern volatile s32 bgload_complete;
extern struct nupad_s *Pad[2];
extern f32 NuTrigTable[];
extern struct nunrand_s mg_rand;
extern struct nuvec_s mg_player_pos, mg_player_actpos, mg_gopos, mg_fontpos, mg_fontscale, mg_scorewumpa;
extern struct nuhspecial_s mg_wumpa_shapes[3];
extern struct mg_wumpa_s mg_wumpaobj[32];
extern struct numtx_s mg_wumpamtx;
extern f32 mg_spd, mg_yspd, mg_accel, mg_friction, mg_spd_rng, mg_player_rng, mg_player_y_offset, mg_player_tilt_scale;
extern f32 mg_scoreoff, mg_scoregibber, mg_scoremaxgibber;
extern s32 mg_player_spin, mg_player_spin_rate, mg_player_tilt, mg_wumpa_ix, mg_wumpa_issue_delay;
extern s32 mg_wumpa_issue_extra_delay, mg_wumpatot;
extern s32 mg_scoregibbercntx, mg_scoregibberratex, mg_scoregibbercnty, mg_scoregibberratey;
s32 MGCheckRng(struct vec2_s *pos);
void GameSfx(s32 sfx, struct nuvec_s *pos);
void Text3D(char *txt, f32 x, f32 y, f32 z, f32 scalex, f32 scaley, f32 scalez, s32 align, s32 colour);

#define NW_MG_HUB 0x25
#define NW_MG_SECONDS 30
#define NW_MG_GO_FRAMES 50
#define NW_MG_FAR 8.0f  /* where the fruit leaves the tunnel; the character is at 0, the camera at -4 */
#define NW_MG_NEAR -3.0f
#define NW_MG_FLY 0.12f /* fruit speed per 50 Hz frame */
#define NW_MG_SFX_WUMPA 0x19
#define NW_MG_START 0x800
#define NW_MG_BEST_HUB 5 /* its padding holds the best: save/savecodec.h NW_SAVE_MINIGAME_BEST */

typedef enum NwMgState {
    NW_MG_OFF,
    NW_MG_WAIT, /* the level still loads */
    NW_MG_GO,
    NW_MG_PLAY,
    NW_MG_END, /* time is up, the last fruit flies */
    NW_MG_DONE,
} NwMgState;

static SDL_AtomicInt nw_mg_asked;

static struct {
    NwMgState state;
    bool loaded;  /* BGLoadLevel finished; bgload_complete is held at 0 until the game ends */
    float frames; /* in the state, 50 Hz frames */
    float issue;  /* frames to the next fruit */
    float spin;   /* of a catch's turn still to do */
    float turn;   /* the spin so far */
} nw_mg;

static bool nw_mg_level_ok(int level)
{
    return level >= 0 && level < (int)SDL_arraysize(LData) && (u16)LData[level].character <= 1 &&
           (LData[level].flags & 0x100) && nw_level_name(level);
}

void nw_minigame_start(void)
{
    int level = nw_level_current();
    if (!nw_mg_level_ok(level)) {
        level = NW_MG_HUB;
    }
    for (int i = 0; i < (int)SDL_arraysize(LData) && !nw_mg_level_ok(level); i++) {
        level = i;
    }
    if (nw_mg_level_ok(level)) {
        SDL_SetAtomicInt(&nw_mg_asked, 1);
        nw_level_load(level);
    }
}

static float nw_mg_tick(void)
{
    return NW_HZ(1.0f, 50.0f / 60.0f);
}

static float nw_mg_rand(void)
{
    return NuFloatRand(&mg_rand) * 2.0f - 1.0f;
}

int nw_minigame_best(void)
{
    const struct hub_s *h = &Game.hub[NW_MG_BEST_HUB];
    return (u8)h->pad1 | (u8)h->pad2 << 8;
}

/* Lets BGLoad's phase 2 go on to the level. */
static void nw_mg_release(void)
{
    if (nw_mg.state >= NW_MG_GO && mg_wumpatot > nw_minigame_best()) {
        int best = SDL_min(mg_wumpatot, 0xFFFF);
        Game.hub[NW_MG_BEST_HUB].pad1 = (char)(best & 0xFF);
        Game.hub[NW_MG_BEST_HUB].pad2 = (char)(best >> 8);
    }
    nw_mg.state = NW_MG_DONE;
    for (int i = 0; i < (int)SDL_arraysize(mg_wumpaobj); i++) {
        mg_wumpaobj[i].type = -1;
    }
    if (nw_mg.loaded) {
        bgload_complete = 1;
    }
}

void MiniGameReset(void)
{
    MiniGameReset__retail();
    nw_mg.state = SDL_CompareAndSwapAtomicInt(&nw_mg_asked, 1, 0) ? NW_MG_WAIT : NW_MG_OFF;
    if (nw_mg.state == NW_MG_OFF) {
        return;
    }
    struct nuvec_s scale = { 0.65f, 0.65f, 0.65f };
    NuMtxSetScale(&mg_wumpamtx, &scale);
    SDL_zeroa(mg_wumpa_shapes);
    for (int i = 0; i < (int)SDL_arraysize(mg_wumpaobj); i++) {
        mg_wumpaobj[i].type = -1;
    }
    NuRandSetSeed(&mg_rand, (int)SDL_GetTicks());
    mg_wumpa_ix = 0;
    mg_wumpatot = 0;
    mg_spd = mg_yspd = 0.0f;
    mg_scoregibber = 0.0f;
    nw_mg.loaded = false;
    nw_mg.frames = nw_mg.issue = nw_mg.spin = nw_mg.turn = 0.0f;
}

static void nw_mg_steer(float sx, float sy, float k)
{
    float friction = powf(mg_friction, k);
    mg_spd = SDL_clamp((mg_spd + sx * mg_accel * k) * friction, -mg_spd_rng, mg_spd_rng);
    mg_yspd = SDL_clamp((mg_yspd + sy * mg_accel * k) * friction, -mg_spd_rng, mg_spd_rng);
    mg_player_pos.x += mg_spd * k;
    mg_player_pos.y += mg_yspd * k;
    if (SDL_fabsf(mg_player_pos.x) > mg_player_rng) {
        mg_player_pos.x = SDL_copysignf(mg_player_rng, mg_player_pos.x);
        mg_spd = 0.0f;
    }
    if (SDL_fabsf(mg_player_pos.y) > mg_player_rng) {
        mg_player_pos.y = SDL_copysignf(mg_player_rng, mg_player_pos.y);
        mg_yspd = 0.0f;
    }
    mg_player_tilt = (s32)(mg_spd * mg_player_tilt_scale);
    if (nw_mg.spin > 0.0f) {
        float step = SDL_min(mg_player_spin_rate * k, nw_mg.spin);
        nw_mg.spin -= step;
        nw_mg.turn = nw_mg.spin > 0.0f ? fmodf(nw_mg.turn + step, 65536.0f) : 0.0f;
    }
    mg_player_spin = (s32)nw_mg.turn;
}

static void nw_mg_fruit(const struct nuvec_s *base, float k)
{
    if (nw_mg.state == NW_MG_PLAY) {
        nw_mg.issue -= k;
        if (nw_mg.issue <= 0.0f) {
            struct mg_wumpa_s *w = &mg_wumpaobj[mg_wumpa_ix];
            mg_wumpa_ix = (mg_wumpa_ix + 1) % (int)SDL_arraysize(mg_wumpaobj);
            w->pos.x = base->x + nw_mg_rand() * mg_player_rng;
            w->pos.y = base->y + nw_mg_rand() * mg_player_rng;
            w->pos.z = NW_MG_FAR;
            w->type = 0;
            nw_mg.issue += mg_wumpa_issue_delay + NuFloatRand(&mg_rand) * mg_wumpa_issue_extra_delay;
        }
    }
    bool flying = false;
    for (int i = 0; i < (int)SDL_arraysize(mg_wumpaobj); i++) {
        struct mg_wumpa_s *w = &mg_wumpaobj[i];
        if (w->type < 0) {
            continue;
        }
        float z = w->pos.z;
        w->pos.z -= NW_MG_FLY * k;
        if (z > mg_player_actpos.z && w->pos.z <= mg_player_actpos.z && MGCheckRng((struct vec2_s *)&w->pos)) {
            w->type = -1;
            mg_wumpatot++;
            GameSfx(NW_MG_SFX_WUMPA, NULL);
            mg_scoregibber = mg_scoremaxgibber;
            if (nw_mg.spin <= 65536.0f) {
                nw_mg.spin += 65536.0f; /* the turn still ends facing the camera */
            }
        } else if (w->pos.z < NW_MG_NEAR) {
            w->type = -1;
        } else {
            flying = true;
        }
    }
    if (nw_mg.state == NW_MG_END && !flying) {
        nw_mg_release();
    }
}

static void nw_mg_play(const struct nuvec_s *pos)
{
    float k = nw_mg_tick();
    if (nw_mg.state == NW_MG_WAIT) {
        if (!nw_mg.loaded) {
            return;
        }
        if (!wumpa_scene || !NuSpecialFind(wumpa_scene, &mg_wumpa_shapes[0], "wumpa")) {
            nw_mg_release();
            return;
        }
        nw_mg.state = NW_MG_GO;
        nw_mg.frames = 0.0f;
    }
    float sx = 0.0f, sy = 0.0f;
    struct nupad_s *pad = Pad[0];
    if (pad && NuPs2ReadPad(pad)) {
        sx = SDL_clamp(pad->l_nx, -1.0f, 1.0f);
        sy = SDL_clamp(pad->l_ny, -1.0f, 1.0f);
        sx = pad->paddata & 0x8000 ? -1.0f : pad->paddata & 0x2000 ? 1.0f : sx;
        sy = pad->paddata & 0x4000 ? -1.0f : pad->paddata & 0x1000 ? 1.0f : sy;
        if (pad->paddata_db & NW_MG_START) {
            nw_mg_release();
            return;
        }
    }
    nw_mg.frames += k;
    if (nw_mg.state == NW_MG_GO && nw_mg.frames >= NW_MG_GO_FRAMES) {
        nw_mg.state = NW_MG_PLAY;
        nw_mg.frames = 0.0f;
    } else if (nw_mg.state == NW_MG_PLAY && nw_mg.frames >= NW_MG_SECONDS * 50) {
        nw_mg.state = NW_MG_END;
    }
    nw_mg_steer(sx, sy, k);
    struct nuvec_s base = { pos->x, pos->y + mg_player_y_offset, pos->z };
    mg_player_actpos = (struct nuvec_s){ base.x + mg_player_pos.x, base.y + mg_player_pos.y, base.z };
    nw_mg_fruit(&base, k);
}

void MiniGame(struct nuvec_s *pos)
{
    MiniGame__retail(pos);
    if (nw_mg.state == NW_MG_OFF || nw_mg.state == NW_MG_DONE) {
        return;
    }
    /* An exchange, so a completion the loader stores meanwhile is not lost. */
    if (!nw_mg.loaded && __atomic_exchange_n(&bgload_complete, 0, __ATOMIC_SEQ_CST)) {
        nw_mg.loaded = true;
    }
    if (pos) {
        nw_mg_play(pos);
    }
}

static void nw_mg_draw(const struct nuvec_s *pos, int angle)
{
    struct numtx_s m = mg_wumpamtx;
    NuMtxRotateY(&m, angle & 0xFFFF);
    m._30 = pos->x;
    m._31 = pos->y;
    m._32 = pos->z;
    NuSpecialDrawAt(&mg_wumpa_shapes[0], &m);
}

void MiniGameRender(void)
{
    MiniGameRender__retail();
    if (nw_mg.state < NW_MG_GO || !mg_wumpa_shapes[0].special) {
        return;
    }
    int spin = (int)(SDL_GetTicks() * 30);
    for (int i = 0; i < (int)SDL_arraysize(mg_wumpaobj); i++) {
        if (mg_wumpaobj[i].type >= 0) {
            nw_mg_draw(&mg_wumpaobj[i].pos, spin + i * 0x1C00);
        }
    }
}

void MiniGameRenderScore(void)
{
    MiniGameRenderScore__retail();
    if (nw_mg.state < NW_MG_GO || !mg_wumpa_shapes[0].special) {
        return;
    }
    float k = nw_mg_tick();
    mg_scoreoff = SDL_max(mg_scoreoff - 0.05f * k, 0.0f);
    float gx = NuTrigTable[mg_scoregibbercntx & 0xFFFF] * mg_scoregibber;
    float gy = NuTrigTable[mg_scoregibbercnty & 0xFFFF] * mg_scoregibber;
    mg_scoregibbercntx += (s32)(mg_scoregibberratex * k);
    mg_scoregibbercnty += (s32)(mg_scoregibberratey * k);
    mg_scoregibber *= powf(0.9f, k);

    char buf[16];
    snprintf(buf, sizeof(buf), "%i", (int)mg_wumpatot);
    float y = mg_fontpos.y + mg_scoreoff;
    Text3D(buf, mg_fontpos.x + gx, y + gy, mg_fontpos.z, mg_fontscale.x, mg_fontscale.y, mg_fontscale.z, 2, 1);
    struct nuvec_s icon = { mg_scorewumpa.x, mg_scorewumpa.y + mg_scoreoff * 2.5f, mg_scorewumpa.z };
    nw_mg_draw(&icon, (int)(SDL_GetTicks() * 30));

    int left = nw_mg.state == NW_MG_PLAY ? NW_MG_SECONDS - (int)(nw_mg.frames / 50.0f)
               : nw_mg.state == NW_MG_GO ? NW_MG_SECONDS
                                         : 0;
    snprintf(buf, sizeof(buf), "%i:%02i", left / 60, left % 60);
    Text3D(buf, -mg_fontpos.x, y, mg_fontpos.z, mg_fontscale.x, mg_fontscale.y, mg_fontscale.z, 8, 1);
    if (nw_mg.state == NW_MG_GO) {
        Text3D("GO!", mg_gopos.x, mg_gopos.y, 1.0f + mg_gopos.z, 2.0f, 2.0f, 2.0f, 0, 1);
    }
}
