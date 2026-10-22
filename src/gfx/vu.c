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
    "uniform vec4 u_uvoff;\n"
    "out vec4 v_col;\n"
    "out vec2 v_uv;\n"
    "void main()\n"
    "{\n"
    "    gl_Position = u_lv * vec4(a_pos.xyz, 1.0);\n"
    "    vec3 n = vec3(a_pos.w, a_tex.zw / 4096.0);\n"
    "    vec3 nl = max(vec3(dot(u_lightdir[0].xyz, n), dot(u_lightdir[1].xyz, n), dot(u_lightdir[2].xyz, n)), 0.0);\n"
    "    vec4 lit = u_lightcol[0] * nl.x + u_lightcol[1] * nl.y + u_lightcol[2] * nl.z;\n"
    "    v_uv = a_tex.xy / 4096.0;\n"
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
    "}\n";

/* A white texture under the GS modulate rule (0x80 = 1.0) until the materials draw theirs. */
static const char nw_vu_fs[] =
    "in vec4 v_col;\n"
    "in vec2 v_uv;\n"
    "out vec4 o_col;\n"
    "void main()\n"
    "{\n"
    "    o_col = vec4(min(v_col.rgb / 128.0, 1.0), v_col.a / 128.0);\n"
    "}\n";

static struct {
    GLuint prog;
    GLint lv, diffuse, ambient, lightdir, lightcol, uvoff;
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

void nw_vu1init(NwVuObject *o, const float local[16], const float view[16])
{
    memset(o, 0, sizeof(*o));
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            o->lv[r * 4 + c] = local[r * 4] * view[c] + local[r * 4 + 1] * view[4 + c] +
                               local[r * 4 + 2] * view[8 + c] + local[r * 4 + 3] * view[12 + c];
        }
    }
}

void nw_vu_use(NwVuProgram program, const NwVuObject *o, const float diffuse[4], const float uvoff[2])
{
    glUseProgram(nw_vu[program].prog);
    glUniformMatrix4fv(nw_vu[program].lv, 1, GL_FALSE, o->lv);
    glUniform4fv(nw_vu[program].diffuse, 1, diffuse);
    glUniform4fv(nw_vu[program].ambient, 1, o->ambient);
    glUniform4fv(nw_vu[program].lightdir, 3, o->lightdir[0]);
    glUniform4fv(nw_vu[program].lightcol, 3, o->lightcol[0]);
    const float uv[4] = { uvoff[0], uvoff[1], 0.0f, 0.0f };
    glUniform4fv(nw_vu[program].uvoff, 1, uv);
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
