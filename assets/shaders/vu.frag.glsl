in vec4 v_col;
in vec2 v_uv;
uniform usampler2D u_tex;
uniform usampler2DArray u_array;
uniform int u_layer;
#if ATS
uniform vec3 u_atsuv[2];
uniform ivec2 u_atsmode;
#endif
uniform usampler2D u_clut;
uniform sampler2D u_surf;
uniform sampler2D u_depth;
uniform usampler2D u_zpal;
uniform uvec4 u_zclut;
uniform uvec3 u_zclear;
uniform int u_zalpha;
uniform int u_zad;
uniform sampler2D u_dst;
uniform ivec4 u_gsblend;
uniform ivec4 u_texmode;
uniform ivec4 u_texlod;
uniform int u_filter;
uniform int u_tcc;
uniform int u_rgba8;
uniform int u_ascolour;
uniform int u_black;
uniform ivec4 u_atest;
uniform vec4 u_vu[2];
uniform vec4 u_frag;
#if EFFECTS
uniform vec4 u_fx[4];
uniform mat4 u_lv;
in vec3 v_fx;
in vec3 v_nrm;
in vec3 v_pos;
#endif
out vec4 o_col;

/* Inline expression helpers preserve the original arithmetic after preprocessing. */
#define FADE(lo, hi, x) (1.0 - smoothstep(lo, hi, x))
#define GS_COLOUR(c, a) vec4(clamp(c, 0.0, 1.0) * 255.0, a)
#define TAU 6.2831853

/* GS texture addressing, filtering and depth-palette lookup. */
ivec2 tex_size(int lod)
{
#if !PARTICLE
    if (u_layer >= 0) return textureSize(u_array, lod).xy;
#endif
#if PARTICLE
    return textureSize(u_tex, lod);
#else
    return u_texmode.x == 3 ? textureSize(u_surf, 0) : textureSize(u_tex, lod);
#endif
}
int wrap(int p, int n)
{
#if PROGRAM == PTL
    return p & (n - 1);
#else
    return p >= 0 ? p % n : n - 1 - (-1 - p) % n;
#endif
}
#if ATS
int address(int p, int n, int clamp_axis)
{
    if (u_atsmode.x == 2) { int r = wrap(p, n * 2); return r < n ? r : n * 2 - 1 - r; }
    return (u_atsmode.x < 0 ? clamp_axis : u_atsmode.x) == 1 ? clamp(p, 0, n - 1) : wrap(p, n);
}
#endif
vec4 texel(ivec2 p, int lod)
{
    ivec2 size = tex_size(lod);
#if ATS
    p = ivec2(address(p.x, size.x, u_texmode.z), address(p.y, size.y, u_texmode.w));
#else
    p = ivec2(u_texmode.z != 0 ? clamp(p.x, 0, size.x - 1) : wrap(p.x, size.x),
              u_texmode.w != 0 ? clamp(p.y, 0, size.y - 1) : wrap(p.y, size.y));
#endif
#if PARTICLE
    return vec4(texelFetch(u_tex, p, lod));
#else
    if (u_layer >= 0) return vec4(texelFetch(u_array, ivec3(p, u_layer), lod));
    if (u_texmode.x == 3) {
        vec4 s = texelFetch(u_surf, ivec2(p.x, size.y - 1 - p.y), 0);
        return floor(vec4(s.rgb * 255.0, s.a * 128.0) + 0.5);
    }
    uvec4 c = texelFetch(u_tex, p, lod);
    return vec4(u_texmode.x == 2 ? c : texelFetch(u_clut, ivec2(int(c.r), 0), 0));
#endif
}
vec4 sample_level(vec2 uv, int lod, bool bilinear)
{
#if ATS
    if (u_atsmode.y >= 0) {
        ivec2 mode = u_atsmode.x < 0 ? u_texmode.zw : ivec2(u_atsmode.x);
        uv = vec2(mode.x == 1 ? clamp(uv.x, 0.0, 1.0) : mod(uv.x, mode.x == 2 ? 2.0 : 1.0),
                  mode.y == 1 ? clamp(uv.y, 0.0, 1.0) : mod(uv.y, mode.y == 2 ? 2.0 : 1.0));
    }
#endif
    vec2 t = uv * vec2(tex_size(lod));
    if (!bilinear) {
        return texel(ivec2(floor(t)), lod);
    }
    t -= 0.5;
    ivec2 i = ivec2(floor(t));
    vec2 f = t - floor(t);
    return mix(mix(texel(i, lod), texel(i + ivec2(1, 0), lod), f.x),
               mix(texel(i + ivec2(0, 1), lod), texel(i + 1, lod), f.x), f.y);
}
bool bilerp(bool gs) { return u_filter == 0 || u_texmode.x >= 3 ? gs : u_filter == 2; }
vec4 sample_gs(vec2 uv)
{
#if !PARTICLE
#if ATS
    if (u_atsmode.y >= 0) {
        bool bilinear = bilerp(u_texmode.y == 1);
        if (u_atsmode.y == 0) return sample_level(uv, 0, bilinear);
        vec2 size = vec2(tex_size(0));
        float rho = max(length(dFdx(uv) * size), length(dFdy(uv) * size));
        float lod = clamp(log2(max(rho, 1.0)), 0.0, float(u_atsmode.y));
        if (u_filter == 1) return sample_level(uv, int(lod + 0.5), false);
        int level = int(lod);
        return mix(sample_level(uv, level, bilinear), sample_level(uv, min(level + 1, u_atsmode.y), bilinear), fract(lod));
    }
#endif
    if (u_layer >= 0) return sample_level(uv, 0, bilerp(u_texmode.y == 1));
#endif
#if PROGRAM == PTL
    return sample_level(uv, 0, true);
#else
    float lod = log2(1.0 / gl_FragCoord.w) * float(1 << u_texlod.z) + float(u_texlod.w) / 16.0;
    if (lod <= 0.0 || u_texlod.x < 2) {
        return sample_level(uv, 0, bilerp((lod <= 0.0 ? u_texmode.y : u_texlod.x) == 1));
    }
    bool bilinear = bilerp(u_texlod.x >= 4);
    lod = min(lod, float(u_texlod.y));
    if (u_texlod.x == 3 || u_texlod.x == 5) {
        int l0 = int(lod);
        return mix(sample_level(uv, l0, bilinear), sample_level(uv, min(l0 + 1, u_texlod.y), bilinear),
                   fract(lod));
    }
    return sample_level(uv, min(int(lod + 0.5), u_texlod.y), bilinear);
#endif
}
vec4 filtered(vec2 st)
{
    return floor(texture(u_surf, vec2(st.x, 1.0 - st.y)) * vec4(255.0, 255.0, 255.0, 128.0) + 0.5);
}
/* The GS Z under the pixel, back from the depth the GS matrix (src/gfx/backend.c) gave it. */
vec4 zclut()
{
    float d = texelFetch(u_depth, ivec2(gl_FragCoord.xy), 0).r;
    uint z = uint((1.0 - d) * 8388607.0 + 0.5);
    uint i = z < u_zclut.y ? u_zclut.z
           : z - u_zclut.y >= (256u << u_zclut.x) ? u_zclut.w : (z >> u_zclut.x) & 0xFFu;
    vec4 t = vec4(texelFetch(u_zpal, ivec2(int(i & 0xFFu), 0), 0));
    if (i == 0x100u) {
        t = vec4(0.0);
    } else if (i == 0x200u) {
        t = vec4(texelFetch(u_zpal, ivec2(int((z >> u_zclut.x) & 0xFFu), 0), 0));
    }
    if (u_zclear.x == 1u && t.a < 128.0 && z <= u_zclear.y) {
        t.a = 128.0;
    }
    if (u_zclear.x == 1u && t.a >= 128.0 && z >= u_zclear.z) {
        t.a = 0.0;
    }
    if (u_zclear.x == 2u && t.a >= 128.0 && ((u_zclear.y != 0u && z <= u_zclear.y) ||
                                           (u_zclear.z != 0u && z >= u_zclear.z))) {
        t = vec4(0.0);
    }
    return t;
}
#if EFFECTS
#define DETAIL_NOISE(p) fx_noise(p, 1000.0)

float fx_hash(vec2 p)
{
    p = fract(p * vec2(0.1031, 0.1030));
    p += dot(p, p.yx + 33.33);
    return fract((p.x + p.y) * p.x);
}
/* Value noise, periodic in x every per cells. */
float fx_noise(vec2 p, float per)
{
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float x0 = mod(i.x, per), x1 = mod(i.x + 1.0, per);
    return mix(mix(fx_hash(vec2(x0, i.y)), fx_hash(vec2(x1, i.y)), u.x),
               mix(fx_hash(vec2(x0, i.y + 1.0)), fx_hash(vec2(x1, i.y + 1.0)), u.x), u.y);
}
float fx_fbm(vec2 p, float per)
{
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 4; i++) {
        s += a * fx_noise(p, per);
        p = p * 2.0 + vec2(0.0, 7.31);
        per *= 2.0;
        a *= 0.5;
    }
    return s / 0.9375;
}
/* Spiral water at angle a (turns), log radius lr and time t: streaks in x, crests in y.
   Crossfade two flow phases to keep the faster inner water from winding up the pattern. */
vec2 fx_water(float a, float lr, float t)
{
    const float twist = 0.32; /* turns of an arm per e of distance */
    float rho = exp(lr);
    float spin = 0.10 / (rho + 0.15); /* turns a second */
    vec2 h = vec2(0.0);
    for (int k = 0; k < 2; k++) {
        float ph = fract(t / 4.0 + 0.5 * float(k));
        float w = 1.0 - abs(2.0 * ph - 1.0);
        float tt = (ph - 0.5) * 4.0;
        float turn = a + twist * lr - 0.04 * t - spin * tt;
        float in_ = lr + 0.22 * t;
        float streak = fx_fbm(vec2(turn * 18.0, in_ * 3.0 + 11.0 * float(k)), 18.0);
        float warp = fx_fbm(vec2(turn * 5.0 + 3.0, in_ * 1.3 + 7.0 * float(k)), 5.0);
        float crest = 0.5 + 0.5 * sin(TAU * (4.0 * turn + 1.5 * lr) + 5.0 * warp);
        h += w * vec2(streak, pow(crest, 14.0));
    }
    return h;
}
float fx_h1(float n) { return fract(sin(n * 12.9898) * 43758.5453); }
/* Lightning slot k (0-3): intensity, angle in turns, strike number, panel-side flag.
   Vortex and ring share this timing; slot 0 strikes the panel for 0.4 s at t1, waking it. */
vec4 fx_strike(int k, float t, float t1)
{
    if (t < t1) {
        return vec4(0.0);
    }
    if (k == 0 && t < t1 + 0.4) {
        float ph = (t - t1) / 0.4;
        return vec4((1.0 - 0.4 * ph) * (0.7 + 0.3 * step(0.25, fx_h1(floor(t * 30.0)))), -0.38, -7.0, 1.0);
    }
    float fk = float(k);
    float p = 1.6 + 0.45 * fk, u = (t + fk * 0.53) / p;
    float cyc = floor(u), ph = u - cyc, dur = 0.32 / p;
    float on = step(fx_h1(cyc * 7.0 + fk), 0.7) * step(ph, dur);
    float flick = step(0.3, fx_h1(floor(t * 24.0) + fk * 13.0));
    float panel = k < 2 ? 1.0 : 0.0;
    float a0 = panel > 0.5 ? -0.46 + 0.16 * fx_h1(cyc * 3.0 + fk) : fx_h1(cyc * 5.0 + fk + 0.5);
    return vec4(on * flick * (1.0 - 0.5 * ph / max(dur, 1e-3)), a0, cyc, panel);
}
float fx_panel_strike(float t, float t1) { return max(fx_strike(0, t, t1).x, fx_strike(1, t, t1).x); }
/* Title vortex: blue spiral water, foam and debris; fill (0-1) forms it inward from the rim. GS units out. */
vec4 fx_vortex(vec4 v)
{
    float t = u_fx[0].y, fill = u_fx[0].z;
    float a = atan(v_fx.y, v_fx.x) / TAU;
    float rho = length(v_fx.xy);
    float depth = clamp(v_fx.z, 0.0, 1.0); /* 0 the rim, 1 the drain */
    float lr = log(max(rho, 0.01));
    vec2 h = fx_water(a, lr, t);
    /* the slope across and along the arms, for the light */
    const float e = 0.003;
    vec2 ha = fx_water(a + e, lr, t), hr = fx_water(a, lr + 0.02, t);
    vec2 radial = normalize(v_fx.xy + 1e-5), tangent = vec2(-radial.y, radial.x);
    float sa = (ha.x - h.x + 0.4 * (ha.y - h.y)) / (e * TAU * max(rho, 0.05));
    float sr = (hr.x - h.x + 0.4 * (hr.y - h.y)) / (0.02 * max(rho, 0.05));
    vec2 grad = (sa * tangent + sr * radial) * 0.02;
    vec2 light = normalize(vec2(0.5, 0.85)) * u_fx[0].w;
    float lit = clamp(0.5 + dot(grad, light), 0.0, 1.0);
    vec3 deep = vec3(0.03, 0.04, 0.30), blue = vec3(0.06, 0.36, 0.92), pale = vec3(0.42, 0.75, 1.0);
    vec3 col = mix(blue, deep, smoothstep(0.3, 0.97, depth));
    col = mix(col, pale, smoothstep(0.45, 0.85, h.x) * 0.75 * smoothstep(0.04, 0.25, rho));
    col *= 0.6 + 0.8 * lit;
    /* Fade crests near the drain when they become narrower than a pixel. */
    float px = length(fwidth(v_fx.xy)) / max(rho, 0.01) * 3.0;
    float sharp = smoothstep(0.5, 0.15, px) * smoothstep(0.07, 0.3, rho);
    col = mix(col, vec3(0.9, 0.96, 1.0), clamp(h.y * 0.9, 0.0, 1.0) * mix(1.0, 0.55, depth) * sharp);
    col += vec3(0.85, 0.95, 1.0) * pow(lit, 8.0) * 0.6;
    col = mix(col, vec3(0.62, 0.82, 1.0), smoothstep(0.05, 0.0, depth) * smoothstep(-0.2, 0.6, -v_fx.y) * 0.5);
    col *= mix(0.3, 1.0, smoothstep(0.0, 0.3, rho));
    float rr = 1.0 - depth; /* 0 the drain, 1 the rim */
    /* clouds swept round in it (u_fx[2].w): soft white wisps along the arms */
    float cl = fx_fbm(vec2(a * 6.0 + 0.35 * lr - 0.05 * t, lr * 1.6 + 0.12 * t), 6.0);
    col = mix(col, vec3(0.9, 0.94, 1.0), smoothstep(0.52, 0.8, cl) * smoothstep(0.2, 0.5, rr) * FADE(0.75, 1.0, rr) * 0.55 * u_fx[2].w);
    /* Splashes (u_fx[3].x): expanding foam and drops toward the panel, lasting about 0.5 s. */
    for (int k = 0; k < 2 && u_fx[3].x > 0.0; k++) {
        float fk = float(k), pr = 3.7 + 1.9 * fk, u = (t + fk * 1.9) / pr;
        float cyc = floor(u), ph = (u - cyc) * pr / 0.6;
        float live = step(ph, 1.0) * step(fx_h1(cyc * 11.0 + fk), 0.75);
        float a0 = 0.49 + 0.07 * (fx_h1(cyc * 3.7 + fk) - 0.5), r0 = 0.42 + 0.22 * fx_h1(cyc * 1.3 + fk);
        vec2 c0 = r0 * vec2(cos(a0 * TAU), sin(a0 * TAU));
        vec2 dv = v_fx.xy - c0;
        /* Spiky foam ring, hollowing as it expands, with drops beyond it. */
        float R = 0.06 + 0.2 * sqrt(ph);
        float sa = atan(dv.y, dv.x);
        float spike = 0.75 + 0.5 * DETAIL_NOISE(vec2(sa * 2.5 + cyc * 4.0, fk));
        float dd = length(dv) / (R * spike);
        float foam = fx_fbm(dv * 22.0 + vec2(cyc * 3.1, fk * 7.0), 1000.0);
        float body = clamp((1.0 - dd) * 3.0 + (foam - 0.5) * 1.6, 0.0, 1.0);
        float hollow = mix(1.0, smoothstep(0.15, 0.75, dd + (foam - 0.5) * 0.5), smoothstep(0.2, 0.8, ph));
        float crest = body * hollow;
        vec2 cell = floor(dv * 70.0);
        float drop = step(0.9, fx_h1(dot(cell, vec2(17.0, 91.0)) + cyc)) * smoothstep(0.9, 1.05, dd) * FADE(1.2, 1.6, dd);
        float fade = 1.0 - ph * ph;
        col = mix(col, vec3(0.93, 0.97, 1.0), clamp(crest * 1.2 + drop, 0.0, 1.0) * clamp(fade, 0.0, 1.0) * live * u_fx[3].x);
    }
    /* fire (u_fx[2].z): a flickering arm spiralling in */
    float sp = fract(a + 0.22 * lr - 0.085 * t);
    float flame = fx_fbm(vec2(a * 30.0 - t * 3.0, lr * 8.0 - t * 2.2), 30.0);
    float fire = exp(-pow((sp - 0.5) / 0.04, 2.0)) * smoothstep(0.3, 0.75, flame + 0.15) * smoothstep(0.15, 0.4, rr) * FADE(0.8, 1.0, rr);
    col = mix(col, mix(vec3(1.0, 0.32, 0.04), vec3(1.0, 0.85, 0.35), flame), clamp(fire * 1.4, 0.0, 1.0) * u_fx[2].z);
    /* rocks (u_fx[2].y): tumbling in toward the drain, shrinking with the depth */
    for (int i = 0; i < 6 && u_fx[2].y > 0.0; i++) {
        float fi = float(i), pr = 6.0 + 1.3 * fi;
        float ph = fract(t / pr + fi * 0.37);
        float rk = 0.95 - 0.82 * ph;
        float ak = TAU * (fi * 0.27 + ph * 1.7);
        vec2 cpos = rk * vec2(cos(ak), sin(ak));
        float size = 0.05 * (0.35 + 0.65 * rk) * (0.8 + 0.4 * fx_h1(fi));
        vec2 dv = (v_fx.xy - cpos) / size;
        float ang = atan(dv.y, dv.x) + t * (0.6 + fi * 0.2);
        float rim = 1.0 + 0.22 * (DETAIL_NOISE(vec2(ang * 1.6 + fi * 9.0, fi)) - 0.5) * 2.0;
        float d = length(dv) / rim;
        float inside = FADE(0.82, 1.0, d) * smoothstep(0.08, 0.25, rk) * FADE(0.9, 0.98, rk);
        if (inside > 0.0) {
            vec3 nr = normalize(vec3(dv / rim, sqrt(max(1.0 - d * d, 0.0)) + 0.2));
            float dl = max(dot(nr, normalize(vec3(0.5, 0.85, 0.7))), 0.0);
            float grain = DETAIL_NOISE(dv * 3.0 + fi * 5.0);
            vec3 rock = vec3(0.32, 0.24, 0.17) * (0.25 + 0.95 * dl) * (0.8 + 0.4 * grain);
            col = mix(col, rock, inside * u_fx[2].y);
        }
    }
    /* lightning (u_fx[2].x): bolts flashing out from the drain across the water, some to the panel's rim */
    float strike = 0.0;
    for (int k = 0; k < 4 && u_fx[2].x > 0.0; k++) {
        vec4 b = fx_strike(k, t, u_fx[3].y);
        /* from near the drain out to the rim at the strike's angle, zigzagging across its way, a fork halfway */
        float ae = b.y * TAU;
        vec2 S0 = 0.06 * vec2(cos(ae + 1.2), sin(ae + 1.2)), E = (b.w > 0.5 ? 1.12 : 0.8) * vec2(cos(ae), sin(ae));
        vec2 dir = E - S0;
        float len = length(dir);
        dir /= len;
        vec2 q = v_fx.xy - S0;
        float u = dot(q, dir) / len;
        float side = dir.x * q.y - dir.y * q.x;
        float uc = clamp(u, 0.0, 1.0);
        float zig = (0.09 * (DETAIL_NOISE(vec2(uc * 7.0 + b.z * 3.7, float(k) * 5.1)) - 0.5) +
                     0.035 * (DETAIL_NOISE(vec2(uc * 26.0 + b.z * 1.3, float(k) * 2.3)) - 0.5)) * sin(3.14159 * uc) + 0.08 * uc * (1.0 - uc);
        float d = abs(side - zig) + max(max(-u, u - 1.0), 0.0) * len;
        float fork = 0.0;
        if (u > 0.45 && u < 0.8) {
            float zf = zig + (u - 0.45) * 0.5 + 0.03 * (DETAIL_NOISE(vec2(u * 30.0, b.z)) - 0.5);
            fork = FADE(0.0, 0.006, abs(side - zf)) * FADE(0.6, 0.8, u);
        }
        float lit = FADE(0.002, 0.009, d) + 0.5 * exp(-d * 55.0) + fork;
        strike = max(strike, b.x * lit);
    }
    col = mix(col, vec3(0.85, 0.97, 1.0), clamp(strike, 0.0, 1.0) * u_fx[2].x);
    col += vec3(0.2, 0.55, 0.8) * strike * 0.25 * u_fx[2].x;
    /* forming: in from the rim along the arms, foamy and see-through until it settles */
    float r = 1.0 - depth;
    float front = r + 0.25 * (h.x - 0.5);
    float edge = 1.0 - fill * 1.4;
    float water = smoothstep(edge, edge + 0.1, front);
    float settle = smoothstep(0.55, 1.0, fill);
    col = mix(mix(vec3(0.8, 0.92, 1.0), col, 0.5), col, settle);
    float alpha = water * mix(0.5, 1.0, settle) * min(v.a / 127.0, 1.0);
    return GS_COLOUR(col, floor(alpha * 128.0));
}
/* The camera in u_lv's space: where its x, y and w rows all vanish. */
vec3 fx_eye()
{
    vec4 r0 = vec4(u_lv[0][0], u_lv[1][0], u_lv[2][0], u_lv[3][0]);
    vec4 r1 = vec4(u_lv[0][1], u_lv[1][1], u_lv[2][1], u_lv[3][1]);
    vec4 r3 = vec4(u_lv[0][3], u_lv[1][3], u_lv[2][3], u_lv[3][3]);
    vec3 c0 = cross(r1.xyz, r3.xyz), c1 = cross(r3.xyz, r0.xyz), c2 = cross(r0.xyz, r1.xyz);
    return -(r0.w * c0 + r1.w * c1 + r3.w * c2) / dot(r0.xyz, c0);
}
/* The pixel's normal, the way to the eye, the screen's right and up across it, a key light from the upper left. */
void fx_frame(out vec3 n, out vec3 v, out vec3 right, out vec3 up, out vec3 key)
{
    v = normalize(fx_eye() - v_pos);
    n = normalize(v_nrm);
    if (dot(n, v) < 0.0) {
        n = -n;
    }
    vec3 rx = vec3(u_lv[0][0], u_lv[1][0], u_lv[2][0]), ry = vec3(u_lv[0][1], u_lv[1][1], u_lv[2][1]);
    right = normalize(rx - dot(rx, v) * v);
    up = normalize(ry - dot(ry, v) * v);
    key = normalize(-0.5 * right + 0.8 * up + 0.6 * v);
}
/* Ring metal: sage top faces (model x), teal walls and upper-left bevel highlights.
   u_fx[1].rgb tints it; .w controls the texture's contribution. */
vec4 fx_metal(vec4 c)
{
    vec3 n, v, right, up, key;
    fx_frame(n, v, right, up, key);
    float top = abs(n.x);
    float wu = dot(n, up), wr = dot(n, right);
    float front = clamp(0.5 - 0.5 * dot(v_pos, up), 0.0, 1.0); /* 0 the back of the ring, 1 its front */
    /* the trailer's teal metal (its frames' colours) */
    vec3 topc = mix(vec3(0.40, 0.56, 0.55), vec3(0.30, 0.52, 0.55), front);
    vec3 wall = vec3(0.14, 0.39, 0.47);
    wall = mix(wall, vec3(0.06, 0.50, 0.48), smoothstep(-0.05, -0.45, wu));
    wall = mix(wall, vec3(0.06, 0.20, 0.24), smoothstep(-0.55, -0.95, wu));
    wall = mix(wall, vec3(0.45, 0.72, 0.72), smoothstep(0.1, 0.7, wu));
    wall *= 1.0 + 0.15 * clamp(-wr, 0.0, 1.0); /* lighter turning to the light's side */
    vec3 col = mix(wall, topc, smoothstep(0.55, 0.9, top));
    /* the bevels: between a top and a wall, turned up and to the light */
    float bevel = smoothstep(0.3, 0.55, top) * FADE(0.8, 0.95, top);
    float lit = clamp(dot(n, key) * 1.4 - 0.2, 0.0, 1.0) * smoothstep(-0.1, 0.4, wu);
    col = mix(col, vec3(0.50, 0.78, 0.76), bevel * lit * 0.8);
    /* where on the ring: its plane over the vortex's rim (the model's y z), 1 at the rim, the angle round it */
    vec2 rp = v_pos.yz / vec2(0.955, 0.675);
    float e = length(rp), ang = atan(rp.y, rp.x) / TAU;
    /* contact shadows: the top darkening down into the vortex's lip and toward the ring's outer edge */
    float ao = 1.0 - 0.38 * FADE(1.0, 1.09, e) * smoothstep(0.92, 1.0, e);
    ao *= 1.0 - 0.22 * smoothstep(1.16, 1.3, e) * smoothstep(0.55, 0.9, top);
    /* brushed round the ring: fine streaks along it, a few broad ones */
    float brush = fx_noise(vec2(ang * 24.0, e * 420.0), 24.0) * 0.6 + fx_noise(vec2(ang * 6.0, e * 40.0), 6.0) * 0.4;
    col *= ao * (0.96 + 0.08 * brush);
    float lip = smoothstep(0.9, 1.0, e) * FADE(1.0, 1.06, e) * FADE(0.55, 0.9, top);
    col = mix(col, vec3(0.04, 0.50, 0.49), lip * 0.6);
    /* Studio reflections: a broad horizon band near the key light, a softer band from the right. */
    vec3 r = reflect(-v, n);
    float band = exp(-pow((dot(r, up) - 0.35) / 0.18, 2.0)) * (0.55 + 0.45 * clamp(dot(r, key), 0.0, 1.0));
    band += 0.5 * exp(-pow((dot(r, right) - 0.6) / 0.2, 2.0)) * smoothstep(-0.2, 0.3, dot(r, up));
    col += mix(col, vec3(0.55, 0.82, 0.82), 0.35) * band * 0.45 * (0.85 + 0.3 * brush);
    /* a tight highlight from the key, pale, not white */
    col += vec3(0.50, 0.72, 0.72) * pow(max(dot(r, key), 0.0), 40.0) * 0.5 * ao;
    /* Left softbox: teal on the top and left-facing walls, a pale line on the inner lip. */
    float leftk = smoothstep(0.3, 1.0, -dot(v_pos, right)) * smoothstep(-0.45, 0.15, dot(v_pos, up));
    float facing = max(smoothstep(0.55, 0.9, top), smoothstep(-0.2, -0.7, wr));
    col += vec3(0.08, 0.22, 0.22) * leftk * facing * (0.85 + 0.3 * brush) * u_fx[0].y;
    col += vec3(0.30, 0.48, 0.55) * leftk * smoothstep(0.93, 1.0, e) * FADE(1.0, 1.05, e) * u_fx[0].y;
    /* a polished tube (the exhausts, u_fx[0].w): a teal body, darker beneath, a pale streak along its top */
    if (u_fx[0].w > 0.0) {
        /* Round tube: teal light, dark silhouette, pale streak and sky reflection. */
        float ndv = max(dot(n, v), 0.0);
        float dl = dot(n, key);
        vec3 tube = mix(vec3(0.02, 0.10, 0.11), vec3(0.24, 0.64, 0.62), smoothstep(-0.55, 0.65, dl));
        tube *= 0.5 + 0.65 * smoothstep(0.05, 0.7, ndv);
        float streak = pow(max(dot(n, normalize(v + 0.15 * up - 0.4 * right)), 0.0), 10.0);
        tube = mix(tube, vec3(0.84, 0.95, 0.95), streak * 0.85);
        tube += vec3(0.22, 0.45, 0.46) * exp(-pow((dot(r, up) - 0.3) / 0.15, 2.0)) * 0.6;
        tube += vec3(0.9, 0.97, 0.97) * pow(max(dot(r, key), 0.0), 24.0) * 0.7;
        col = mix(col, tube, u_fx[0].w);
    }
    col *= u_fx[1].rgb;
    if (u_texmode.x != 0) {
        vec3 t = floor(sample_gs(v_uv)).rgb / 128.0;
        col *= mix(1.0, dot(t, vec3(0.333)) * 1.6, u_fx[1].w);
    }
    if (u_fx[2].y > 0.0) {
        /* Panel arcs at 15 Hz, flaring with rim strikes: u_fx[2].x time, .y strength, .z first strike. */
        float et = u_fx[2].x, tq = floor(et * 15.0), boost = fx_panel_strike(et, u_fx[2].z);
        float awake = smoothstep(u_fx[2].z + 0.05, u_fx[2].z + 0.3, et); /* from the first bolt's strike */
        vec2 ep = v_pos.yz;
        float mask = FADE(0.2, 0.45, ep.x) * FADE(-0.08, 0.12, ep.y) * smoothstep(0.95, 1.0, e) * awake;
        vec2 q = ep * 7.0 + vec2(tq * 0.37, -tq * 0.61);
        float n1 = (DETAIL_NOISE(q) + 0.5 * DETAIL_NOISE(q * 2.3 + 4.1)) / 1.5;
        float ridge = abs(n1 - 0.5);
        float gate = smoothstep(0.42, 0.68, DETAIL_NOISE(ep * 2.5 + vec2(tq * 0.8, tq * 0.3)) + boost * 0.45);
        float arc = FADE(0.0, 0.03, ridge) + FADE(0.0, 0.14, ridge) * 0.3;
        col += vec3(0.55, 0.95, 1.0) * mask * gate * arc * (0.8 + boost) * u_fx[2].y;
        col += vec3(0.1, 0.35, 0.45) * mask * boost * 0.5 * u_fx[2].y;
    }
    float ndv = max(dot(n, v), 0.0);
    col += vec3(0.2, 0.75, 1.0) * pow(1.0 - ndv, 4.0) * u_fx[0].z;
    return GS_COLOUR(col, c.a);
}
/* TITLE.GHG texture 0 down its height (0 top, 1 bottom): yellow, lime, green, sea green, cyan (0-255). */
vec3 fx_wrath_gradient(float t)
{
    const float k[7] = float[7](0.0, 0.19, 0.25, 0.44, 0.69, 0.81, 1.0);
    const vec3 c[7] = vec3[7](vec3(249.0, 243.0, 58.0), vec3(139.0, 253.0, 52.0), vec3(107.0, 255.0, 51.0),
                              vec3(102.0, 223.0, 51.0), vec3(52.0, 205.0, 91.0), vec3(60.0, 204.0, 132.0),
                              vec3(102.0, 204.0, 204.0));
    for (int i = 0; i < 6; i++) {
        if (t <= k[i + 1]) {
            return mix(c[i], c[i + 1], (t - k[i]) / (k[i + 1] - k[i]));
        }
    }
    return c[6];
}
/* Gloss: u_fx[1] lit RGB/rim strength, [2] shadow RGB/glow, [0].zw highlight strength/sharpness.
   [2].w modes: -1 title gradient, -2 height gradient ([0].y half height), -3 vertex colour. */
vec4 fx_gloss(vec4 c)
{
    vec3 n, v, right, up, key;
    fx_frame(n, v, right, up, key);
    float d = max(dot(n, key), 0.0);
    vec3 lit = u_fx[1].rgb, shade = u_fx[2].rgb;
    if (u_fx[2].w == -1.0) {
        /* Measured TITLE.GHG gradient avoids texture seams at UV 1; [0].y flips it for texture 1. */
        float gv = clamp(v_uv.y, 0.0, 1.0);
        gv = u_fx[0].y > 0.5 ? 1.0 - gv : gv;
        vec3 t = fx_wrath_gradient(gv) / 128.0;
        lit = t * u_fx[1].rgb;
        shade = t * u_fx[2].rgb;
    } else if (u_fx[2].w == -2.0) {
        float h = clamp(0.5 + 0.5 * dot(v_pos, up) / u_fx[0].y, 0.0, 1.0);
        lit = mix(u_fx[2].rgb, u_fx[1].rgb, smoothstep(0.1, 0.9, h));
        shade = lit * (u_fx[3].r + u_fx[3].g + u_fx[3].b > 0.0 ? u_fx[3].rgb : vec3(0.25, 0.35, 0.25));
    } else if (u_fx[2].w == -3.0) {
        /* the vertex colour, lit and in shade by [1] and [2] (the Universal letters' faces and chamfers) */
        lit = c.rgb / 255.0 * u_fx[1].rgb;
        shade = c.rgb / 255.0 * u_fx[2].rgb;
    }
    float ndv = max(dot(n, v), 0.0);
    vec3 col = mix(shade, lit, smoothstep(0.0, 0.85, 0.25 + 0.85 * d));
    col = mix(col, shade, FADE(0.12, 0.55, ndv) * 0.6);
    col += lit * pow(1.0 - ndv, 3.0) * u_fx[1].w;
    /* the highlight takes the paint's colour, white only at its heart */
    float sp = pow(max(dot(reflect(-key, n), v), 0.0), u_fx[0].w) * u_fx[0].z;
    col += mix(lit, vec3(1.0), 0.35) * sp;
    if (u_fx[2].w == -1.0) {
        col += lit * u_fx[3].w; /* the letters' pulse */
        /* hidden in the panel's black until the panel wakes (u_fx[3].x 0-1), a white-cyan flash as they come (.y) */
        col = mix(vec3(0.03, 0.035, 0.04), col, u_fx[3].x) + vec3(0.6, 0.95, 1.0) * u_fx[3].y;
    }
    float glow = max(u_fx[2].w, 0.0);
    if (glow > 0.0) {
        /* a lamp: dim between its flashes, a saturated glow lit, hot only at its peak */
        col *= 0.3 + 0.7 * min(glow * 1.5, 1.0);
        col += lit * glow * 0.5 + vec3(1.0, 0.45, 0.3) * max(glow - 0.5, 0.0) * 0.7;
    }
    return GS_COLOUR(col, c.a);
}
/* A part's glow: one of its shells (gfx/backend.c), the part pushed out and back behind itself, u_fx[3].rgb. */
vec4 fx_halo(vec4 c) { return GS_COLOUR(u_fx[3].rgb, 128.0); }
#endif
bool atest(int a)
{
    switch (u_atest.x) {
    case 0: return false;
    case 1: return true;
    case 2: return a < u_atest.y;
    case 3: return a <= u_atest.y;
    case 4: return a == u_atest.y;
    case 5: return a >= u_atest.y;
    case 6: return a > u_atest.y;
    default: return a != u_atest.y;
    }
}
void main()
{
    vec4 c = clamp(floor(v_col + 1.0 / 256.0), 0.0, 255.0);
#if PROGRAM == WATER || PROGRAM == TRIGLASS || PROGRAM == SKNGLASS
    vec2 gs = gl_FragCoord.xy * u_frag.xy + u_frag.zw;
#if PROGRAM == WATER
    vec2 st = min((gs + u_vu[1].zw * 8.0 - 32768.0) * u_vu[1].xy + v_uv, (u_vu[1].zw - 1.0) * u_vu[1].xy * 16.0);
#else
    vec2 st = min(gs * u_vu[1].xy + u_vu[0].xy + v_uv, u_vu[1].zw);
#endif
#else
    vec2 st = v_uv;
#endif
#if ATS
    if (u_atsmode.y >= 0) st = vec2(dot(u_atsuv[0], vec3(st, 1)), dot(u_atsuv[1], vec3(st, 1)));
#endif
#if EFFECTS
    if (u_fx[0].x == 1.0) {
        c = fx_vortex(c);
    } else if (u_fx[0].x == 2.0) {
        c = fx_metal(c);
    } else if (u_fx[0].x == 3.0) {
        c = fx_gloss(c);
    } else if (u_fx[0].x == 4.0) {
        c = fx_halo(c);
    } else
#endif
    if (u_texmode.x != 0) {
#if PARTICLE
        vec4 t = u_rgba8 != 0 ? sample_gs(st) : floor(sample_gs(st));
#else
        vec4 t = u_texmode.x == 4 ? zclut() : u_texmode.x == 5 ? filtered(st) :
                 u_rgba8 != 0 ? sample_gs(st) : floor(sample_gs(st));
        if (u_zalpha != 0) {
            t.a = zclut().a;
        }
#endif
        c.rgb = min(floor(c.rgb * t.rgb / 128.0), 255.0);
        c.a = u_tcc == 0 ? c.a : u_rgba8 != 0 ? min(c.a * t.a / 255.0, 255.0) :
                                               min(floor(c.a * t.a / 128.0), 255.0);
    }
#if EFFECTS
    if ((u_fx[0].x == 1.0 || atest(int(c.a))) == (u_atest.z != 0)) {
#else
    if (atest(int(c.a)) == (u_atest.z != 0)) {
#endif
        discard;
    }
#if EFFECTS
    if (u_fx[0].x == 5.0) {
        const float bayer[16] = float[16](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);
        ivec2 q = ivec2(gl_FragCoord.xy) & 3;
        float keep = clamp((v_pos.y - u_fx[1].y) / (u_fx[1].x - u_fx[1].y), 0.0, 1.0);
        if (keep * keep * (3.0 - 2.0 * keep) * 16.0 <= bayer[q.x + q.y * 4]) discard;
    }
#endif
    if (u_black != 0) {
        float a = u_black == 2 ? smoothstep(0.08, 0.4, c.a / 128.0) : 1.0;
        o_col = vec4(0.0, 0.0, 0.0, a);
        return;
    }
#if !PARTICLE
    if (u_gsblend.x >= 0) {
        vec4 d = texelFetch(u_dst, ivec2(gl_FragCoord.xy), 0);
        vec3 cd = floor(d.rgb * 255.0 + 0.5);
        vec3 col[3] = vec3[3](c.rgb, cd, vec3(0.0));
        float ad = u_zad >= 0 ? min(floor(float(u_zad) * zclut().a / 128.0), 255.0) : floor(d.a * 128.0 + 0.5);
        float k[3] = float[3](c.a, ad, 0.0);
        vec3 r = floor((col[u_gsblend.x] - col[u_gsblend.y]) * k[u_gsblend.z] / 128.0) + col[u_gsblend.w];
        o_col = vec4(clamp(r, 0.0, 255.0) / 255.0, c.a / 128.0);
        return;
    }
#endif
    o_col = vec4(u_ascolour != 0 ? vec3(c.a / 128.0) : c.rgb / 255.0, c.a / 128.0);
}
