/* game/font3d's Text3D (port/units.toml override): retail's, with the glyph codes of the pad's buttons (x o s t, and
   the port's q r u v for L1 R1 L2 R2) drawn as the prompts nw_font3d_prompts last handed it. */
#include "port/font3d.h"

#include <string.h>

#include <SDL3/SDL.h>

#include "game/font3d.h"
#include "game/gametypes.h"
#include "gfx/rndr.h"
#include "nu3d/nugobj.h"
#include "nu3d/nugscn.h"
#include "nu3d/numtl.h"
#include "nu3d/nustream.h"
#include "nu3d/nutex.h"
#include "numath/numtx.h"

extern struct font3dobj_s Font3DObjTab[];
extern struct font3dtab_s Font3DTab[];
extern struct font3djchar_s Font3DAccentTab[];
extern signed char Font3DRemap[];
extern struct numtl_s *Font3DMtlTab[5][2];
extern s32 font3d_initialised;
extern struct objtab_s ObjTab[201];
extern struct game_s Game;
extern struct nugscn_s *font3d_scene;
extern f32 font3d_dx, font3d_dy;
extern f32 font3d_xleft, font3d_xright, font3d_xmid;
extern f32 font3d_ytop, font3d_ybottom, font3d_ymid;
extern f32 FONT3D_JSCALEDX, FONT3DYMUL, FONT3DSIZE;
extern f32 D_0062E358, D_0062E35C, D_0062E360;
extern f32 PANEL3DMULX, PANEL3DMULY;
extern struct numtl_s **nurndr_forced_mtl_table;

s32 RemapAccentedCharacter(char *c);
s32 CombinationCharacterBD(char c0, char c1);
s32 CombinationCharacterBC(char c0, char c1);
s32 DrawPanel3DObject(s32 i, f32 x, f32 y, f32 z, f32 sx, f32 sy, f32 sz, s32 a1, s32 a2, s32 rot, void *scene,
                      void *special, s32 last);
void DrawPanel3DCharacter(s32 i, f32 x, f32 y, f32 z, f32 sx, f32 sy, f32 sz, s32 a1, s32 a2, s32 a3, s32 action,
                          f32 anim_time, s32 last);

/* build/gen/prompts.c: assets/prompts/<name>.png. */
struct nw_embed_file {
    const char *name;
    const unsigned char *data;
    size_t size;
};
extern const struct nw_embed_file nw_prompt_icons[];
extern const size_t nw_prompt_icons_count;

#define NW_FONT3D_ICONS 64
/* An icon over the cross model's extent. */
#define NW_FONT3D_ICON_SCALE 1.5f

static SDL_SpinLock nw_font3d_lock;
static bool nw_font3d_handed;
static NwPrompts nw_font3d_next, nw_font3d_glyphs;
static struct {
    char name[32];
    struct numtl_s *mtl; /* NULL when it does not load */
} nw_font3d_icons[NW_FONT3D_ICONS];
static int nw_font3d_nicons;

void nw_font3d_prompts(const NwPrompts *p)
{
    SDL_LockSpinlock(&nw_font3d_lock);
    nw_font3d_next = *p;
    nw_font3d_handed = true;
    SDL_UnlockSpinlock(&nw_font3d_lock);
}

/* The glyph's target, -1 none; the Japanese build's text keeps retail's. */
static int nw_font3d_target(char c)
{
    if (Game.language == 0x63) {
        return -1;
    }
    switch (c) {
    case 'x': return 0;
    case 'o': return 1;
    case 's': return 2;
    case 't': return 3;
    case 'q': return 4;
    case 'r': return 5;
    case 'u': return 6;
    case 'v': return 7;
    default: return -1;
    }
}

/* name's material on its first use: the PNG as a type 3 bitmap (A, B, G, R), blended over everything, unlit. */
static struct numtl_s *nw_font3d_icon(const char *name)
{
    for (int i = 0; i < nw_font3d_nicons; i++) {
        if (strcmp(nw_font3d_icons[i].name, name) == 0) {
            return nw_font3d_icons[i].mtl;
        }
    }
    if (nw_font3d_nicons == NW_FONT3D_ICONS) {
        return NULL;
    }
    int n = nw_font3d_nicons++;
    SDL_strlcpy(nw_font3d_icons[n].name, name, sizeof(nw_font3d_icons[n].name));
    nw_font3d_icons[n].mtl = NULL;
    const struct nw_embed_file *file = NULL;
    for (size_t i = 0; i < nw_prompt_icons_count && !file; i++) {
        file = strcmp(nw_prompt_icons[i].name, name) == 0 ? &nw_prompt_icons[i] : NULL;
    }
    SDL_IOStream *io = file ? SDL_IOFromConstMem(file->data, file->size) : NULL;
    SDL_Surface *png = io ? SDL_LoadPNG_IO(io, true) : NULL;
    SDL_Surface *abgr = png ? SDL_ConvertSurface(png, SDL_PIXELFORMAT_ABGR32) : NULL;
    SDL_DestroySurface(png);
    if (!abgr || abgr->pitch != abgr->w * 4) {
        SDL_Log("prompts: icon %s: %s", name, file ? SDL_GetError() : "not in assets/prompts");
        SDL_DestroySurface(abgr);
        return NULL;
    }
    struct nutex_s tex = { 3, abgr->w, abgr->h, 1, abgr->pixels, NULL };
    struct numtl_s *mtl = NuMtlCreate(1);
    mtl->diffuse.r = 1.0f;
    mtl->diffuse.g = 1.0f;
    mtl->diffuse.b = 1.0f;
    mtl->alpha = 0.999f;
    mtl->attrib.cull = 2;
    mtl->attrib.zmode = 3;
    mtl->attrib.alpha = 1;
    mtl->attrib.lighting = 2;
    mtl->attrib.colour = 1;
    mtl->tid = NuTexCreate(&tex);
    NuMtlUpdate(mtl);
    SDL_DestroySurface(abgr);
    return nw_font3d_icons[n].mtl = mtl;
}

/* A glyph code's width in letters, 0 for another character. */
static f32 nw_font3d_width(char c)
{
    int t = nw_font3d_target(c);
    if (t < 0) {
        return 0.0f;
    }
    const NwPromptGlyph *g = &nw_font3d_glyphs.glyph[t];
    if (g->icon[0] && nw_font3d_icon(g->icon)) {
        return 1.0f;
    }
    if (g->text[0]) {
        f32 w = 0.0f;
        for (const char *s = g->text; *s; s++) {
            w += *s == ':' || *s == '.' ? 0.5f : 1.0f;
        }
        return w;
    }
    return 1.0f;
}

/* Retail's draw of a letter (or a button) of Font3DTab in the cell at x. */
static void nw_font3d_letter(char c, f32 x, f32 y, f32 z, f32 dx, f32 scalex, f32 scaley, f32 scalez)
{
    s32 idx = Font3DRemap[(int)c];
    if (idx == -1) {
        return;
    }
    f32 f = c == 'x' || c == 't' || c == 'o' || c == 's' ? 1.0f : 4.0f;
    f32 x0 = c == ':' || c == '.' ? x - dx * 0.25f : x;
    DrawPanel3DObject(-1, x0, y, z, FONT3DSIZE * scalex, FONT3DSIZE * scaley, (FONT3DSIZE * scalez) * f, 0, 0, 0,
                      font3d_scene, Font3DTab[idx].obj.special, 1);
}

/* The icon as a square on the cross model's extent, under DrawPanel3DObject's matrix. */
static void nw_font3d_quad(struct numtl_s *mtl, f32 x, f32 y, f32 z, f32 scalex, f32 scaley)
{
    struct nuvec_s lo = { -1.0f, -1.0f, 0.0f }, hi = { 1.0f, 1.0f, 0.0f };
    s32 cross = Font3DRemap['x'];
    struct nuspecial_s *special = cross != -1 ? Font3DTab[cross].obj.special : NULL;
    if (special) {
        struct nugobj_s *gobj = font3d_scene->gobjs[special->instance->objid];
        lo = gobj->min;
        hi = gobj->max;
    }
    f32 cx = (lo.x + hi.x) * 0.5f, cy = (lo.y + hi.y) * 0.5f;
    f32 half = SDL_max(hi.x - lo.x, hi.y - lo.y) * 0.5f * NW_FONT3D_ICON_SCALE;
    struct nuvec_s s = { FONT3DSIZE * scalex, FONT3DSIZE * scaley, 1.0f };
    struct numtx_s m;
    struct nuvtx_tc1_s v[4];
    NuMtxSetScale(&m, &s);
    m._30 = x * PANEL3DMULX;
    m._31 = y * PANEL3DMULY;
    m._32 = z;
    memset(v, 0, sizeof(v));
    for (int k = 0; k < 4; k++) {
        v[k].pnt.x = cx + (k & 1 ? half : -half);
        v[k].pnt.y = cy + (k & 2 ? -half : half);
        v[k].tc[0] = (f32)(k & 1);
        v[k].tc[1] = (f32)(k >> 1);
        v[k].diffuse = (s32)0x80808080;
    }
    NuRndrStreamAddMtl(&mtl->rs, mtl);
    nw_rndr_strip(v, 4, &m, &mtl->rs);
}

static void nw_font3d_glyph(char c, f32 x, f32 y, f32 z, f32 dx, f32 scalex, f32 scaley, f32 scalez)
{
    const NwPromptGlyph *g = &nw_font3d_glyphs.glyph[nw_font3d_target(c)];
    struct numtl_s *mtl = g->icon[0] ? nw_font3d_icon(g->icon) : NULL;
    if (mtl) {
        nw_font3d_quad(mtl, x, y, z, scalex, scaley);
    } else if (g->text[0]) {
        for (const char *s = g->text; *s; s++) {
            nw_font3d_letter(*s, x, y, z, dx, scalex, scaley, scalez);
            x += *s == ':' || *s == '.' ? dx * 0.5f : dx;
        }
    } else {
        nw_font3d_letter(g->model ? g->model : c, x, y, z, dx, scalex, scaley, scalez);
    }
}

void Text3D(char *txt, f32 x, f32 y, f32 z, f32 scalex, f32 scaley, f32 scalez, s32 align, s32 colour)
{
    s32 i;
    s32 l;
    f32 w;
    f32 f;
    f32 dx;
    f32 dy;
    f32 x0;
    f32 y0;
    f32 xpulse;
    struct font3dobj_s *obj;
    s32 idx;
    s32 accent;
    char c;
    char c1;

    SDL_LockSpinlock(&nw_font3d_lock);
    if (nw_font3d_handed) {
        nw_font3d_glyphs = nw_font3d_next;
    } else {
        nw_prompts_retail(&nw_font3d_glyphs);
    }
    SDL_UnlockSpinlock(&nw_font3d_lock);

    if (font3d_initialised == 0) {
        return;
    }
    if (font3d_scene == 0) {
        return;
    }
    font3d_dx = 0.0f;
    font3d_dy = 0.0f;
    font3d_xleft = font3d_xmid = font3d_xright = x;
    font3d_ytop = font3d_ymid = font3d_ybottom = y;
    if (txt == 0) {
        return;
    }
    l = strlen(txt);
    if (l < 1) {
        return;
    }

    for (i = 0, w = 0.0f; txt[i] != 0; i++) {
        if (txt[i] == '#') {
            if (txt[i + 1] != 0) {
                i++;
            }
        } else if (Game.language == 0x63) {
            if (txt[i + 1] != 0) {
                c1 = txt[i + 1];
                c = txt[i];
                f = 1.0f;
                if (c1 == ' ') {
                    if ((c == ':') || (c == '.')) {
                        f = 0.5f;
                    } else if (c == 'p') {
                        f = 12.0f;
                    }
                } else if (((c == '8' || c == '9') || (c >= 'A' && c <= 'F')) &&
                           ((c1 >= '0' && c1 <= '9') || (c1 >= 'A' && c1 <= 'F'))) {
                    f = FONT3D_JSCALEDX;
                    if (txt[i + 2] == 'B' &&
                        ((txt[i + 3] == 'D' && CombinationCharacterBD(c, c1)) ||
                         (txt[i + 3] == 'C' && CombinationCharacterBC(c, c1)))) {
                        i = i + 2;
                    }
                }
                w += f;
                i++;
            }
        } else {
            if (nw_font3d_target(txt[i]) >= 0) {
                w += nw_font3d_width(txt[i]);
            } else if ((txt[i] == ':') || (txt[i] == '.')) {
                w += 0.5f;
            } else if (txt[i] == 'p') {
                w += 12.0f;
            } else {
                w += 1.0f;
            }
        }
    }

    dx = scalex * D_0062E358;
    font3d_dx = dx;
    dy = (scaley * D_0062E358) * FONT3DYMUL;
    font3d_dy = dy;
    i = align & 5;
    if (i == 4) {
        y = y + dy * 0.5f;
    } else if (i == 1) {
        y = y - dy * 0.5f;
    }
    i = align & 0xA;
    if (i == 8) {
        x = x - (dx * w - dx * 0.5f);
    } else if (i == 2) {
        x = x + dx * 0.5f;
    } else {
        x = x - (dx * w * 0.5f - dx * 0.5f);
    }
    i = ((u32)colour < 5) ? colour : 0;
    nurndr_forced_mtl_table = &Font3DMtlTab[i][0];
    font3d_xleft = x - dx * 0.5f;

    for (i = 0; i < l; i++) {
        c = txt[i];
        xpulse = 1.0f;
        if (Game.language == 0x63) {
            c1 = txt[i + 1];
            if (c1 == 0) {
                goto Finish;
            }
            if ((c != '#') && (c1 != ' ')) {
                goto Skip;
            }
        }
        if ((c >= 0) || (c == (char)0xF8) || (c == (char)0xFE)) {
            if (c == '#') {
                if (txt[i + 1] != 0) {
                    switch (txt[i + 1]) {
                        case 'o': idx = 0; break;
                        case 'w': idx = 1; break;
                        case 'c': idx = 2; break;
                        case 'b': idx = 3; break;
                        case 'g': idx = 4; break;
                        default: idx = -1; break;
                    }
                    if (idx != -1) {
                        nurndr_forced_mtl_table = &Font3DMtlTab[idx][0];
                    }
                    i++;
                }
                continue;
            } else if (nw_font3d_target(c) >= 0) {
                nw_font3d_glyph(c, x, y, z, dx, scalex, scaley, scalez);
            } else if ((c >= 'a' && c <= 'z') && (Font3DRemap[(int)c] == -1)) {
                obj = &Font3DObjTab[c - 'a'];
                if (obj->i != -1) {
                    if ((obj->flags & 2) != 0) {
                        if (ObjTab[obj->i].obj.special != 0) {
                            DrawPanel3DObject(obj->i, x, y, z,
                                obj->scale * scalex, obj->scale * scaley,
                                obj->scale * scalez, 0, 0, 0,
                                ObjTab[obj->i].obj.scene, ObjTab[obj->i].obj.special, 1);
                        }
                    } else if ((obj->flags & 1) != 0) {
                        DrawPanel3DCharacter(obj->i, x, y, z,
                            obj->scale * scalex, obj->scale * scaley,
                            obj->scale * scalez, 0, 0, 0, obj->action,
                            obj->anim_time, 1);
                    }
                }
            } else {
                idx = Font3DRemap[(int)c];
                if (idx != -1) {
                    if (c == 'x' || c == 't' || c == 'o' || c == 's') {
                        f = 1.0f;
                    } else {
                        f = 4.0f;
                    }
                    if ((c == ':') || (c == '.')) {
                        x0 = x - dx * 0.25f;
                    } else if (c == 'p') {
                        x0 = x + dx * 5.5f;
                    } else {
                        x0 = x;
                    }
                    if (c == (char)0xFE) {
                        x0 = x0 - dx * D_0062E35C;
                        y0 = y + dy * D_0062E360;
                    } else {
                        y0 = y;
                    }
                    DrawPanel3DObject(-1, x0, y0, z, FONT3DSIZE * scalex,
                        FONT3DSIZE * scaley, (FONT3DSIZE * scalez) * f, 0, 0, 0,
                        font3d_scene, Font3DTab[idx].obj.special, 1);
                }
            }
        } else if (c < 0) {
            accent = RemapAccentedCharacter(&c);
            if (accent != -1) {
                idx = Font3DRemap[(int)c];
                if (idx != -1) {
                    DrawPanel3DObject(-1, x, y, z, FONT3DSIZE * scalex,
                        FONT3DSIZE * scaley, (FONT3DSIZE * scalez) * 4.0f, 0, 0, 0,
                        font3d_scene, Font3DTab[idx].obj.special, 1);
                }
                DrawPanel3DObject(-1, x, y, z, FONT3DSIZE * scalex,
                    FONT3DSIZE * scaley, (FONT3DSIZE * scalez) * 6.0f, 0, 0, 0,
                    font3d_scene, Font3DAccentTab[accent].obj.special, 1);
            }
        }
    Skip:
        if (nw_font3d_target(c) >= 0) {
            x = x + (dx * nw_font3d_width(c)) * xpulse;
        } else if ((c == ':') || (c == '.')) {
            x = x + (dx * 0.5f) * xpulse;
        } else if (c == 'p') {
            x = x + (dx * 12.0f) * xpulse;
        } else {
            x = x + dx * xpulse;
        }
        if (Game.language == 0x63) {
            i++;
        }
    }

Finish:
    font3d_xright = x - dx * 0.5f;
    font3d_xmid = (font3d_xleft + font3d_xright) * 0.5f;
    font3d_ytop = y + dy * 0.5f;
    font3d_ybottom = y - dy * 0.5f;
    font3d_ymid = (font3d_ytop + font3d_ybottom) * 0.5f;
    nurndr_forced_mtl_table = 0;
}
