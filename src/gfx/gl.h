/* Minimal OpenGL 3.3 core loader on top of SDL_GL_GetProcAddress. */
#ifndef NUWORLD_GFX_GL_H
#define NUWORLD_GFX_GL_H

#include <stdbool.h>
#include <stddef.h>
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
typedef double GLdouble;
typedef unsigned char GLubyte;
typedef unsigned char GLboolean;
typedef char GLchar;
typedef short GLshort;
typedef unsigned short GLushort;
typedef ptrdiff_t GLsizeiptr;

#define GL_FALSE 0
#define GL_TRUE 1
#define GL_ZERO 0
#define GL_ONE 1
#define GL_TRIANGLES 0x0004
#define GL_NEVER 0x0200
#define GL_LESS 0x0201
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_DST_COLOR 0x0306
#define GL_LEQUAL 0x0203
#define GL_ALWAYS 0x0207
#define GL_FRONT_AND_BACK 0x0408
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_DEPTH_TEST 0x0B71
#define GL_SCISSOR_TEST 0x0C11
#define GL_BLEND 0x0BE2
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_TEXTURE_2D 0x0DE1
#define GL_UNSIGNED_BYTE 0x1401
#define GL_SHORT 0x1402
#define GL_UNSIGNED_INT 0x1405
#define GL_RGBA 0x1908
#define GL_FLOAT 0x1406
#define GL_LINE 0x1B01
#define GL_FILL 0x1B02
#define GL_VENDOR 0x1F00
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_NEAREST_MIPMAP_NEAREST 0x2700
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_FUNC_ADD 0x8006
#define GL_FUNC_SUBTRACT 0x800A
#define GL_FUNC_REVERSE_SUBTRACT 0x800B
#define GL_TEXTURE_MAX_LEVEL 0x813D
#define GL_R8UI 0x8232
#define GL_TEXTURE0 0x84C0
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STREAM_DRAW 0x88E0
#define GL_STATIC_DRAW 0x88E4
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_RGBA8 0x8058
#define GL_DEPTH_COMPONENT24 0x81A6
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_FRAMEBUFFER 0x8D40
#define GL_RENDERBUFFER 0x8D41
#define GL_RGBA8UI 0x8D7C
#define GL_RED_INTEGER 0x8D94
#define GL_RGBA_INTEGER 0x8D99

/* X(return type, name, parameter list) */
#define NW_GL_FUNCTIONS(X) \
    X(void, glClear, (GLbitfield mask)) \
    X(void, glClearColor, (GLfloat r, GLfloat g, GLfloat b, GLfloat a)) \
    X(void, glViewport, (GLint x, GLint y, GLsizei w, GLsizei h)) \
    X(void, glScissor, (GLint x, GLint y, GLsizei w, GLsizei h)) \
    X(void, glClearDepth, (GLdouble depth)) \
    X(const GLubyte *, glGetString, (GLenum name)) \
    X(void, glEnable, (GLenum cap)) \
    X(void, glDisable, (GLenum cap)) \
    X(void, glDepthFunc, (GLenum func)) \
    X(void, glDepthMask, (GLboolean flag)) \
    X(void, glColorMask, (GLboolean r, GLboolean g, GLboolean b, GLboolean a)) \
    X(void, glBlendFunc, (GLenum sfactor, GLenum dfactor)) \
    X(void, glBlendEquation, (GLenum mode)) \
    X(void, glPolygonMode, (GLenum face, GLenum mode)) \
    X(void, glPixelStorei, (GLenum pname, GLint param)) \
    X(void, glReadPixels, (GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, void *data)) \
    X(void, glGenTextures, (GLsizei n, GLuint *textures)) \
    X(void, glDeleteTextures, (GLsizei n, const GLuint *textures)) \
    X(void, glBindTexture, (GLenum target, GLuint texture)) \
    X(void, glActiveTexture, (GLenum texture)) \
    X(void, glTexParameteri, (GLenum target, GLenum pname, GLint param)) \
    X(void, glTexImage2D, (GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, \
        GLint border, GLenum format, GLenum type, const void *pixels)) \
    X(void, glGenBuffers, (GLsizei n, GLuint *buffers)) \
    X(void, glDeleteBuffers, (GLsizei n, const GLuint *buffers)) \
    X(void, glBindBuffer, (GLenum target, GLuint buffer)) \
    X(void, glBufferData, (GLenum target, GLsizeiptr size, const void *data, GLenum usage)) \
    X(void, glGenVertexArrays, (GLsizei n, GLuint *arrays)) \
    X(void, glDeleteVertexArrays, (GLsizei n, const GLuint *arrays)) \
    X(void, glBindVertexArray, (GLuint array)) \
    X(void, glEnableVertexAttribArray, (GLuint index)) \
    X(void, glVertexAttribPointer, \
        (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer)) \
    X(GLuint, glCreateShader, (GLenum type)) \
    X(void, glShaderSource, (GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length)) \
    X(void, glCompileShader, (GLuint shader)) \
    X(void, glGetShaderiv, (GLuint shader, GLenum pname, GLint *params)) \
    X(void, glGetShaderInfoLog, (GLuint shader, GLsizei size, GLsizei *length, GLchar *log)) \
    X(void, glDeleteShader, (GLuint shader)) \
    X(GLuint, glCreateProgram, (void)) \
    X(void, glAttachShader, (GLuint program, GLuint shader)) \
    X(void, glLinkProgram, (GLuint program)) \
    X(void, glGetProgramiv, (GLuint program, GLenum pname, GLint *params)) \
    X(void, glGetProgramInfoLog, (GLuint program, GLsizei size, GLsizei *length, GLchar *log)) \
    X(void, glUseProgram, (GLuint program)) \
    X(void, glDeleteProgram, (GLuint program)) \
    X(GLint, glGetUniformLocation, (GLuint program, const GLchar *name)) \
    X(void, glUniformMatrix4fv, (GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)) \
    X(void, glUniform1i, (GLint location, GLint v0)) \
    X(void, glUniform4i, (GLint location, GLint v0, GLint v1, GLint v2, GLint v3)) \
    X(void, glUniform4fv, (GLint location, GLsizei count, const GLfloat *value)) \
    X(void, glDrawElements, (GLenum mode, GLsizei count, GLenum type, const void *indices)) \
    X(void, glGenFramebuffers, (GLsizei n, GLuint *framebuffers)) \
    X(void, glDeleteFramebuffers, (GLsizei n, const GLuint *framebuffers)) \
    X(void, glBindFramebuffer, (GLenum target, GLuint framebuffer)) \
    X(GLenum, glCheckFramebufferStatus, (GLenum target)) \
    X(void, glFramebufferTexture2D, \
        (GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level)) \
    X(void, glFramebufferRenderbuffer, \
        (GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer)) \
    X(void, glGenRenderbuffers, (GLsizei n, GLuint *renderbuffers)) \
    X(void, glDeleteRenderbuffers, (GLsizei n, const GLuint *renderbuffers)) \
    X(void, glBindRenderbuffer, (GLenum target, GLuint renderbuffer)) \
    X(void, glRenderbufferStorage, (GLenum target, GLenum internalformat, GLsizei width, GLsizei height)) \
    X(void, glBlitFramebuffer, (GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, \
        GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter))

#define NW_GL_DECLARE(ret, name, params) \
    typedef ret (NW_GLAPI *PFN_nw_##name) params; \
    extern PFN_nw_##name nw_##name;
NW_GL_FUNCTIONS(NW_GL_DECLARE)
#undef NW_GL_DECLARE

#define glClear nw_glClear
#define glClearColor nw_glClearColor
#define glViewport nw_glViewport
#define glScissor nw_glScissor
#define glClearDepth nw_glClearDepth
#define glGetString nw_glGetString
#define glEnable nw_glEnable
#define glDisable nw_glDisable
#define glDepthFunc nw_glDepthFunc
#define glDepthMask nw_glDepthMask
#define glColorMask nw_glColorMask
#define glBlendFunc nw_glBlendFunc
#define glBlendEquation nw_glBlendEquation
#define glPolygonMode nw_glPolygonMode
#define glPixelStorei nw_glPixelStorei
#define glReadPixels nw_glReadPixels
#define glGenTextures nw_glGenTextures
#define glDeleteTextures nw_glDeleteTextures
#define glBindTexture nw_glBindTexture
#define glActiveTexture nw_glActiveTexture
#define glTexParameteri nw_glTexParameteri
#define glTexImage2D nw_glTexImage2D
#define glGenBuffers nw_glGenBuffers
#define glDeleteBuffers nw_glDeleteBuffers
#define glBindBuffer nw_glBindBuffer
#define glBufferData nw_glBufferData
#define glGenVertexArrays nw_glGenVertexArrays
#define glDeleteVertexArrays nw_glDeleteVertexArrays
#define glBindVertexArray nw_glBindVertexArray
#define glEnableVertexAttribArray nw_glEnableVertexAttribArray
#define glVertexAttribPointer nw_glVertexAttribPointer
#define glCreateShader nw_glCreateShader
#define glShaderSource nw_glShaderSource
#define glCompileShader nw_glCompileShader
#define glGetShaderiv nw_glGetShaderiv
#define glGetShaderInfoLog nw_glGetShaderInfoLog
#define glDeleteShader nw_glDeleteShader
#define glCreateProgram nw_glCreateProgram
#define glAttachShader nw_glAttachShader
#define glLinkProgram nw_glLinkProgram
#define glGetProgramiv nw_glGetProgramiv
#define glGetProgramInfoLog nw_glGetProgramInfoLog
#define glUseProgram nw_glUseProgram
#define glDeleteProgram nw_glDeleteProgram
#define glGetUniformLocation nw_glGetUniformLocation
#define glUniformMatrix4fv nw_glUniformMatrix4fv
#define glUniform1i nw_glUniform1i
#define glUniform4i nw_glUniform4i
#define glUniform4fv nw_glUniform4fv
#define glDrawElements nw_glDrawElements
#define glGenFramebuffers nw_glGenFramebuffers
#define glDeleteFramebuffers nw_glDeleteFramebuffers
#define glBindFramebuffer nw_glBindFramebuffer
#define glCheckFramebufferStatus nw_glCheckFramebufferStatus
#define glFramebufferTexture2D nw_glFramebufferTexture2D
#define glFramebufferRenderbuffer nw_glFramebufferRenderbuffer
#define glGenRenderbuffers nw_glGenRenderbuffers
#define glDeleteRenderbuffers nw_glDeleteRenderbuffers
#define glBindRenderbuffer nw_glBindRenderbuffer
#define glRenderbufferStorage nw_glRenderbufferStorage
#define glBlitFramebuffer nw_glBlitFramebuffer

/* Loads every function in NW_GL_FUNCTIONS; needs a current GL context. */
bool nw_gl_load(void);

#endif
