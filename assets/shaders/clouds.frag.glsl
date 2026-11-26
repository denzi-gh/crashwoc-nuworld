#version 330 core
uniform mat4 u_lv, u_inv;
uniform vec4 u_c;   /* centre, the planet's radius */
uniform vec4 u_r;   /* r_in, r_out, time, opacity */
uniform vec4 u_sun;
in vec2 ndc; out vec4 colour;
float h31(vec3 p) { p = fract(p * vec3(.1031, .1030, .0973)); p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z); }
float vn(vec3 p) { vec3 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(h31(i), h31(i + vec3(1,0,0)), f.x), mix(h31(i + vec3(0,1,0)), h31(i + vec3(1,1,0)), f.x), f.y),
               mix(mix(h31(i + vec3(0,0,1)), h31(i + vec3(1,0,1)), f.x), mix(h31(i + vec3(0,1,1)), h31(i + vec3(1,1,1)), f.x), f.y), f.z); }
float density(vec3 p) {
    vec3 d = p - u_c.xyz; float r = length(d); vec3 n = d / r;
    float h = (r - u_r.x) / (u_r.y - u_r.x);
    if (h < 0.0 || h > 1.0) return 0.0;
    float t = u_r.z;
    vec3 q = n * 20.0 + vec3(t * 0.110, 0.0, t * 0.070);
    float big = 0.6 * vn(n * 5.0 + vec3(0.0, t * 0.025, t * 0.010)) + 0.4 * vn(n * 11.0 - t * 0.02);
    float b1 = 1.0 - abs(2.0 * vn(q * 2.1 + t * 0.09) - 1.0), b2 = 1.0 - abs(2.0 * vn(q * 4.3 - t * 0.12) - 1.0);
    float f = 0.55 * vn(q + vec3(0.0, t * 0.06, 0.0)) + 0.28 * b1 * b1 + 0.17 * b2 * b2;
    float cover = smoothstep(0.53, 0.72, big);
    float puff = f + 0.30 * cover - 0.60 + 0.30 * (1.0 - abs(2.0 * h - 1.0)) - (1.0 - u_r.w) * 0.9;
    float shape = smoothstep(0.0, 0.25, h) * smoothstep(1.0, 0.55, h);
    return clamp(puff * 4.0, 0.0, 1.0) * shape * cover;
}
bool sphere(vec3 ro, vec3 rd, float r, out float t0, out float t1) {
    vec3 o = ro - u_c.xyz; float b = dot(o, rd), c = dot(o, o) - r * r, h = b * b - c;
    if (h < 0.0) return false; h = sqrt(h); t0 = -b - h; t1 = -b + h; return true; }
void main() {
    if (u_r.w <= 0.0) discard;
    vec4 a = u_inv * vec4(ndc, -1.0, 1.0), b = u_inv * vec4(ndc, 1.0, 1.0);
    vec3 ro = a.xyz / a.w, rd = normalize(b.xyz / b.w - ro);
    float o0, o1; if (!sphere(ro, rd, u_r.y, o0, o1) || o1 <= 0.0) discard;
    float tend = o1, e0, e1;
    if (sphere(ro, rd, u_c.w, e0, e1) && e0 > 0.0) tend = e0;
    float tstart = max(o0, 0.0);
    const int N = 28; float dt = (tend - tstart) / float(N);
    vec3 sun = normalize(u_sun.xyz); vec3 acc = vec3(0.0); float alpha = 0.0, tfirst = -1.0;
    float shell = u_r.y - u_r.x;
    for (int i = 0; i < N; i++) {
        float t = tstart + (float(i) + 0.5) * dt; vec3 p = ro + rd * t;
        float d = density(p);
        if (d <= 0.001) continue;
        float sh = density(p + sun * shell * 0.35) + density(p + sun * shell * 0.8);
        float lit = exp(-sh * 1.6);
        float day = clamp(dot(normalize(p - u_c.xyz), sun) * 0.4 + 0.75, 0.45, 1.05);
        vec3 col = mix(vec3(0.70, 0.79, 0.94), vec3(1.0), lit) * day;
        float k = 1.0 - exp(-d * dt / shell * 3.5);
        acc += (1.0 - alpha) * k * col; alpha += (1.0 - alpha) * k;
        if (tfirst < 0.0 && alpha > 0.04) tfirst = t;
        if (alpha > 0.97) break;
    }
    if (alpha < 0.01 || tfirst < 0.0) discard;
    vec4 clip = u_lv * vec4(ro + rd * tfirst, 1.0);
    gl_FragDepth = clamp(clip.z / clip.w * 0.5 + 0.5, 0.0, 1.0);
    colour = vec4(acc, alpha) * clamp(u_r.w * 2.0, 0.0, 1.0);
}
