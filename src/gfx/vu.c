#include "gfx/vu.h"

#include <stddef.h>
#include <stdint.h>

#include <SDL3/SDL.h>

#include "gfx/gl.h"
#include "gfx/vif.h"

/* Row-major PS2 matrices uploaded as they are: GL reads them transposed, which makes u_lv * v the VU's v x LV. */
static const char nw_vu_vs[] =
    "#version 330 core\n"
    "layout(location = 0) in vec4 a_pos;\n" /* VF06 */
    "layout(location = 1) in vec4 a_tex;\n" /* VF08 before ITOF12 */
    "layout(location = 2) in vec4 a_col;\n" /* VF31 before ITOF0 */
    "uniform mat4 u_lv;\n"
    "out vec4 v_col;\n"
    "out vec2 v_uv;\n"
    "void main()\n"
    "{\n"
    "    gl_Position = u_lv * vec4(a_pos.xyz, 1.0);\n"
    "    v_uv = a_tex.xy / 4096.0;\n"
    "    v_col = a_col;\n"
    "}\n";

/* A white texture under the GS modulate rule (0x80 = 1.0) until the materials draw theirs. */
static const char nw_vu_fs[] =
    "#version 330 core\n"
    "in vec4 v_col;\n"
    "in vec2 v_uv;\n"
    "out vec4 o_col;\n"
    "void main()\n"
    "{\n"
    "    o_col = vec4(min(v_col.rgb / 128.0, 1.0), v_col.a / 128.0);\n"
    "}\n";

static GLuint nw_vu_prog;
static GLint nw_vu_lv;

static GLuint nw_vu_shader(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    GLint ok = 0;
    glShaderSource(s, 1, &src, NULL);
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

bool nw_vu_init(void)
{
    GLuint vs = nw_vu_shader(GL_VERTEX_SHADER, nw_vu_vs), fs = vs ? nw_vu_shader(GL_FRAGMENT_SHADER, nw_vu_fs) : 0;
    if (!fs) {
        glDeleteShader(vs);
        return false;
    }
    GLint ok = 0;
    nw_vu_prog = glCreateProgram();
    glAttachShader(nw_vu_prog, vs);
    glAttachShader(nw_vu_prog, fs);
    glLinkProgram(nw_vu_prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    glGetProgramiv(nw_vu_prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024] = "";
        glGetProgramInfoLog(nw_vu_prog, sizeof(log), NULL, log);
        nw_vu_shutdown();
        return SDL_SetError("vu: program: %s", log);
    }
    nw_vu_lv = glGetUniformLocation(nw_vu_prog, "u_lv");
    return true;
}

void nw_vu_shutdown(void)
{
    glDeleteProgram(nw_vu_prog);
    nw_vu_prog = 0;
}

void nw_vu1init(NwVuObject *o, const float local[16], const float view[16])
{
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            o->lv[r * 4 + c] = local[r * 4] * view[c] + local[r * 4 + 1] * view[4 + c] +
                               local[r * 4 + 2] * view[8 + c] + local[r * 4 + 3] * view[12 + c];
        }
    }
}

void nw_vu_use(const NwVuObject *o)
{
    glUseProgram(nw_vu_prog);
    glUniformMatrix4fv(nw_vu_lv, 1, GL_FALSE, o->lv);
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
