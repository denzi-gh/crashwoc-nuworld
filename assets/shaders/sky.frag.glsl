#version 330 core
uniform vec4 uright, uup, ufwd;
vec3 right, up;
uniform vec4 lens; /* tan x, tan y, time, 0 */
uniform vec4 uband; /* where the nebula gathers */
in vec2 uv; out vec4 colour;
float h31(vec3 p) { p = fract(p * vec3(.1031, .1030, .0973)); p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z); }
vec3 h33(vec3 p) { p = fract(p * vec3(.1031, .1030, .0973)); p += dot(p, p.yxz + 33.33);
    return fract((p.xxy + p.yxx) * p.zyx); }
float vnoise(vec3 p) { vec3 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(h31(i), h31(i + vec3(1,0,0)), f.x), mix(h31(i + vec3(0,1,0)), h31(i + vec3(1,1,0)), f.x), f.y),
               mix(mix(h31(i + vec3(0,0,1)), h31(i + vec3(1,0,1)), f.x), mix(h31(i + vec3(0,1,1)), h31(i + vec3(1,1,1)), f.x), f.y), f.z); }
float fbm(vec3 p) { float a = 0.5, s = 0.0; for (int i = 0; i < 5; i++) { s += a * vnoise(p); p = p * 2.03 + 7.1; a *= 0.5; }
    return s; }
vec3 stars(vec3 d, float scale, float density, float rpx, float bright, float glint, float t) {
    float px = lens.w; vec3 p = d * scale, c = floor(p); vec3 acc = vec3(0.0);
    for (int z = -1; z <= 1; z++) for (int y = -1; y <= 1; y++) for (int x = -1; x <= 1; x++) {
        vec3 cell = c + vec3(x, y, z); vec3 r = h33(cell);
        if (r.x > density) continue;
        vec3 sp = normalize(cell + 0.2 + 0.6 * h33(cell + 17.0));
        float a = acos(clamp(dot(d, sp), -1.0, 1.0));
        if (a > 40.0 * px) continue;
        float tw = 0.7 + 0.3 * sin(t * (1.2 + 2.5 * r.y) + 6.28 * r.z);
        float b = bright * (0.35 + 0.65 * r.y * r.y) * tw;
        vec3 tint = mix(vec3(0.70, 0.82, 1.0), vec3(1.0, 0.88, 0.70), r.z);
        float rad = rpx * (0.7 + 0.6 * r.y) * px;
        acc += tint * b * smoothstep(rad, rad * 0.25, a);
        if (glint > 0.0 && r.y > 0.75) { /* a bright one: a soft halo and a cartoon cross */
            vec3 o = d - sp; float ox = abs(dot(o, right)) / px, oy = abs(dot(o, up)) / px;
            float len = glint * (6.0 + 10.0 * r.y);
            float cross = smoothstep(0.9, 0.0, oy) * pow(max(0.0, 1.0 - ox / len), 2.0) +
                          smoothstep(0.9, 0.0, ox) * pow(max(0.0, 1.0 - oy / len), 2.0);
            acc += tint * tw * (0.9 * cross + 0.35 * exp(-a / (3.0 * px)));
        } }
    return acc; }
vec3 nebula(vec3 d, float t) {
    vec3 b = normalize(uband.xyz);
    vec3 bx = normalize(cross(b, vec3(0.0, 1.0, 0.0)) + vec3(1e-4)); vec3 by = cross(bx, b);
    float gather = smoothstep(-0.35, 0.75, dot(d, b));
    vec3 p = vec3(dot(d, bx) * 1.6, dot(d, by) * 5.0, dot(d, b) * 1.6);
    vec3 q = p + vec3(t * 0.006, 0.0, t * 0.004);
    vec3 w = vec3(fbm(q + vec3(3.1, 0.0, t * 0.012)), fbm(q + vec3(9.7, t * 0.010, 0.0)), fbm(q + vec3(1.4, 4.0, -t * 0.008)));
    float neb = fbm(q * 1.4 + 1.8 * w);
    vec3 sun = normalize(-right * 0.6 + up * 0.8);
    vec3 so = vec3(dot(sun, bx) * 1.6, dot(sun, by) * 5.0, dot(sun, b) * 1.6) * 0.06;
    float lit = clamp((neb - fbm((q + so) * 1.4 + 1.8 * w)) * 6.0, 0.0, 1.0);
    float dens = smoothstep(0.38, 0.82, neb) * gather;
    float core = smoothstep(0.62, 0.95, neb) * gather;
    vec3 deep = vec3(0.035, 0.11, 0.28), mid = vec3(0.09, 0.25, 0.56), pale = vec3(0.32, 0.52, 0.86);
    vec3 c = mix(deep, mid, smoothstep(0.3, 0.8, w.x)) * dens;
    c += pale * core * 0.45 + pale * lit * dens * 0.55;
    c += vec3(0.16, 0.10, 0.34) * smoothstep(0.62, 0.9, w.y) * dens * 0.35;
    float a = t * 0.0025, ca = cos(a), sa = sin(a);
    vec3 sd = vec3(ca * d.x + sa * d.z, d.y, -sa * d.x + ca * d.z);
    c += stars(sd, 150.0, 0.0035, 1.2, 0.75, 0.0, t);
    c += stars(sd, 60.0, 0.0030, 1.8, 1.10, 0.45, t * 0.8);
    return c; }
void main() {
    vec2 n = uv * 2.0 - 1.0;
    right = uright.xyz; up = uup.xyz;
    vec3 d = normalize(ufwd.xyz + n.x * lens.x * right + n.y * lens.y * up);
    colour = vec4(nebula(d, lens.z), 1.0);
}
