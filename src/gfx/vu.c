#include "gfx/vu.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "gfx/gl.h"
#include "gfx/vif.h"

/* Row-major PS2 matrices uploaded as they are: GL reads them transposed, which makes u_lv * v the VU's v x LV. */
static const char nw_vu_vs[] =
    "layout(location = 0) in vec4 a_pos;\n" /* VF06: x, y, z, the normal's x */
    "layout(location = 1) in vec4 a_tex;\n" /* VF08 before ITOF12: u, v, the normal's y, z */
    "layout(location = 2) in vec4 a_col;\n" /* VF31 before ITOF0 */
    "uniform mat4 u_lv;\n"
    "uniform vec4 u_diffuse;\n"
    "uniform vec4 u_ambient;\n"
    "uniform vec4 u_lightdir[3];\n"
    "uniform vec4 u_lightcol[3];\n"
    "uniform vec4 u_uvoff;\n" /* VU 0x14 u, v; the STROW row u, v (x4096) the texcoords add under STMOD 1 */
    "uniform vec4 u_sprite;\n"
    "out vec4 v_col;\n"
    "out vec2 v_uv;\n"
    "void main()\n"
    "{\n"
    /* faceon adds its corners in GS units before the divide; facey spans its quad along VF15 and the object's y. */
    "#if PROGRAM >= 4\n"
    "    vec2 corner = 1.0 - 2.0 * a_tex.xy;\n"
    "#if PROGRAM == 4\n"
    "    gl_Position = u_lv * vec4(a_pos.xyz, 1.0);\n"
    "    gl_Position.xy += corner * a_tex.zw * u_sprite.xy;\n"
    "#else\n"
    "    vec3 p = a_pos.xyz - corner.x * a_tex.z * u_sprite.xyz + vec3(0.0, corner.y * a_tex.w, 0.0);\n"
    "    gl_Position = u_lv * vec4(p, 1.0);\n"
    "#endif\n"
    "    v_uv = a_tex.xy;\n"
    "    v_col = a_col;\n"
    "#else\n"
    "    gl_Position = u_lv * vec4(a_pos.xyz, 1.0);\n"
    "    vec3 n = vec3(a_pos.w, a_tex.zw / 4096.0);\n"
    "    vec3 nl = max(vec3(dot(u_lightdir[0].xyz, n), dot(u_lightdir[1].xyz, n), dot(u_lightdir[2].xyz, n)), 0.0);\n"
    "    vec4 lit = u_lightcol[0] * nl.x + u_lightcol[1] * nl.y + u_lightcol[2] * nl.z;\n"
    "    v_uv = (a_tex.xy + u_uvoff.zw) / 4096.0;\n"
    "#if PROGRAM == 0\n" /* tri, tric: vertex colour plus the lit material diffuse */
    "    v_col = vec4(min(a_col.rgb + u_diffuse.rgb * (u_ambient.rgb + lit.rgb), 255.0), a_col.a);\n"
    "#elif PROGRAM == 1\n" /* tricu: unlit */
    "    v_col = a_col;\n"
    "#elif PROGRAM == 2\n" /* trica: the lights' w scale the alpha */
    "    v_col = vec4(a_col.rgb, a_col.a * min(lit.a, 1.0));\n"
    "    v_uv += u_uvoff.xy;\n"
    "#else\n" /* tricdv: the light modulates the vertex colour */
    "    v_col = vec4(min((u_ambient.rgb + lit.rgb) * a_col.rgb, 255.0), a_col.a);\n"
    "    v_uv += u_uvoff.xy;\n"
    "#endif\n"
    "#endif\n"
    "}\n";

/* The GS texture unit: CLUT lookups in GS units, CLAMP/REPEAT, the TEX1 filters and LOD over the mip levels. */
static const char nw_vu_fs[] =
    "in vec4 v_col;\n"
    "in vec2 v_uv;\n"
    "uniform usampler2D u_tex;\n"
    "uniform usampler2D u_clut;\n"
    "uniform sampler2D u_surf;\n"
    "uniform ivec4 u_texmode;\n" /* 0 untextured, 1 CLUT, 2 direct colour, 3 surface; MMAG; CLAMP u; CLAMP v */
    "uniform ivec4 u_texlod;\n"  /* MMIN, MXL, L, K (1/16) */
    "uniform int u_tcc;\n"
    "uniform int u_ascolour;\n" /* the blend needs As as the source colour */
    "uniform ivec4 u_atest;\n"  /* ATST (1 ALWAYS without ATE), AREF, the pass keeps the failing pixels */
    "out vec4 o_col;\n"
    "ivec2 tex_size(int lod)\n"
    "{\n"
    "    return u_texmode.x == 3 ? textureSize(u_surf, 0) : textureSize(u_tex, lod);\n"
    "}\n"
    "vec4 texel(ivec2 p, int lod)\n"
    "{\n"
    "    ivec2 size = tex_size(lod);\n"
    /* GS sizes are powers of two (TW, TH); a mask wraps negative coordinates too, where % is undefined. */
    "    p = ivec2(u_texmode.z != 0 ? clamp(p.x, 0, size.x - 1) : p.x & (size.x - 1),\n"
    "              u_texmode.w != 0 ? clamp(p.y, 0, size.y - 1) : p.y & (size.y - 1));\n"
    "    if (u_texmode.x == 3) {\n"
    "        vec4 s = texelFetch(u_surf, ivec2(p.x, size.y - 1 - p.y), 0);\n"
    "        return floor(vec4(s.rgb * 255.0, s.a * 128.0) + 0.5);\n"
    "    }\n"
    "    uvec4 c = texelFetch(u_tex, p, lod);\n"
    "    return vec4(u_texmode.x == 2 ? c : texelFetch(u_clut, ivec2(int(c.r), 0), 0));\n"
    "}\n"
    "vec4 sample_level(vec2 uv, int lod, bool bilinear)\n"
    "{\n"
    "    vec2 t = uv * vec2(tex_size(lod));\n"
    "    if (!bilinear) {\n"
    "        return texel(ivec2(floor(t)), lod);\n"
    "    }\n"
    "    t -= 0.5;\n"
    "    ivec2 i = ivec2(floor(t));\n"
    "    vec2 f = t - floor(t);\n"
    "    return mix(mix(texel(i, lod), texel(i + ivec2(1, 0), lod), f.x),\n"
    "               mix(texel(i + ivec2(0, 1), lod), texel(i + 1, lod), f.x), f.y);\n"
    "}\n"
    /* TEX1 with LCM 0: LOD = log2(1/Q) << L + K; MMAG up to LOD 0, MMIN past it. */
    "vec4 sample_gs(vec2 uv)\n"
    "{\n"
    "    float lod = log2(1.0 / gl_FragCoord.w) * float(1 << u_texlod.z) + float(u_texlod.w) / 16.0;\n"
    "    if (lod <= 0.0 || u_texlod.x < 2) {\n"
    "        return sample_level(uv, 0, (lod <= 0.0 ? u_texmode.y : u_texlod.x) == 1);\n"
    "    }\n"
    "    bool bilinear = u_texlod.x >= 4;\n"
    "    lod = min(lod, float(u_texlod.y));\n"
    "    if (u_texlod.x == 3 || u_texlod.x == 5) {\n"
    "        int l0 = int(lod);\n"
    "        return mix(sample_level(uv, l0, bilinear), sample_level(uv, min(l0 + 1, u_texlod.y), bilinear),\n"
    "                   fract(lod));\n"
    "    }\n"
    "    return sample_level(uv, min(int(lod + 0.5), u_texlod.y), bilinear);\n"
    "}\n"
    "bool atest(int a)\n"
    "{\n"
    "    switch (u_atest.x) {\n"
    "    case 0: return false;\n"
    "    case 1: return true;\n"
    "    case 2: return a < u_atest.y;\n"
    "    case 3: return a <= u_atest.y;\n"
    "    case 4: return a == u_atest.y;\n"
    "    case 5: return a >= u_atest.y;\n"
    "    case 6: return a > u_atest.y;\n"
    "    default: return a != u_atest.y;\n"
    "    }\n"
    "}\n"
    /* TFX MODULATE in GS units (0x80 = 1.0): Cf = Cv * Ct >> 7, Af the same with TCC; untextured Cf = Cv. */
    "void main()\n"
    "{\n"
    "    vec4 c = clamp(floor(v_col), 0.0, 255.0);\n"
    "    if (u_texmode.x != 0) {\n"
    "        vec4 t = floor(sample_gs(v_uv));\n"
    "        c.rgb = min(floor(c.rgb * t.rgb / 128.0), 255.0);\n"
    "        c.a = u_tcc != 0 ? min(floor(c.a * t.a / 128.0), 255.0) : c.a;\n"
    "    }\n"
    "    if (atest(int(c.a)) == (u_atest.z != 0)) {\n"
    "        discard;\n"
    "    }\n"
    "    o_col = vec4(u_ascolour != 0 ? vec3(c.a / 128.0) : c.rgb / 255.0, c.a / 128.0);\n"
    "}\n";

static struct {
    GLuint prog;
    GLint lv, diffuse, ambient, lightdir, lightcol, uvoff, sprite, texmode, texlod, tcc, ascolour, atest;
} nw_vu[NW_VU_PROGRAMS];

static GLuint nw_vu_shader(GLenum type, NwVuProgram program, const char *src)
{
    char head[64];
    SDL_snprintf(head, sizeof(head), "#version 330 core\n#define PROGRAM %d\n", (int)program);
    const GLchar *parts[2] = { head, src };
    GLuint s = glCreateShader(type);
    GLint ok = 0;
    glShaderSource(s, 2, parts, NULL);
    glCompileShader(s);
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = "";
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        SDL_SetError("vu: shader: %s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static bool nw_vu_link(NwVuProgram program)
{
    GLuint vs = nw_vu_shader(GL_VERTEX_SHADER, program, nw_vu_vs);
    GLuint fs = vs ? nw_vu_shader(GL_FRAGMENT_SHADER, program, nw_vu_fs) : 0;
    if (!fs) {
        glDeleteShader(vs);
        return false;
    }
    GLint ok = 0;
    GLuint p = nw_vu[program].prog = glCreateProgram();
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
    nw_vu[program].lv = glGetUniformLocation(p, "u_lv");
    nw_vu[program].diffuse = glGetUniformLocation(p, "u_diffuse");
    nw_vu[program].ambient = glGetUniformLocation(p, "u_ambient");
    nw_vu[program].lightdir = glGetUniformLocation(p, "u_lightdir");
    nw_vu[program].lightcol = glGetUniformLocation(p, "u_lightcol");
    nw_vu[program].uvoff = glGetUniformLocation(p, "u_uvoff");
    nw_vu[program].sprite = glGetUniformLocation(p, "u_sprite");
    nw_vu[program].texmode = glGetUniformLocation(p, "u_texmode");
    nw_vu[program].texlod = glGetUniformLocation(p, "u_texlod");
    nw_vu[program].tcc = glGetUniformLocation(p, "u_tcc");
    nw_vu[program].ascolour = glGetUniformLocation(p, "u_ascolour");
    nw_vu[program].atest = glGetUniformLocation(p, "u_atest");
    glUseProgram(p);
    glUniform1i(glGetUniformLocation(p, "u_tex"), 0);
    glUniform1i(glGetUniformLocation(p, "u_clut"), 1);
    glUniform1i(glGetUniformLocation(p, "u_surf"), 2);
    return true;
}

bool nw_vu_init(void)
{
    for (int i = 0; i < NW_VU_PROGRAMS; i++) {
        if (!nw_vu_link((NwVuProgram)i)) {
            nw_vu_shutdown();
            return false;
        }
    }
    return true;
}

void nw_vu_shutdown(void)
{
    for (int i = 0; i < NW_VU_PROGRAMS; i++) {
        glDeleteProgram(nw_vu[i].prog);
        nw_vu[i].prog = 0;
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
    };
    for (size_t i = 0; package && i < SDL_arraysize(map); i++) {
        if (strcmp(package, map[i].name) == 0) {
            return map[i].program;
        }
    }
    return NW_VU_TRIC;
}

uint32_t nw_vu_use(NwVuProgram program, const NwVuObject *o, const NwVuMaterial *m, NwMtlPass pass[2])
{
    glUseProgram(nw_vu[program].prog);
    glUniformMatrix4fv(nw_vu[program].lv, 1, GL_FALSE, o->lv);
    glUniform4fv(nw_vu[program].diffuse, 1, m->diffuse);
    glUniform4fv(nw_vu[program].ambient, 1, o->ambient);
    glUniform4fv(nw_vu[program].lightdir, 3, o->lightdir[0]);
    glUniform4fv(nw_vu[program].lightcol, 3, o->lightcol[0]);
    glUniform4fv(nw_vu[program].uvoff, 1, m->uvoff);
    glUniform4fv(nw_vu[program].sprite, 1, o->sprite);
    bool textured = m->tex && m->tex->index;
    glUniform4i(nw_vu[program].texmode, textured ? (m->tex->surface ? 3 : m->tex->clut ? 1 : 2) : 0, m->state.mmag,
                m->state.wms == 1,
                m->state.wmt == 1);
    glUniform4i(nw_vu[program].texlod, m->state.mmin, m->state.mxl, m->state.l, m->state.k);
    glUniform1i(nw_vu[program].tcc, textured && m->tex->tcc);
    NwMtlBlend b = { 0 };
    if (m->state.abe && nw_mtl_blend(m->state.alpha, &b)) {
        static const GLenum ops[] = { GL_FUNC_ADD, GL_FUNC_SUBTRACT, GL_FUNC_REVERSE_SUBTRACT };
        static const GLenum factors[] = { GL_ZERO, GL_ONE, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_COLOR };
        glEnable(GL_BLEND);
        glBlendEquation(ops[b.op]);
        glBlendFunc(factors[b.src], factors[b.dst]);
    } else {
        glDisable(GL_BLEND);
    }
    glUniform1i(nw_vu[program].ascolour, m->state.abe && b.as_colour);
    /* The GS keeps the larger z; the screen matrices give GL the smaller one for the nearer point. */
    static const GLenum ztst[4] = { GL_NEVER, GL_ALWAYS, GL_LEQUAL, GL_LESS };
    glDepthFunc(ztst[m->state.ztst & 3]);
    if (textured && m->tex->surface) {
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

void nw_vu_pass(NwVuProgram program, const NwVuMaterial *m, const NwMtlPass *pass)
{
    glUniform4i(nw_vu[program].atest, m->state.ate ? m->state.atst : 1, m->state.aref, pass->failed, 0);
    glColorMask(pass->rgb, pass->rgb, pass->rgb, pass->alpha);
    glDepthMask(pass->z);
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

void nw_vu_texture(NwVuTexture *tex, const NwTex *t)
{
    GLuint names[2] = { 0, 0 };
    glGenTextures(t->ncolours ? 2 : 1, names);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glActiveTexture(GL_TEXTURE0);
    nw_vu_upload(names[0], t->ncolours ? GL_R8UI : GL_RGBA8UI, t->ncolours ? GL_RED_INTEGER : GL_RGBA_INTEGER,
                 t->levels, t->nlevels);
    if (t->ncolours) {
        const NwTexLevel clut = { 256, 1, (uint8_t *)t->clut };
        nw_vu_upload(names[1], GL_RGBA8UI, GL_RGBA_INTEGER, &clut, 1);
    }
    tex->index = names[0];
    tex->clut = names[1];
    tex->tcc = t->tcc;
    tex->surface = false;
}

void nw_vu_texture_free(NwVuTexture *tex)
{
    const GLuint names[2] = { tex->index, tex->clut };
    glDeleteTextures(2, names);
    tex->index = tex->clut = 0;
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
