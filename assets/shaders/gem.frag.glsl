in vec3 v_wpos;
in vec3 v_wnrm;
in vec3 v_lnrm;
uniform sampler2D u_dst;
uniform mat4 u_world;
uniform mat4 u_vp;
uniform vec3 u_eye;
uniform mat3 u_cam;
uniform vec3 u_tint;
uniform ivec3 u_gem;
uniform vec4 u_viewport;
out vec4 o_col;

const int CRYSTAL = 1, CLEAR = 2, COLOUR = 3, RELIC = 4;
const int BODY = 1, METAL = 2;

/* Helpers expand to the original expressions. */
#define LUMA(c) dot(c, vec3(0.3, 0.5, 0.2))
#define UNIT_TINT(c) c / max(max(c.r, c.g), max(c.b, 0.05))
#define GEM_OUTPUT(c) vec4(knee(c * (panel ? 0.82 : 1.0)), 1.0)

vec2 screen_of(vec3 p)
{
    vec4 c = u_vp * vec4(p, 1.0);
    vec2 ndc = c.xy / max(c.w, 1e-4);
    return clamp(u_viewport.xy + (ndc * 0.5 + 0.5) * u_viewport.zw, u_viewport.xy + 1.0,
                 u_viewport.xy + u_viewport.zw - 2.0);
}

vec3 backdrop(vec2 px) { return texelFetch(u_dst, ivec2(px), 0).rgb; }

vec3 backdrop_soft(vec2 px, float r)
{
    vec3 c = backdrop(px) * 0.4;
    c += (backdrop(px + vec2(r, 0.0)) + backdrop(px - vec2(r, 0.0))) * 0.15;
    c += (backdrop(px + vec2(0.0, r)) + backdrop(px - vec2(0.0, r))) * 0.15;
    return c;
}

float shine = 1.0;

float box(vec3 v, vec3 dir, float soft, float hot)
{
    float d = dot(v, normalize(dir));
    return (smoothstep(soft, 0.985, d) * 0.6 + smoothstep(hot, 0.997, d) * 2.4) * shine;
}

vec3 studio(vec3 d, vec3 level)
{
    vec3 v = vec3(dot(d, u_cam[0]), dot(d, u_cam[1]), dot(d, u_cam[2]));
    vec3 c = mix(vec3(0.10, 0.10, 0.13), vec3(0.62, 0.66, 0.78), smoothstep(-0.6, 0.9, v.y));
    c *= mix(vec3(1.08, 0.98, 0.88), vec3(0.88, 0.97, 1.10), 0.5 + 0.5 * v.x);
    c = mix(c, level * (0.5 + 0.9 * smoothstep(-0.6, 0.9, v.y)), 0.3);
    c += vec3(2.6, 2.5, 2.3) * box(v, vec3(-0.55, 0.62, -0.55), 0.80, 0.975);
    c += vec3(1.5, 1.7, 2.0) * box(v, vec3(0.72, 0.32, -0.42), 0.84, 0.98);
    c += vec3(1.8) * box(v, vec3(0.1, 0.35, 0.93), 0.86, 0.985);
    c += vec3(0.5) * exp(-abs(v.y - 0.05) * 14.0);
    return c;
}

float hash(vec3 p) { return fract(sin(dot(p, vec3(12.9898, 78.233, 37.719))) * 43758.5453); }

vec3 knee(vec3 c)
{
    vec3 over = max(c - 1.0, 0.0);
    c += vec3(over.r + over.g + over.b) * 0.25;
    return mix(c, 0.85 + 0.15 * (1.0 - exp(-(c - 0.85) * 4.0)), step(0.85, c));
}

void main()
{
    int kind = u_gem.x, role = u_gem.y;
    bool panel = u_gem.z != 0;
    shine = panel ? 0.35 : 1.0;
    vec3 P = v_wpos;
    vec3 V = normalize(u_eye - P);
    vec3 Ng = cross(dFdx(P), dFdy(P));
    Ng = dot(Ng, Ng) > 0.0 ? normalize(Ng) : V;
    Ng = dot(Ng, V) < 0.0 ? -Ng : Ng;
    vec3 Nv = normalize(v_wnrm);
    Nv = dot(Nv, V) < 0.0 ? -Nv : Nv;
    vec3 dome = P - (u_world * vec4(0.0, 0.0, 0.0, 1.0)).xyz;
    dome = dot(dome, dome) > 0.0 ? normalize(dome) : Ng;
    float NdotV = clamp(dot(Ng, V), 0.0, 1.0);
    float r = u_viewport.w * 0.06;
    vec2 lo = u_viewport.xy + 1.0, hi = u_viewport.xy + u_viewport.zw - 2.0;
    vec3 level = (backdrop(clamp(gl_FragCoord.xy + vec2(r, 0.0), lo, hi)) +
                  backdrop(clamp(gl_FragCoord.xy - vec2(r, 0.0), lo, hi)) +
                  backdrop(clamp(gl_FragCoord.xy + vec2(0.0, r), lo, hi)) +
                  backdrop(clamp(gl_FragCoord.xy - vec2(0.0, r), lo, hi))) * 0.25;
    vec3 col;

    if (role == METAL) {
        vec3 N = normalize(mix(Ng, Nv, 0.8) + dome * 0.15);
        float nv = clamp(dot(N, V), 0.0, 1.0);
        vec3 R = reflect(-V, N);
        vec3 t = UNIT_TINT(u_tint);
        vec3 f0 = t.g > 0.95 && t.b > 0.95 ? vec3(0.92, 0.92, 0.95)
                : t.r > 0.95 && t.b < 0.3   ? vec3(1.0, 0.71, 0.26)
                                            : vec3(0.32, 0.55, 1.0);
        vec3 F = f0 + (1.0 - f0) * pow(1.0 - nv, 5.0);
        float env = LUMA(studio(R, level));
        vec3 around = backdrop_soft(screen_of(P + R * 0.9), 3.0);
        col = F * (env * 1.2 + LUMA(around) * 0.35) + f0 * around * 0.12;
        o_col = GEM_OUTPUT(col);
        return;
    }

    vec3 N = normalize(Ng + dome * 0.28);
    vec3 R = reflect(-V, N);
    vec3 tint = kind == CLEAR ? vec3(1.0, 0.985, 0.96) : UNIT_TINT(u_tint);
    if (kind != CLEAR) {
        tint = clamp(mix(vec3(LUMA(tint)), tint, 1.25), 0.0, 1.0);
        tint /= max(max(tint.r, tint.g), max(tint.b, 0.05));
    }
    float f0 = kind == CLEAR ? 0.17 : 0.09;
    float F = f0 + (1.0 - f0) * pow(1.0 - NdotV, 5.0);
    float thick = kind == CRYSTAL ? 0.25 : 0.18;
    float spread = kind == CLEAR ? 0.08 : 0.04;
    vec3 through;
    through.r = backdrop(screen_of(P + refract(-V, Ng, 1.0 / 1.52) * thick)).r;
    through.g = backdrop(screen_of(P + refract(-V, Ng, 1.0 / (1.52 + spread)) * thick)).g;
    through.b = backdrop(screen_of(P + refract(-V, Ng, 1.0 / (1.52 + 2.0 * spread)) * thick)).b;
    float facet = hash(floor(normalize(v_lnrm) * 7.0 + 0.5));
    vec3 T = refract(-V, N, 1.0 / 1.6);
    vec3 Nb = normalize(vec3(-Ng.x, -abs(Ng.y) - 0.6, -Ng.z) + (facet - 0.5) * 0.6);
    vec3 inner = studio(reflect(T, Nb), level);
    vec3 absorb = mix(vec3(1.0), tint, 0.6 + 0.25 * (1.0 - NdotV));
    vec3 body = (through * 0.78 + inner * 0.3) * absorb;
    if (kind != CLEAR) {
        vec3 deep = mix(vec3(1.0), tint, 0.85 + 0.15 * (1.0 - NdotV));
        body = tint * LUMA(through) * 1.4 + through * deep * 0.25 + inner * tint * 0.28 +
               tint * 0.07;
    }
    vec3 refl = studio(R, level);
    vec3 around = backdrop_soft(screen_of(P + R * 0.6), 2.0);
    col = mix(body, mix(refl, around * 1.3, 0.2), F) + refl * 0.04;
    o_col = GEM_OUTPUT(col);
}
