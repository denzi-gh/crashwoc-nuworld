/* Helpers expand to the original expressions. */
#define SKINNED (PROGRAM == SKIN || PROGRAM == SKINXUV || PROGRAM == SKNGLASS || PROGRAM == SKNGEM)
#define LIGHT_LEVELS(n) max(vec3(dot(u_lightdir[0].xyz, n), dot(u_lightdir[1].xyz, n), dot(u_lightdir[2].xyz, n)), 0.0)
#define ENV_UV(n) vec2(dot(u_uv[0].xyz, n), dot(u_uv[1].xyz, n)) + u_uv[3].xy

#if SKINNED
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_nrm;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in uvec4 a_weight0;
layout(location = 4) in uvec4 a_weight1;
layout(location = 5) in uvec4 a_joint0;
layout(location = 6) in uvec4 a_joint1;
layout(location = 7) in uvec2 a_nrmjoint;
layout(location = 8) in uint a_delta;
uniform samplerBuffer u_pool;
uniform samplerBuffer u_weights;
uniform samplerBuffer u_deltas;
uniform int u_joints;
uniform ivec2 u_blend;
#else
layout(location = 0) in vec4 a_pos;
layout(location = 1) in vec4 a_tex;
layout(location = 2) in vec4 a_col;
#endif
uniform mat4 u_lv;
uniform vec4 u_diffuse;
#if EFFECTS
uniform vec4 u_fx[4];
#endif
uniform vec4 u_ambient;
uniform vec4 u_lightdir[3];
uniform vec4 u_lightcol[3];
uniform vec4 u_uvoff;
uniform vec4 u_sprite;
uniform vec4 u_uv[4];
uniform vec4 u_vu[2];
#if PARTICLE
uniform uvec4 u_ptl[132];
uniform ivec4 u_atest;
uniform ivec2 u_texalpha;
uniform int u_zero;
#elif PROGRAM == WATER || PROGRAM == WATERUV
uniform vec4 u_wave[32];
#endif
out vec4 v_col;
out vec2 v_uv;
#if EFFECTS
out vec3 v_fx;
out vec3 v_nrm;
out vec3 v_pos;
#endif
#if GEMS
uniform mat4 u_world;
out vec3 v_wpos;
out vec3 v_wnrm;
out vec3 v_lnrm;
#endif
#if SKINNED
vec3 row(uint joint, int r) { return texelFetch(u_pool, u_joints + int(joint) * 4 + r).xyz; }
#endif
#if EFFECTS
vec3 fx_eye()
{
    vec4 r0 = vec4(u_lv[0][0], u_lv[1][0], u_lv[2][0], u_lv[3][0]);
    vec4 r1 = vec4(u_lv[0][1], u_lv[1][1], u_lv[2][1], u_lv[3][1]);
    vec4 r3 = vec4(u_lv[0][3], u_lv[1][3], u_lv[2][3], u_lv[3][3]);
    vec3 c0 = cross(r1.xyz, r3.xyz), c1 = cross(r3.xyz, r0.xyz), c2 = cross(r0.xyz, r1.xyz);
    return -(r0.w * c0 + r1.w * c1 + r3.w * c2) / dot(r0.xyz, c0);
}
vec4 fx_shell(vec3 p, vec3 n)
{
    n = normalize(n);
    vec3 v = normalize(fx_eye() - p);
    vec4 c = u_lv * vec4(p - v * u_fx[0].y, 1.0);
    vec4 d = u_lv * vec4(n, 0.0);
    vec2 dir = d.xy - c.xy * d.w / c.w;
    float side = sqrt(sqrt(max(1.0 - dot(n, v) * dot(n, v), 0.0)));
    if (dot(dir, dir) > 1e-12) {
        c.xy += normalize(dir) * side * u_fx[3].w * c.w;
    }
    return c;
}
vec3 fx_wobble(vec3 p)
{
    float t = u_fx[3].w - 1.0;
    float ph = dot(u_lv[3].xy / max(abs(u_lv[3].w), 1e-4), vec2(9.3, 4.1));
    float tilt = 0.045 * sin(t * 1.15 + ph * 1.7);
    float c = cos(tilt), s = sin(tilt);
    p.yz = vec2(c * p.y - s * p.z, s * p.y + c * p.z);
    p.y += 0.012 * sin(t * 1.6 + ph);
    p.x += 0.008 * sin(t * 1.3 + ph * 2.3);
    return p;
}
vec3 fx_ring(vec3 p, inout vec3 n)
{
    float D = u_fx[3].x, k = u_fx[3].y, d = p.z + u_fx[3].z, x = p.x * u_fx[3].w;
    if (k < 1e-5) return vec3(x, p.y, d - D);
    float rho = 1.0 / k - d, th = k * x, c = cos(th), s = sin(th);
    n = vec3(n.x * c - n.z * s, n.y, n.x * s + n.z * c);
    return vec3(rho * s, p.y, 1.0 / k - D - rho * c);
}
#endif
void main()
{
#if EFFECTS
    v_fx = vec3(0.0);
    v_nrm = vec3(0.0, 0.0, 1.0);
    v_pos = vec3(0.0);
#endif
#if PARTICLE
    const int strip[6] = int[6](0, 1, 2, 1, 2, 3);
    int corner = strip[gl_VertexID];
    vec4 head = uintBitsToFloat(u_ptl[0]);
    float t = head.w - a_pos.w;
    float k = a_tex.w * t;
    if (t < 0.0 || k > 62.9) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }
    int i = int(k) & 63;
    float f = k - float(int(k));
    vec4 v01 = mix(uintBitsToFloat(u_ptl[4 + 2 * i]), uintBitsToFloat(u_ptl[4 + 2 * min(i + 1, 63)]), f);
    vec2 v2 = mix(uintBitsToFloat(u_ptl[5 + 2 * i].xy), uintBitsToFloat(u_ptl[5 + 2 * min(i + 1, 63)].xy), f);
    vec2 offset = (corner & 1) != 0 ? v2 : v01.xy;
    if (corner >= 2) {
        offset += v01.zw;
    }
    vec3 p = a_pos.xyz + a_tex.xyz * t + vec3(0.0, head.y * t * t, 0.0);
    gl_Position = u_lv * vec4(p, 1.0);
    gl_Position.xy += offset * u_sprite.xy;
    const ivec2 uv[4] = ivec2[4](ivec2(1, 0), ivec2(2, 0), ivec2(1, 2), ivec2(2, 2));
    uvec4 corners = u_ptl[uv[corner].x];
    uvec2 st = uv[corner].y == 0 ? corners.xy : corners.zw;
    v_uv = vec2(st & 0x3FFFu) * u_sprite.zw;
    v_col = vec4(u_ptl[4 + 2 * i] & 0xFFu);
    int lo = min(int(v_col.a) * u_texalpha.x / 128, 255);
    int hi = min(int(v_col.a) * u_texalpha.y / 128, 255);
    bool anypass, allpass;
    switch (u_atest.x) {
    case 0: anypass = false; allpass = false; break;
    case 1: anypass = true; allpass = true; break;
    case 2: anypass = lo < u_atest.y; allpass = hi < u_atest.y; break;
    case 3: anypass = lo <= u_atest.y; allpass = hi <= u_atest.y; break;
    case 4: anypass = lo <= u_atest.y && hi >= u_atest.y; allpass = lo == u_atest.y && hi == lo; break;
    case 5: anypass = hi >= u_atest.y; allpass = lo >= u_atest.y; break;
    case 6: anypass = hi > u_atest.y; allpass = lo > u_atest.y; break;
    default: anypass = lo != u_atest.y || hi != u_atest.y; allpass = hi < u_atest.y || lo > u_atest.y; break;
    }
    if ((u_atest.z != 0 ? allpass : !anypass) || (u_zero != 0 && hi == 0))
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
#elif PROGRAM == FACEON || PROGRAM == FACEY
    vec2 corner = 1.0 - 2.0 * a_tex.xy;
#if PROGRAM == FACEON
    gl_Position = u_lv * vec4(a_pos.xyz, 1.0);
    gl_Position.xy += corner * a_tex.zw * u_sprite.xy;
#else
    vec3 p = a_pos.xyz - corner.x * a_tex.z * u_sprite.xyz + vec3(0.0, corner.y * a_tex.w, 0.0);
    gl_Position = u_lv * vec4(p, 1.0);
#endif
    v_uv = a_tex.xy;
    v_col = a_col;
#elif SKINNED
    vec3 q = a_pos;
    for (uint k = 0u; k < a_nrmjoint.y && u_blend.y > 0; k++) {
        vec4 d = texelFetch(u_deltas, int(a_delta + k));
        if (int(d.w) < u_blend.y) {
            q += texelFetch(u_weights, u_blend.x + int(d.w)).x * d.xyz;
        }
    }
    vec3 p = vec3(0.0);
    for (int k = 0; k < 8; k++) {
        uint j = k < 4 ? a_joint0[k] : a_joint1[k - 4];
        float w = float(k < 4 ? a_weight0[k] : a_weight1[k - 4]) / 32768.0;
        p += w * (q.x * row(j, 0) + q.y * row(j, 1) + q.z * row(j, 2) + row(j, 3));
    }
    gl_Position = u_lv * vec4(p, 1.0);
#if EFFECTS
    if (u_fx[0].x == 1.0) {
        v_fx = vec3((q.yz - u_fx[1].xy) / u_fx[1].zw, a_uv.y);
    }
#endif
    vec3 n = a_nrm.x * row(a_nrmjoint.x, 0) + a_nrm.y * row(a_nrmjoint.x, 1) + a_nrm.z * row(a_nrmjoint.x, 2);
#if EFFECTS
    v_nrm = n;
    v_pos = p;
    if (u_fx[0].x == 4.0) {
        gl_Position = fx_shell(p, n);
    }
#endif
#if PROGRAM == SKNGEM
    v_wpos = (u_world * vec4(p, 1.0)).xyz;
    v_wnrm = mat3(u_world) * n;
    v_lnrm = a_nrm;
    v_col = u_diffuse;
    v_uv = a_uv;
#elif PROGRAM == SKIN
    vec3 nl = LIGHT_LEVELS(n);
    vec4 lit = u_lightcol[0] * nl.x + u_lightcol[1] * nl.y + u_lightcol[2] * nl.z;
    v_col = vec4(min(u_diffuse.rgb * (u_ambient.rgb + lit.rgb), 255.0), u_diffuse.a);
    v_uv = a_uv + u_uvoff.xy;
#else
    v_col = u_diffuse;
    v_uv = ENV_UV(n);
#if PROGRAM == SKNGLASS
    v_uv *= u_vu[0].z;
#endif
#endif
#elif PROGRAM == WATER || PROGRAM == WATERUV
    vec4 wave = u_wave[int(a_tex.z) & 31];
    gl_Position = u_lv * vec4(a_pos.x, a_pos.y + wave.w, a_pos.z, 1.0);
    v_uv = wave.xy * u_vu[0].z;
#if PROGRAM == WATER
    v_col = u_diffuse;
#else
    v_uv += (a_tex.xy + u_uvoff.zw) / 4096.0;
    v_col = a_col;
#endif
#else
    /* Rigid meshes share position and packed-normal decoding. */
    gl_Position = u_lv * vec4(a_pos.xyz, 1.0);
    vec3 n = vec3(a_pos.w, a_tex.zw / 4096.0);
#if PROGRAM == GEM
    v_wpos = (u_world * vec4(a_pos.xyz, 1.0)).xyz;
    v_wnrm = mat3(u_world) * n;
    v_lnrm = n;
    v_col = a_col;
    v_uv = (a_tex.xy + u_uvoff.zw) / 4096.0;
#elif PROGRAM == TRIXUV || PROGRAM == TRIGLASS
    v_col = u_diffuse;
    v_uv = ENV_UV(n);
#if PROGRAM == TRIGLASS
    v_uv *= u_vu[0].z;
#endif
#else
#if EFFECTS
    if (u_fx[0].x == 3.0 && u_fx[2].w == -3.0 && u_fx[3].z > 0.0) {
        gl_Position = u_lv * vec4(fx_ring(a_pos.xyz, n), 1.0);
    }
#endif
    vec3 nl = LIGHT_LEVELS(n);
    vec4 lit = u_lightcol[0] * nl.x + u_lightcol[1] * nl.y + u_lightcol[2] * nl.z;
    v_uv = (a_tex.xy + u_uvoff.zw) / 4096.0;
#if EFFECTS
    if (u_fx[0].x == 1.0) {
        v_fx = vec3((a_pos.yz - u_fx[1].xy) / u_fx[1].zw, v_uv.y);
    }
    v_nrm = n;
    v_pos = a_pos.xyz;
    if (u_fx[0].x == 3.0 && u_fx[2].w == -2.0 && u_fx[3].w > 0.0) {
        v_pos = fx_wobble(a_pos.xyz);
        gl_Position = u_lv * vec4(v_pos, 1.0);
    }
    if (u_fx[0].x == 4.0) {
        gl_Position = fx_shell(a_pos.xyz, n);
    }
#endif
#if PROGRAM == TRIC
    v_col = vec4(min(a_col.rgb + u_diffuse.rgb * (u_ambient.rgb + lit.rgb), 255.0), a_col.a);
#elif PROGRAM == TRICU
    v_col = a_col;
#elif PROGRAM == TRICA
    v_col = vec4(a_col.rgb, a_col.a * min(lit.a, 1.0));
    v_uv += u_uvoff.xy;
#else
    v_col = vec4(min((u_ambient.rgb + lit.rgb) * a_col.rgb, 255.0), a_col.a);
    v_uv += u_uvoff.xy;
#endif
#endif
#endif
}
