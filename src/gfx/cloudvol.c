#include "gfx/cloudvol.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "app.h"
#include "gfx/gl.h"

#define NW_CLOUD_NOISE 64 /* the noise texture's texels across; it tiles */
#define NW_CLOUD_UNIT 9   /* its texture unit, past the ones the VU programs bind */

static struct {
    bool tried; /* made, or failed to */
    GLuint prog, vao, vbo, noise;
    GLint lv, cam, min, max, shape, sun, anim, storm, grow, tex;
    GLuint sky; /* the storm's layer over a sky dome */
    GLint sky_lv, sky_inv, sky_full, sky_cam, sky_local, sky_camw, sky_sun, sky_front, sky_anim, sky_tex;
} nw_cloud;

/* The box's corners from 0 to 1, two triangles a face; the fragment shader keeps the faces nearer the camera. */
static const float nw_cloud_box[36][3] = {
    { 0, 0, 0 }, { 1, 0, 0 }, { 1, 1, 0 }, { 0, 0, 0 }, { 1, 1, 0 }, { 0, 1, 0 },
    { 0, 0, 1 }, { 1, 1, 1 }, { 1, 0, 1 }, { 0, 0, 1 }, { 0, 1, 1 }, { 1, 1, 1 },
    { 0, 0, 0 }, { 0, 1, 1 }, { 0, 0, 1 }, { 0, 0, 0 }, { 0, 1, 0 }, { 0, 1, 1 },
    { 1, 0, 0 }, { 1, 0, 1 }, { 1, 1, 1 }, { 1, 0, 0 }, { 1, 1, 1 }, { 1, 1, 0 },
    { 0, 0, 0 }, { 0, 0, 1 }, { 1, 0, 1 }, { 0, 0, 0 }, { 1, 0, 1 }, { 1, 0, 0 },
    { 0, 1, 0 }, { 1, 1, 1 }, { 0, 1, 1 }, { 0, 1, 0 }, { 1, 1, 0 }, { 1, 1, 1 },
};

static const char nw_cloud_vs[] =
    "#version 330 core\n"
    "layout(location = 0) in vec3 a_pos;\n"
    "uniform mat4 u_lv;\n"
    "uniform vec4 u_min, u_max;\n"
    "out vec3 v_obj;\n"
    "void main() {\n"
    "    v_obj = mix(u_min.xyz, u_max.xyz, a_pos);\n"
    "    gl_Position = u_lv * vec4(v_obj, 1.0);\n"
    "}\n";

/* Marched in the cloud's own space (object space times u_shape), where the box is centre c, half size e. The shape is
   a cumulus building into a storm cell: a flat base under towers of round puffs, each rising and breathing on its own
   beat, its surface pushed in and out by the Worley billows and the Perlin-Worley of the noise (a signed distance in
   half heights), its edges eroded by finer noise, wispy under and billowing on top. The noise keeps its size in the
   world as the cutscene grows the card (u_grow), so the cloud builds new billows at its edges instead of swelling like
   a picture, and two layers of it rise at different speeds, so the billows churn. Light: the sun's transmittance a few
   steps towards it, a two-lobe Henyey-Greenstein phase (the bright rim against the sun), the sky's ambient by height;
   as the storm comes it gets thicker and heavier, its tops still in the sun and its base darker. Extinction is per the
   cloud's height, so it looks the same at any size. The colour is premultiplied, its highlights rolled off. */
static const char nw_cloud_fs[] =
    "#version 330 core\n"
    "uniform vec4 u_cam;\n" /* the camera, object space */
    "uniform vec4 u_min, u_max;\n"
    "uniform vec4 u_shape;\n" /* object to cloud space; w the seed */
    "uniform vec4 u_sun;\n" /* towards the sun, cloud space, unit */
    "uniform vec4 u_anim;\n" /* x z the drift, y the billows' rise, w the detail's rise and the puffs' beat, in turns */
    "uniform vec4 u_storm;\n" /* x how far the storm is, 0-1; y how far eroded; z how much of the cloud is left */
    "uniform vec4 u_grow;\n" /* xyz how far the cutscene has grown the card since the scene placed it */
    "uniform sampler3D u_noise;\n"
    "in vec3 v_obj;\n"
    "out vec4 o_col;\n"
    "const int STEPS = 64;\n"
    "const float EXTINCTION = 9.0;\n"
    /* A building cumulus, grey-white, turning heavy and slate as the storm gathers */
    "const vec3 ALBEDO = vec3(0.70, 0.71, 0.74);\n"
    "const vec3 HEAVY = vec3(0.40, 0.42, 0.47);\n"
    "const vec3 SUN = vec3(2.7, 2.5, 2.25);\n"
    "const vec3 SKY_HIGH = vec3(0.55, 0.63, 0.76);\n"
    "const vec3 SKY_LOW = vec3(0.16, 0.18, 0.23);\n"
    /* Under the storm's deck, as the sky's own clouds are lit there: teal, as the PS2's storm sky */
    "const vec3 STORM_ALBEDO = vec3(0.50, 0.53, 0.52);\n"
    "const vec3 STORM_LIGHT = vec3(0.75, 1.0, 0.95);\n"
    "const vec3 STORM_LOW = vec3(0.10, 0.30, 0.27);\n"
    "const vec3 STORM_HIGH = vec3(0.45, 0.70, 0.64);\n"
    "const int PUFFS = 12;\n"
    /* x in the cloud's half width, y and the radius in its half height: a base row, then towers over its middle */
    "const vec3 PUFF[PUFFS] = vec3[](vec3(-0.82, -0.46, 0.38), vec3(-0.52, -0.36, 0.48), vec3(-0.18, -0.30, 0.54),\n"
    "                                vec3(0.18, -0.30, 0.56), vec3(0.52, -0.38, 0.48), vec3(0.82, -0.48, 0.36),\n"
    "                                vec3(-0.40, 0.06, 0.48), vec3(-0.02, 0.16, 0.60), vec3(0.36, 0.04, 0.50),\n"
    "                                vec3(-0.20, 0.50, 0.42), vec3(0.16, 0.58, 0.40), vec3(0.46, 0.40, 0.32));\n"
    "vec3 c, e, g;\n"
    "float tile, aspect, flip, amp;\n"
    "float hg(float mu, float g) {\n"
    "    return (1.0 - g * g) / pow(1.0 + g * g - 2.0 * g * mu, 1.5);\n"
    "}\n"
    /* p in the cloud's space; returns 0-1 */
    "float density(vec3 p, bool fine) {\n"
    "    vec3 m = (p - c) / e.y;\n" /* round: units of the half height */
    "    m.x *= flip;\n"
    "    float sd = -1.0;\n"
    "    for (int i = 0; i < PUFFS; i++) {\n"
    "        float beat = u_anim.w * 6.2831853 * 2.0 + float(i) * 1.7;\n"
    "        vec3 pc = vec3(PUFF[i].x * aspect * 0.82, PUFF[i].y + 0.05 * sin(beat), 0.0);\n"
    "        float r = PUFF[i].z * (1.0 + 0.07 * sin(beat * 1.3 + 0.5));\n"
    "        sd = max(sd, r - length((m - pc) * vec3(1.0, 1.0, 1.25)));\n"
    "    }\n"
    "    sd = min(sd, (m.y + 0.78) * 0.8);\n" /* the flat base */
    "    sd = min(sd, min(aspect - abs(m.x), min(1.0 - abs(m.y), e.z / e.y - abs(m.z))));\n"
    "    if (sd < -0.4) {\n"
    "        return 0.0;\n"
    "    }\n"
    /* In the world's size about the centre: as the card grows the billows stay, and more come up at its edges. */
    "    vec3 q = (p - c) * g / tile + u_shape.w;\n"
    "    vec4 a = texture(u_noise, q + vec3(u_anim.x, -u_anim.y, u_anim.z));\n"
    "    vec4 b = texture(u_noise, q * 1.37 + vec3(0.41 + u_anim.z, 0.23 - u_anim.y * 1.7, 0.67 + u_anim.x));\n"
    "    float billow = mix(a.g * 0.55 + a.b * 0.30 + a.a * 0.15, b.g * 0.55 + b.b * 0.30 + b.a * 0.15, 0.45);\n"
    "    float pw = mix(a.r, b.r, 0.45);\n"
    "    float field = sd + ((billow - 0.55) * 0.8 + (pw - 0.75) * 0.25) * amp * smoothstep(-0.4, -0.1, sd);\n"
    "    if (fine && field > -0.2 && field < 0.35) {\n"
    "        vec4 hi = texture(u_noise, q * 3.1 + vec3(0.37, 0.61 - u_anim.w, 0.11));\n"
    "        float h = clamp((m.y + 0.78) * 0.9, 0.0, 1.0);\n"
    "        float f = hi.g * 0.65 + hi.b * 0.35;\n"
    "        field -= mix(f, 1.0 - f, h) * 0.2;\n"
    "    }\n"
    "    return smoothstep(u_storm.y, u_storm.y + 0.2, field) * u_storm.z;\n"
    "}\n"
    "void main() {\n"
    "    vec3 s = u_shape.xyz;\n"
    "    vec3 ro = u_cam.xyz * s;\n"
    "    vec3 rd = (v_obj - u_cam.xyz) * s;\n"
    "    float at = length(rd);\n"
    "    rd /= at;\n"
    "    vec3 bmin = min(u_min.xyz * s, u_max.xyz * s), bmax = max(u_min.xyz * s, u_max.xyz * s);\n"
    "    c = (bmin + bmax) * 0.5;\n"
    "    e = (bmax - bmin) * 0.5;\n"
    "    g = max(u_grow.xyz, vec3(0.05));\n"
    "    tile = 2.4 * e.y;\n"
    "    aspect = e.x / e.y;\n"
    "    flip = u_shape.w > 0.2 ? -1.0 : 1.0;\n"
    /* The billows' depth in half heights, as large in the world as the card grows: shallower against it. */
    "    amp = inversesqrt(max((g.x + g.y) * 0.5, 1.0));\n"
    "    vec3 t0 = (bmin - ro) / rd, t1 = (bmax - ro) / rd;\n"
    "    vec3 tlo = min(t0, t1), thi = max(t0, t1);\n"
    "    float tin = max(max(tlo.x, tlo.y), tlo.z), tout = min(min(thi.x, thi.y), thi.z);\n"
    /* Outside the box a face past the middle of the ray through it is a far face: the near one draws that ray. */
    "    if (tout <= max(tin, 0.0) || (tin > 0.0 && at > 0.5 * (tin + tout))) {\n"
    "        discard;\n"
    "    }\n"
    "    tin = max(tin, 0.0);\n"
    "    float dt = (tout - tin) / float(STEPS);\n"
    "    float t = tin + dt * fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));\n"
    "    float mu = dot(rd, u_sun.xyz);\n"
    "    float phase = mix(hg(mu, 0.65), hg(mu, -0.2), 0.5);\n"
    "    float heavy = smoothstep(0.0, 0.5, u_storm.x);\n" /* the storm gathering in it */
    "    float sigma = EXTINCTION * (1.0 + 0.9 * heavy) / e.y;\n"
    "    float storm = smoothstep(0.55, 0.95, u_storm.x);\n"
    "    vec3 albedo = mix(mix(ALBEDO, HEAVY, heavy), STORM_ALBEDO, storm), light = mix(SUN, STORM_LIGHT, storm);\n"
    "    vec3 low = mix(SKY_LOW, STORM_LOW, storm), high = mix(SKY_HIGH, STORM_HIGH, storm);\n"
    "    vec3 col = vec3(0.0);\n"
    "    float trans = 1.0;\n"
    "    for (int i = 0; i < STEPS && trans > 0.01; i++, t += dt) {\n"
    "        vec3 p = ro + rd * t;\n"
    "        float d = density(p, true);\n"
    "        if (d <= 0.002) {\n"
    "            continue;\n"
    "        }\n"
    "        float od = 0.0, ls = e.y * 0.08;\n"
    "        vec3 lp = p;\n"
    "        for (int k = 0; k < 5; k++) {\n"
    "            lp += u_sun.xyz * ls;\n"
    "            od += density(lp, k < 1) * ls;\n"
    "            ls *= 1.7;\n"
    "        }\n"
    /* The sun straight through, and a softer second term for the light scattered on inside, which a thick cloud
       lives on */
    "        float lit = (exp(-sigma * od * 0.5) + 0.3 * exp(-sigma * od * 0.12)) *\n"
    "              (1.0 - 0.5 * exp(-sigma * d * e.y * 0.4));\n"
    /* The ambient by height: the base in the cloud's own shadow, darker the heavier it is */
    "        float h = clamp(((p.y - c.y) / e.y + 0.78) * 0.6, 0.0, 1.0);\n"
    "        vec3 amb = mix(low * (1.0 - 0.5 * heavy), high, h * h);\n"
    "        vec3 lum = albedo * (light * lit * phase + amb);\n"
    "        float st = exp(-sigma * d * dt);\n"
    "        col += trans * lum * (1.0 - st);\n"
    "        trans *= st;\n"
    "    }\n"
    "    if (trans > 0.997) {\n"
    "        discard;\n"
    "    }\n"
    "    float a = 1.0 - trans;\n"
    "    vec3 lin = col / a;\n"
    "    float m = max(max(lin.r, lin.g), max(lin.b, 1e-4));\n" /* rolled off by its brightest, keeping its hue */
    "    col = lin * (mix(m, 1.0 - exp(-1.2 * m), smoothstep(0.45, 1.4, m)) / m) * a;\n"
    "    o_col = vec4(col, a);\n"
    "}\n";

/* The storm over a sky dome: its own triangles again, at their depth, each pixel the sky the way it looks; then a
   triangle over the whole screen at the far plane for the sky the dome leaves open, which only the clear Z passes.
   v_obj is a point on the pixel's ray in the dome's object space, homogeneous. */
static const char nw_cloud_sky_vs[] =
    "#version 330 core\n"
    "layout(location = 0) in vec4 a_pos;\n"
    "uniform mat4 u_lv;\n"
    "uniform mat4 u_inv;\n"  /* lv's inverse */
    "uniform vec4 u_full;\n" /* x 1: the screen's triangle */
    "out vec4 v_obj;\n"
    "void main() {\n"
    "    if (u_full.x > 0.0) {\n"
    "        vec2 c = vec2(gl_VertexID == 1 ? 3.0 : -1.0, gl_VertexID == 2 ? 3.0 : -1.0);\n"
    "        v_obj = u_inv * vec4(c, 0.0, 1.0);\n"
    "        gl_Position = vec4(c, 1.0, 1.0);\n"
    "    } else {\n"
    "        v_obj = vec4(a_pos.xyz, 1.0);\n"
    "        gl_Position = u_lv * vec4(a_pos.xyz, 1.0);\n"
    "    }\n"
    "}\n";

/* World space, y up. The whole sky in place of the dome's picture: a fair sky (a gradient, the sun's glow) and,
   between BASE and TOP, a layer of cumulus marched along the view. How stormy it is (the cutscene's
   time, or the dome's own weather) brings more clouds and bigger, first where the storm gathers, then over the sun,
   darker and darker until they close the sky; the sun reaches them through the deck above, as thick as its cover. */
static const char nw_cloud_sky_fs[] =
    "#version 330 core\n"
    "uniform vec4 u_cam;\n" /* the camera, the dome's object space */
    "uniform mat4 u_local;\n" /* the dome's axes */
    "uniform vec4 u_camw;\n" /* the camera, world; w how stormy, 0 a fair sky to 1 overcast */
    "uniform vec4 u_sun;\n" /* towards the sun, world, unit; w its angular radius */
    "uniform vec4 u_front;\n" /* towards where the storm gathers first, world, unit */
    "uniform vec4 u_anim;\n" /* xz the clouds' drift, w the detail's turn, in tiles */
    "uniform sampler3D u_noise;\n"
    "in vec4 v_obj;\n"
    "out vec4 o_col;\n"
    "const int STEPS = 12;\n"
    "const float BASE = 22.0;\n"
    "const float TOP = 44.0;\n"
    "const float TILE = 90.0;\n"
    "const float SIGMA = 0.24;\n"
    "const vec3 ZENITH = vec3(0.13, 0.48, 0.86);\n"
    "const vec3 MIDSKY = vec3(0.27, 0.71, 0.95);\n"
    "const vec3 HORIZON = vec3(0.70, 0.89, 0.96);\n"
    "const vec3 WARM = vec3(1.0, 0.86, 0.66);\n"
    "const vec3 STORMSKY = vec3(0.11, 0.32, 0.29);\n"
    "const vec3 PALE = vec3(0.56, 0.64, 0.70);\n"
    "const vec3 CORE = vec3(1.0, 1.0, 0.95);\n"
    "const vec3 HALO = vec3(1.0, 0.94, 0.72);\n"
    "const vec3 THROUGH = vec3(0.95, 0.92, 0.82);\n"
    "float cover, many;\n"
    "float hg(float mu, float g) {\n"
    "    return (1.0 - g * g) / pow(1.0 + g * g - 2.0 * g * mu, 1.5);\n"
    "}\n"
    /* Round Worley cells, a cloud each: the cover lowers the bar they have to clear, so more of them show and
       each grows, and the storm's smaller cells take over, so it brings more clouds rather than one big one; the
       bar rises with height, so only their cores reach the top, in domes over flat bases. */
    "float density(vec3 p, bool fine) {\n"
    "    float h = (p.y - BASE) / (TOP - BASE);\n"
    "    if (h <= 0.0 || h >= 1.0) {\n"
    "        return 0.0;\n"
    "    }\n"
    "    vec3 q = vec3(p.x, p.y * 1.3, p.z) / TILE + vec3(u_anim.x, 0.0, u_anim.z);\n"
    "    vec4 n = texture(u_noise, q);\n"
    "    float cells = n.g * 0.55 + n.b * 0.3 + n.r * 0.15;\n"
    "    if (many > 0.0) {\n"
    "        vec4 s = texture(u_noise, q * 1.8 + vec3(0.43, 0.17, 0.71));\n"
    "        cells = mix(cells, s.g * 0.55 + s.b * 0.3 + s.r * 0.15, many);\n"
    "    }\n"
    "    float th = mix(0.95, 0.25, cover) + h * mix(0.45, 0.2, cover);\n"
    "    float d = smoothstep(th, th + 0.07, cells) * smoothstep(0.0, 0.08, h);\n"
    "    if (fine && d > 0.0 && d < 0.95) {\n"
    "        vec4 m = texture(u_noise, q * 3.4 + vec3(0.21, 0.53 - u_anim.w, 0.77));\n"
    "        d = clamp(d - (m.g * 0.6 + m.b * 0.4) * 0.3 * (1.0 - d), 0.0, 1.0);\n"
    "    }\n"
    "    return d;\n"
    "}\n"
    "void main() {\n"
    "    vec3 d = normalize(mat3(u_local) * (v_obj.xyz / v_obj.w - u_cam.xyz));\n"
    "    vec3 cam = u_camw.xyz;\n"
    "    float p = u_camw.w;\n"
    "    float storm = smoothstep(0.55, 0.95, p);\n"
    "    many = smoothstep(0.45, 0.9, p);\n" /* the storm brings more clouds, smaller, rather than bigger ones */
    /* The fair sky: from the horizon to the zenith, warm low down on the sun's side; the sun a white core in a wide
       warm glow. The storm darkens it. */
    "    float e = d.y;\n"
    "    vec3 sky = mix(HORIZON, MIDSKY, smoothstep(-0.02, 0.22, e));\n"
    "    sky = mix(sky, ZENITH, smoothstep(0.22, 0.95, e));\n"
    "    float side = max(dot(normalize(d.xz + vec2(1e-5, 0.0)), normalize(u_sun.xz + vec2(1e-5, 0.0))), 0.0);\n"
    "    sky = mix(sky, WARM, 0.4 * side * side * (1.0 - smoothstep(0.0, 0.45, e)));\n"
    "    float sa = acos(clamp(dot(d, u_sun.xyz), -1.0, 1.0)), r = u_sun.w;\n"
    "    float core = 1.0 - smoothstep(r * 0.2, r * 0.38, sa);\n"
    "    vec3 glow = HALO * (0.55 * exp(-pow(sa / (r * 0.7), 2.0)) + 0.18 * exp(-pow(sa / (r * 3.0), 2.0)));\n"
    "    sky = mix(min(sky + glow, vec3(1.0)), CORE, core);\n"
    "    sky = mix(sky, PALE, 0.4 * smoothstep(0.3, 0.8, p) * (1.0 - storm));\n"
    "    sky = mix(sky, STORMSKY, 0.65 * storm);\n"
    /* The cumulus: scattered fair-weather clouds that grow in number and size as the storm comes, first
       where it gathers, then over the sun, until they close the sky. */
    "    float fa = acos(clamp(dot(d, u_front.xyz), -1.0, 1.0));\n"
    /* The front: from where the storm gathers it spreads across the sky as the storm goes on, its edge ragged with
       the noise, so the clouds roll in from that side rather than thickening everywhere at once; by the end it has
       crossed the whole sky. */
    "    float reach = mix(0.25, 3.3, pow(smoothstep(0.15, 1.0, p), 1.5));\n"
    "    float ragged = texture(u_noise, vec3(d.x * 1.7 + u_anim.x, 0.31, d.z * 1.7 + u_anim.z)).g - 0.5;\n"
    "    float front = (1.0 - smoothstep(reach - 0.6, reach + 0.1, fa + ragged * 0.5)) * smoothstep(0.08, 0.4, p);\n"
    "    cover = 0.26 + 0.35 * p * p + 0.42 * front + 0.25 * smoothstep(0.6, 0.9, p) * exp(-pow(sa / 0.6, 2.0));\n"
    "    cover = clamp(cover, 0.0, 1.0);\n"
    "    vec3 col = vec3(0.0);\n"
    "    float trans = 1.0;\n"
    "    float horizon = smoothstep(0.02, 0.2, e);\n"
    "    if (horizon > 0.0) {\n"
    "        float t0 = (BASE - cam.y) / e, t1 = min((TOP - cam.y) / e, t0 + 45.0);\n"
    "        float dt = (t1 - t0) / float(STEPS);\n"
    "        float t = t0 + dt * fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));\n"
    "        float mu = dot(d, u_sun.xyz);\n"
    "        float phase = mix(hg(mu, 0.5), hg(mu, -0.2), 0.5);\n"
    "        float fade = exp(-t0 / 170.0) * horizon;\n"
    /* Fair: white clouds with lavender shadows; under the storm teal, as the PS2's storm sky. */
    "        vec3 light = mix(vec3(2.2, 2.1, 1.9), vec3(0.75, 1.0, 0.95), storm);\n"
    "        vec3 albedo = mix(vec3(0.92), vec3(0.62, 0.66, 0.64), storm);\n"
    "        vec3 under = mix(vec3(0.62, 0.66, 0.86), vec3(0.10, 0.30, 0.27), storm);\n"
    "        vec3 over = mix(vec3(0.80, 0.86, 0.98), vec3(0.45, 0.70, 0.64), storm);\n"
    "        float shade = mix(0.45, 0.8, storm);\n"
    "        for (int i = 0; i < STEPS && trans > 0.02; i++, t += dt) {\n"
    "            vec3 pos = cam + d * t;\n"
    "            float dens = density(pos, true) * fade;\n"
    "            if (dens <= 0.002) {\n"
    "                continue;\n"
    "            }\n"
    /* Towards the sun: two samples near, then the rest of the deck above as thick as its cover. */
    "            float od = density(pos + u_sun.xyz * 3.0, false) * 3.0 +\n"
    "                       density(pos + u_sun.xyz * 8.0, false) * 5.0 +\n"
    "                       cover * cover * mix(0.7, 0.35, storm) * (TOP - pos.y) / max(u_sun.y, 0.15);\n"
    "            float h = (pos.y - BASE) / (TOP - BASE);\n"
    "            vec3 lum = albedo * (light * exp(-SIGMA * od * shade) * phase + mix(under, over, h));\n"
    "            float st = exp(-SIGMA * dens * dt);\n"
    "            col += trans * lum * (1.0 - st);\n"
    "            trans *= st;\n"
    "        }\n"
    "        float ca = 1.0 - trans;\n"
    /* The sun through the cloud in front of it: thin cloud lit white around it, thick cloud a dull glow. */
    "        col += THROUGH * ca * (1.0 - 0.6 * ca) * exp(-pow(sa / (r * 1.4), 2.0)) * (1.0 - 0.6 * p);\n"
    "        vec3 lin = ca > 0.0 ? col / ca : vec3(0.0);\n"
    "        float m = max(max(lin.r, lin.g), max(lin.b, 1e-4));\n" /* rolled off by its brightest, keeping its hue */
    "        col = lin * (mix(m, 1.0 - exp(-1.2 * m), smoothstep(0.45, 1.4, m)) / m) * ca;\n"
    "    }\n"
    "    o_col = vec4(sky * trans + col, 1.0);\n"
    "}\n";

static uint32_t nw_cloud_hash(uint32_t x, uint32_t y, uint32_t z, uint32_t seed)
{
    uint32_t h = x * 0x8DA6B343u ^ y * 0xD8163841u ^ z * 0xCB1AB31Fu ^ seed * 0x165667B1u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return h;
}

static float nw_cloud_unit(uint32_t h)
{
    return (float)(h >> 8) / 16777216.0f;
}

static int nw_cloud_wrap(int i, int period)
{
    return ((i % period) + period) % period;
}

/* Gradient noise of period cells, about -1 to 1, its lattice's gradients (an edge of the cube each) in grads. */
static float nw_cloud_perlin(const uint8_t *grads, int period, float x, float y, float z)
{
    static const float dirs[12][3] = { { 1, 1, 0 }, { -1, 1, 0 }, { 1, -1, 0 }, { -1, -1, 0 },
                                       { 1, 0, 1 }, { -1, 0, 1 }, { 1, 0, -1 }, { -1, 0, -1 },
                                       { 0, 1, 1 }, { 0, -1, 1 }, { 0, 1, -1 }, { 0, -1, -1 } };
    int xi = (int)floorf(x), yi = (int)floorf(y), zi = (int)floorf(z);
    float f[3] = { x - (float)xi, y - (float)yi, z - (float)zi }, w[3];
    for (int k = 0; k < 3; k++) {
        w[k] = f[k] * f[k] * f[k] * (f[k] * (f[k] * 6.0f - 15.0f) + 10.0f);
    }
    float n[8];
    for (int k = 0; k < 8; k++) {
        int dx = k & 1, dy = (k >> 1) & 1, dz = k >> 2;
        int at = (nw_cloud_wrap(zi + dz, period) * period + nw_cloud_wrap(yi + dy, period)) * period +
                 nw_cloud_wrap(xi + dx, period);
        const float *g = dirs[grads[at]];
        n[k] = g[0] * (f[0] - (float)dx) + g[1] * (f[1] - (float)dy) + g[2] * (f[2] - (float)dz);
    }
    for (int k = 0; k < 4; k++) {
        n[k] = n[2 * k] + (n[2 * k + 1] - n[2 * k]) * w[0];
    }
    n[0] += (n[1] - n[0]) * w[1];
    n[2] += (n[3] - n[2]) * w[1];
    return n[0] + (n[2] - n[0]) * w[2];
}

/* 1 at the nearest of one point a cell (points in cell units, period cells a side), 0 a cell away or more. */
static float nw_cloud_worley(const float (*points)[3], int period, float x, float y, float z)
{
    int xi = (int)floorf(x), yi = (int)floorf(y), zi = (int)floorf(z);
    float best = 1.0f;
    for (int dz = -1; dz <= 1; dz++) {
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                int cx = xi + dx, cy = yi + dy, cz = zi + dz;
                const float *pt = points[(nw_cloud_wrap(cz, period) * period + nw_cloud_wrap(cy, period)) * period +
                                         nw_cloud_wrap(cx, period)];
                float ex = (float)cx + pt[0] - x, ey = (float)cy + pt[1] - y, ez = (float)cz + pt[2] - z;
                float d2 = ex * ex + ey * ey + ez * ez;
                best = d2 < best ? d2 : best;
            }
        }
    }
    return 1.0f - sqrtf(best);
}

static uint8_t nw_cloud_byte(float v)
{
    return (uint8_t)(v <= 0.0f ? 0 : v >= 1.0f ? 255 : (int)(v * 255.0f + 0.5f));
}

/* RGBA: R Perlin-Worley (Perlin fbm dilated by the Worley fbm of G B A), G B A Worley at 4, 8 and 16 cells a tile. */
static uint8_t *nw_cloud_texels(void)
{
    enum { N = NW_CLOUD_NOISE, CELLS = 4 * 4 * 4 + 8 * 8 * 8 + 16 * 16 * 16 };
    static const int worley[3] = { 4, 8, 16 };
    uint8_t *texels = SDL_malloc((size_t)N * N * N * 4);
    float(*points)[3] = SDL_malloc(sizeof(*points) * CELLS);
    uint8_t *grads = SDL_malloc(CELLS);
    if (!texels || !points || !grads) {
        SDL_free(texels);
        SDL_free(points);
        SDL_free(grads);
        return NULL;
    }
    float(*wpts[3])[3];
    uint8_t *pgrad[3];
    size_t at = 0;
    for (int o = 0; o < 3; o++) {
        int p = worley[o];
        wpts[o] = points + at;
        pgrad[o] = grads + at;
        for (int i = 0; i < p * p * p; i++) {
            uint32_t h = nw_cloud_hash((uint32_t)i, (uint32_t)p, 1u, 0x5EEDu);
            wpts[o][i][0] = nw_cloud_unit(h);
            wpts[o][i][1] = nw_cloud_unit(nw_cloud_hash(h, 2u, (uint32_t)p, 0x5EEDu));
            wpts[o][i][2] = nw_cloud_unit(nw_cloud_hash(h, 3u, (uint32_t)p, 0x5EEDu));
            pgrad[o][i] = (uint8_t)(nw_cloud_hash((uint32_t)i, (uint32_t)p, 7u, 0xC10Du) % 12u);
        }
        at += (size_t)(p * p * p);
    }
    uint8_t *t = texels;
    for (int z = 0; z < N; z++) {
        for (int y = 0; y < N; y++) {
            for (int x = 0; x < N; x++, t += 4) {
                float u[3] = { ((float)x + 0.5f) / N, ((float)y + 0.5f) / N, ((float)z + 0.5f) / N };
                float w[3], pn = 0.0f;
                for (int o = 0; o < 3; o++) {
                    float f = (float)worley[o];
                    w[o] = nw_cloud_worley((const float(*)[3])wpts[o], worley[o], u[0] * f, u[1] * f, u[2] * f);
                    pn += nw_cloud_perlin(pgrad[o], worley[o], u[0] * f, u[1] * f, u[2] * f) / (float)(1 << o);
                }
                float perlin = 0.5f + 0.5f * pn / 1.75f, fbm = w[0] * 0.625f + w[1] * 0.25f + w[2] * 0.125f;
                t[0] = nw_cloud_byte(fbm + perlin * (1.0f - fbm));
                t[1] = nw_cloud_byte(w[0]);
                t[2] = nw_cloud_byte(w[1]);
                t[3] = nw_cloud_byte(w[2]);
            }
        }
    }
    SDL_free(points);
    SDL_free(grads);
    return texels;
}

static GLuint nw_cloud_shader(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    GLint ok = 0;
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, NW_APP_NAME ": cloud shader: %s\n", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

/* A program of the two sources, 0 when it does not build. */
static GLuint nw_cloud_program(const char *vs_src, const char *fs_src)
{
    GLuint prog = 0;
    GLuint vs = nw_cloud_shader(GL_VERTEX_SHADER, vs_src), fs = nw_cloud_shader(GL_FRAGMENT_SHADER, fs_src);
    if (vs && fs) {
        GLuint p = glCreateProgram();
        GLint ok = 0;
        glAttachShader(p, vs);
        glAttachShader(p, fs);
        glLinkProgram(p);
        glGetProgramiv(p, GL_LINK_STATUS, &ok);
        if (ok) {
            prog = p;
        } else {
            char log[2048];
            glGetProgramInfoLog(p, sizeof(log), NULL, log);
            fprintf(stderr, NW_APP_NAME ": cloud program: %s\n", log);
            glDeleteProgram(p);
        }
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

/* The programs, the box and the noise, once; false when any is missing. */
static bool nw_cloud_init(void)
{
    if (nw_cloud.tried) {
        return nw_cloud.prog && nw_cloud.sky && nw_cloud.noise;
    }
    nw_cloud.tried = true;
    uint64_t start = SDL_GetTicksNS();
    nw_cloud.prog = nw_cloud_program(nw_cloud_vs, nw_cloud_fs);
    nw_cloud.sky = nw_cloud_program(nw_cloud_sky_vs, nw_cloud_sky_fs);
    if (!nw_cloud.prog || !nw_cloud.sky) {
        return false;
    }
    nw_cloud.lv = glGetUniformLocation(nw_cloud.prog, "u_lv");
    nw_cloud.cam = glGetUniformLocation(nw_cloud.prog, "u_cam");
    nw_cloud.min = glGetUniformLocation(nw_cloud.prog, "u_min");
    nw_cloud.max = glGetUniformLocation(nw_cloud.prog, "u_max");
    nw_cloud.shape = glGetUniformLocation(nw_cloud.prog, "u_shape");
    nw_cloud.sun = glGetUniformLocation(nw_cloud.prog, "u_sun");
    nw_cloud.anim = glGetUniformLocation(nw_cloud.prog, "u_anim");
    nw_cloud.storm = glGetUniformLocation(nw_cloud.prog, "u_storm");
    nw_cloud.grow = glGetUniformLocation(nw_cloud.prog, "u_grow");
    nw_cloud.tex = glGetUniformLocation(nw_cloud.prog, "u_noise");
    nw_cloud.sky_lv = glGetUniformLocation(nw_cloud.sky, "u_lv");
    nw_cloud.sky_inv = glGetUniformLocation(nw_cloud.sky, "u_inv");
    nw_cloud.sky_full = glGetUniformLocation(nw_cloud.sky, "u_full");
    nw_cloud.sky_cam = glGetUniformLocation(nw_cloud.sky, "u_cam");
    nw_cloud.sky_local = glGetUniformLocation(nw_cloud.sky, "u_local");
    nw_cloud.sky_camw = glGetUniformLocation(nw_cloud.sky, "u_camw");
    nw_cloud.sky_sun = glGetUniformLocation(nw_cloud.sky, "u_sun");
    nw_cloud.sky_front = glGetUniformLocation(nw_cloud.sky, "u_front");
    nw_cloud.sky_anim = glGetUniformLocation(nw_cloud.sky, "u_anim");
    nw_cloud.sky_tex = glGetUniformLocation(nw_cloud.sky, "u_noise");

    glGenVertexArrays(1, &nw_cloud.vao);
    glGenBuffers(1, &nw_cloud.vbo);
    glBindVertexArray(nw_cloud.vao);
    glBindBuffer(GL_ARRAY_BUFFER, nw_cloud.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(nw_cloud_box), nw_cloud_box, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), NULL);
    glBindVertexArray(0);

    uint8_t *texels = nw_cloud_texels();
    if (texels) {
        glGenTextures(1, &nw_cloud.noise);
        glActiveTexture(GL_TEXTURE0 + NW_CLOUD_UNIT);
        glBindTexture(GL_TEXTURE_3D, nw_cloud.noise);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAX_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_REPEAT);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA8, NW_CLOUD_NOISE, NW_CLOUD_NOISE, NW_CLOUD_NOISE, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, texels);
        glActiveTexture(GL_TEXTURE0);
        SDL_free(texels);
    }
    SDL_Log("clouds: noise and programs ready in %.1f ms", (double)(SDL_GetTicksNS() - start) / 1e6);
    return nw_cloud.noise != 0;
}

bool nw_cloudvol_invert(const float m[16], float out[16])
{
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] +
             m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] -
             m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] +
             m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] -
              m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] -
             m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] +
             m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] -
             m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] +
              m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] +
             m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] -
             m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] +
              m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] -
              m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] -
             m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] +
             m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] -
              m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] +
              m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (!(fabsf(det) > 1e-30f) || !isfinite(det)) {
        return false;
    }
    for (int i = 0; i < 16; i++) {
        out[i] = inv[i] / det;
    }
    return true;
}

/* The camera in lv's object space: the point lv takes to x = y = w = 0, row 2 of the inverse as a row vector times
   it is. False when lv has no inverse or no camera. */
static bool nw_cloud_camera(const float lv[16], float cam[4], float inv[16])
{
    if (!nw_cloudvol_invert(lv, inv) || !(fabsf(inv[11]) > 1e-30f)) {
        return false;
    }
    cam[0] = inv[8] / inv[11];
    cam[1] = inv[9] / inv[11];
    cam[2] = inv[10] / inv[11];
    cam[3] = 0.0f;
    return true;
}

static void nw_cloud_normalise(float v[4])
{
    float len = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    for (int c = 0; c < 3 && len > 0.0f; c++) {
        v[c] /= len;
    }
}

static void nw_cloud_noise(void)
{
    glActiveTexture(GL_TEXTURE0 + NW_CLOUD_UNIT);
    glBindTexture(GL_TEXTURE_3D, nw_cloud.noise);
    glActiveTexture(GL_TEXTURE0);
}

bool nw_cloudvol_ready(void)
{
    return nw_cloud_init();
}

unsigned int nw_cloudvol_noise(void)
{
    return nw_cloud_init() ? nw_cloud.noise : 0;
}

bool nw_cloudvol_draw(const NwGpuCloud *cloud, const float local[16], const float lv[16], float seconds, float storm)
{
    float cam[4], inv[16];
    if (!nw_cloud_init() || !nw_cloud_camera(lv, cam, inv)) {
        return false;
    }
    bool inside = true;
    for (int k = 0; k < 3; k++) {
        float lo = SDL_min(cloud->min[k], cloud->max[k]), hi = SDL_max(cloud->min[k], cloud->max[k]);
        float margin = (hi - lo) * 0.01f;
        inside = inside && cam[k] > lo - margin && cam[k] < hi + margin;
    }

    /* Towards the sun in the cloud's space. The scene's sun is a card in the sky, so its light comes the way the camera
       sees it, as from far off: from the camera to the card, in object space through the inverse of the cloud's axes,
       then scaled as the cloud's space is. Without a sun, from above and to the side. */
    float sun[4] = { 0.35f, 0.85f, -0.4f, 0.0f };
    float axes[16] = { local[0], local[1], local[2], 0, local[4], local[5], local[6], 0,
                       local[8], local[9], local[10], 0, 0, 0, 0, 1 }, axinv[16];
    if (cloud->sun_ok && nw_cloudvol_invert(axes, axinv)) {
        float d[3] = { cloud->sun[0] - local[12], cloud->sun[1] - local[13], cloud->sun[2] - local[14] };
        for (int c = 0; c < 3; c++) {
            sun[c] = (d[0] * axinv[c] + d[1] * axinv[4 + c] + d[2] * axinv[8 + c] - cam[c]) * cloud->shape[c];
        }
    }
    nw_cloud_normalise(sun);
    /* Speeds in tiles a second that are whole turns in the 10000 s the clock wraps at, so it wraps unseen: the drift
       along the wind, and two rises of the billows at different speeds, so they churn rather than slide. */
    const float anim[4] = { fmodf(seconds * 0.006f, 1.0f), fmodf(seconds * 0.017f, 1.0f),
                            fmodf(seconds * 0.004f, 1.0f), fmodf(seconds * 0.03f, 1.0f) };
    const float bmin[4] = { cloud->min[0], cloud->min[1], cloud->min[2], 0.0f };
    const float bmax[4] = { cloud->max[0], cloud->max[1], cloud->max[2], 0.0f };
    const float shape[4] = { cloud->shape[0], cloud->shape[1], cloud->shape[2], cloud->seed };
    /* How much the cutscene has grown the card since the scene placed it, per axis: the billows keep their size in the
       world as it grows, so new ones come up at its edges rather than the old ones swelling as a picture does. */
    float grow[4] = { 1.0f, 1.0f, 1.0f, 0.0f };
    for (int k = 0; k < 3; k++) {
        float len = sqrtf(local[4 * k] * local[4 * k] + local[4 * k + 1] * local[4 * k + 1] +
                          local[4 * k + 2] * local[4 * k + 2]);
        grow[k] = cloud->shape[k] > 0.0f && len > 0.0f ? len / cloud->shape[k] : 1.0f;
    }
    /* As the storm's own clouds close the sky the card's cloud spreads and thins into them: its edge eats in, the thin
       parts first, and what is left fades into the deck, with no step. */
    if (storm >= 0.97f) {
        return true; /* gone into the storm's clouds */
    }
    float erode = SDL_clamp((storm - 0.62f) / 0.33f, 0.0f, 1.0f);
    float fade = 1.0f - SDL_clamp((storm - 0.8f) / 0.17f, 0.0f, 1.0f);
    const float weather[4] = { storm, erode * erode * (3.0f - 2.0f * erode) * 0.9f, fade * fade * (3.0f - 2.0f * fade),
                               0.0f };

    glUseProgram(nw_cloud.prog);
    glUniformMatrix4fv(nw_cloud.lv, 1, GL_FALSE, lv);
    glUniform4fv(nw_cloud.cam, 1, cam);
    glUniform4fv(nw_cloud.min, 1, bmin);
    glUniform4fv(nw_cloud.max, 1, bmax);
    glUniform4fv(nw_cloud.shape, 1, shape);
    glUniform4fv(nw_cloud.sun, 1, sun);
    glUniform4fv(nw_cloud.anim, 1, anim);
    glUniform4fv(nw_cloud.storm, 1, weather);
    glUniform4fv(nw_cloud.grow, 1, grow);
    glUniform1i(nw_cloud.tex, NW_CLOUD_UNIT);
    nw_cloud_noise();
    glEnable(GL_BLEND);
    glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
    /* From inside the near faces are behind the camera: the far ones draw, over whatever is in the box. */
    glDepthFunc(inside ? GL_ALWAYS : GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
    glBindVertexArray(nw_cloud.vao);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    return true;
}

bool nw_cloudvol_sky(const NwGpuMesh *dome, const float local[16], const float lv[16], float seconds,
                     const NwCloudSky *sky)
{
    float cam[4], inv[16];
    if (!dome->vao || !nw_cloud_init() || !nw_cloud_camera(lv, cam, inv)) {
        return false;
    }
    const NwGpuCloud *cloud = &dome->cloud;
    float camw[4], sun[4] = { 0.35f, 0.85f, -0.4f, 0.0f }, front[4] = { 0.0f, 1.0f, 0.0f, 0.0f };
    for (int c = 0; c < 3; c++) {
        camw[c] = cam[0] * local[c] + cam[1] * local[4 + c] + cam[2] * local[8 + c] + local[12 + c];
    }
    camw[3] = sky->storm;
    /* The sun the way the camera sees it: its card when the frame has it (as large as the card), else the scene's
       sun, as large as its card looks from the beach. */
    const float *at = sky->sun_on ? sky->sun_at : cloud->sun_ok ? cloud->sun : NULL;
    for (int c = 0; c < 3; c++) {
        if (at) {
            sun[c] = at[c] - camw[c];
        }
        front[c] = cloud->front[c] - camw[c];
    }
    float dist = sqrtf(sun[0] * sun[0] + sun[1] * sun[1] + sun[2] * sun[2]);
    nw_cloud_normalise(sun);
    nw_cloud_normalise(front);
    sun[3] = sky->sun_on && dist > 0.0f ? atanf(sky->sun_radius / dist) : 0.2f;
    const float anim[4] = { fmodf(seconds * 0.03f, 1.0f), 0.0f, fmodf(seconds * 0.012f, 1.0f),
                            fmodf(seconds * 0.02f, 1.0f) };

    static const float dome_pass[4] = { 0.0f, 0.0f, 0.0f, 0.0f }, screen_pass[4] = { 1.0f, 0.0f, 0.0f, 0.0f };

    glUseProgram(nw_cloud.sky);
    glUniformMatrix4fv(nw_cloud.sky_lv, 1, GL_FALSE, lv);
    glUniformMatrix4fv(nw_cloud.sky_inv, 1, GL_FALSE, inv);
    glUniform4fv(nw_cloud.sky_full, 1, dome_pass);
    glUniformMatrix4fv(nw_cloud.sky_local, 1, GL_FALSE, local);
    glUniform4fv(nw_cloud.sky_cam, 1, cam);
    glUniform4fv(nw_cloud.sky_camw, 1, camw);
    glUniform4fv(nw_cloud.sky_sun, 1, sun);
    glUniform4fv(nw_cloud.sky_front, 1, front);
    glUniform4fv(nw_cloud.sky_anim, 1, anim);
    glUniform1i(nw_cloud.sky_tex, NW_CLOUD_UNIT);
    nw_cloud_noise();
    glEnable(GL_BLEND);
    glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
    glBindVertexArray(dome->vao);
    glDrawElements(GL_TRIANGLES, (GLsizei)(dome->mesh.ntris * 3), GL_UNSIGNED_INT, NULL);
    glUniform4fv(nw_cloud.sky_full, 1, screen_pass);
    glBindVertexArray(nw_cloud.vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    return true;
}

void nw_cloudvol_shutdown(void)
{
    if (nw_cloud.prog) {
        glDeleteProgram(nw_cloud.prog);
    }
    if (nw_cloud.sky) {
        glDeleteProgram(nw_cloud.sky);
    }
    if (nw_cloud.vao) {
        glDeleteVertexArrays(1, &nw_cloud.vao);
        glDeleteBuffers(1, &nw_cloud.vbo);
    }
    if (nw_cloud.noise) {
        glDeleteTextures(1, &nw_cloud.noise);
    }
    memset(&nw_cloud, 0, sizeof(nw_cloud));
}
