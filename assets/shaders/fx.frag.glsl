#version 330 core
uniform mat4 u_lv; uniform vec4 u_fx[8]; uniform vec4 u_vp; /* viewport w, h, time */
in vec2 ndc; out vec4 colour;
vec3 proj(vec3 p, out float depth, out float w) { vec4 c = u_lv * vec4(p, 1.0); w = c.w;
    depth = c.z / c.w * 0.5 + 0.5; return vec3(c.xy / c.w, 0.0); }
vec2 px(vec2 n) { return n * 0.5 * u_vp.xy; }
float seg(vec2 p, vec2 a, vec2 b, out float h) { vec2 pa = p - a, ba = b - a;
    h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-6), 0.0, 1.0); return length(pa - ba * h); }
void main() {
    vec2 me = px(ndc); vec3 add = vec3(0.0); vec4 body = vec4(0.0); float zbest = 2.0;
    for (int i = 0; i < 4; i++) {
        vec4 a = u_fx[i * 2], b = u_fx[i * 2 + 1];
        int kind = int(floor(a.x)); if (kind == 0) continue; float fade = clamp(fract(a.x) * 4.0, 0.0, 1.0);
        vec3 P = vec3(a.z, a.w, b.x), V = b.yzw; float s = a.y;
        float dep, w; vec2 h = px(proj(P, dep, w).xy); if (w <= 0.0) continue;
        float spx = s * u_vp.y * 0.5 / w * 1.3; /* the size's pixels at its distance */
        if (kind == 1 || kind == 2) {
            float d2, w2; vec2 t = px(proj(P - V * s * (kind == 1 ? 30.0 : 12.0), d2, w2).xy);
            float k; float dd = seg(me, h, t, k);
            float width = max(1.2, spx * (kind == 1 ? 1.0 : 0.35)) * mix(1.0, 0.25, k);
            float tail = exp(-dd * dd / (width * width)) * pow(1.0 - k, kind == 1 ? 1.4 : 2.0);
            float r = length(me - h); float head = exp(-r * r / max(1.0, spx * spx * 0.8));
            vec3 tc = kind == 1 ? mix(vec3(0.55, 0.85, 1.0), vec3(0.9, 0.95, 1.0), k) : vec3(1.0, 0.95, 0.8);
            vec3 c = tc * tail * 0.9 + vec3(1.0) * head * 1.4 + vec3(0.6, 0.8, 1.0) * exp(-r / max(2.0, spx * 3.0)) * 0.35;
            c *= fade;
            if (dot(c, c) > 0.0004) { add += c; zbest = min(zbest, dep); }
        } else if (kind == 3) {
            vec2 q = (me - h) / max(spx, 1.0); float tilt = 0.15 * sin(u_vp.z * 1.7);
            q = vec2(q.x * cos(tilt) - q.y * sin(tilt), q.x * sin(tilt) + q.y * cos(tilt));
            float disc = length(q * vec2(1.0, 3.2));
            float dome = length((q - vec2(0.0, 0.18)) * vec2(1.9, 1.9));
            vec4 c = vec4(0.0);
            if (disc < 1.0) { float sh = 0.55 + 0.45 * (1.0 - q.y * 3.0) * (1.0 - abs(q.x) * 0.4);
                c = vec4(vec3(0.62, 0.66, 0.78) * sh + vec3(0.25) * smoothstep(0.8, 1.0, disc), 1.0); }
            if (dome < 1.0 && q.y > 0.05) { float hl = smoothstep(0.6, 0.0, length(q - vec2(-0.12, 0.42)));
                c = vec4(mix(vec3(0.35, 0.85, 0.75), vec3(0.85, 1.0, 0.95), hl), 1.0); }
            for (int l = 0; l < 6; l++) { float ang = 6.2832 * float(l) / 6.0 + u_vp.z * 2.0;
                vec2 lp = vec2(cos(ang) * 0.82, -0.02 + sin(ang) * 0.08); float on = step(0.0, sin(ang));
                float g = exp(-dot(q - lp, q - lp) * 260.0) * on;
                vec3 lc = l % 2 == 0 ? vec3(1.0, 0.85, 0.2) : vec3(1.0, 0.3, 0.3);
                add += lc * g * 1.5; if (g > 0.05) zbest = min(zbest, dep); }
            float trail = 0.0; float k; float dd = seg(me, h, px(proj(P - V * s * 8.0, k, k).xy), k);
            trail = exp(-dd * dd / max(1.0, spx * spx * 0.08)) * (1.0 - k) * 0.5;
            add += vec3(0.5, 0.9, 1.0) * trail;
            if (c.a > 0.0) { body = c; zbest = min(zbest, dep); }
        }
    }
    if (zbest > 1.5) discard;
    gl_FragDepth = zbest;
    colour = vec4(body.rgb * body.a + add * (1.0 - body.a), body.a);
}
