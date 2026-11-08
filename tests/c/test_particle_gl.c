/* Compare the optimized particle shader with the original integer-texture triangle path, including framebuffer
   alpha and Z. Synthetic data only; skipped when an OpenGL 3.3 context is unavailable. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "gfx/gl.h"
#include "gfx/packet.h"
#include "gfx/vu.h"

#define SIZE 32

static uint32_t packed(float f, uint8_t colour)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    return (u & ~255u) | colour;
}

static float as_float(uint32_t u)
{
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static void clear(void)
{
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glClearColor(13.0f / 255.0f, 17.0f / 255.0f, 23.0f / 255.0f, 0.3f);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

static void draw(NwVuProgram program, const NwVuObject *object, const NwVuMaterial *material, GLuint vao,
                 const uint32_t image[132][4], uint8_t pixels[SIZE * SIZE * 4], float depth[SIZE * SIZE])
{
    clear();
    glBindVertexArray(vao);
    NwMtlPass pass[2];
    unsigned n = nw_vu_use(program, object, material, pass);
    bool particle = program == NW_VU_PTL || program == NW_VU_PTL_GENERAL;
    if (particle) {
        nw_vu_ptl(program, image);
    }
    for (unsigned i = 0; i < n; i++) {
        nw_vu_pass(program, material, &pass[i]);
        if (particle) {
            glDrawArraysInstanced(GL_TRIANGLES, 0, 6, 1);
        } else {
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }
    }
    glReadPixels(0, 0, SIZE, SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glReadPixels(0, 0, SIZE, SIZE, 0x1902 /* GL_DEPTH_COMPONENT */, GL_FLOAT, depth);
}

int main(void)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return 77;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_Window *window = SDL_CreateWindow("Particle rendering test", SIZE, SIZE, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    SDL_GLContext context = window ? SDL_GL_CreateContext(window) : NULL;
    if (!context || !nw_gl_load()) {
        SDL_Log("particle-gl skipped: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 77;
    }
    if (!nw_vu_init()) {
        SDL_Log("particle-gl shaders: %s", SDL_GetError());
        return 1;
    }
    glViewport(0, 0, SIZE, SIZE);
    glEnable(GL_DEPTH_TEST);
    GLuint vao[2], vbo[2];
    glGenVertexArrays(2, vao);
    glGenBuffers(2, vbo);
    NwPtl particle = { .rate = 1.0f };
    glBindVertexArray(vao[0]);
    glBindBuffer(GL_ARRAY_BUFFER, vbo[0]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(particle), &particle, GL_STATIC_DRAW);
    nw_vu_ptl_attribs(0);
    glBindVertexArray(vao[1]);
    glBindBuffer(GL_ARRAY_BUFFER, vbo[1]);
    nw_vu_sprite_attribs();

    static const uint8_t alphas[] = { 0, 64, 126, 127, 128, 255 };
    static const uint8_t refs[] = { 0, 127, 128, 255 };
    unsigned cases = 0;
    for (unsigned kind = 0; kind < 3; kind++) {
        uint8_t indices[6] = { 0, 1, 2, 3, 1, 2 }, mip_index = 2;
        uint8_t colours[24] = { 128, 32, 64, 0, 32, 128, 64, 64, 64, 32, 128, 128,
                                128, 128, 128, 255, 32, 128, 64, 64, 64, 32, 128, 128 };
        uint8_t mip_colour[4] = { 64, 32, 128, 128 };
        NwTex source = { .tcc = true, .ncolours = kind == 1 ? 0 : 4, .nlevels = 2 };
        memcpy(source.clut, colours, 16);
        if (kind == 0) {
            source.clut[3][3] = 128; /* a narrow range must actually eliminate some alpha-test passes */
        }
        source.levels[0] = (NwTexLevel){ kind == 2 ? 3 : 2, 2, kind == 1 ? colours : indices };
        source.levels[1] = (NwTexLevel){ 1, 1, kind == 1 ? mip_colour : &mip_index };
        NwVuTexture texture = { 0 };
        nw_vu_texture(&texture, &source);
        if (!nw_vu_texture_rgba(&texture, &source) || texture.power_of_two != (kind != 2)) {
            return 1;
        }
        for (int filter = 0; filter < NW_FILTERS; filter++) {
            nw_vu_filter((NwTexFilter)filter);
            for (unsigned test = 0; test < 8; test++) {
                for (unsigned fail = 0; fail < 4; fail++) {
                    for (unsigned ai = 0; ai < SDL_arraysize(alphas); ai++) {
                        uint32_t image[132][4] = { 0 };
                        image[4][0] = packed(-128.0f, 80);
                        image[4][1] = packed(-128.0f, 96);
                        image[4][2] = packed(0.0f, 112);
                        image[4][3] = packed(256.0f, alphas[ai]);
                        image[5][0] = packed(128.0f, 0);
                        image[5][1] = packed(-128.0f, 0);
                        unsigned u = source.levels[0].width * 32, v = source.levels[0].height * 32;
                        image[1][0] = 0; image[1][1] = 0; image[1][2] = 0; image[1][3] = v;
                        image[2][0] = u; image[2][1] = 0; image[2][2] = u; image[2][3] = v;
                        NwVuObject object = { 0 };
                        for (unsigned i = 0; i < 4; i++) object.lv[i * 5] = 1.0f;
                        object.sprite[0] = 1.0f / (8.0f * SIZE);
                        object.sprite[1] = -object.sprite[0];
                        object.sprite[2] = 1.0f / (16.0f * source.levels[0].width);
                        object.sprite[3] = 1.0f / (16.0f * source.levels[0].height);
                        static const unsigned strip[] = { 0, 1, 2, 1, 2, 3 };
                        NwVuSpriteVertex verts[6] = { 0 };
                        for (unsigned i = 0; i < 6; i++) {
                            unsigned c = strip[i];
                            float x = as_float(image[c & 1 ? 5 : 4][0]);
                            float y = as_float(image[c & 1 ? 5 : 4][1]);
                            if (c >= 2) { x += as_float(image[4][2]); y += as_float(image[4][3]); }
                            verts[i].pos[0] = x * object.sprite[0];
                            verts[i].pos[1] = y * object.sprite[1];
                            verts[i].tex[0] = (c & 1 ? u : 0) * object.sprite[2] * 4096.0f;
                            verts[i].tex[1] = (c >= 2 ? v : 0) * object.sprite[3] * 4096.0f;
                            for (unsigned k = 0; k < 4; k++) verts[i].col[k] = image[4][k] & 255;
                        }
                        glBindVertexArray(vao[1]); glBindBuffer(GL_ARRAY_BUFFER, vbo[1]);
                        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STREAM_DRAW);
                        texture.tcc = ai % 3 != 0;
                        NwVuMaterial material = { .tex = &texture, .state = {
                            .abe = true, .alpha = ai & 1 ? 0x44 : 0x48, .ztst = 2, .zmsk = ai & 1,
                            .ate = true, .atst = test, .aref = refs[(test + fail + ai) % 4], .afail = fail,
                            .mmag = 1, .mmin = ai & 2 ? 1 : 5, .mxl = 1, .k = ai & 1 ? 8 : 0,
                            .wms = fail & 1, .wmt = fail & 2 ? 1 : 0 } };
                        uint8_t reference[SIZE * SIZE * 4], actual[SIZE * SIZE * 4];
                        float reference_z[SIZE * SIZE], actual_z[SIZE * SIZE];
                        draw(NW_VU_TRICU, &object, &material, vao[1], image, reference, reference_z);
                        draw(nw_vu_particle_program(&texture, &material.state), &object, &material, vao[0], image,
                             actual, actual_z);
                        if (memcmp(reference, actual, sizeof(actual)) || memcmp(reference_z, actual_z, sizeof(actual_z))) {
                            fprintf(stderr, "particle-gl mismatch: texture %u filter %d test %u fail %u alpha %u\n",
                                    kind, filter, test, fail, alphas[ai]);
                            return 1;
                        }
                        cases++;
                    }
                }
            }
        }
        nw_vu_texture_free(&texture);
    }
    printf("particle-gl: %u exact colour, framebuffer-alpha and depth comparisons passed\n", cases);
    glDeleteBuffers(2, vbo); glDeleteVertexArrays(2, vao);
    nw_vu_shutdown(); SDL_GL_DestroyContext(context); SDL_DestroyWindow(window); SDL_Quit();
    return 0;
}
