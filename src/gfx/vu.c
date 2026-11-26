#include "gfx/vu.h"

#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "gfx/gl.h"
#include "gfx/tex.h"
#include "gfx/vif.h"

/* Read once per renderer initialization from shaders/ beside the executable, also for lazily linked variants. */
static struct {
    const char *name;
    char *text;
    size_t size;
} nw_vu_sources[] = {
    { "vu.vert.glsl", NULL, 0 },
    { "vu.frag.glsl", NULL, 0 },
    { "gem.frag.glsl", NULL, 0 },
};

enum { NW_VU_SOURCE_VS, NW_VU_SOURCE_FS, NW_VU_SOURCE_GEM };

static bool nw_vu_load_sources(void)
{
    const char *base = SDL_GetBasePath();
    if (!base) {
        return false;
    }
    for (size_t i = 0; i < SDL_arraysize(nw_vu_sources); i++) {
        char *path = NULL;
        if (SDL_asprintf(&path, "%sshaders/%s", base, nw_vu_sources[i].name) < 0) {
            return false;
        }
        nw_vu_sources[i].text = SDL_LoadFile(path, &nw_vu_sources[i].size);
        bool ok = nw_vu_sources[i].text != NULL;
        if (!ok) {
            char reason[512];
            SDL_strlcpy(reason, SDL_GetError(), sizeof(reason));
            SDL_SetError("vu: cannot load %s: %s", path, reason);
        } else if (nw_vu_sources[i].size == 0 || nw_vu_sources[i].size > INT_MAX) {
            ok = SDL_SetError("vu: invalid shader file size: %s", path);
        }
        SDL_free(path);
        if (!ok) {
            return false;
        }
    }
    return true;
}

typedef struct NwVuState {
    GLuint prog;
    GLint lv, diffuse, fx, ambient, lightdir, lightcol, uvoff, sprite, texmode, texlod, tcc, ascolour, atest, joints, blend,
        uv, ptl, zclut, zclear, zalpha, zad, gsblend, vu, frag, wave, filter, texalpha, zero, black, rgba8, layer;
    GLint world, vp, eye, cam, tint, gem, viewport, atsuv, atsmode;
} NwVuState;

/* Ordinary materials keep the retail shaders; the opening's effects (u_fx) and ATS UVs take variants of their own,
   linked when first used: effects bit 0, ATS bit 1. */
static NwVuState nw_vu_variants[4][NW_VU_PROGRAMS];
static NwVuState *nw_vu[NW_VU_PROGRAMS];
static NwTexFilter nw_vu_filter_mode;

/* text (size bytes, NUL-terminated when size is 0) under the program's defines; name for the error. */
static GLuint nw_vu_shader_text(GLenum type, NwVuProgram program, const char *name, const char *text, size_t size,
                                bool effects, bool ats)
{
    static const char *const names[NW_VU_PROGRAMS] = { "TRIC",    "TRICU", "TRICA", "TRICDV",  "FACEON",
                                                       "FACEY",   "SKIN",  "TRIXUV", "SKINXUV", "PTL",
                                                       "WATER", "WATERUV", "TRIGLASS", "SKNGLASS", "PTL_GENERAL",
                                                       "GEM",     "SKNGEM" };
    char head[768];
    int n = SDL_snprintf(head, sizeof(head), "#version 330 core\n");
    for (int i = 0; i < NW_VU_PROGRAMS; i++) {
        n += SDL_snprintf(head + n, sizeof(head) - (size_t)n, "#define %s %d\n", names[i], i);
    }
    SDL_snprintf(head + n, sizeof(head) - (size_t)n,
                  "#define PROGRAM %d\n#define PARTICLE (PROGRAM == PTL || PROGRAM == PTL_GENERAL)\n"
                  "#define GEMS (PROGRAM == GEM || PROGRAM == SKNGEM)\n#define EFFECTS %d\n#define ATS %d\n",
                 (int)program, effects, ats);
    const GLchar *parts[] = { head, text };
    const GLint lengths[] = { -1, size ? (GLint)size : -1 };
    GLuint s = glCreateShader(type);
    GLint ok = 0;
    glShaderSource(s, 2, parts, lengths);
    glCompileShader(s);
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = "";
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        SDL_SetError("vu: %s (%s): %s", name, names[program], log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint nw_vu_shader(GLenum type, NwVuProgram program, size_t source, bool effects, bool ats)
{
    return nw_vu_shader_text(type, program, nw_vu_sources[source].name, nw_vu_sources[source].text,
                             nw_vu_sources[source].size, effects, ats);
}

static bool nw_vu_link(NwVuProgram program, bool effects, bool ats)
{
    nw_vu[program] = &nw_vu_variants[effects + 2 * ats][program];
    GLuint vs = nw_vu_shader(GL_VERTEX_SHADER, program, NW_VU_SOURCE_VS, effects, ats);
    bool gem = program == NW_VU_GEM || program == NW_VU_SKNGEM;
    GLuint fs = vs ? nw_vu_shader(GL_FRAGMENT_SHADER, program, gem ? NW_VU_SOURCE_GEM : NW_VU_SOURCE_FS, effects, ats)
                   : 0;
    if (!fs) {
        glDeleteShader(vs);
        return false;
    }
    GLint ok = 0;
    GLuint p = nw_vu[program]->prog = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    glDeleteShader(vs);
    glDeleteShader(fs);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024] = "";
        glGetProgramInfoLog(p, sizeof(log), NULL, log);
        glDeleteProgram(p);
        nw_vu[program]->prog = 0;
        return SDL_SetError("vu: program: %s", log);
    }
    nw_vu[program]->lv = glGetUniformLocation(p, "u_lv");
    nw_vu[program]->diffuse = glGetUniformLocation(p, "u_diffuse");
    nw_vu[program]->fx = glGetUniformLocation(p, "u_fx");
    nw_vu[program]->ambient = glGetUniformLocation(p, "u_ambient");
    nw_vu[program]->lightdir = glGetUniformLocation(p, "u_lightdir");
    nw_vu[program]->lightcol = glGetUniformLocation(p, "u_lightcol");
    nw_vu[program]->uvoff = glGetUniformLocation(p, "u_uvoff");
    nw_vu[program]->sprite = glGetUniformLocation(p, "u_sprite");
    nw_vu[program]->texmode = glGetUniformLocation(p, "u_texmode");
    nw_vu[program]->texlod = glGetUniformLocation(p, "u_texlod");
    nw_vu[program]->tcc = glGetUniformLocation(p, "u_tcc");
    nw_vu[program]->rgba8 = glGetUniformLocation(p, "u_rgba8");
    nw_vu[program]->layer = glGetUniformLocation(p, "u_layer");
    nw_vu[program]->atsuv = glGetUniformLocation(p, "u_atsuv");
    nw_vu[program]->atsmode = glGetUniformLocation(p, "u_atsmode");
    nw_vu[program]->ascolour = glGetUniformLocation(p, "u_ascolour");
    nw_vu[program]->atest = glGetUniformLocation(p, "u_atest");
    nw_vu[program]->joints = glGetUniformLocation(p, "u_joints");
    nw_vu[program]->blend = glGetUniformLocation(p, "u_blend");
    nw_vu[program]->uv = glGetUniformLocation(p, "u_uv");
    nw_vu[program]->ptl = glGetUniformLocation(p, "u_ptl");
    nw_vu[program]->zclut = glGetUniformLocation(p, "u_zclut");
    nw_vu[program]->zclear = glGetUniformLocation(p, "u_zclear");
    nw_vu[program]->zalpha = glGetUniformLocation(p, "u_zalpha");
    nw_vu[program]->zad = glGetUniformLocation(p, "u_zad");
    nw_vu[program]->gsblend = glGetUniformLocation(p, "u_gsblend");
    nw_vu[program]->vu = glGetUniformLocation(p, "u_vu");
    nw_vu[program]->frag = glGetUniformLocation(p, "u_frag");
    nw_vu[program]->wave = glGetUniformLocation(p, "u_wave");
    nw_vu[program]->filter = glGetUniformLocation(p, "u_filter");
    nw_vu[program]->texalpha = glGetUniformLocation(p, "u_texalpha");
    nw_vu[program]->zero = glGetUniformLocation(p, "u_zero");
    nw_vu[program]->black = glGetUniformLocation(p, "u_black");
    nw_vu[program]->world = glGetUniformLocation(p, "u_world");
    nw_vu[program]->vp = glGetUniformLocation(p, "u_vp");
    nw_vu[program]->eye = glGetUniformLocation(p, "u_eye");
    nw_vu[program]->cam = glGetUniformLocation(p, "u_cam");
    nw_vu[program]->tint = glGetUniformLocation(p, "u_tint");
    nw_vu[program]->gem = glGetUniformLocation(p, "u_gem");
    nw_vu[program]->viewport = glGetUniformLocation(p, "u_viewport");
    glUseProgram(p);
    glUniform1i(glGetUniformLocation(p, "u_tex"), 0);
    glUniform1i(glGetUniformLocation(p, "u_array"), 10);
    glUniform1i(glGetUniformLocation(p, "u_clut"), 1);
    glUniform1i(glGetUniformLocation(p, "u_surf"), 2);
    glUniform1i(glGetUniformLocation(p, "u_pool"), 3);
    glUniform1i(glGetUniformLocation(p, "u_weights"), 4);
    glUniform1i(glGetUniformLocation(p, "u_deltas"), 5);
    glUniform1i(glGetUniformLocation(p, "u_depth"), 6);
    glUniform1i(glGetUniformLocation(p, "u_zpal"), 7);
    glUniform1i(glGetUniformLocation(p, "u_dst"), 8);
    return true;
}

static const char nw_vu_silhouette_fs[] =
    "out vec4 o_col;\n"
    "void main()\n"
    "{\n"
    "    o_col = vec4(1.0, 1.0, 1.0, 0.25);\n"
    "}\n";

static struct {
    GLuint prog;
    GLint lv, joints, blend;
} nw_vu_sil[2];

static bool nw_vu_silhouette_link(bool skin)
{
    NwVuProgram program = skin ? NW_VU_SKIN : NW_VU_TRICU;
    GLuint vs = nw_vu_shader(GL_VERTEX_SHADER, program, NW_VU_SOURCE_VS, false, false);
    GLuint fs = vs ? nw_vu_shader_text(GL_FRAGMENT_SHADER, program, "silhouette", nw_vu_silhouette_fs, 0, false, false)
                   : 0;
    if (!fs) {
        glDeleteShader(vs);
        return false;
    }
    GLint ok = 0;
    GLuint p = nw_vu_sil[skin].prog = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    glDeleteShader(vs);
    glDeleteShader(fs);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024] = "";
        glGetProgramInfoLog(p, sizeof(log), NULL, log);
        return SDL_SetError("vu: program: %s", log);
    }
    nw_vu_sil[skin].lv = glGetUniformLocation(p, "u_lv");
    nw_vu_sil[skin].joints = glGetUniformLocation(p, "u_joints");
    nw_vu_sil[skin].blend = glGetUniformLocation(p, "u_blend");
    glUseProgram(p);
    glUniform1i(glGetUniformLocation(p, "u_pool"), 3);
    glUniform1i(glGetUniformLocation(p, "u_weights"), 4);
    glUniform1i(glGetUniformLocation(p, "u_deltas"), 5);
    return true;
}

bool nw_vu_init(void)
{
    GLint side; glGetIntegerv(GL_MAX_TEXTURE_SIZE,&side); nw_tex_set_max_side(side);
    GLint layers; glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS,&layers); nw_tex_set_max_layers(layers);
    if (!nw_vu_load_sources()) {
        nw_vu_shutdown();
        return false;
    }
    for (int i = 0; i < NW_VU_PROGRAMS; i++) {
        if (!nw_vu_link((NwVuProgram)i, false, false)) {
            nw_vu_shutdown();
            return false;
        }
    }
    if (!nw_vu_silhouette_link(false) || !nw_vu_silhouette_link(true)) {
        nw_vu_shutdown();
        return false;
    }
    return true;
}

void nw_vu_shutdown(void)
{
    nw_tex_set_max_side(0);
    nw_tex_set_max_layers(0);
    for (int i = 0; i < NW_VU_PROGRAMS; i++) {
        for (int variant = 0; variant < 4; variant++) {
            glDeleteProgram(nw_vu_variants[variant][i].prog);
            nw_vu_variants[variant][i].prog = 0;
        }
        nw_vu[i] = NULL;
    }
    for (size_t i = 0; i < SDL_arraysize(nw_vu_sources); i++) {
        SDL_free(nw_vu_sources[i].text);
        nw_vu_sources[i].text = NULL;
        nw_vu_sources[i].size = 0;
    }
    for (int i = 0; i < 2; i++) {
        glDeleteProgram(nw_vu_sil[i].prog);
        nw_vu_sil[i].prog = 0;
    }
}

void nw_vu_silhouette(bool skin, const float lv[16], uint32_t joints)
{
    glUseProgram(nw_vu_sil[skin].prog);
    glUniformMatrix4fv(nw_vu_sil[skin].lv, 1, GL_FALSE, lv);
    if (skin) {
        glUniform1i(nw_vu_sil[skin].joints, (GLint)joints * 4);
        glUniform2i(nw_vu_sil[skin].blend, 0, 0);
    }
}

NwVuProgram nw_vu_program(const char *package)
{
    static const struct {
        const char *name;
        NwVuProgram program;
    } map[] = {
        { "vu_load_tricu_mpg", NW_VU_TRICU },
        { "vu_load_trica_mpg", NW_VU_TRICA },
        { "vu_load_tricdv_mpg", NW_VU_TRICDV },
        { "vu_load_xformuv_mpg", NW_VU_TRIXUV },
        { "vu_load_fx2_mpg", NW_VU_TRICU }, /* its triangles at MSCAL 0x306: vertex colour, unlit */
        { "vu_load_water_mpg", NW_VU_WATER },
        { "vu_load_wateruv_mpg", NW_VU_WATERUV },
        { "vu_load_glass_mpg", NW_VU_TRIGLASS },
    };
    for (size_t i = 0; package && i < SDL_arraysize(map); i++) {
        if (strcmp(package, map[i].name) == 0) {
            return map[i].program;
        }
    }
    return NW_VU_TRIC;
}

NwVuProgram nw_vu_skin_program(const char *package)
{
    if (package && strcmp(package, "vu_load_tri_mpg") == 0) {
        return NW_VU_SKIN;
    }
    if (package && strcmp(package, "vu_load_glass_mpg") == 0) {
        return NW_VU_SKNGLASS;
    }
    return package && strcmp(package, "vu_load_xformuv_mpg") == 0 ? NW_VU_SKINXUV : NW_VU_PROGRAMS;
}

void nw_vu_skin(NwVuProgram program, uint32_t joints, uint32_t weights, uint32_t nweights)
{
    glUniform1i(nw_vu[program]->joints, (GLint)joints * 4);
    glUniform2i(nw_vu[program]->blend, (GLint)weights, (GLint)nweights);
}

void nw_vu_waves(const float waves[32][4])
{
    for (int program = NW_VU_WATER; program <= NW_VU_WATERUV; program++) {
        for (int variant = 0; variant < 4; variant++) {
            const NwVuState *state = &nw_vu_variants[variant][program];
            if (state->prog) {
                glUseProgram(state->prog);
                glUniform4fv(state->wave, 32, waves[0]);
            }
        }
    }
}

void nw_vu_ptl(NwVuProgram program, const uint32_t image[132][4])
{
    glUniform4uiv(nw_vu[program]->ptl, 132, image[0]);
}

NwVuProgram nw_vu_particle_program(const NwVuTexture *texture, const NwMtlState *state)
{
    bool bilinear = nw_vu_filter_mode == NW_FILTER_BILINEAR ||
                    (nw_vu_filter_mode == NW_FILTER_RETAIL && state->mmag == 1 && state->mmin == 1);
    return texture && texture->power_of_two && state->mmin < 2 && bilinear ? NW_VU_PTL : NW_VU_PTL_GENERAL;
}

uint32_t nw_vu_use(NwVuProgram program, const NwVuObject *o, const NwVuMaterial *m, NwMtlPass pass[2])
{
    bool array = m->tex && m->tex->layers;
    if (array && (!m->tex->index || m->layer < 0 || (uint32_t)m->layer >= m->tex->layers ||
                  program == NW_VU_PTL || program == NW_VU_PTL_GENERAL)) return 0;
    if (m->uv.binding && (!m->tex || m->tex->surface || program==NW_VU_PTL || program==NW_VU_PTL_GENERAL ||
                          program==NW_VU_GEM || program==NW_VU_SKNGEM)) {
        SDL_SetError("ATS UV/mips require an image material, not particles or a render surface"); return 0;
    }
    bool effects = m->fx[0] != 0.0f && program != NW_VU_PTL && program != NW_VU_PTL_GENERAL &&
                   program != NW_VU_GEM && program != NW_VU_SKNGEM;
    bool ats = m->uv.binding != 0;
    int variant = effects + 2 * ats;
    if (!nw_vu_variants[variant][program].prog && !nw_vu_link(program, effects, ats)) {
        SDL_Log("vu: effect program: %s", SDL_GetError());
        nw_vu_variants[variant][program].prog = 0;
        return 0;
    }
    nw_vu[program] = &nw_vu_variants[variant][program];
    glUseProgram(nw_vu[program]->prog);
    glUniform1i(nw_vu[program]->black, 0);
    glUniformMatrix4fv(nw_vu[program]->lv, 1, GL_FALSE, o->lv);
    glUniform4fv(nw_vu[program]->diffuse, 1, m->diffuse);
    glUniform4fv(nw_vu[program]->fx, 4, m->fx);
    glUniform4fv(nw_vu[program]->ambient, 1, o->ambient);
    glUniform4fv(nw_vu[program]->lightdir, 3, o->lightdir[0]);
    glUniform4fv(nw_vu[program]->lightcol, 3, o->lightcol[0]);
    glUniform4fv(nw_vu[program]->uvoff, 1, m->uvoff);
    glUniform4fv(nw_vu[program]->sprite, 1, o->sprite);
    glUniform4fv(nw_vu[program]->uv, 4, o->uv[0]);
    glUniform4fv(nw_vu[program]->vu, 2, m->vu);
    glUniform4fv(nw_vu[program]->frag, 1, o->frag);
    if (m->uv.binding) {
        const float *v=m->uv.value; float degrees=remainderf(v[4],360.f), angle=degrees*0.01745329252f;
        float c=cosf(angle), s=sinf(angle);
        if (remainderf(degrees,90.f)==0) {
            int q=(int)(degrees/90.f); c=q==0 ? 1 : q==2 || q==-2 ? -1 : 0; s=q==1 ? 1 : q==-1 ? -1 : 0;
        }
        float a[6]={c*v[2],-s*v[3],0,s*v[2],c*v[3],0};
        a[2]=v[5]+v[0]-a[0]*v[5]-a[1]*v[6]; a[5]=v[6]+v[1]-a[3]*v[5]-a[4]*v[6];
        for (int j=0; j<6; j++) if (!isfinite(a[j])) { SDL_SetError("UV transform exceeds renderer precision"); return 0; }
        glUniform3fv(nw_vu[program]->atsuv,2,a);
    }
    glUniform2i(nw_vu[program]->atsmode,m->uv.binding ? m->uv.address : -1,
                m->uv.binding ? (m->uv.mips ? (int)m->tex->levels-1 : 0) : -1);
    const NwVuZClut *z = m->zclut;
    bool zonly = z && !z->alpha && !z->ad;
    bool textured = !zonly && m->tex && m->tex->index;
    glUniform4i(nw_vu[program]->texmode,
                zonly      ? 4
                : textured ? (m->tex->surface ? (m->tex->filtered ? 5 : 3) : m->tex->clut ? 1 : 2)
                           : 0,
                m->state.mmag, m->state.wms == 1, m->state.wmt == 1);
    glUniform4i(nw_vu[program]->texlod, m->state.mmin, m->state.mxl, m->state.l, m->state.k);
    glUniform1i(nw_vu[program]->filter, (GLint)nw_vu_filter_mode);
    glUniform1i(nw_vu[program]->tcc, (z && !z->ad) || (textured && m->tex->tcc));
    glUniform1i(nw_vu[program]->rgba8, textured && m->tex->rgba8 && !(z && z->alpha));
    glUniform1i(nw_vu[program]->layer, array && textured && !zonly ? m->layer : -1);
    bool particle = program == NW_VU_PTL || program == NW_VU_PTL_GENERAL;
    if (particle) {
        bool alpha = textured && m->tex->tcc;
        int lo = alpha ? m->tex->alpha_min : 128, hi = alpha ? m->tex->alpha_max : 128;
        if (alpha && m->tex->rgba8) { lo = lo*128/255; hi = (hi*128+254)/255; }
        glUniform2i(nw_vu[program]->texalpha,lo,hi);
    }
    glUniform1i(nw_vu[program]->zalpha, z && z->alpha);
    glUniform1i(nw_vu[program]->zad, z && z->ad ? z->ad - 1 : -1);
    if (z) {
        glUniform4ui(nw_vu[program]->zclut, z->shift, z->base, z->far, z->near);
        glUniform3ui(nw_vu[program]->zclear, z->clear[0], z->clear[1], z->clear[2]);
        glActiveTexture(GL_TEXTURE0 + 6);
        glBindTexture(GL_TEXTURE_2D, z->depth);
        glActiveTexture(GL_TEXTURE0 + 7);
        glBindTexture(GL_TEXTURE_2D, z->clut);
        glActiveTexture(GL_TEXTURE0);
    }
    NwMtlBlend b = { 0 };
    uint8_t a = m->state.alpha;
    if (m->state.abe && m->dst) {
        glDisable(GL_BLEND);
        glUniform4i(nw_vu[program]->gsblend, a & 3, (a >> 2) & 3, (a >> 4) & 3, (a >> 6) & 3);
        glActiveTexture(GL_TEXTURE0 + 8);
        glBindTexture(GL_TEXTURE_2D, m->dst->index);
        glActiveTexture(GL_TEXTURE0);
    } else if (m->state.abe && nw_mtl_blend(m->state.alpha, &b)) {
        static const GLenum ops[] = { GL_FUNC_ADD, GL_FUNC_SUBTRACT, GL_FUNC_REVERSE_SUBTRACT };
        static const GLenum factors[] = { GL_ZERO,      GL_ONE,       GL_SRC_ALPHA,          GL_ONE_MINUS_SRC_ALPHA,
                                          GL_DST_COLOR, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA };
        glEnable(GL_BLEND);
        glBlendEquationSeparate(ops[b.op], GL_FUNC_ADD);
        /* The GS writes As to the frame's alpha unblended. */
        glBlendFuncSeparate(factors[b.src], factors[b.dst], GL_ONE, GL_ZERO);
    } else {
        glDisable(GL_BLEND);
    }
    if (!m->state.abe || !m->dst) {
        glUniform4i(nw_vu[program]->gsblend, -1, -1, -1, -1);
    }
    glUniform1i(nw_vu[program]->ascolour, m->state.abe && b.as_colour);
    /* The GS keeps the larger z; the screen matrices give GL the smaller one for the nearer point. */
    static const GLenum ztst[4] = { GL_NEVER, GL_ALWAYS, GL_LEQUAL, GL_LESS };
    glDepthFunc(ztst[m->state.ztst & 3]);
    if (textured && particle && m->tex->rgba) {
        glBindTexture(GL_TEXTURE_2D, m->tex->rgba);
    } else if (textured && array) {
        glActiveTexture(GL_TEXTURE0 + 10);
        glBindTexture(GL_TEXTURE_2D_ARRAY, m->tex->index);
        glActiveTexture(GL_TEXTURE0);
    } else if (textured && m->tex->surface) {
        glActiveTexture(GL_TEXTURE0 + 2);
        glBindTexture(GL_TEXTURE_2D, m->tex->index);
        glActiveTexture(GL_TEXTURE0);
    } else if (textured) {
        glActiveTexture(GL_TEXTURE0 + 1);
        glBindTexture(GL_TEXTURE_2D, m->tex->clut);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m->tex->index);
    }
    return nw_mtl_passes(&m->state, pass);
}

static bool nw_vu_alpha = true;

void nw_vu_gem(NwVuProgram program, const NwVuGem *g)
{
    glUniformMatrix4fv(nw_vu[program]->world, 1, GL_FALSE, g->world);
    glUniformMatrix4fv(nw_vu[program]->vp, 1, GL_FALSE, g->vp);
    glUniform3fv(nw_vu[program]->eye, 1, g->eye);
    glUniformMatrix3fv(nw_vu[program]->cam, 1, GL_FALSE, g->cam);
    glUniform3fv(nw_vu[program]->tint, 1, g->tint);
    glUniform3i(nw_vu[program]->gem, g->kind, g->role, g->panel);
    glUniform4f(nw_vu[program]->viewport, (float)g->viewport[0], (float)g->viewport[1], (float)g->viewport[2],
                (float)g->viewport[3]);
}

void nw_vu_alpha_writes(bool on)
{
    nw_vu_alpha = on;
}

void nw_vu_filter(NwTexFilter filter)
{
    nw_vu_filter_mode = filter;
}

void nw_vu_pass(NwVuProgram program, const NwVuMaterial *m, const NwMtlPass *pass)
{
    glUniform4i(nw_vu[program]->atest, m->state.ate ? m->state.atst : 1, m->state.aref, pass->failed, 0);
    if (program == NW_VU_PTL || program == NW_VU_PTL_GENERAL) {
        NwMtlBlend blend;
        uint8_t a = m->state.alpha;
        bool keep = m->state.abe && ((a >> 4) & 3) == 0 && (a >> 6) == 1 && nw_mtl_blend(a, &blend);
        glUniform1i(nw_vu[program]->zero, keep && !pass->z && !(pass->alpha && nw_vu_alpha));
    }
    glColorMask(pass->rgb, pass->rgb, pass->rgb, pass->alpha && nw_vu_alpha);
    glDepthMask(pass->z);
}

/* Black over the frame where the program draws, as solid as the blend would make it: a blended material (feathers,
   hair: cards whose texture's alpha draws the shape) covers inside its shape, a short ramp at its edge, nothing past
   it; any other all over. Its alpha test still cuts it out. Blends over the frame, leaving its alpha. */
void nw_vu_black(NwVuProgram program, bool blended)
{
    glUniform1i(nw_vu[program]->black, blended ? 2 : 1);
    glEnable(GL_BLEND);
    glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
}

/* Integer textures are only read by texelFetch; a mip chain needs a mipmap MIN_FILTER to be complete. */
static void nw_vu_upload(GLuint name, GLint format, GLenum layout, const NwTexLevel *levels, uint32_t n)
{
    glBindTexture(GL_TEXTURE_2D, name);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, n > 1 ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, (GLint)n - 1);
    for (uint32_t i = 0; i < n; i++) {
        glTexImage2D(GL_TEXTURE_2D, (GLint)i, format, (GLsizei)levels[i].width, (GLsizei)levels[i].height, 0, layout,
                     GL_UNSIGNED_BYTE, levels[i].texels);
    }
}

/* A texture of no levels is a CLUT alone, which a draw puts over its material texture's. */
void nw_vu_texture(NwVuTexture *tex, const NwTex *t)
{
    if (t->layers) {
        *tex = (NwVuTexture){.tcc=t->tcc,.rgba8=t->rgba8,.layers=t->layers,.levels=t->nlevels};
        glPixelStorei(GL_UNPACK_ALIGNMENT,1);
        glActiveTexture(GL_TEXTURE0 + 10);
        glGenTextures(1,&tex->index);
        glBindTexture(GL_TEXTURE_2D_ARRAY,tex->index);
        glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MIN_FILTER,t->nlevels>1 ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MAX_LEVEL,(GLint)t->nlevels-1);
        for (uint32_t n=0; n<t->nlevels; n++) glTexImage3D(GL_TEXTURE_2D_ARRAY,(GLint)n,GL_RGBA8UI,
            (GLsizei)t->levels[n].width,(GLsizei)t->levels[n].height,(GLsizei)t->layers,0,
            GL_RGBA_INTEGER,GL_UNSIGNED_BYTE,t->levels[n].texels);
        GLenum error = glGetError();
        if (error) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER,"texture array upload failed: GL 0x%04x",error);
            glDeleteTextures(1,&tex->index); tex->index = 0;
        }
        glActiveTexture(GL_TEXTURE0);
        return;
    }
    GLuint names[2] = { 0, 0 };
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glActiveTexture(GL_TEXTURE0);
    if (t->nlevels) {
        glGenTextures(1, &names[0]);
        nw_vu_upload(names[0], t->ncolours ? GL_R8UI : GL_RGBA8UI, t->ncolours ? GL_RED_INTEGER : GL_RGBA_INTEGER,
                     t->levels, t->nlevels);
    }
    if (t->ncolours) {
        const NwTexLevel clut = { 256, 1, (uint8_t *)t->clut };
        glGenTextures(1, &names[1]);
        nw_vu_upload(names[1], GL_RGBA8UI, GL_RGBA_INTEGER, &clut, 1);
    }
    *tex = (NwVuTexture){ .index = names[0], .clut = names[1], .tcc = t->tcc, .rgba8 = t->rgba8, .levels=t->nlevels };
}

void nw_vu_texture_free(NwVuTexture *tex)
{
    const GLuint names[3] = { tex->index, tex->clut, tex->rgba == tex->index ? 0 : tex->rgba };
    glDeleteTextures(3, names);
    tex->index = tex->clut = tex->rgba = 0;
}

bool nw_vu_texture_rgba(NwVuTexture *tex, const NwTex *t)
{
    if (t->layers) return SDL_SetError("texture arrays are not particle textures");
    if (tex->rgba || !t->nlevels) {
        return true;
    }
    GLuint name = tex->index;
    if (t->ncolours) {
        glGenTextures(1, &name);
        glActiveTexture(GL_TEXTURE0 + 2);
        glBindTexture(GL_TEXTURE_2D, name);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, (GLint)t->nlevels - 1);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    unsigned alpha_min = 255, alpha_max = 0;
    tex->power_of_two = true;
    for (uint32_t l = 0; l < t->nlevels; l++) {
        const NwTexLevel *level = &t->levels[l];
        size_t pixels = (size_t)level->width * level->height;
        tex->power_of_two &= !(level->width & (level->width - 1)) && !(level->height & (level->height - 1));
        uint8_t *rgba = t->ncolours ? SDL_malloc(pixels * 4) : level->texels;
        if (!rgba) {
            glDeleteTextures(1, &name);
            glActiveTexture(GL_TEXTURE0);
            return false;
        }
        if (t->ncolours) {
            for (size_t i = 0; i < pixels; i++) {
                memcpy(rgba + i * 4, t->clut[level->texels[i]], 4);
            }
        }
        for (size_t i = 0; i < pixels; i++) {
            alpha_min = SDL_min(alpha_min, rgba[i * 4 + 3]);
            alpha_max = SDL_max(alpha_max, rgba[i * 4 + 3]);
        }
        if (t->ncolours) {
            glTexImage2D(GL_TEXTURE_2D, (GLint)l, GL_RGBA8UI, (GLsizei)level->width, (GLsizei)level->height, 0,
                         GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, rgba);
            SDL_free(rgba);
        }
    }
    tex->rgba = name;
    tex->alpha_min = alpha_min;
    tex->alpha_max = alpha_max;
    glActiveTexture(GL_TEXTURE0);
    return true;
}

void nw_vu_attribs(void)
{
    const GLsizei stride = sizeof(NwVifVertex);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, stride, (const void *)offsetof(NwVifVertex, pos));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_SHORT, GL_FALSE, stride, (const void *)offsetof(NwVifVertex, tex));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_FALSE, stride, (const void *)offsetof(NwVifVertex, col));
}

/* The weights as FTOI15 integers. */
void nw_vu_skin_attribs(void)
{
    const GLsizei stride = sizeof(NwVifSkinVertex);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (const void *)offsetof(NwVifSkinVertex, pos));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (const void *)offsetof(NwVifSkinVertex, nrm));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (const void *)offsetof(NwVifSkinVertex, uv));
    for (GLuint i = 0; i < 2; i++) {
        glEnableVertexAttribArray(3 + i);
        glVertexAttribIPointer(3 + i, 4, GL_UNSIGNED_SHORT, stride,
                               (const void *)(offsetof(NwVifSkinVertex, weight) + i * 8));
        glEnableVertexAttribArray(5 + i);
        glVertexAttribIPointer(5 + i, 4, GL_UNSIGNED_BYTE, stride,
                               (const void *)(offsetof(NwVifSkinVertex, joint) + i * 4));
    }
    glEnableVertexAttribArray(7);
    glVertexAttribIPointer(7, 2, GL_UNSIGNED_BYTE, stride, (const void *)offsetof(NwVifSkinVertex, nrmjoint));
    glEnableVertexAttribArray(8);
    glVertexAttribIPointer(8, 1, GL_UNSIGNED_INT, stride, (const void *)offsetof(NwVifSkinVertex, delta));
}

void nw_vu_ptl_attribs(size_t offset)
{
    const GLsizei stride = 8 * sizeof(float);
    for (GLuint i = 0; i < 2; i++) {
        glEnableVertexAttribArray(i);
        glVertexAttribPointer(i, 4, GL_FLOAT, GL_FALSE, stride, (const void *)(offset + i * 4 * sizeof(float)));
        glVertexAttribDivisor(i, 1);
    }
}

void nw_vu_sprite_attribs(void)
{
    const GLsizei stride = sizeof(NwVuSpriteVertex);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, stride, (const void *)offsetof(NwVuSpriteVertex, pos));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, (const void *)offsetof(NwVuSpriteVertex, tex));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_FALSE, stride, (const void *)offsetof(NwVuSpriteVertex, col));
}
