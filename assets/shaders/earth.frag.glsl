#version 330 core
uniform mat4 u_lv, u_inv;
uniform vec4 u_c;      /* centre, the sphere's radius */
uniform vec4 u_e;      /* the film look's opacity, the halo's strength, its style, time */
uniform vec4 u_rot[3]; /* world to the map's frame, rows */
uniform vec4 u_pass;
uniform sampler2D u_map, u_mask;
uniform vec4 u_txt[4]; /* world to the text's space, rows */
uniform vec4 u_arc;    /* the letters' arc: its axis' z, radius; the mask's angles from, span */
uniform vec4 u_ty;     /* the mask's heights from, span; the shadow's strength */
uniform vec4 u_tkey;   /* the shadow's light, toward it */
uniform vec4 u_look;   /* the continents' bevel, the coasts' sheen, the map's u at the tiles' u 0 */
uniform vec4 u_cam[3]; /* the camera's right, up, back */
uniform sampler2D u_cap;
in vec2 ndc; out vec4 colour;
/* the Corporate globe's baked light (gamma 2.2 in, out) at camera-frame normal q: tile k of the 6 */
vec3 cap(float k, vec3 q) {
    vec2 c = vec2(0.5 + 0.5 * q.x, 0.5 - 0.5 * q.y) * (126.0 / 128.0) + 1.0 / 128.0;
    return pow(textureLod(u_cap, vec2((k + c.x) / 6.0, c.y), 0.0).rgb, vec3(2.2));
}
vec2 map_uv(vec3 f) { return vec2(0.5 + atan(f.x, -f.z) / 6.2831853, 0.5 - asin(clamp(f.y, -1.0, 1.0)) / 3.1415927); }
/* the land toned as the 1997 film's: per channel, its curve through bytes 0, 32 ... 255 (fitted: our frontal
   land's histograms onto the film's, tools: land_lut.py) */
vec3 film_tone(vec3 c) {
    const vec3 t[9] = vec3[9](vec3(0.000, 0.220, 0.106), vec3(0.333, 0.263, 0.337), vec3(0.471, 0.369, 0.451), vec3(0.553, 0.518, 0.510), vec3(0.651, 0.655, 0.553), vec3(0.714, 0.714, 0.608), vec3(0.765, 0.761, 0.682), vec3(0.800, 0.792, 0.706), vec3(0.910, 0.835, 0.706));
    vec3 x = clamp(c, 0.0, 1.0) * 8.0, i = min(floor(x), vec3(7.0)), r;
    for (int k = 0; k < 3; k++) r[k] = mix(t[int(i[k])][k], t[int(i[k]) + 1][k], x[k] - i[k]);
    return r;
}
/* the globe at map direction f (its colour, land in alpha): the land's blurred mask (a coarser level) raises the
   continents with a bevel, the normal tilted down its slope; that normal into the camera's frame reads the baked
   light: the sea's, the land's by each channel's level between the baked greys (toned as the film's), the coasts'
   sheen */
/* (lod: the map's level for the pixel's footprint, worked out by the caller from the ray: no derivatives here,
   where neighbours may have missed the sphere) */
vec3 corporate(vec3 f, float lod) {
    vec2 uv = map_uv(f) + vec2(u_look.z, 0.0);
    vec4 tx = textureLod(u_map, uv, lod);
    float cl = sqrt(max(1.0 - f.y * f.y, 1e-4)), du = 1.0 / 512.0, dv = 1.0 / 256.0;
    float mb = textureLod(u_map, uv, 3.5).a;
    float gx = (textureLod(u_map, uv + vec2(du, 0.0), 3.5).a - textureLod(u_map, uv - vec2(du, 0.0), 3.5).a) / (2.0 * du * 6.2831853 * cl);
    float gy = (textureLod(u_map, uv - vec2(0.0, dv), 3.5).a - textureLod(u_map, uv + vec2(0.0, dv), 3.5).a) / (2.0 * dv * 3.1415927);
    vec3 e = normalize(vec3(-f.z, 0.0, f.x)), nn = cross(f, e);
    vec3 fb = normalize(f - u_look.x * (gx * e + gy * nn));
    vec3 nw = fb.x * u_rot[0].xyz + fb.y * u_rot[1].xyz + fb.z * u_rot[2].xyz;
    vec3 q = vec3(dot(nw, u_cam[0].xyz), dot(nw, u_cam[1].xyz), dot(nw, u_cam[2].xyz));
    float r = length(q.xy); if (r > 1.0) q.xy /= r;
    vec3 sea = cap(0.0, q), x = clamp(tx.rgb / max(tx.a, 1e-3), 0.0, 1.0) * 255.0;
    vec3 land = cap(1.0, q) * clamp(x / 64.0, 0.0, 1.0);
    land = mix(land, cap(2.0, q), clamp((x - 64.0) / 64.0, 0.0, 1.0));
    land = mix(land, cap(3.0, q), clamp((x - 128.0) / 64.0, 0.0, 1.0));
    land = mix(land, cap(4.0, q), clamp((x - 192.0) / 63.0, 0.0, 1.0));
    land = pow(film_tone(pow(max(land, vec3(0.0)), vec3(1.0 / 2.2))), vec3(2.2));
    float coast = clamp(4.0 * mb * (1.0 - mb), 0.0, 1.0) * u_look.y;
    vec3 col = mix(sea, land, smoothstep(0.25, 0.75, tx.a)) + coast * max(cap(5.0, q) - sea, vec3(0.0));
    return pow(max(col, vec3(0.0)), vec3(1.0 / 2.2));
}
/* how much of the light the film UNIVERSAL's letters keep from p: the ray toward it into the text's space, out
   through the arc they stand on, their coverage there */
float text_shadow(vec3 p, vec3 L) {
    if (u_ty.z <= 0.0) return 0.0;
    vec3 o = p.x * u_txt[0].xyz + p.y * u_txt[1].xyz + p.z * u_txt[2].xyz + u_txt[3].xyz;
    vec3 d = L.x * u_txt[0].xyz + L.y * u_txt[1].xyz + L.z * u_txt[2].xyz;
    vec2 a = vec2(o.x, o.z - u_arc.x), e = vec2(d.x, d.z);
    float qa = dot(e, e), qb = dot(a, e), qc = dot(a, a) - u_arc.y * u_arc.y, dsc = qb * qb - qa * qc;
    if (qa < 1e-8 || dsc < 0.0) return 0.0;
    float t = (-qb + sqrt(dsc)) / qa; if (t <= 0.0) return 0.0;
    vec3 h = o + d * t;
    vec2 m = vec2((atan(h.x, -(h.z - u_arc.x)) - u_arc.z) / u_arc.w, (h.y - u_ty.x) / u_ty.y);
    if (any(lessThan(m, vec2(0.0))) || any(greaterThan(m, vec2(1.0)))) return 0.0;
    return textureLod(u_mask, m, 0.0).r * u_ty.z;
}
float depth_of(vec3 p) { vec4 c = u_lv * vec4(p, 1.0); return clamp(c.z / c.w * 0.5 + 0.5, 0.0, 1.0); }
void main() {
    vec4 a = u_inv * vec4(ndc, -1.0, 1.0), b = u_inv * vec4(ndc, 1.0, 1.0);
    vec3 ro = a.xyz / a.w, rd = normalize(b.xyz / b.w - ro);
    vec3 o = ro - u_c.xyz; float bb = dot(o, rd), cc = dot(o, o) - u_c.w * u_c.w, h = bb * bb - cc;
    if (u_pass.x < 0.5) {
/* the map's level: the pixel's angle (the ray's step, taken before any pixel is discarded) at the hit's distance,
   stretched by the surface's slant, over the sphere's radius, in the map's texels a radian */
        float pa = max(length(dFdx(rd)), length(dFdy(rd)));
        if (h < 0.0) discard;
        float t = -bb - sqrt(h); if (t <= 0.0) discard;
        vec3 p = ro + rd * t, n = normalize(p - u_c.xyz);
        float slant = max(-dot(n, rd), 0.05);
        float lod = log2(max(float(textureSize(u_map, 0).x) / 6.2831853 * pa * t / (u_c.w * slant), 1e-6));
        vec3 col = corporate(vec3(dot(n, u_rot[0].xyz), dot(n, u_rot[1].xyz), dot(n, u_rot[2].xyz)), lod);
        col *= 1.0 - text_shadow(p, normalize(u_tkey.xyz));
        gl_FragDepth = depth_of(p);
        colour = vec4(col, 1.0) * u_e.x;
        return;
    }
    float tc = -bb; if (tc <= 0.0) discard;
    vec3 q = o + rd * tc; float d = length(q), x = (d - u_pass.y) / u_pass.y;
    if (x > 0.6) discard;
    vec3 e1 = normalize(cross(rd, abs(rd.y) < 0.9 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0))), e2 = cross(e1, rd);
    float ang = atan(dot(q, e2), dot(q, e1)), tm = u_e.w;
    float out_ = max(x, 0.0), edge = smoothstep(-0.03, 0.0, x);
/* the film's corona billows: soft lumps round the globe, its reach and strength varying round it (measured: a fall
   to 1/e from 0.10 to 0.20 of the radius out), the lumps drifting both ways round it and so changing shape; whole
   waves round the circle, no seam */
    float w = 0.40 * sin(5.0 * ang + 0.9 * tm) + 0.30 * sin(8.0 * ang - 1.3 * tm + 1.3)
            + 0.20 * sin(12.0 * ang + 1.7 * tm + 2.1) + 0.10 * sin(17.0 * ang - 2.2 * tm + 4.0);
/* their profiles measured: the film's grey-blue, about 0.6 at the edge, a third of it at a quarter radius out,
   billowing; the station earth's (space.gsc's glow sprite) grey-white, 0.7 at the edge, softer */
    vec3 film = vec3(0.80, 0.86, 0.97) * 0.60 * (1.0 + 0.18 * w) * exp(-out_ / (0.16 * (1.0 + 0.40 * w)));
    vec3 station = vec3(0.98, 0.99, 1.0) * 0.72 * exp(-out_ / 0.20);
    vec3 col = mix(film, station, clamp(u_e.z, 0.0, 1.0)) * edge * (1.0 - smoothstep(0.35, 0.6, x)) * u_e.y;
    if (dot(col, col) < 1e-6) discard;
    gl_FragDepth = depth_of(u_c.xyz);
    colour = vec4(col, 0.0);
}
