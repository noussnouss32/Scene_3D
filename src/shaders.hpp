// ============================================================================
//  shaders.hpp - Tous les shaders GLSL 3.30 core du projet
//  Le modèle d'éclairage reproduit MeshStandardMaterial :
//   - Lambert/π pour le diffus, GGX (Smith corrélé + Schlick) pour le spéculaire
//   - lumière hémisphérique, directionnelle (ombres PCF), 3 lumières ponctuelles
//     (decay = 2, distance de coupure), ombre cubique pour le feu de camp
//   - brouillard exponentiel FogExp2 calculé en espace linéaire
// ============================================================================
#pragma once

namespace Shaders {

// ---------------------------------------------------------------------------
//  Matériau PBR principal
// ---------------------------------------------------------------------------
static const char* PBR_VS = R"GLSL(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
uniform mat3 uNormalMat;

out vec3  vWorld;
out vec3  vNormal;
out vec2  vUV;
out float vFogDepth;

void main() {
    vec4 w = uModel * vec4(aPos, 1.0);
    vWorld  = w.xyz;
    vNormal = normalize(uNormalMat * aNormal);
    vUV     = aUV;
    vec4 v  = uView * w;
    vFogDepth = -v.z;
    gl_Position = uProj * v;
}
)GLSL";

static const char* PBR_FS = R"GLSL(
#version 330 core
in vec3  vWorld;
in vec3  vNormal;
in vec2  vUV;
in float vFogDepth;
out vec4 FragColor;

const float PI = 3.141592653589793;
const float RECIPROCAL_PI = 0.3183098861837907;
const float EPSILON = 1e-6;

uniform vec3  uCamPos;

// ---- matériau ----
uniform vec3  uColor;
uniform vec3  uEmissive;      // déjà multiplié par emissiveIntensity
uniform float uRough;
uniform float uMetal;
uniform bool  uFlat;
uniform bool  uHasMap;
uniform bool  uHasNormalMap;
uniform bool  uReceive;
uniform sampler2D uMap;
uniform sampler2D uNormalMap;
uniform vec2  uMapRepeat;
uniform vec2  uNormalRepeat;

// ---- lumières ----
uniform vec3  uHemiSky;       // couleur * intensité
uniform vec3  uHemiGround;
uniform vec3  uSunDir;        // direction VERS le soleil
uniform vec3  uSunColor;      // couleur * intensité
uniform vec3  uPointPos[3];
uniform vec3  uPointColor[3]; // couleur * intensité
uniform float uPointDist[3];

uniform sampler2DShadow uSunShadow;
uniform mat4  uSunVP;
uniform float uSunShadowSize;
uniform samplerCube uFireShadow;
uniform float uFireFar;

// ---- brouillard ----
uniform vec3  uFogColor;
uniform float uFogDensity;

float pow2(float x) { return x * x; }
float pow4(float x) { float x2 = x * x; return x2 * x2; }
float sat(float x)  { return clamp(x, 0.0, 1.0); }

// --- BRDF GGX (identique à Three.js) ---
vec3 F_Schlick(vec3 f0, float f90, float dotVH) {
    float fresnel = exp2((-5.55473 * dotVH - 6.98316) * dotVH);
    return f0 * (1.0 - fresnel) + (f90 * fresnel);
}
float V_GGX_SmithCorrelated(float alpha, float dotNL, float dotNV) {
    float a2 = pow2(alpha);
    float gv = dotNL * sqrt(a2 + (1.0 - a2) * pow2(dotNV));
    float gl = dotNV * sqrt(a2 + (1.0 - a2) * pow2(dotNL));
    return 0.5 / max(gv + gl, EPSILON);
}
float D_GGX(float alpha, float dotNH) {
    float a2 = pow2(alpha);
    float denom = pow2(dotNH) * (a2 - 1.0) + 1.0;
    return RECIPROCAL_PI * a2 / pow2(denom);
}
vec3 BRDF_GGX(vec3 L, vec3 V, vec3 N, vec3 f0, float rough) {
    float alpha = pow2(rough);
    vec3 H = normalize(L + V);
    float dotNL = sat(dot(N, L));
    float dotNV = sat(dot(N, V));
    float dotNH = sat(dot(N, H));
    float dotVH = sat(dot(V, H));
    vec3  F = F_Schlick(f0, 1.0, dotVH);
    float Vis = V_GGX_SmithCorrelated(alpha, dotNL, dotNV);
    float D = D_GGX(alpha, dotNH);
    return F * (Vis * D);
}

// --- normal mapping (perturbNormal2Arb de Three.js) ---
vec3 perturbNormal(vec3 pos, vec3 N, vec2 uv, vec3 mapN) {
    vec3 q0 = dFdx(pos);
    vec3 q1 = dFdy(pos);
    vec2 st0 = dFdx(uv);
    vec2 st1 = dFdy(uv);
    vec3 q1perp = cross(q1, N);
    vec3 q0perp = cross(N, q0);
    vec3 T = q1perp * st0.x + q0perp * st1.x;
    vec3 B = q1perp * st0.y + q0perp * st1.y;
    float det = max(dot(T, T), dot(B, B));
    float scale = (det == 0.0) ? 0.0 : inversesqrt(det);
    return normalize(T * (mapN.x * scale) + B * (mapN.y * scale) + N * mapN.z);
}

// --- ombre soleil (PCF 3x3 avec comparaison matérielle bilinéaire) ---
float sunShadow() {
    vec4 sc = uSunVP * vec4(vWorld, 1.0);
    vec3 p = sc.xyz / sc.w * 0.5 + 0.5;
    if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0 || p.z > 1.0) return 1.0;
    float ts = 1.0 / uSunShadowSize;
    float s = 0.0;
    for (int x = -1; x <= 1; x++)
        for (int y = -1; y <= 1; y++)
            s += texture(uSunShadow, vec3(p.xy + vec2(x, y) * ts, p.z - 0.0005));
    return s / 9.0;
}

// --- ombre omnidirectionnelle du feu de camp ---
const vec3 SAMPLE_OFFSETS[20] = vec3[](
    vec3( 1, 1, 1), vec3( 1,-1, 1), vec3(-1,-1, 1), vec3(-1, 1, 1),
    vec3( 1, 1,-1), vec3( 1,-1,-1), vec3(-1,-1,-1), vec3(-1, 1,-1),
    vec3( 1, 1, 0), vec3( 1,-1, 0), vec3(-1,-1, 0), vec3(-1, 1, 0),
    vec3( 1, 0, 1), vec3(-1, 0, 1), vec3( 1, 0,-1), vec3(-1, 0,-1),
    vec3( 0, 1, 1), vec3( 0,-1, 1), vec3( 0,-1,-1), vec3( 0, 1,-1));

float fireShadow() {
    vec3 d = vWorld - uPointPos[0];
    float dist = length(d);
    if (dist >= uFireFar) return 1.0;
    float diskR = 0.012 + dist * 0.004;
    float lit = 0.0;
    for (int i = 0; i < 20; i++) {
        float closest = texture(uFireShadow, d + SAMPLE_OFFSETS[i] * diskR * dist * 0.25).r * uFireFar;
        lit += (dist - 0.12 <= closest) ? 1.0 : 0.0;
    }
    return lit / 20.0;
}

void main() {
    vec3 P = vWorld;
    vec3 V = normalize(uCamPos - P);

    // ---- normale géométrique (lisse ou facettée) ----
    vec3 N = uFlat ? normalize(cross(dFdx(P), dFdy(P))) : normalize(vNormal);

    // anti-aliasing spéculaire (geometryRoughness de Three.js)
    vec3 dxy = max(abs(dFdx(N)), abs(dFdy(N)));
    float geometryRoughness = max(max(dxy.x, dxy.y), dxy.z);

    // ---- normal map ----
    if (uHasNormalMap) {
        vec2 nuv = vUV * uNormalRepeat;
        vec3 mapN = texture(uNormalMap, nuv).xyz * 2.0 - 1.0;
        N = perturbNormal(P, N, nuv, mapN);
    }

    // ---- albedo ----
    vec3 base = uColor;
    if (uHasMap) base *= texture(uMap, vUV * uMapRepeat).rgb;

    float rough = min(max(clamp(uRough, 0.0, 1.0), 0.0525) + geometryRoughness, 1.0);
    vec3 diffuseCol = base * (1.0 - uMetal);
    vec3 f0 = mix(vec3(0.04), base, uMetal);

    vec3 direct = vec3(0.0);

    // ---- soleil / lune ----
    {
        vec3 L = normalize(uSunDir);
        float nl = sat(dot(N, L));
        if (nl > 0.0) {
            vec3 col = uSunColor;
            if (uReceive) col *= sunShadow();
            vec3 irr = nl * col;
            direct += irr * (BRDF_GGX(L, V, N, f0, rough) + RECIPROCAL_PI * diffuseCol);
        }
    }

    // ---- lumières ponctuelles : feu, champignon cyan, champignon violet ----
    for (int i = 0; i < 3; i++) {
        vec3 Lv = uPointPos[i] - P;
        float d = length(Lv);
        vec3 L = Lv / d;
        float nl = sat(dot(N, L));
        float att = 1.0 / max(pow(d, 2.0), 0.01);
        if (uPointDist[i] > 0.0) att *= pow2(sat(1.0 - pow4(d / uPointDist[i])));
        vec3 col = uPointColor[i] * att;
        if (nl > 0.0 && att > 0.0) {
            if (i == 0 && uReceive) col *= fireShadow();
            vec3 irr = nl * col;
            direct += irr * (BRDF_GGX(L, V, N, f0, rough) + RECIPROCAL_PI * diffuseCol);
        }
    }

    // ---- lumière hémisphérique (ambiante ciel/sol) ----
    float hw = 0.5 * N.y + 0.5;
    vec3 hemiIrr = mix(uHemiGround, uHemiSky, hw);
    vec3 indirect = hemiIrr * RECIPROCAL_PI * diffuseCol;

    vec3 color = direct + indirect + uEmissive;

    // ---- brouillard exponentiel (FogExp2) ----
    float fogFactor = 1.0 - exp(-uFogDensity * uFogDensity * vFogDepth * vFogDepth);
    color = mix(color, uFogColor, fogFactor);

    FragColor = vec4(color, 1.0);
}
)GLSL";

// ---------------------------------------------------------------------------
//  Matériau "basic" (flamme filaire) - non éclairé, soumis au brouillard
// ---------------------------------------------------------------------------
static const char* BASIC_FS = R"GLSL(
#version 330 core
in vec3  vWorld;
in vec3  vNormal;
in vec2  vUV;
in float vFogDepth;
out vec4 FragColor;
uniform vec3  uColor;
uniform vec3  uFogColor;
uniform float uFogDensity;
void main() {
    float f = 1.0 - exp(-uFogDensity * uFogDensity * vFogDepth * vFogDepth);
    FragColor = vec4(mix(uColor, uFogColor, f), 1.0);
}
)GLSL";

// ---------------------------------------------------------------------------
//  Particules (lucioles / pluie) - THREE.Points avec sizeAttenuation
// ---------------------------------------------------------------------------
static const char* POINTS_VS = R"GLSL(
#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4  uView;
uniform mat4  uProj;
uniform float uSize;      // taille monde
uniform float uScale;     // hauteur framebuffer / 2
out float vFogDepth;
void main() {
    vec4 mv = uView * vec4(aPos, 1.0);
    vFogDepth = -mv.z;
    gl_Position = uProj * mv;
    gl_PointSize = uSize * (uScale / -mv.z);
}
)GLSL";

static const char* POINTS_FS = R"GLSL(
#version 330 core
in float vFogDepth;
out vec4 FragColor;
uniform vec3  uColor;
uniform float uOpacity;
uniform vec3  uFogColor;
uniform float uFogDensity;
void main() {
    float f = 1.0 - exp(-uFogDensity * uFogDensity * vFogDepth * vFogDepth);
    FragColor = vec4(mix(uColor, uFogColor, f), uOpacity);
}
)GLSL";

// ---------------------------------------------------------------------------
//  Passes d'ombres
// ---------------------------------------------------------------------------
static const char* DEPTH_VS = R"GLSL(
#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uModel;
uniform mat4 uVP;
out vec3 vWorld;
void main() {
    vec4 w = uModel * vec4(aPos, 1.0);
    vWorld = w.xyz;
    gl_Position = uVP * w;
}
)GLSL";

static const char* DEPTH_FS = R"GLSL(
#version 330 core
void main() {}
)GLSL";

static const char* CUBE_DEPTH_FS = R"GLSL(
#version 330 core
in vec3 vWorld;
out float FragDist;
uniform vec3  uLightPos;
uniform float uFar;
void main() { FragDist = length(vWorld - uLightPos) / uFar; }
)GLSL";

// ---------------------------------------------------------------------------
//  Post-traitement : UnrealBloomPass (haute-luminosité, flou gaussien x5, composite)
// ---------------------------------------------------------------------------
static const char* FULLSCREEN_VS = R"GLSL(
#version 330 core
out vec2 vUv;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";

static const char* HIGHPASS_FS = R"GLSL(
#version 330 core
in vec2 vUv;
out vec4 FragColor;
uniform sampler2D uTex;
uniform float uThreshold;
uniform float uSmoothWidth;
void main() {
    vec4 texel = texture(uTex, vUv);
    float v = dot(texel.rgb, vec3(0.299, 0.587, 0.114));
    float alpha = smoothstep(uThreshold, uThreshold + uSmoothWidth, v);
    FragColor = mix(vec4(0.0), texel, alpha);
}
)GLSL";

static const char* BLUR_FS = R"GLSL(
#version 330 core
in vec2 vUv;
out vec4 FragColor;
uniform sampler2D uTex;
uniform vec2  uInvSize;
uniform vec2  uDir;
uniform int   uRadius;
uniform float uSigma;
float gaussianPdf(float x, float sigma) { return 0.39894 * exp(-0.5 * x * x / (sigma * sigma)) / sigma; }
void main() {
    float weightSum = gaussianPdf(0.0, uSigma);
    vec3 sum = texture(uTex, vUv).rgb * weightSum;
    for (int i = 1; i < uRadius; i++) {
        float x = float(i);
        float w = gaussianPdf(x, uSigma);
        vec2 off = uDir * uInvSize * x;
        sum += (texture(uTex, vUv + off).rgb + texture(uTex, vUv - off).rgb) * w;
        weightSum += 2.0 * w;
    }
    FragColor = vec4(sum / weightSum, 1.0);
}
)GLSL";

static const char* COMPOSITE_FS = R"GLSL(
#version 330 core
in vec2 vUv;
out vec4 FragColor;
uniform sampler2D uBlur0;
uniform sampler2D uBlur1;
uniform sampler2D uBlur2;
uniform sampler2D uBlur3;
uniform sampler2D uBlur4;
uniform float uStrength;
uniform float uRadius;
float lerpBloomFactor(float factor) {
    float mirrorFactor = 1.2 - factor;
    return mix(factor, mirrorFactor, uRadius);
}
void main() {
    vec3 c = lerpBloomFactor(1.0) * texture(uBlur0, vUv).rgb
           + lerpBloomFactor(0.8) * texture(uBlur1, vUv).rgb
           + lerpBloomFactor(0.6) * texture(uBlur2, vUv).rgb
           + lerpBloomFactor(0.4) * texture(uBlur3, vUv).rgb
           + lerpBloomFactor(0.2) * texture(uBlur4, vUv).rgb;
    FragColor = vec4(uStrength * c, 1.0);
}
)GLSL";

// Sortie finale : scène linéaire -> sRGB, puis ajout additif du bloom (comme le HTML)
static const char* FINAL_FS = R"GLSL(
#version 330 core
in vec2 vUv;
out vec4 FragColor;
uniform sampler2D uScene;
uniform sampler2D uBloom;
vec3 linearToSRGB(vec3 c) {
    return mix(pow(c, vec3(0.41666)) * 1.055 - vec3(0.055), c * 12.92, vec3(lessThanEqual(c, vec3(0.0031308))));
}
void main() {
    vec3 scene = texture(uScene, vUv).rgb;
    vec3 bloom = texture(uBloom, vUv).rgb;
    FragColor = vec4(linearToSRGB(clamp(scene, 0.0, 1.0)) + bloom, 1.0);
}
)GLSL";

} // namespace Shaders
