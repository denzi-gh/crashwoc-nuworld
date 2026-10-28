#include "gfx/vu.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "gfx/gl.h"
#include "gfx/vif.h"

/* Row-major PS2 matrices uploaded as they are: GL reads them transposed, which makes u_lv * v the VU's v x LV. */
static const char nw_vu_vs[] =
    "#if PROGRAM == SKIN || PROGRAM == SKINXUV\n"
    "layout(location = 0) in vec3 a_pos;\n"
    "layout(location = 1) in vec3 a_nrm;\n"
    "layout(location = 2) in vec2 a_uv;\n"
    "layout(location = 3) in uvec4 a_weight0;\n" /* FTOI15 */
    "layout(location = 4) in uvec4 a_weight1;\n"
    "layout(location = 5) in uvec4 a_joint0;\n"
    "layout(location = 6) in uvec4 a_joint1;\n"
    "layout(location = 7) in uvec2 a_nrmjoint;\n" /* the normal's joint, the delta count */
    "layout(location = 8) in uint a_delta;\n"     /* the first delta */
    "uniform samplerBuffer u_pool;\n"             /* the packet's palettes, a matrix as its four rows */
    "uniform samplerBuffer u_weights;\n"          /* the packet's blend-shape weights */
    "uniform samplerBuffer u_deltas;\n"           /* the geom's deltas: x, y, z, shape */
    "uniform int u_joints;\n"                     /* the draw's first row */
    "uniform ivec2 u_blend;\n"                    /* the draw's first weight and their count */
    "#else\n"
    "layout(location = 0) in vec4 a_pos;\n" /* VF06: x, y, z, the normal's x */
    "layout(location = 1) in vec4 a_tex;\n" /* VF08 before ITOF12: u, v, the normal's y, z */
    "layout(location = 2) in vec4 a_col;\n" /* VF31 before ITOF0 */
    "#endif\n"
    "uniform mat4 u_lv;\n"
    "uniform vec4 u_diffuse;\n"
    "uniform vec4 u_ambient;\n"
    "uniform vec4 u_lightdir[3];\n"
    "uniform vec4 u_lightcol[3];\n"
    "uniform vec4 u_uvoff;\n" /* VU 0x14 u, v; the STROW row u, v (x4096) the texcoords add under STMOD 1 */
    "uniform vec4 u_sprite;\n"
    "uniform vec4 u_uv[4];\n" /* the xuv programs' VF24-27 */
    "#if PROGRAM == PTL\n"
    "uniform uvec4 u_ptl[132];\n" /* VU 0x3C-0xBF */
    "#endif\n"
    "out vec4 v_col;\n"
    "out vec2 v_uv;\n"
    "#if PROGRAM == SKIN || PROGRAM == SKINXUV\n"
    "vec3 row(uint joint, int r)\n"
    "{\n"
    "    return texelFetch(u_pool, u_joints + int(joint) * 4 + r).xyz;\n"
    "}\n"
    "#endif\n"
    "void main()\n"
    "{\n"
    /* ptl.vsm: a particle aged t = time - birth falls under gravity; k = rate * t picks the table entry whose offsets,
       lerped to the next by k's fraction, its corners add in GS units before the divide, in the order (v0, uv0),
       (v2, uv2), (v0 + v1, uv1), (v2 + v1, uv3) that strips them; the entry's colour is not lerped. Dead before its
       birth or past entry 62.9. */
    "#if PROGRAM == PTL\n"
    "    const int strip[6] = int[6](0, 1, 2, 1, 2, 3);\n"
    "    int corner = strip[gl_VertexID];\n"
    "    vec4 head = uintBitsToFloat(u_ptl[0]);\n"
    "    float t = head.w - a_pos.w;\n"
    "    float k = a_tex.w * t;\n"
    "    if (t < 0.0 || k > 62.9) {\n"
    "        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);\n"
    "        return;\n"
    "    }\n"
    "    int i = int(k) & 63;\n"
    "    float f = k - float(int(k));\n"
    "    vec4 v01 = mix(uintBitsToFloat(u_ptl[4 + 2 * i]), uintBitsToFloat(u_ptl[4 + 2 * min(i + 1, 63)]), f);\n"
    "    vec2 v2 = mix(uintBitsToFloat(u_ptl[5 + 2 * i].xy), uintBitsToFloat(u_ptl[5 + 2 * min(i + 1, 63)].xy), f);\n"
    "    vec2 offset = (corner & 1) != 0 ? v2 : v01.xy;\n"
    "    if (corner >= 2) {\n"
    "        offset += v01.zw;\n"
    "    }\n"
    "    vec3 p = a_pos.xyz + a_tex.xyz * t + vec3(0.0, head.y * t * t, 0.0);\n"
    "    gl_Position = u_lv * vec4(p, 1.0);\n"
    "    gl_Position.xy += offset * u_sprite.xy;\n"
    "    const ivec2 uv[4] = ivec2[4](ivec2(1, 0), ivec2(2, 0), ivec2(1, 2), ivec2(2, 2));\n"
    "    uvec4 corners = u_ptl[uv[corner].x];\n"
    "    uvec2 st = uv[corner].y == 0 ? corners.xy : corners.zw;\n"
    "    v_uv = vec2(st & 0x3FFFu) * u_sprite.zw;\n"
    "    v_col = vec4(u_ptl[4 + 2 * i] & 0xFFu);\n"
    /* faceon adds its corners in GS units before the divide; facey spans its quad along VF15 and the object's y. */
    "#elif PROGRAM == FACEON || PROGRAM == FACEY\n"
    "    vec2 corner = 1.0 - 2.0 * a_tex.xy;\n"
    "#if PROGRAM == FACEON\n"
    "    gl_Position = u_lv * vec4(a_pos.xyz, 1.0);\n"
    "    gl_Position.xy += corner * a_tex.zw * u_sprite.xy;\n"
    "#else\n"
    "    vec3 p = a_pos.xyz - corner.x * a_tex.z * u_sprite.xyz + vec3(0.0, corner.y * a_tex.w, 0.0);\n"
    "    gl_Position = u_lv * vec4(p, 1.0);\n"
    "#endif\n"
    "    v_uv = a_tex.xy;\n"
    "    v_col = a_col;\n"
    "#elif PROGRAM == SKIN || PROGRAM == SKINXUV\n"
    /* skinmod.vsm first: each delta of a shape with a weight moves the point. Then skin.vsm: the weights (not
       normalised) sum the point under each of its joints' palette matrices, the normal turns under the heaviest joint
       alone. skin lights the material diffuse, with no vertex colour; skinxuv (as trixuv) takes the material colour
       unlit and its texcoords from the normal. */
    "    vec3 q = a_pos;\n"
    "    for (uint k = 0u; k < a_nrmjoint.y && u_blend.y > 0; k++) {\n"
    "        vec4 d = texelFetch(u_deltas, int(a_delta + k));\n"
    "        if (int(d.w) < u_blend.y) {\n"
    "            q += texelFetch(u_weights, u_blend.x + int(d.w)).x * d.xyz;\n"
    "        }\n"
    "    }\n"
    "    vec3 p = vec3(0.0);\n"
    "    for (int k = 0; k < 8; k++) {\n"
    "        uint j = k < 4 ? a_joint0[k] : a_joint1[k - 4];\n"
    "        float w = float(k < 4 ? a_weight0[k] : a_weight1[k - 4]) / 32768.0;\n"
    "        p += w * (q.x * row(j, 0) + q.y * row(j, 1) + q.z * row(j, 2) + row(j, 3));\n"
    "    }\n"
    "    gl_Position = u_lv * vec4(p, 1.0);\n"
    "    vec3 n = a_nrm.x * row(a_nrmjoint.x, 0) + a_nrm.y * row(a_nrmjoint.x, 1) + a_nrm.z * row(a_nrmjoint.x, 2);\n"
    "#if PROGRAM == SKIN\n"
    "    vec3 nl = max(vec3(dot(u_lightdir[0].xyz, n), dot(u_lightdir[1].xyz, n), dot(u_lightdir[2].xyz, n)), 0.0);\n"
    "    vec4 lit = u_lightcol[0] * nl.x + u_lightcol[1] * nl.y + u_lightcol[2] * nl.z;\n"
    "    v_col = vec4(min(u_diffuse.rgb * (u_ambient.rgb + lit.rgb), 255.0), u_diffuse.a);\n"
    "    v_uv = a_uv + u_uvoff.xy;\n"
    "#else\n"
    "    v_col = u_diffuse;\n"
    "    v_uv = vec2(dot(u_uv[0].xyz, n), dot(u_uv[1].xyz, n)) + u_uv[3].xy;\n"
    "#endif\n"
    "#elif PROGRAM == TRIXUV\n"
    /* trixuv.vsm: the material colour unlit, the texcoords the normal in the UV matrix (specular, environment). */
    "    gl_Position = u_lv * vec4(a_pos.xyz, 1.0);\n"
    "    vec3 n = vec3(a_pos.w, a_tex.zw / 4096.0);\n"
    "    v_col = u_diffuse;\n"
    "    v_uv = vec2(dot(u_uv[0].xyz, n), dot(u_uv[1].xyz, n)) + u_uv[3].xy;\n"
    "#else\n"
    "    gl_Position = u_lv * vec4(a_pos.xyz, 1.0);\n"
    "    vec3 n = vec3(a_pos.w, a_tex.zw / 4096.0);\n"
    "    vec3 nl = max(vec3(dot(u_lightdir[0].xyz, n), dot(u_lightdir[1].xyz, n), dot(u_lightdir[2].xyz, n)), 0.0);\n"
    "    vec4 lit = u_lightcol[0] * nl.x + u_lightcol[1] * nl.y + u_lightcol[2] * nl.z;\n"
    "    v_uv = (a_tex.xy + u_uvoff.zw) / 4096.0;\n"
    "#if PROGRAM == TRIC\n" /* tri, tric: vertex colour plus the lit material diffuse */
    "    v_col = vec4(min(a_col.rgb + u_diffuse.rgb * (u_ambient.rgb + lit.rgb), 255.0), a_col.a);\n"
    "#elif PROGRAM == TRICU\n" /* tricu: unlit */
    "    v_col = a_col;\n"
    "#elif PROGRAM == TRICA\n" /* trica: the lights' w scale the alpha */
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
    "uniform sampler2D u_depth;\n" /* a depth CLUT's: the target's Z */
    "uniform uvec4 u_zclut;\n"     /* shift, base, far, near */
    "uniform uvec3 u_zclear;\n"    /* NuLightFogClear: on, the two Z */
    "uniform ivec4 u_texmode;\n" /* 0 untextured, 1 CLUT, 2 direct colour, 3 surface, 4 depth CLUT; MMAG; CLAMP u, v */
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
    /* The GS Z under the pixel, back from the depth the GS matrix (src/gfx/backend.c) gave it. */
    "vec4 zclut()\n"
    "{\n"
    "    float d = texelFetch(u_depth, ivec2(gl_FragCoord.xy), 0).r;\n"
    "    uint z = uint((1.0 - d) * 8388607.0 + 0.5);\n"
    "    uint i = z < u_zclut.y ? u_zclut.z\n"
    "           : z - u_zclut.y >= (256u << u_zclut.x) ? u_zclut.w : (z >> u_zclut.x) & 0xFFu;\n"
    "    vec4 t = vec4(texelFetch(u_clut, ivec2(int(i), 0), 0));\n"
    "    if (u_zclear.x != 0u && t.a < 128.0 && z <= u_zclear.y) {\n"
    "        t.a = 128.0;\n"
    "    }\n"
    "    if (u_zclear.x != 0u && t.a >= 128.0 && z >= u_zclear.z) {\n"
    "        t.a = 0.0;\n"
    "    }\n"
    "    return t;\n"
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
    "        vec4 t = u_texmode.x == 4 ? zclut() : floor(sample_gs(v_uv));\n"
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
    GLint lv, diffuse, ambient, lightdir, lightcol, uvoff, sprite, texmode, texlod, tcc, ascolour, atest, joints, blend,
        uv, ptl, zclut, zclear;
} nw_vu[NW_VU_PROGRAMS];

static GLuint nw_vu_shader(GLenum type, NwVuProgram program, const char *src)
{
    static const char *const names[NW_VU_PROGRAMS] = { "TRIC", "TRICU", "TRICA", "TRICDV", "FACEON", "FACEY", "SKIN",
                                                       "TRIXUV", "SKINXUV", "PTL" };
    char head[512];
    int n = SDL_snprintf(head, sizeof(head), "#version 330 core\n");
    for (int i = 0; i < NW_VU_PROGRAMS; i++) {
        n += SDL_snprintf(head + n, sizeof(head) - (size_t)n, "#define %s %d\n", names[i], i);
    }
    SDL_snprintf(head + n, sizeof(head) - (size_t)n, "#define PROGRAM %d\n", (int)program);
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
    nw_vu[program].joints = glGetUniformLocation(p, "u_joints");
    nw_vu[program].blend = glGetUniformLocation(p, "u_blend");
    nw_vu[program].uv = glGetUniformLocation(p, "u_uv");
    nw_vu[program].ptl = glGetUniformLocation(p, "u_ptl");
    nw_vu[program].zclut = glGetUniformLocation(p, "u_zclut");
    nw_vu[program].zclear = glGetUniformLocation(p, "u_zclear");
    glUseProgram(p);
    glUniform1i(glGetUniformLocation(p, "u_tex"), 0);
    glUniform1i(glGetUniformLocation(p, "u_clut"), 1);
    glUniform1i(glGetUniformLocation(p, "u_surf"), 2);
    glUniform1i(glGetUniformLocation(p, "u_pool"), 3);
    glUniform1i(glGetUniformLocation(p, "u_weights"), 4);
    glUniform1i(glGetUniformLocation(p, "u_deltas"), 5);
    glUniform1i(glGetUniformLocation(p, "u_depth"), 6);
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
        { "vu_load_xformuv_mpg", NW_VU_TRIXUV },
        { "vu_load_fx2_mpg", NW_VU_TRICU }, /* its triangles at MSCAL 0x306: vertex colour, unlit */
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
    return package && strcmp(package, "vu_load_xformuv_mpg") == 0 ? NW_VU_SKINXUV : NW_VU_PROGRAMS;
}

void nw_vu_skin(NwVuProgram program, uint32_t joints, uint32_t weights, uint32_t nweights)
{
    glUniform1i(nw_vu[program].joints, (GLint)joints * 4);
    glUniform2i(nw_vu[program].blend, (GLint)weights, (GLint)nweights);
}

void nw_vu_ptl(const uint32_t image[132][4])
{
    glUniform4uiv(nw_vu[NW_VU_PTL].ptl, 132, image[0]);
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
    glUniform4fv(nw_vu[program].uv, 4, o->uv[0]);
    const NwVuZClut *z = m->zclut;
    bool textured = !z && m->tex && m->tex->index;
    glUniform4i(nw_vu[program].texmode, z ? 4 : textured ? (m->tex->surface ? 3 : m->tex->clut ? 1 : 2) : 0,
                m->state.mmag, m->state.wms == 1, m->state.wmt == 1);
    glUniform4i(nw_vu[program].texlod, m->state.mmin, m->state.mxl, m->state.l, m->state.k);
    glUniform1i(nw_vu[program].tcc, z || (textured && m->tex->tcc));
    if (z) {
        glUniform4ui(nw_vu[program].zclut, z->shift, z->base, z->far, z->near);
        glUniform3ui(nw_vu[program].zclear, z->clear[0], z->clear[1], z->clear[2]);
        glActiveTexture(GL_TEXTURE0 + 6);
        glBindTexture(GL_TEXTURE_2D, z->depth);
        glActiveTexture(GL_TEXTURE0 + 1);
        glBindTexture(GL_TEXTURE_2D, z->clut);
        glActiveTexture(GL_TEXTURE0);
    }
    NwMtlBlend b = { 0 };
    if (m->state.abe && nw_mtl_blend(m->state.alpha, &b)) {
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

/* A texture of no levels is a CLUT alone, which a draw puts over its material texture's. */
void nw_vu_texture(NwVuTexture *tex, const NwTex *t)
{
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

/* The weights as FTOI15 integers, the joints and the delta range as integers. */
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
