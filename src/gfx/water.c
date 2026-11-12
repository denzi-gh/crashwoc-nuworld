#include "gfx/water.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "app.h"
#include "gfx/cloudvol.h"
#include "gfx/gl.h"

#define NW_WATER_RIPPLE 256 /* the ripple map's texels across; it tiles */
#define NW_WATER_NOISE 9    /* the clouds' noise's texture unit, as cloudvol binds it */
#define NW_WATER_COLOR 10   /* the frame copy's */
#define NW_WATER_DEPTH 11   /* its Z's */
#define NW_WATER_MAP 12     /* the ripple map's */
#define NW_WATER_LAYER 13   /* the level's water texture's */
#define NW_WATER_LAYERS 8   /* the level textures kept as RGBA */

static struct {
    bool tried; /* made, or failed to */
    GLuint prog, ripple;
    GLint lv, local, wave, color, depth, map, noise, screen, unscreen, vp, cam, tint, sun, layer, blend;
    struct {
        const NwGpuTex *key;
        const uint8_t *texels; /* the key's level 0 when made: another texture at a freed one's address differs */
        GLuint gl;
    } layers[NW_WATER_LAYERS];
    uint32_t next; /* the layer slot to take next */
} nw_water;

/* water.vsm's lift: the normal's y picks one of the table's 32 waves, whose w raises the vertex. v_world is where
   it ends up. */
static const char nw_water_vs[] =
    "#version 330 core\n"
    "layout(location = 0) in vec4 a_pos;\n"
    "layout(location = 1) in vec4 a_tex;\n"
    "uniform mat4 u_lv;\n"
    "uniform mat4 u_local;\n"
    "uniform vec4 u_wave[32];\n"
    "out vec3 v_world;\n"
    "void main() {\n"
    "    vec4 p = vec4(a_pos.x, a_pos.y + u_wave[int(a_tex.z) & 31].w, a_pos.z, 1.0);\n"
    "    v_world = (u_local * p).xyz;\n"
    "    gl_Position = u_lv * p;\n"
    "}\n";

static const char nw_water_fs[] =
    "#version 330 core\n"
    "uniform sampler2D u_color;\n" /* the frame before the water */
    "uniform sampler2D u_depth;\n" /* its Z */
    "uniform sampler2D u_ripple;\n" /* the ripples: xy the slope (0.5 flat), z a foam noise; tiling */
    "uniform sampler3D u_noise;\n" /* the clouds' noise, for the caustics */
    "uniform mat4 u_screen;\n" /* world to GL clip */
    "uniform mat4 u_unscreen;\n" /* its inverse */
    "uniform vec4 u_vp;\n" /* the viewport, pixels: x, y, width, height */
    "uniform vec4 u_cam;\n" /* the camera, world; w the time, seconds */
    "uniform vec4 u_tint;\n" /* the level's water colour; w how murky, 0-1 */
    "uniform vec4 u_sun;\n" /* towards the sun, world, unit */
    "uniform sampler2D u_layer;\n" /* the level's water texture */
    "uniform vec4 u_blend;\n" /* x how the PS2 laid it: 0 not, 1 blended, 2 added; y its opacity */
    "in vec3 v_world;\n"
    "out vec4 o_col;\n"
    "const int MARCH = 14;\n"
    "vec2 tex_at(vec2 frag) {\n"
    "    return frag / vec2(textureSize(u_color, 0));\n"
    "}\n"
    /* Where the frame's Z puts the pixel; Z at the far plane (nothing drawn, the clear) a long way off rather than at
       infinity. */
    "vec3 world_at(vec2 frag, float z) {\n"
    "    vec4 w = u_unscreen * vec4((frag - u_vp.xy) / u_vp.zw * 2.0 - 1.0, min(z, 0.9999) * 2.0 - 1.0, 1.0);\n"
    "    return w.xyz / w.w;\n"
    "}\n"
    "void main() {\n"
    "    float t = u_cam.w;\n"
    "    vec3 P = v_world;\n"
    "    vec3 V = normalize(P - u_cam.xyz);\n"
    "    float dist = length(P - u_cam.xyz);\n"
    /* Ripples: two layers of the map drifting across each other, calmer far off. */
    "    vec2 s1 = texture(u_ripple, P.xz * 0.11 + vec2(0.017, 0.009) * t).xy * 2.0 - 1.0;\n"
    "    vec2 s2 = texture(u_ripple, P.xz * 0.29 + vec2(-0.013, 0.021) * t).xy * 2.0 - 1.0;\n"
    "    vec2 slope = (s1 + s2 * 0.6) * mix(1.0, 0.35, smoothstep(10.0, 80.0, dist));\n"
    "    vec3 N = normalize(vec3(slope.x, 2.2, slope.y));\n"
    /* The bed under it, and how much water the view crosses to reach it. */
    "    vec2 frag = gl_FragCoord.xy;\n"
    "    float z0 = texture(u_depth, tex_at(frag)).r;\n"
    "    float deep0 = max(dot(world_at(frag, z0) - P, V), 0.0);\n"
    /* The bed seen bent by the ripples, less so in the shallows; a bent look that lands in front of the water
       (a bank, a leg) is not taken. */
    "    vec2 rf = frag + N.xz * u_vp.z * 0.06 * min(deep0 * 0.3, 1.0);\n"
    "    float z1 = texture(u_depth, tex_at(rf)).r;\n"
    "    vec3 G = world_at(rf, z1);\n"
    "    if (dot(G - P, V) < 0.0) {\n"
    "        rf = frag;\n"
    "        G = world_at(frag, z0);\n"
    "    }\n"
    "    float deep = max(dot(G - P, V), 0.0);\n"
    "    vec3 under = texture(u_color, tex_at(rf)).rgb;\n"
    /* Caustics on the bed: where the ridges of two drifting Worley layers meet, fading with depth. */
    "    vec3 cq = vec3(G.xz * 0.07, t * 0.04);\n"
    "    float c1 = 1.0 - smoothstep(0.22, 0.5, texture(u_noise, cq).g);\n"
    "    float c2 = 1.0 - smoothstep(0.22, 0.5, texture(u_noise, cq * 1.31 + vec3(0.31, 0.57, t * 0.02)).g);\n"
    "    under += under * c1 * c2 * 2.4 * exp(-deep * 0.5);\n"
    /* Through the water: toward its colour the deeper, the murkier the faster. A texture the PS2 added is its
       highlights, not its colour: under it the water is dark. */
    "    vec3 body = u_tint.rgb * (u_blend.x > 1.5 ? 0.22 : 0.6);\n"
    "    vec3 refr = mix(under, body, 1.0 - exp(-deep * mix(0.15, 1.0, u_tint.w)));\n"
    /* Reflection: the reflected ray marched against the frame's Z; past the frame, the frame's top. */
    "    vec3 R = reflect(V, N);\n"
    "    float top = u_vp.y + u_vp.w * 0.9;\n"
    "    vec3 refl = (texture(u_color, tex_at(vec2(frag.x, top))).rgb +\n"
    "                 texture(u_color, tex_at(vec2(u_vp.x + u_vp.z * 0.25, top))).rgb +\n"
    "                 texture(u_color, tex_at(vec2(u_vp.x + u_vp.z * 0.75, top))).rgb) / 3.0;\n"
    "    float ds = max(dist * 0.02, 0.15), tt = ds;\n"
    "    for (int i = 0; i < MARCH; i++) {\n"
    "        vec4 c = u_screen * vec4(P + R * tt, 1.0);\n"
    "        if (c.w <= 0.0) {\n"
    "            break;\n"
    "        }\n"
    "        vec3 ndc = c.xyz / c.w;\n"
    "        if (abs(ndc.x) > 1.0 || abs(ndc.y) > 1.0) {\n"
    "            break;\n"
    "        }\n"
    "        vec2 qf = (ndc.xy * 0.5 + 0.5) * u_vp.zw + u_vp.xy;\n"
    "        float zs = texture(u_depth, tex_at(qf)).r;\n"
    "        if (ndc.z * 0.5 + 0.5 > zs) {\n"
    "            float edge = 1.0 - smoothstep(0.8, 1.0, max(abs(ndc.x), abs(ndc.y)));\n"
    "            refl = mix(refl, texture(u_color, tex_at(qf)).rgb, edge);\n"
    "            break;\n"
    "        }\n"
    "        tt += ds;\n"
    "        ds *= 1.35;\n"
    "    }\n"
    "    float fres = 0.04 + 0.96 * pow(1.0 - max(dot(-V, N), 0.0), 5.0);\n"
    "    vec3 col = mix(refr, refl, fres);\n"
    /* The level's own water texture, laid in the world and drifting, as the PS2 laid it over its water. */
    "    if (u_blend.x > 0.0) {\n"
    "        vec4 lt = texture(u_layer, P.xz * 0.07 + vec2(0.011, 0.007) * t + slope * 0.03);\n"
    "        float a = lt.a * u_blend.y * 0.75 * (1.0 - smoothstep(50.0, 150.0, dist));\n"
    "        col = u_blend.x > 1.5 ? col + lt.rgb * a : mix(col, lt.rgb, a);\n"
    "    }\n"
    /* The sun's glint. */
    "    col += vec3(1.0, 0.97, 0.9) * pow(max(dot(R, u_sun.xyz), 0.0), 220.0) * 1.2;\n"
    /* Foam where it meets the shore, broken by the noise. */
    "    float f = texture(u_ripple, P.xz * 0.55 + vec2(0.0, t * 0.04)).z;\n"
    "    float f2 = texture(u_ripple, P.zx * 1.3 - vec2(t * 0.03, 0.0)).z;\n"
    "    float shore = 1.0 - smoothstep(0.0, 0.22, deep0);\n"
    "    float foam = smoothstep(0.62, 0.85, f * 0.6 + f2 * 0.4 + shore * 0.2) * shore * shore;\n"
    "    col = mix(col, vec3(0.95, 0.98, 1.0), foam * 0.65);\n"
    "    o_col = vec4(col, 1.0);\n"
    "}\n";

static uint32_t nw_water_hash(uint32_t x, uint32_t y, uint32_t seed)
{
    uint32_t h = x * 0x8DA6B343u ^ y * 0xD8163841u ^ seed * 0x165667B1u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return h;
}

/* Smooth value noise of period cells across the unit square, 0-1. */
static float nw_water_value(float x, float y, int period, uint32_t seed)
{
    x *= (float)period;
    y *= (float)period;
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float fx = x - (float)xi, fy = y - (float)yi;
    fx = fx * fx * (3.0f - 2.0f * fx);
    fy = fy * fy * (3.0f - 2.0f * fy);
    float v[4];
    for (int k = 0; k < 4; k++) {
        int cx = ((xi + (k & 1)) % period + period) % period, cy = ((yi + (k >> 1)) % period + period) % period;
        v[k] = (float)(nw_water_hash((uint32_t)cx, (uint32_t)cy, seed) >> 8) / 16777216.0f;
    }
    float a = v[0] + (v[1] - v[0]) * fx, b = v[2] + (v[3] - v[2]) * fx;
    return a + (b - a) * fy;
}

/* The ripple map: the slope of a few octaves of value noise in RG (0.5 flat), another noise for the foam in B. */
static uint8_t *nw_water_texels(void)
{
    enum { N = NW_WATER_RIPPLE };
    float *height = SDL_malloc(sizeof(float) * N * N);
    uint8_t *texels = SDL_malloc((size_t)N * N * 4);
    if (!height || !texels) {
        SDL_free(height);
        SDL_free(texels);
        return NULL;
    }
    for (int y = 0; y < N; y++) {
        for (int x = 0; x < N; x++) {
            float u = (float)x / N, v = (float)y / N;
            height[y * N + x] = nw_water_value(u, v, 8, 1u) * 0.5f + nw_water_value(u, v, 16, 2u) * 0.3f +
                                nw_water_value(u, v, 32, 3u) * 0.2f;
        }
    }
    for (int y = 0; y < N; y++) {
        for (int x = 0; x < N; x++) {
            float dx = height[y * N + (x + 1) % N] - height[y * N + (x + N - 1) % N];
            float dy = height[((y + 1) % N) * N + x] - height[((y + N - 1) % N) * N + x];
            float u = (float)x / N, v = (float)y / N;
            float foam = nw_water_value(u, v, 16, 7u) * 0.6f + nw_water_value(u, v, 48, 8u) * 0.4f;
            uint8_t *t = &texels[(y * N + x) * 4];
            t[0] = (uint8_t)SDL_clamp((int)(128.0f + dx * 1400.0f), 0, 255);
            t[1] = (uint8_t)SDL_clamp((int)(128.0f + dy * 1400.0f), 0, 255);
            t[2] = (uint8_t)SDL_clamp((int)(foam * 255.0f), 0, 255);
            t[3] = 255;
        }
    }
    SDL_free(height);
    return texels;
}

static GLuint nw_water_shader(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    GLint ok = 0;
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, NW_APP_NAME ": water shader: %s\n", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

/* The program and the ripple map, once; false when either is missing. */
static bool nw_water_init(void)
{
    if (nw_water.tried) {
        return nw_water.prog && nw_water.ripple;
    }
    nw_water.tried = true;
    GLuint vs = nw_water_shader(GL_VERTEX_SHADER, nw_water_vs), fs = nw_water_shader(GL_FRAGMENT_SHADER, nw_water_fs);
    if (vs && fs) {
        GLuint p = glCreateProgram();
        GLint ok = 0;
        glAttachShader(p, vs);
        glAttachShader(p, fs);
        glLinkProgram(p);
        glGetProgramiv(p, GL_LINK_STATUS, &ok);
        if (ok) {
            nw_water.prog = p;
        } else {
            char log[2048];
            glGetProgramInfoLog(p, sizeof(log), NULL, log);
            fprintf(stderr, NW_APP_NAME ": water program: %s\n", log);
            glDeleteProgram(p);
        }
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!nw_water.prog) {
        return false;
    }
    GLuint p = nw_water.prog;
    nw_water.lv = glGetUniformLocation(p, "u_lv");
    nw_water.local = glGetUniformLocation(p, "u_local");
    nw_water.wave = glGetUniformLocation(p, "u_wave");
    nw_water.color = glGetUniformLocation(p, "u_color");
    nw_water.depth = glGetUniformLocation(p, "u_depth");
    nw_water.map = glGetUniformLocation(p, "u_ripple");
    nw_water.noise = glGetUniformLocation(p, "u_noise");
    nw_water.screen = glGetUniformLocation(p, "u_screen");
    nw_water.unscreen = glGetUniformLocation(p, "u_unscreen");
    nw_water.vp = glGetUniformLocation(p, "u_vp");
    nw_water.cam = glGetUniformLocation(p, "u_cam");
    nw_water.tint = glGetUniformLocation(p, "u_tint");
    nw_water.sun = glGetUniformLocation(p, "u_sun");
    nw_water.layer = glGetUniformLocation(p, "u_layer");
    nw_water.blend = glGetUniformLocation(p, "u_blend");

    uint8_t *texels = nw_water_texels();
    if (texels) {
        glGenTextures(1, &nw_water.ripple);
        glActiveTexture(GL_TEXTURE0 + NW_WATER_MAP);
        glBindTexture(GL_TEXTURE_2D, nw_water.ripple);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, NW_WATER_RIPPLE, NW_WATER_RIPPLE, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     texels);
        glGenerateMipmap(GL_TEXTURE_2D);
        glActiveTexture(GL_TEXTURE0);
        SDL_free(texels);
    }
    return nw_water.ripple != 0;
}

/* The level's water texture as a plain RGBA texture (its CLUT applied, alpha 0x80 to 1.0) that tiles, made once. */
static GLuint nw_water_layer(const NwGpuTex *t)
{
    const NwTexLevel *l = t && t->tex.nlevels ? &t->tex.levels[0] : NULL;
    if (!l || !l->texels || !l->width || !l->height) {
        return 0;
    }
    for (uint32_t i = 0; i < NW_WATER_LAYERS; i++) {
        if (nw_water.layers[i].key == t && nw_water.layers[i].texels == l->texels) {
            return nw_water.layers[i].gl;
        }
    }
    uint32_t n = l->width * l->height;
    uint8_t *rgba = SDL_malloc((size_t)n * 4);
    if (!rgba) {
        return 0;
    }
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t *c = t->tex.ncolours ? t->tex.clut[l->texels[i]] : &l->texels[i * 4];
        rgba[i * 4] = c[0];
        rgba[i * 4 + 1] = c[1];
        rgba[i * 4 + 2] = c[2];
        rgba[i * 4 + 3] = (uint8_t)SDL_min(c[3] * 2, 255);
    }
    uint32_t slot = nw_water.next++ % NW_WATER_LAYERS;
    if (!nw_water.layers[slot].gl) {
        glGenTextures(1, &nw_water.layers[slot].gl);
    }
    glActiveTexture(GL_TEXTURE0 + NW_WATER_LAYER);
    glBindTexture(GL_TEXTURE_2D, nw_water.layers[slot].gl);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)l->width, (GLsizei)l->height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 rgba);
    glGenerateMipmap(GL_TEXTURE_2D);
    glActiveTexture(GL_TEXTURE0);
    SDL_free(rgba);
    nw_water.layers[slot].key = t;
    nw_water.layers[slot].texels = l->texels;
    return nw_water.layers[slot].gl;
}

bool nw_water_ready(void)
{
    return nw_water_init() && nw_cloudvol_noise();
}

bool nw_water_draw(const NwGpuMesh *mesh, const float local[16], const float screen[16], const NwWaterFrame *f)
{
    float unscreen[16], inv[16], lv[16];
    GLuint noise = nw_cloudvol_noise();
    if (!mesh->vao || !noise || !nw_water_init() || !nw_cloudvol_invert(screen, unscreen) ||
        !nw_cloudvol_invert(local, inv)) {
        return false;
    }
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            lv[r * 4 + c] = local[r * 4] * screen[c] + local[r * 4 + 1] * screen[4 + c] +
                            local[r * 4 + 2] * screen[8 + c] + local[r * 4 + 3] * screen[12 + c];
        }
    }
    /* The camera is the point the screen takes to x = y = w = 0, as for the clouds. */
    if (!(fabsf(unscreen[11]) > 1e-30f)) {
        return false;
    }
    const float cam[4] = { unscreen[8] / unscreen[11], unscreen[9] / unscreen[11], unscreen[10] / unscreen[11],
                           f->seconds };
    static const float sun[4] = { -0.42f, 0.74f, -0.52f, 0.0f };
    static const float flat[32][4];
    const float vp[4] = { (float)f->viewport[0], (float)f->viewport[1], (float)f->viewport[2],
                          (float)f->viewport[3] };

    glUseProgram(nw_water.prog);
    glUniformMatrix4fv(nw_water.lv, 1, GL_FALSE, lv);
    glUniformMatrix4fv(nw_water.local, 1, GL_FALSE, local);
    glUniform4fv(nw_water.wave, 32, f->waves ? f->waves[0] : flat[0]);
    glUniformMatrix4fv(nw_water.screen, 1, GL_FALSE, screen);
    glUniformMatrix4fv(nw_water.unscreen, 1, GL_FALSE, unscreen);
    glUniform4fv(nw_water.vp, 1, vp);
    glUniform4fv(nw_water.cam, 1, cam);
    glUniform4fv(nw_water.tint, 1, f->tint);
    glUniform4fv(nw_water.sun, 1, sun);
    GLuint layer = nw_water_layer(f->layer);
    const float blend[4] = { layer ? (float)f->layer_blend : 0.0f, f->layer_opacity, 0.0f, 0.0f };
    glUniform4fv(nw_water.blend, 1, blend);
    glUniform1i(nw_water.layer, NW_WATER_LAYER);
    glUniform1i(nw_water.color, NW_WATER_COLOR);
    glUniform1i(nw_water.depth, NW_WATER_DEPTH);
    glUniform1i(nw_water.map, NW_WATER_MAP);
    glUniform1i(nw_water.noise, NW_WATER_NOISE);
    glActiveTexture(GL_TEXTURE0 + NW_WATER_COLOR);
    glBindTexture(GL_TEXTURE_2D, f->color);
    glActiveTexture(GL_TEXTURE0 + NW_WATER_DEPTH);
    glBindTexture(GL_TEXTURE_2D, f->depth);
    glActiveTexture(GL_TEXTURE0 + NW_WATER_MAP);
    glBindTexture(GL_TEXTURE_2D, nw_water.ripple);
    glActiveTexture(GL_TEXTURE0 + NW_WATER_LAYER);
    glBindTexture(GL_TEXTURE_2D, layer);
    glActiveTexture(GL_TEXTURE0 + NW_WATER_NOISE);
    glBindTexture(GL_TEXTURE_3D, noise);
    glActiveTexture(GL_TEXTURE0);
    glDisable(GL_BLEND);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
    glBindVertexArray(mesh->vao);
    glDrawElements(GL_TRIANGLES, (GLsizei)(mesh->mesh.ntris * 3), GL_UNSIGNED_INT, NULL);
    return true;
}

void nw_water_shutdown(void)
{
    if (nw_water.prog) {
        glDeleteProgram(nw_water.prog);
    }
    if (nw_water.ripple) {
        glDeleteTextures(1, &nw_water.ripple);
    }
    for (uint32_t i = 0; i < NW_WATER_LAYERS; i++) {
        if (nw_water.layers[i].gl) {
            glDeleteTextures(1, &nw_water.layers[i].gl);
        }
    }
    memset(&nw_water, 0, sizeof(nw_water));
}
