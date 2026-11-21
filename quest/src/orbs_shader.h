// GLSL shared by gfx.cpp and the host harness tools/orbsview.cpp (which renders the orbs with Mesa to a PNG, so a
// shader change can be looked at and a compile error caught before an APK goes on the headset).
#pragma once

namespace vrq {
namespace shaders {

inline const char* kVS = R"(#version 300 es
out vec2 vUv;
void main() {
    // full-screen triangle
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vUv = vec2(p.x, 1.0 - p.y);   // decoded image row 0 is the top of the picture
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";
// Sound orbs (+91): the ambisonic peaks as volumetric blobs floating in the room, one eye per call, so they carry
// real stereo depth (the flat map is at infinity). Each pixel's ray is marched through the union of the orbs'
// bounding spheres only; the density is a soft sphere per orb wobbled by two octaves of value noise, summed so
// neighbours merge, lit by a density-weighted outward normal (no gradient taps) with a rim. Output is
// premultiplied alpha for XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT; pixels off every orb cost one loop.
inline const char* kFSOrbs = R"(#version 300 es
precision highp float;
const int kMax = 8;
uniform vec3 uEye;
uniform mat3 uRot;            // view -> world (columns: the view's right, up, back in world)
uniform vec4 uTan;            // tan of angleLeft, angleRight, angleUp, angleDown (left and down negative)
uniform float uTime;
uniform int uCount;
uniform vec4 uOrb[kMax];      // centre xyz, radius
uniform vec4 uOrbP[kMax];     // level, alpha, phase, unused
in vec2 vUv;
out vec4 oColor;
float hash(vec3 p) {
    p = fract(p * 0.3183099 + 0.1);
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}
float vnoise(vec3 x) {
    vec3 i = floor(x), f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(hash(i), hash(i + vec3(1, 0, 0)), f.x), mix(hash(i + vec3(0, 1, 0)), hash(i + vec3(1, 1, 0)), f.x), f.y),
               mix(mix(hash(i + vec3(0, 0, 1)), hash(i + vec3(1, 0, 1)), f.x), mix(hash(i + vec3(0, 1, 1)), hash(i + vec3(1, 1, 1)), f.x), f.y), f.z);
}
// False colour by intensity, a limited warm ramp: a quiet source is a deep translucent red, a loud one yellow-white.
vec3 ramp(float l) {
    l = clamp(l, 0.0, 1.0);
    vec3 lo = vec3(0.55, 0.06, 0.04), mid = vec3(1.0, 0.42, 0.06), hi = vec3(1.0, 0.95, 0.55);
    return l < 0.5 ? mix(lo, mid, l * 2.0) : mix(mid, hi, (l - 0.5) * 2.0);
}
// the orbs' summed density at p: a soft shell per orb, lumped by noise, summed so neighbours bridge into one shape
// the way the flat map's blobs ran together; lev is the density-weighted intensity for the colour ramp
float dens(vec3 p, out float glow, out float lev) {
    float rho = 0.0;
    glow = 0.0;
    lev = 0.0;
    for (int i = 0; i < kMax; i++) {
        if (i >= uCount) break;
        vec3 rel = p - uOrb[i].xyz;
        float dist = length(rel);
        float r = uOrb[i].w;
        if (dist > r * 1.6) continue;
        float ph = uOrbP[i].z;
        // slow amorphous wobble: lobes that drift and breathe rather than spin
        vec3 q = rel * (1.45 / r) + vec3(ph, ph * 0.7, -ph) + vec3(0.21, 0.15, 0.12) * uTime;
        float n = 0.72 * vnoise(q) + 0.28 * vnoise(q.yzx * 1.9 + 3.7 - vec3(0.09, 0.06, 0.05) * uTime);
        float re = r * (0.58 + 0.80 * n) * (0.6 + 0.4 * uOrbP[i].y);   // fades in by growing as well as clearing
        float s = clamp(1.0 - dist / re, 0.0, 1.0);                      // 1 at the centre, 0 at the lumpy surface
        float m = clamp(1.0 - (dist * dist) / (re * re * 1.56), 0.0, 1.0);   // metaball falloff reaching 1.25 re, so
        float di = m * m * (0.35 + 0.65 * uOrbP[i].x) * uOrbP[i].y;          // two near orbs sum into one bridged shape
        rho += di;
        glow += smoothstep(0.3, 0.95, s) * uOrbP[i].x * uOrbP[i].y;
        lev += di * uOrbP[i].x;
    }
    return rho;
}
void main() {
    vec3 dv = normalize(vec3(mix(uTan.x, uTan.y, vUv.x), mix(uTan.z, uTan.w, vUv.y), -1.0));
    vec3 d = uRot * dv;
    // the ray's span through the orbs' bounding spheres (1.6 r covers the wobble and the soft skirt)
    float t0 = 1e9, t1 = -1e9;
    float rmin = 1e9;
    for (int i = 0; i < kMax; i++) {
        if (i >= uCount) break;
        vec3 oc = uEye - uOrb[i].xyz;
        float R = uOrb[i].w * 1.6;
        float b = dot(oc, d), c = dot(oc, oc) - R * R;
        float disc = b * b - c;
        if (disc > 0.0) {
            float s = sqrt(disc);
            t0 = min(t0, -b - s);
            t1 = max(t1, -b + s);
            rmin = min(rmin, uOrb[i].w);
        }
    }
    if (t1 < 0.0) { oColor = vec4(0.0); return; }
    t0 = max(t0, 0.0);
    const int N = 28;
    float dt = (t1 - t0) / float(N);
    float eps = 0.07 * rmin;
    vec3 acc = vec3(0.0);
    float T = 1.0;
    vec3 L = normalize(vec3(0.35, 0.8, 0.45));                       // light from above and behind the viewer
    float jit = hash(vec3(vUv * 1024.0, 0.5));                      // fixed per-pixel start offset: grain instead of step rings
    bool lit = false;
    vec3 nn = vec3(0.0);
    float diff = 0.8, spec = 0.0, rim = 0.0;
    for (int k = 0; k < N; k++) {
        float t = t0 + (float(k) + jit) * dt;
        vec3 p = uEye + d * t;
        float glow, lev;
        float rho = dens(p, glow, lev);
        if (rho <= 0.002) continue;
        float a = 1.0 - exp(-rho * dt * 3.0 / max(rmin, 0.05));       // thin: the picture stays visible through a full orb
        if (!lit && 1.0 - T * (1.0 - a) > 0.18) {   // the normal from forward differences of the density, taken once
            lit = true;                               // the ray is well into the skin (the skirt's gradient is speckle)
            float g1, g2, l1, l2;
            nn = normalize(vec3(rho - dens(p + vec3(eps, 0, 0), g1, l1), rho - dens(p + vec3(0, eps, 0), g2, l2), rho - dens(p + vec3(0, 0, eps), g1, l1)) + vec3(1e-6));
            diff = 0.55 + 0.45 * max(dot(nn, L), 0.0);
            spec = pow(max(dot(reflect(-L, nn), -d), 0.0), 36.0);
            rim = pow(1.0 - abs(dot(nn, -d)), 2.5);
        }
        vec3 base = ramp(lev / max(rho, 1e-4));
        vec3 col = base * diff + base * glow * 0.9 + vec3(1.0, 0.98, 0.9) * spec * 0.5 + base * rim * 0.7;
        acc += T * a * col;
        T *= 1.0 - a;
        if (T < 0.03) break;
    }
    float alpha = (1.0 - T) * 0.8;   // never fully opaque: this is a scope over the picture, not a thing in it
    oColor = vec4(acc * 0.8, alpha);
}
)";

}  // namespace shaders
}  // namespace vrq
