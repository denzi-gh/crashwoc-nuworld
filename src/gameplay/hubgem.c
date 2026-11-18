#include "gameplay/hubgem.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "game/creature.h"
#include "game/gametypes.h"
#include "game/main.retail.h"
#include "gameplay/gameplay.h"
#include "gfx/interp.h"
#include "nu3d/nucamera.h"
#include "nu3d/nugscn.h"
#include "nu_port.h"
#include "numath/numtx.h"
#include "numath/nutrig.h"
#include "nups2/ps2ctl.h"
#include "platform/log.h"
#include "port/heap.h"
#include "port/level.h"
#include "port/nugscn.h"
#include "save/savedoc.h"

extern const unsigned char nw_hubgem_toml[];
extern const size_t nw_hubgem_toml_size;

extern s32 Level, Demo, GameMode, Paused, cutmovie, new_level, new_mode, fadeval, last_level, editor_active;
extern s32 PLAYERCOUNT, stick_bits, stick_bits_db, gamesfx_effect_volume, WallSplCount;
extern struct nuvec_s WallSplList[128];
extern int PANELOFF, NuRndrShadMaskCount;
extern u16 new_lev_flags;
extern float tumble_time, tumble_duration;
extern struct game_s Game;
extern struct gtimer_s GameTimer;
extern struct cursor_s Cursor;
extern struct cammtx_s GameCam[];
extern struct nucamera_s *pNuCam;
extern struct nupad_s *Pad[];
int NuFileSize(char *name);
void NuCameraSet(struct nucamera_s *cam);
void StartTransporter(struct obj_s *cyl, struct nuvec_s *pos);
s32 ObjectCylinderCollision(struct obj_s *obj, struct obj_s *cyl, f32 scale, s32 flag);
void AddGameDebris(s32 i, struct nuvec_s *pos);
void GameSfx(s32 sfx, struct nuvec_s *pos);
void BlendGameCamera(struct cammtx_s *cam, f32 time);
void Text3D(char *txt, f32 x, f32 y, f32 z, f32 scalex, f32 scaley, f32 scalez, s32 align, s32 colour);

enum { NW_HUBGEM_IDLE, NW_HUBGEM_RUN, NW_HUBGEM_OUT };

static char *nw_hubgem_cursor;

static struct {
    char *buf;
    size_t size;
    struct nugscn_s *scene;
    struct nuhspecial_s sph;
    bool found, failed;
    struct obj_s cyl;
    int phase;
    float settle, t;
    bool appeared, held, handback;
    int panel;
    float text;
    struct nuvec_s from;
    int from_x, from_y, from_z;
} nw_hg;

static const NwDocValue *nw_hubgem_value(const NwDoc *doc, const char *table, const char *name, NwDocType type)
{
    const char *path[1] = { table };
    const NwDocTable *t = nw_doc_table(doc, path, 1);
    const NwDocValue *v = t ? nw_doc_get(t, name) : NULL;
    if (!v || v->type != type) {
        SDL_SetError("[%s] %s", table, name);
        return NULL;
    }
    return v;
}

static bool nw_hubgem_int(const NwDoc *doc, const char *table, const char *name, int64_t lo, int64_t hi, int *out)
{
    const NwDocValue *v = nw_hubgem_value(doc, table, name, NW_DOC_INT);
    if (!v || v->i < lo || v->i > hi) {
        SDL_SetError("[%s] %s", table, name);
        return false;
    }
    *out = (int)v->i;
    return true;
}

static bool nw_hubgem_milli(const NwDoc *doc, const char *table, const char *name, int64_t lo, int64_t hi, float *out)
{
    int i;
    if (!nw_hubgem_int(doc, table, name, lo, hi, &i)) {
        return false;
    }
    *out = (float)i / 1000.0f;
    return true;
}

static bool nw_hubgem_vec(const NwDoc *doc, const char *table, const char *name, float out[3])
{
    const NwDocValue *v = nw_hubgem_value(doc, table, name, NW_DOC_ARRAY);
    if (!v || v->len != 3) {
        SDL_SetError("[%s] %s", table, name);
        return false;
    }
    for (size_t i = 0; i < 3; i++) {
        if (v->items[i].type != NW_DOC_INT || v->items[i].i < -1000000 || v->items[i].i > 1000000) {
            SDL_SetError("[%s] %s", table, name);
            return false;
        }
        out[i] = (float)v->items[i].i / 1000.0f;
    }
    return true;
}

static bool nw_hubgem_list(const NwDoc *doc, const char *table, const char *name, float *out, size_t lo, size_t hi,
                           size_t step, int *count)
{
    const NwDocValue *v = nw_hubgem_value(doc, table, name, NW_DOC_ARRAY);
    if (!v || v->len < lo || v->len > hi || v->len % step) {
        SDL_SetError("[%s] %s", table, name);
        return false;
    }
    for (size_t i = 0; i < v->len; i++) {
        if (v->items[i].type != NW_DOC_INT || v->items[i].i < -1000000 || v->items[i].i > 1000000) {
            SDL_SetError("[%s] %s", table, name);
            return false;
        }
        out[i] = (float)v->items[i].i / 1000.0f;
    }
    *count = (int)(v->len / step);
    return true;
}

static bool nw_hubgem_str(const NwDoc *doc, const char *table, const char *name, char *out, size_t size)
{
    const NwDocValue *v = nw_hubgem_value(doc, table, name, NW_DOC_STR);
    if (!v || v->len == 0 || v->len >= size || SDL_strnlen((const char *)v->bytes, v->len) != v->len) {
        SDL_SetError("[%s] %s", table, name);
        return false;
    }
    memcpy(out, v->bytes, v->len);
    out[v->len] = '\0';
    return true;
}

static bool nw_hubgem_bool(const NwDoc *doc, const char *table, const char *name, bool *out)
{
    const NwDocValue *v = nw_hubgem_value(doc, table, name, NW_DOC_BOOL);
    if (!v) {
        return false;
    }
    *out = v->i != 0;
    return true;
}

bool nw_hubgem_parse(NwHubGemData *d, const char *text, size_t size)
{
    NwDoc doc;
    memset(d, 0, sizeof(*d));
    if (!nw_doc_init(&doc)) {
        return false;
    }
    int yaw = 0, boxes = 0;
    bool ok = nw_doc_parse(&doc, text, size) &&
              nw_hubgem_str(&doc, "model", "file", d->file, sizeof(d->file)) &&
              nw_hubgem_str(&doc, "model", "object", d->object, sizeof(d->object)) &&
              nw_hubgem_int(&doc, "model", "buffer_kb", 1024, 65536, &d->buffer_kb) &&
              nw_hubgem_int(&doc, "unlock", "level", 0, 34, &d->level) &&
              nw_hubgem_int(&doc, "unlock", "flag", 1, 0xFFFF, &d->flag) &&
              nw_hubgem_int(&doc, "unlock", "seen_bit", 0, 7, &d->seen_bit) &&
              nw_hubgem_int(&doc, "place", "level", 0, 43, &d->place_level) &&
              nw_hubgem_vec(&doc, "place", "position_milli", d->pos) &&
              nw_hubgem_int(&doc, "place", "yaw_deg", -360, 360, &yaw) &&
              nw_hubgem_milli(&doc, "place", "scale_milli", 1, 100000, &d->scale) &&
              nw_hubgem_milli(&doc, "place", "radius_milli", 1, 100000, &d->radius) &&
              nw_hubgem_milli(&doc, "place", "height_milli", 1, 100000, &d->height) &&
              nw_hubgem_milli(&doc, "start", "settle_ms", 0, 600000, &d->settle) &&
              nw_hubgem_milli(&doc, "start", "floor_range_milli", 1, 1000000, &d->floor_range) &&
              nw_hubgem_milli(&doc, "start", "clear_milli", 0, 1000000, &d->clear) &&
              nw_hubgem_vec(&doc, "camera", "position_milli", d->cam_pos) &&
              nw_hubgem_vec(&doc, "camera", "look_milli", d->cam_look) &&
              nw_hubgem_milli(&doc, "camera", "blend_in_ms", 0, 600000, &d->blend_in) &&
              nw_hubgem_milli(&doc, "camera", "appear_ms", 0, 600000, &d->appear) &&
              nw_hubgem_milli(&doc, "camera", "hold_ms", 0, 600000, &d->hold) &&
              nw_hubgem_milli(&doc, "camera", "blend_out_ms", 0, 600000, &d->blend_out) &&
              nw_hubgem_int(&doc, "appear", "debris", 0, 169, &d->debris) &&
              nw_hubgem_int(&doc, "appear", "sfx", 0, 0x3FF, &d->sfx) &&
              nw_hubgem_int(&doc, "appear", "volume", 0, 0x7FFF, &d->volume) &&
              nw_hubgem_milli(&doc, "appear", "effect_height_milli", -100000, 100000, &d->effect_height) &&
              nw_hubgem_bool(&doc, "appear", "hide_hud", &d->hide_hud) &&
              nw_hubgem_str(&doc, "message", "text", d->text, sizeof(d->text)) &&
              nw_hubgem_milli(&doc, "message", "range_milli", 0, 1000000, &d->text_range) &&
              nw_hubgem_milli(&doc, "message", "y_milli", -2000, 2000, &d->text_y) &&
              nw_hubgem_milli(&doc, "message", "drop_milli", -2000, 2000, &d->text_drop) &&
              nw_hubgem_milli(&doc, "message", "slide_ms", 0, 600000, &d->text_slide) &&
              nw_hubgem_milli(&doc, "message", "scale_milli", 1, 10000, &d->text_scale) &&
              nw_hubgem_int(&doc, "message", "colour", 0, 31, &d->text_colour) &&
              nw_hubgem_list(&doc, "walls", "open_milli", d->wall_open, 4, 4, 4, &boxes) &&
              nw_hubgem_list(&doc, "walls", "add_milli", d->wall_add, 4, 32, 2, &d->wall_points);
    d->yaw = (int)((float)yaw * (65536.0f / 360.0f)) & 0xFFFF;
    nw_doc_free(&doc);
    return ok;
}

const NwHubGemData *nw_hubgem_data(void)
{
    static NwHubGemData data;
    static int state;
    if (!state) {
        state = nw_hubgem_parse(&data, (const char *)nw_hubgem_toml, nw_hubgem_toml_size) ? 1 : -1;
        if (state < 0) {
            nw_log_warn(NW_LOG_GAME, "hub data: %s", SDL_GetError());
        }
    }
    return state > 0 ? &data : NULL;
}

bool nw_hubgem_owned(const NwHubGemData *d)
{
    return (Game.level[d->level].flags & d->flag) != 0;
}

bool nw_hubgem_seen(const NwHubGemData *d)
{
    return (((u8)Game.level[d->level].pad1 >> d->seen_bit) & 1u) != 0;
}

static bool nw_hubgem_here(const NwHubGemData *d)
{
    return nw_profile.hub_gem && Level == d->place_level && cutmovie == -1 && !Demo;
}

static bool nw_hubgem_pending(const NwHubGemData *d)
{
    return last_level == d->level && (new_lev_flags & d->flag) != 0;
}

bool nw_hubgem_due(const NwHubGemData *d)
{
    return (nw_hubgem_owned(d) || nw_hubgem_pending(d)) && !nw_hubgem_seen(d);
}

bool nw_hubgem_idle(const NwHubGemData *d)
{
    if (!nw_hubgem_here(d) || GameMode != 0 || Paused != 0 || Cursor.menu != -1 || new_level != -1 ||
        new_mode != -1 || fadeval != 0 || GameTimer.frame < 0x32 || tumble_time < tumble_duration ||
        nw_level_pending() >= 0 || nw_cutscene_pending() >= 0 || editor_active || !PLAYERCOUNT || !player) {
        return false;
    }
    const struct obj_s *o = &player->obj;
    float dx = o->pos.x - d->pos[0], dy = o->pos.y - d->pos[1], dz = o->pos.z - d->pos[2];
    return !o->dead && !o->finished && !o->transporting && o->ground && SDL_fabsf(dy) <= d->floor_range &&
           dx * dx + dz * dz >= d->clear * d->clear;
}

void nw_hubgem_aim(const float pos[3], const float look[3], int *xrot, int *yrot)
{
    float dx = look[0] - pos[0], dy = look[1] - pos[1], dz = look[2] - pos[2];
    *xrot = -NuAtan2D(dy, sqrtf(dx * dx + dz * dz)) & 0xFFFF;
    *yrot = NuAtan2D(dx, dz) & 0xFFFF;
}

static bool nw_hubgem_shown(const NwHubGemData *d)
{
    return nw_hg.found && nw_hubgem_here(d) &&
           (nw_hg.phase != NW_HUBGEM_IDLE ? nw_hg.appeared : nw_hubgem_owned(d) && nw_hubgem_seen(d));
}

static void nw_hubgem_load(const NwHubGemData *d)
{
    size_t size = (size_t)d->buffer_kb * 1024;
    nw_hg.failed = true;
    if (!nw_hg.buf) {
        if (!(nw_hg.buf = SDL_malloc(size))) {
            return;
        }
        nw_hg.size = size;
        nw_load_buffer(nw_hg.buf, size);
    }
    if (NuFileSize((char *)d->file) <= 0) {
        return;
    }
    int masks = NuRndrShadMaskCount;
    nw_hubgem_cursor = nw_hg.buf;
    nw_hg.scene = NuGScnRead(&nw_hubgem_cursor, nw_hg.buf + nw_hg.size, (char *)d->file);
    NuRndrShadMaskCount = masks;
    if (!nw_hg.scene) {
        return;
    }
    nw_log_debug(NW_LOG_GAME, "hub data: %u KB", (unsigned)((size_t)(nw_hubgem_cursor - nw_hg.buf) / 1024));
    if (!NuSpecialFind(nw_hg.scene, &nw_hg.sph, (char *)d->object)) {
        nw_gscn_remove(nw_hg.scene);
        nw_hg.scene = NULL;
        return;
    }
    struct nuvec_s pos = { d->pos[0], d->pos[1], d->pos[2] };
    StartTransporter(&nw_hg.cyl, &pos);
    nw_hg.cyl.RADIUS = nw_hg.cyl.radius = d->radius;
    nw_hg.cyl.bot = -d->height;
    nw_hg.found = true;
    nw_hg.failed = false;
}

void nw_hubgem_level(void)
{
    if (!nw_profile.hub_gem) {
        return;
    }
    const NwHubGemData *d = nw_hubgem_data();
    if (!d || !nw_hubgem_here(d) || nw_hg.scene || nw_hg.failed || !(nw_hubgem_owned(d) || nw_hubgem_pending(d))) {
        return;
    }
    NW_DEVIATION(hub_gem);
    nw_hubgem_load(d);
}

static void nw_hubgem_pad(void)
{
    struct nupad_s *pad = Pad[0];
    if (!pad) {
        return;
    }
    NuPs2ReadPad(pad);
    pad->paddata = pad->paddata_db = 0;
    pad->ldx = pad->ldy = pad->rdx = pad->rdy = 0;
    pad->l_nx = pad->l_ny = pad->l_rx = pad->l_ry = pad->l_dx = pad->l_dy = 0.0f;
    pad->r_nx = pad->r_ny = pad->r_rx = pad->r_ry = pad->r_dx = pad->r_dy = 0.0f;
    pad->rdata.buttons_hi = pad->rdata.buttons_lo = 0xFF;
    pad->rdata.l_alg_x = pad->rdata.l_alg_y = pad->rdata.r_alg_x = pad->rdata.r_alg_y = 0x80;
    pad->rdata.l_algpad_r = pad->rdata.l_algpad_l = pad->rdata.l_algpad_u = pad->rdata.l_algpad_d = 0;
    pad->rdata.r_algpad_u = pad->rdata.r_algpad_r = pad->rdata.r_algpad_d = pad->rdata.r_algpad_l = 0;
    pad->rdata.l1_alg = pad->rdata.r1_alg = pad->rdata.l2_alg = pad->rdata.r2_alg = 0;
    stick_bits = stick_bits_db = 0;
}

static void nw_hubgem_start(const NwHubGemData *d)
{
    nw_hg.phase = NW_HUBGEM_RUN;
    nw_hg.t = 0.0f;
    nw_hg.appeared = nw_hg.held = nw_hg.handback = false;
    nw_hg.panel = PANELOFF;
    if (d->hide_hud) {
        PANELOFF = 1;
    }
}

static void nw_hubgem_stop(void)
{
    PANELOFF = nw_hg.panel;
    nw_hg.phase = NW_HUBGEM_IDLE;
    nw_hg.settle = 0.0f;
}

static void nw_hubgem_abort(void)
{
    if (nw_hg.phase == NW_HUBGEM_IDLE) {
        return;
    }
    if (nw_hg.phase == NW_HUBGEM_RUN && nw_hg.held) {
        GameCam[0].mode = -1;
        nw_interp_snap();
    }
    nw_hubgem_stop();
}

static bool nw_hubgem_broken(const NwHubGemData *d)
{
    return !nw_hubgem_here(d) || !nw_hg.found || GameMode != 0 || new_level != -1 || new_mode != -1 ||
           nw_level_pending() >= 0 || nw_cutscene_pending() >= 0 || editor_active || !player || player->obj.dead;
}

static void nw_hubgem_appear(const NwHubGemData *d)
{
    static struct nuvec_s fx;
    fx.x = d->pos[0];
    fx.y = d->pos[1] + d->effect_height;
    fx.z = d->pos[2];
    nw_hg.appeared = true;
    AddGameDebris(d->debris, &fx);
    gamesfx_effect_volume = d->volume;
    GameSfx(d->sfx, &fx);
}

static void nw_hubgem_text(const NwHubGemData *d, float dt)
{
    if (Paused) {
        return;
    }
    bool near = false;
    if (nw_hg.phase == NW_HUBGEM_IDLE && nw_hubgem_shown(d) && player && Cursor.menu == -1) {
        float dx = player->obj.pos.x - d->pos[0], dy = player->obj.pos.y - d->pos[1], dz = player->obj.pos.z - d->pos[2];
        near = dx * dx + dz * dz <= d->text_range * d->text_range && SDL_fabsf(dy) <= d->floor_range;
    }
    float step = d->text_slide > 0.0f ? dt / d->text_slide : 1.0f;
    nw_hg.text = SDL_clamp(nw_hg.text + (near ? step : -step), 0.0f, 1.0f);
}

bool nw_hubgem_tick(void)
{
    if (nw_hg.phase == NW_HUBGEM_IDLE && (cutmovie != -1 || !nw_profile.hub_gem)) {
        nw_hg.text = 0.0f;
        return false;
    }
    const NwHubGemData *d = nw_hubgem_data();
    if (!d) {
        return false;
    }
    float dt = NW_HZ(0.02f, 1.0f / 60.0f);
    nw_hubgem_text(d, dt);
    if (nw_hg.phase == NW_HUBGEM_IDLE) {
        if (!nw_hubgem_here(d)) {
            nw_hg.settle = 0.0f;
            return false;
        }
        if (!nw_hg.scene && !nw_hg.failed && (nw_hubgem_owned(d) || nw_hubgem_pending(d))) {
            NW_DEVIATION(hub_gem);
            nw_hubgem_load(d);
        }
        bool ready = nw_hg.found && nw_hubgem_due(d) && nw_hubgem_idle(d);
        nw_hg.settle = ready ? nw_hg.settle + dt : 0.0f;
        if (!ready || nw_hg.settle < d->settle) {
            return false;
        }
        NW_DEVIATION(hub_gem);
        nw_hubgem_start(d);
    } else if (nw_hubgem_broken(d)) {
        nw_hubgem_abort();
        return false;
    }
    nw_hg.t += dt;
    if (nw_hg.phase == NW_HUBGEM_RUN) {
        if (!nw_hg.appeared && nw_hg.t >= d->appear) {
            nw_hubgem_appear(d);
        }
        if (nw_hg.t >= d->appear + d->hold) {
            Game.level[d->level].pad1 = (char)((u8)Game.level[d->level].pad1 | (1u << d->seen_bit));
            nw_hg.phase = NW_HUBGEM_OUT;
            nw_hg.handback = true;
        }
    } else if (nw_hg.t >= d->appear + d->hold + d->blend_out) {
        nw_hubgem_stop();
    }
    nw_hubgem_pad();
    return true;
}

void DoInput(void)
{
    if (!nw_hubgem_tick()) {
        DoInput__retail();
    }
}

bool nw_hubgem_camera(struct cammtx_s *cam)
{
    if (cam != &GameCam[0] || nw_hg.phase == NW_HUBGEM_IDLE) {
        return false;
    }
    const NwHubGemData *d = nw_hubgem_data();
    if (!d) {
        return false;
    }
    if (nw_hg.phase == NW_HUBGEM_OUT) {
        if (nw_hg.handback) {
            nw_hg.handback = false;
            if (d->blend_out > 0.0f) {
                BlendGameCamera(cam, d->blend_out * NW_HZ(1.0f, 1.2f));
            } else {
                cam->mode = -1;
                nw_interp_snap();
            }
        }
        return false;
    }
    NW_DEVIATION(hub_gem);
    if (!nw_hg.held) {
        nw_hg.held = true;
        nw_hg.from = cam->pos;
        nw_hg.from_x = cam->xrot & 0xFFFF;
        nw_hg.from_y = cam->yrot & 0xFFFF;
        nw_hg.from_z = cam->zrot & 0xFFFF;
        if (d->blend_in <= 0.0f) {
            nw_interp_snap();
        }
    }
    int tx, ty;
    nw_hubgem_aim(d->cam_pos, d->cam_look, &tx, &ty);
    float k = d->blend_in > 0.0f ? SDL_clamp(nw_hg.t / d->blend_in, 0.0f, 1.0f) : 1.0f;
    k = k * k * (3.0f - 2.0f * k);
    cam->pos.x = nw_hg.from.x + (d->cam_pos[0] - nw_hg.from.x) * k;
    cam->pos.y = nw_hg.from.y + (d->cam_pos[1] - nw_hg.from.y) * k;
    cam->pos.z = nw_hg.from.z + (d->cam_pos[2] - nw_hg.from.z) * k;
    int x = (nw_hg.from_x + (int)((float)(int16_t)(tx - nw_hg.from_x) * k)) & 0xFFFF;
    int y = (nw_hg.from_y + (int)((float)(int16_t)(ty - nw_hg.from_y) * k)) & 0xFFFF;
    int z = (nw_hg.from_z + (int)((float)(int16_t)(0 - nw_hg.from_z) * k)) & 0xFFFF;
    cam->newpos = cam->pos;
    cam->xrot = x;
    cam->yrot = y;
    cam->zrot = z;
    cam->new_xrot = (u16)x;
    cam->new_yrot = (u16)y;
    cam->new_zrot = (u16)z;
    NuMtxSetIdentity(&cam->m);
    NuMtxTranslate(&cam->m, &cam->pos);
    NuMtxPreRotateY(&cam->m, y);
    NuMtxPreRotateX(&cam->m, x);
    NuMtxPreRotateZ(&cam->m, z);
    cam->vX.x = cam->m._00;
    cam->vX.y = cam->m._01;
    cam->vX.z = cam->m._02;
    cam->vY.x = cam->m._10;
    cam->vY.y = cam->m._11;
    cam->vY.z = cam->m._12;
    cam->vZ.x = cam->m._20;
    cam->vZ.y = cam->m._21;
    cam->vZ.z = cam->m._22;
    pNuCam->mtx = cam->m;
    NuCameraSet(pNuCam);
    if (player && player->used) {
        cam->hdg_to_player = (u16)NuAtan2D(player->obj.pos.x - cam->pos.x, player->obj.pos.z - cam->pos.z);
    }
    return true;
}

static bool nw_hubgem_has_wall(int n, float x0, float z0, float x1, float z1)
{
    for (int i = 0; i + 1 < n; i += 2) {
        if (WallSplList[i].x == x0 && WallSplList[i].z == z0 && WallSplList[i + 1].x == x1 &&
            WallSplList[i + 1].z == z1) {
            return true;
        }
    }
    return false;
}

void nw_hubgem_walls(void)
{
    if (!nw_hg.found || !nw_profile.hub_gem) {
        return;
    }
    const NwHubGemData *d = nw_hubgem_data();
    if (!d || !nw_hubgem_shown(d)) {
        return;
    }
    NW_DEVIATION(hub_gem);
    int n = 0;
    for (int i = 0; i + 1 < WallSplCount; i += 2) {
        float mx = (WallSplList[i].x + WallSplList[i + 1].x) * 0.5f;
        float mz = (WallSplList[i].z + WallSplList[i + 1].z) * 0.5f;
        if (mx >= d->wall_open[0] && mz >= d->wall_open[1] && mx <= d->wall_open[2] && mz <= d->wall_open[3]) {
            continue;
        }
        WallSplList[n++] = WallSplList[i];
        WallSplList[n++] = WallSplList[i + 1];
    }
    for (int k = 0; k + 1 < d->wall_points && n + 2 <= 128; k++) {
        float x0 = d->wall_add[2 * k], z0 = d->wall_add[2 * k + 1];
        float x1 = d->wall_add[2 * k + 2], z1 = d->wall_add[2 * k + 3];
        if (nw_hubgem_has_wall(n, x0, z0, x1, z1)) {
            continue;
        }
        WallSplList[n].x = x0;
        WallSplList[n].y = 0.0f;
        WallSplList[n++].z = z0;
        WallSplList[n].x = x1;
        WallSplList[n].y = 0.0f;
        WallSplList[n++].z = z1;
    }
    WallSplCount = n;
}

void nw_hubgem_collide(struct obj_s *obj)
{
    if (!nw_hg.found || !player || obj != &player->obj || !nw_profile.hub_gem) {
        return;
    }
    const NwHubGemData *d = nw_hubgem_data();
    if (!d || !nw_hubgem_shown(d)) {
        return;
    }
    NW_DEVIATION(hub_gem);
    nw_hg.cyl.oldpos = nw_hg.cyl.pos;
    nw_hg.cyl.mom.x = nw_hg.cyl.mom.y = nw_hg.cyl.mom.z = 0.0f;
    nw_hg.cyl.dyrot = 0;
    ObjectCylinderCollision(obj, &nw_hg.cyl, 1.0f, 0);
}

void nw_hubgem_draw(void)
{
    if (!nw_hg.found || !nw_profile.hub_gem) {
        return;
    }
    const NwHubGemData *d = nw_hubgem_data();
    if (!d || !nw_hubgem_shown(d)) {
        return;
    }
    NW_DEVIATION(hub_gem);
    static struct numtx_s m;
    struct nuvec_s s = { d->scale, d->scale, d->scale };
    NuMtxSetIdentity(&m);
    NuMtxScale(&m, &s);
    NuMtxRotateY(&m, d->yaw);
    m._30 = d->pos[0];
    m._31 = d->pos[1];
    m._32 = d->pos[2];
    NuSpecialDrawAt(&nw_hg.sph, &m);
}

void nw_hubgem_message(void)
{
    if (nw_hg.text <= 0.0f || !nw_profile.hub_gem || Paused) {
        return;
    }
    const NwHubGemData *d = nw_hubgem_data();
    if (!d || !nw_hubgem_here(d)) {
        return;
    }
    NW_DEVIATION(hub_gem);
    float f = 1.0f - nw_hg.text;
    Text3D((char *)d->text, 0.0f, d->text_y + f * d->text_drop, 1.0f, d->text_scale, d->text_scale, d->text_scale, 0,
           d->text_colour);
}

void nw_hubgem_close(void)
{
    nw_hubgem_abort();
    if (nw_hg.scene) {
        nw_gscn_remove(nw_hg.scene);
        nw_hg.scene = NULL;
    }
    nw_hg.found = nw_hg.failed = false;
    nw_hg.settle = 0.0f;
    nw_hg.text = 0.0f;
}
