/* Minimal OpenGL 3.3 core loader on top of SDL_GL_GetProcAddress. */
#ifndef NUWORLD_GFX_GL_H
#define NUWORLD_GFX_GL_H

#include <stdbool.h>
#include <stdint.h>

#if defined(_WIN32)
#define NW_GLAPI __stdcall
#else
#define NW_GLAPI
#endif

typedef unsigned int GLenum;
typedef unsigned int GLbitfield;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef float GLfloat;
typedef unsigned char GLubyte;

#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_VENDOR 0x1F00
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02

/* X(return type, name, parameter list) */
#define NW_GL_FUNCTIONS(X) \
    X(void, glClear, (GLbitfield mask)) \
    X(void, glClearColor, (GLfloat r, GLfloat g, GLfloat b, GLfloat a)) \
    X(void, glViewport, (GLint x, GLint y, GLsizei w, GLsizei h)) \
    X(const GLubyte *, glGetString, (GLenum name))

#define NW_GL_DECLARE(ret, name, params) \
    typedef ret (NW_GLAPI *PFN_nw_##name) params; \
    extern PFN_nw_##name nw_##name;
NW_GL_FUNCTIONS(NW_GL_DECLARE)
#undef NW_GL_DECLARE

#define glClear nw_glClear
#define glClearColor nw_glClearColor
#define glViewport nw_glViewport
#define glGetString nw_glGetString

/* Loads every function in NW_GL_FUNCTIONS; needs a current GL context. */
bool nw_gl_load(void);

#endif
