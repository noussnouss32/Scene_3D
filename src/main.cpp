// ============================================================================
//  FORÊT ENCHANTÉE 3D  -  Version C++ / OpenGL 3.3 core
//   textures PBR procédurales (écorce, normal map, sol moussu)
//   sapins, chênes, champignons bioluminescents, feu de camp, relief ondulé
//   cycle Jour / Coucher / Nuit / Aube, brouillard exponentiel, vent
//   lucioles, pluie, ombres (soleil + feu de camp), bloom (UnrealBloom)
//   caméra orbitale avec inertie + 4 vues prédéfinies, picking par clic
// ============================================================================
#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
  #define NOMINMAX
  #include <windows.h>
#endif

#include "geometry.hpp"
#include "shaders.hpp"
#include "textures.hpp"

using glm::mat3; using glm::mat4; using glm::vec2; using glm::vec3; using glm::vec4;

static const float PI = 3.14159265358979f;

// ----------------------------------------------------------------------------
//  Utilitaires
// ----------------------------------------------------------------------------
// THREE.Color(0xRRGGBB) : hex sRGB -> linéaire (ColorManagement activé)
static vec3 hexColor(uint32_t hex) {
    auto conv = [](float c) { return c <= 0.04045f ? c * (1.0f / 12.92f) : std::pow((c + 0.055f) / 1.055f, 2.4f); };
    return { conv(((hex >> 16) & 255) / 255.0f), conv(((hex >> 8) & 255) / 255.0f), conv((hex & 255) / 255.0f) };
}

struct Program {
    GLuint id = 0;
    std::unordered_map<std::string, GLint> cache;

    static GLuint compile(GLenum type, const char* src, const char* name) {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[2048]; glGetShaderInfoLog(s, sizeof(log), nullptr, log);
            std::fprintf(stderr, "[Shader %s] erreur de compilation:\n%s\n", name, log);
            std::exit(1);
        }
        return s;
    }
    void build(const char* vs, const char* fs, const char* name) {
        GLuint v = compile(GL_VERTEX_SHADER, vs, name), f = compile(GL_FRAGMENT_SHADER, fs, name);
        id = glCreateProgram();
        glAttachShader(id, v); glAttachShader(id, f);
        glLinkProgram(id);
        GLint ok = 0; glGetProgramiv(id, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[2048]; glGetProgramInfoLog(id, sizeof(log), nullptr, log);
            std::fprintf(stderr, "[Programme %s] erreur d'édition de liens:\n%s\n", name, log);
            std::exit(1);
        }
        glDeleteShader(v); glDeleteShader(f);
    }
    void use() const { glUseProgram(id); }
    GLint u(const char* n) {
        auto it = cache.find(n);
        if (it != cache.end()) return it->second;
        GLint l = glGetUniformLocation(id, n);
        cache[n] = l;
        return l;
    }
    void i1(const char* n, int v)           { glUniform1i(u(n), v); }
    void f1(const char* n, float v)         { glUniform1f(u(n), v); }
    void f2(const char* n, const vec2& v)   { glUniform2fv(u(n), 1, glm::value_ptr(v)); }
    void f3(const char* n, const vec3& v)   { glUniform3fv(u(n), 1, glm::value_ptr(v)); }
    void m4(const char* n, const mat4& m)   { glUniformMatrix4fv(u(n), 1, GL_FALSE, glm::value_ptr(m)); }
    void m3(const char* n, const mat3& m)   { glUniformMatrix3fv(u(n), 1, GL_FALSE, glm::value_ptr(m)); }
};

// ----------------------------------------------------------------------------
//  Matériaux & objets de scène
// ----------------------------------------------------------------------------
struct Material {                     // MeshStandardMaterial
    vec3  color{1.0f};
    vec3  emissive{0.0f};
    float emissiveIntensity = 0.0f;
    float roughness = 1.0f, metalness = 0.0f;
    bool  flat = false;
    GLuint map = 0, normalMap = 0;
    vec2  mapRepeat{1, 1}, normalRepeat{1, 1};
};

struct Object {
    Mesh*     mesh = nullptr;
    Material* mat = nullptr;          // nullptr + wire = flamme filaire
    mat4      model{1.0f};
    bool      castShadow = false, receiveShadow = false;
    bool      wire = false;
    vec3      wireColor{1.0f};
    // animation du vent (rotation locale autour du centre de la géométrie)
    bool      animated = false;
    int       windIdx = 0;
    mat4      base{1.0f};
};

struct Interactive { Mesh* mesh; mat4 model; std::string name; };

static mat4 eulerXYZ(float x, float y, float z) {            // THREE.Euler 'XYZ'
    return glm::rotate(mat4(1), x, vec3(1, 0, 0)) * glm::rotate(mat4(1), y, vec3(0, 1, 0)) * glm::rotate(mat4(1), z, vec3(0, 0, 1));
}

// ----------------------------------------------------------------------------
//  Caméra orbitale 
// ----------------------------------------------------------------------------
struct CameraController {
    vec3  pos{0, 18, 28}, target{0, 0, 0};
    float fovDeg = 45.0f;
    // OrbitControls
    float damping = 0.05f, minDist = 1.5f, maxDist = 45.0f, maxPolar = PI / 2 - 0.02f;
    float sdTheta = 0, sdPhi = 0, scale = 1;
    vec3  panOffset{0};
    // Presets (lerp)
    bool  lerping = false; float t = 0;
    vec3  startPos, startLook, targetPos{0, 18, 28}, targetLook{0, 0, 0};

    void setPreset(const std::string& name) {
        lerping = true; t = 0; startPos = pos; startLook = target;
        if (name == "overview")       { targetPos = {0, 18, 28};        targetLook = {0, 1, 0}; }
        else if (name == "trail")     { targetPos = {-8, 2, 12};        targetLook = {0, 1.5f, 0}; }
        else if (name == "campfire")  { targetPos = {2.5f, 1.8f, 3.5f}; targetLook = {0, 0.5f, 0}; }
        else if (name == "mushrooms") { targetPos = {-5.2f, 0.9f, 6.2f}; targetLook = {-6, 0.4f, 5}; }
    }
    void basis(vec3& right, vec3& up) const {
        vec3 f = glm::normalize(target - pos);
        right = glm::normalize(glm::cross(f, vec3(0, 1, 0)));
        up = glm::cross(right, f);
    }
    void rotate(float dx, float dy, float winH) {
        sdTheta -= 2 * PI * dx / winH;
        sdPhi   -= 2 * PI * dy / winH;
    }
    void pan(float dx, float dy, float winH) {
        vec3 r, u; basis(r, u);
        float dist = glm::length(pos - target) * std::tan(glm::radians(fovDeg * 0.5f));
        panOffset += -r * (2 * dx * dist / winH) + u * (2 * dy * dist / winH);
    }
    void zoom(float scrollY) { scale *= std::pow(0.95f, scrollY); }   // scrollY>0 : rapproche
    void update(float dt) {
        if (lerping) {
            t += dt * 1.8f;
            if (t >= 1.0f) { t = 1.0f; lerping = false; }
            float e = -(std::cos(PI * t) - 1.0f) / 2.0f;
            pos = glm::mix(startPos, targetPos, e);
            target = glm::mix(startLook, targetLook, e);
        }
        const float EPS = 1e-6f;
        vec3 off = pos - target;
        float radius = glm::length(off);
        float theta = std::atan2(off.x, off.z);
        float phi = std::acos(glm::clamp(off.y / std::max(radius, 1e-6f), -1.0f, 1.0f));
        theta += sdTheta * damping;
        phi   += sdPhi * damping;
        phi = glm::clamp(phi, EPS, PI - EPS);
        phi = std::max(0.0f, std::min(maxPolar, phi));
        phi = glm::clamp(phi, EPS, PI - EPS);
        radius = glm::clamp(radius * scale, minDist, maxDist);
        target += panOffset * damping;
        off = radius * vec3(std::sin(phi) * std::sin(theta), std::cos(phi), std::sin(phi) * std::cos(theta));
        pos = target + off;
        sdTheta *= (1 - damping); sdPhi *= (1 - damping); panOffset *= (1 - damping);
        scale = 1;
    }
    mat4 view() const { return glm::lookAt(pos, target, vec3(0, 1, 0)); }
};

// ----------------------------------------------------------------------------
//  Éclairage & cycle jour/nuit
// ----------------------------------------------------------------------------
struct LightingSystem {
    vec3  hemiSky = hexColor(0x87ceeb), hemiGround = hexColor(0x111e15);
    float hemiIntensity = 0.6f;
    vec3  sunPos{25, 40, 20}, sunColor = hexColor(0xfffaed);
    float sunIntensity = 1.2f;
    vec3  bg = hexColor(0x68a0ca);
    vec3  fogColor = hexColor(0x68a0ca);

    // point lights : 0 = feu, 1 = champignon cyan, 2 = champignon violet
    vec3  pPos[3]   = { {0, 0.8f, 0}, {-6, 0.5f, 5}, {5, 0.5f, -4} };
    vec3  pColor[3] = { hexColor(0xff6600), hexColor(0x00f3ff), hexColor(0xa855f7) };
    float pDist[3]  = { 18.0f, 6.0f, 6.0f };
    float pInt[3]   = { 3.5f, 2.0f, 2.0f };

    void setTimeOfDay(const std::string& p) {
        if (p == "day") {
            sunPos = {25, 40, 20}; sunColor = hexColor(0xfffaed); sunIntensity = 1.2f;
            hemiSky = hexColor(0x87ceeb); hemiGround = hexColor(0x111e15);
            bg = fogColor = hexColor(0x68a0ca);
        } else if (p == "sunset") {
            sunPos = {35, 8, -15}; sunColor = hexColor(0xff6b35); sunIntensity = 1.5f;
            hemiSky = hexColor(0xfd8d3c); hemiGround = hexColor(0x2d1200);
            bg = fogColor = hexColor(0x3a1c28);
        } else if (p == "night") {
            sunPos = {-15, 30, -20}; sunColor = hexColor(0x406090); sunIntensity = 0.3f;
            hemiSky = hexColor(0x0d1b2a); hemiGround = hexColor(0x020804);
            bg = fogColor = hexColor(0x050c08);
        } else if (p == "dawn") {
            sunPos = {-30, 10, 20}; sunColor = hexColor(0xffcb74); sunIntensity = 0.9f;
            hemiSky = hexColor(0xb5838d); hemiGround = hexColor(0x102015);
            bg = fogColor = hexColor(0x1d2d3a);
        }
    }
    void update(float time) {
        float flicker = std::sin(time * 12) * 0.3f + std::cos(time * 23) * 0.2f;   // feu de camp
        pInt[0] = 3.5f + flicker;
        pInt[1] = 1.8f + std::sin(time * 2.5f) * 0.6f;                             // champignons
        pInt[2] = 1.8f + std::cos(time * 2.0f) * 0.6f;
    }
};

// ----------------------------------------------------------------------------
//  Particules : lucioles + pluie
// ----------------------------------------------------------------------------
struct PointCloud {
    std::vector<float> pos;
    GLuint vao = 0, vbo = 0;
    bool visible = true;
    void create(Rng& rng, int count, float spanX, float yMin, float yRange, float spanZ) {
        pos.resize(count * 3);
        for (int i = 0; i < count; i++) {
            pos[i * 3]     = (rng.next() - 0.5f) * spanX;
            pos[i * 3 + 1] = rng.next() * yRange + yMin;
            pos[i * 3 + 2] = (rng.next() - 0.5f) * spanZ;
        }
        glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, pos.size() * sizeof(float), pos.data(), GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
        glBindVertexArray(0);
    }
    void sync() {
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, pos.size() * sizeof(float), pos.data());
    }
    void draw() const { glBindVertexArray(vao); glDrawArrays(GL_POINTS, 0, (GLsizei)(pos.size() / 3)); }
};

// ----------------------------------------------------------------------------
//  Effet Bloom (UnrealBloomPass : high-pass + 5 flous gaussiens + composite)
// ----------------------------------------------------------------------------
struct Bloom {
    static constexpr int N = 5;
    int w = 0, h = 0;
    int mw[N], mh[N];
    GLuint fboBright = 0, texBright = 0;
    GLuint fboH[N] = {}, texH[N] = {}, fboV[N] = {}, texV[N] = {};
    float strength = 1.2f, radius = 0.4f, threshold = 0.85f;
    Program high, blur, comp;

    static void makeRT(int W, int H, GLuint& fbo, GLuint& tex) {
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, W, H, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    }
    void initPrograms() {
        high.build(Shaders::FULLSCREEN_VS, Shaders::HIGHPASS_FS, "highpass");
        blur.build(Shaders::FULLSCREEN_VS, Shaders::BLUR_FS, "blur");
        comp.build(Shaders::FULLSCREEN_VS, Shaders::COMPOSITE_FS, "composite");
    }
    void destroy() {
        if (!fboBright) return;
        glDeleteFramebuffers(1, &fboBright); glDeleteTextures(1, &texBright);
        for (int i = 0; i < N; i++) {
            glDeleteFramebuffers(1, &fboH[i]); glDeleteTextures(1, &texH[i]);
            glDeleteFramebuffers(1, &fboV[i]); glDeleteTextures(1, &texV[i]);
        }
        fboBright = 0;
    }
    void resize(int W, int H) {
        destroy();
        w = W; h = H;
        int rx = std::max(1, (int)std::lround(W / 2.0)), ry = std::max(1, (int)std::lround(H / 2.0));
        makeRT(rx, ry, fboBright, texBright);
        for (int i = 0; i < N; i++) {
            mw[i] = rx; mh[i] = ry;
            makeRT(rx, ry, fboH[i], texH[i]);
            makeRT(rx, ry, fboV[i], texV[i]);
            rx = std::max(1, (int)std::lround(rx / 2.0));
            ry = std::max(1, (int)std::lround(ry / 2.0));
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    // Retourne la texture du bloom composite
    GLuint render(GLuint sceneTex) {
        static const int kernel[N] = { 3, 5, 7, 9, 11 };
        glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);

        // 1. extraction des zones lumineuses
        glBindFramebuffer(GL_FRAMEBUFFER, fboBright);
        glViewport(0, 0, mw[0], mh[0]);
        high.use();
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, sceneTex);
        high.i1("uTex", 0); high.f1("uThreshold", threshold); high.f1("uSmoothWidth", 0.01f);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        // 2. flou gaussien séparable sur 5 niveaux de mip
        blur.use(); blur.i1("uTex", 0);
        GLuint input = texBright;
        for (int i = 0; i < N; i++) {
            blur.f2("uInvSize", vec2(1.0f / mw[i], 1.0f / mh[i]));
            blur.i1("uRadius", kernel[i]); blur.f1("uSigma", (float)kernel[i]);
            glViewport(0, 0, mw[i], mh[i]);
            glBindFramebuffer(GL_FRAMEBUFFER, fboH[i]);
            glBindTexture(GL_TEXTURE_2D, input);
            blur.f2("uDir", vec2(1, 0));
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glBindFramebuffer(GL_FRAMEBUFFER, fboV[i]);
            glBindTexture(GL_TEXTURE_2D, texH[i]);
            blur.f2("uDir", vec2(0, 1));
            glDrawArrays(GL_TRIANGLES, 0, 3);
            input = texV[i];
        }

        // 3. composite des 5 niveaux
        glBindFramebuffer(GL_FRAMEBUFFER, fboH[0]);
        glViewport(0, 0, mw[0], mh[0]);
        comp.use();
        const char* names[N] = { "uBlur0", "uBlur1", "uBlur2", "uBlur3", "uBlur4" };
        for (int i = 0; i < N; i++) {
            glActiveTexture(GL_TEXTURE0 + i); glBindTexture(GL_TEXTURE_2D, texV[i]);
            comp.i1(names[i], i);
        }
        comp.f1("uStrength", strength); comp.f1("uRadius", radius);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glActiveTexture(GL_TEXTURE0);
        return texH[0];
    }
};

// ----------------------------------------------------------------------------
//  Application
// ----------------------------------------------------------------------------
struct Options {
    int width = 1280, height = 720;
    std::string screenshot, time = "day", preset;
    bool rain = false;
    int frames = 6;
    uint32_t seed = 20240601;
};

class App {
public:
    explicit App(const Options& o) : opt(o), rng(o.seed) {}
    int run();

    // callbacks (statiques -> instance)
    static void onResize(GLFWwindow*, int, int);
    static void onKey(GLFWwindow*, int, int, int, int);
    static void onMouseButton(GLFWwindow*, int, int, int);
    static void onCursor(GLFWwindow*, double, double);
    static void onScroll(GLFWwindow*, double, double);

private:
    Options opt;
    Rng rng;
    GLFWwindow* window = nullptr;
    int winW = 1280, winH = 720, fbW = 1280, fbH = 720;

    // programmes
    Program pbr, basic, points, depth, cubeDepth, finalPass;

    // ressources de scène
    BarkTextures bark;
    GLuint groundTex = 0;
    std::vector<std::unique_ptr<Mesh>> meshes;
    std::vector<std::unique_ptr<Material>> materials;
    std::vector<Object> objects;
    std::vector<Interactive> interactive;
    int windCounter = 0;
    Material *barkMat, *pineMat, *oakMat, *capCyan, *capPurple, *stemMat, *rockMat, *groundMat;

    LightingSystem lights;
    CameraController cam;
    PointCloud fireflies, rain;
    Bloom bloom;
    float fogDensity = 0.02f;
    bool windActive = true;
    std::string currentTime = "day";

    // rendu
    GLuint emptyVao = 0;
    GLuint msFbo = 0, msColor = 0, msDepth = 0, resFbo = 0, resTex = 0;
    GLuint sunFbo = 0, sunDepthTex = 0;
    GLuint cubeFbo = 0, cubeTex = 0, cubeDepthRb = 0;
    static constexpr int SUN_SIZE = 2048, CUBE_SIZE = 1024;
    mat4 sunVP{1.0f};
    mat4 proj{1.0f}, view{1.0f};

    // entrées
    bool lmb = false, rmb = false;
    double lastX = 0, lastY = 0, pressX = 0, pressY = 0;
    bool fullscreen = false; int savedX = 0, savedY = 0, savedW = 1280, savedH = 720;
    double toastUntil = 0; std::string toastText;
    double fpsTimer = 0; int fpsFrames = 0; double fps = 60;

    // construction
    Material* newMaterial() { materials.emplace_back(new Material()); return materials.back().get(); }
    Mesh* own(Mesh* m) { meshes.emplace_back(m); return m; }
    void buildMaterials();
    void buildTerrain();
    void buildCampfire();
    void createPineTree(float x, float z, float s);
    void createOakTree(float x, float z, float s);
    void createMushroomCluster(float x, float z, Material* capMat);
    void populateForest();
    void buildParticles();
    void createRenderTargets();
    void createShadowTargets();

    // boucle
    void update(double time, float dt);
    void renderShadows();
    void renderScene();
    void renderPost();
    void drawPBR(const Object& o);
    void updateTitle();

    // interactions
    void setTimeOfDay(const std::string& p);
    void showToast(const std::string& t);
    void pick(double mx, double my);
    void handleKey(int key);
    void toggleFullscreen();
    void saveScreenshot(const std::string& path);
    void resizeBuffers();
};

static App* gApp = nullptr;

// ---------------------------------------------------------------------------
//  Construction de la scène
// ---------------------------------------------------------------------------
void App::buildMaterials() {
    bark = TextureGen::createBark(rng);
    groundTex = TextureGen::createMossyGround(rng);

    barkMat = newMaterial();
    barkMat->color = vec3(1.0f); barkMat->map = bark.albedo; barkMat->normalMap = bark.normal;
    barkMat->mapRepeat = {1, 3}; barkMat->normalRepeat = {1, 1};
    barkMat->roughness = 0.85f; barkMat->metalness = 0.1f;

    pineMat = newMaterial();
    pineMat->color = hexColor(0x1e3a29); pineMat->roughness = 0.6f; pineMat->metalness = 0.1f; pineMat->flat = true;

    oakMat = newMaterial();
    oakMat->color = hexColor(0x2d5a27); oakMat->roughness = 0.5f; oakMat->metalness = 0.1f; oakMat->flat = true;

    capCyan = newMaterial();
    capCyan->color = hexColor(0x00f3ff); capCyan->emissive = hexColor(0x00f3ff); capCyan->emissiveIntensity = 0.8f;
    capCyan->roughness = 0.2f;

    capPurple = newMaterial();
    capPurple->color = hexColor(0x8b5cf6); capPurple->emissive = hexColor(0x8b5cf6); capPurple->emissiveIntensity = 0.8f;
    capPurple->roughness = 0.2f;

    stemMat = newMaterial();
    stemMat->color = hexColor(0xe2e8f0); stemMat->roughness = 0.3f;

    rockMat = newMaterial();
    rockMat->color = hexColor(0x475569); rockMat->roughness = 0.9f;

    groundMat = newMaterial();
    groundMat->color = vec3(1.0f); groundMat->map = groundTex;
    groundMat->mapRepeat = {12, 12}; groundMat->roughness = 0.9f; groundMat->metalness = 0.1f;
}

void App::buildTerrain() {
    Object o;
    o.mesh = own(Geo::terrain(80.0f, 64, -0.2f));
    o.mat = groundMat;
    o.receiveShadow = true;
    objects.push_back(o);
}

void App::createPineTree(float x, float z, float s) {
    mat4 g = glm::translate(mat4(1), vec3(x, 0, z));
    // tronc
    Object t;
    t.mesh = own(Geo::cylinder(0.2f * s, 0.4f * s, 3 * s, 8));
    t.mat = barkMat; t.castShadow = t.receiveShadow = true;
    t.model = g * glm::translate(mat4(1), vec3(0, 1.5f * s, 0));
    objects.push_back(t);
    interactive.push_back({ t.mesh, t.model, "Sapin" });
    // 3 étages de feuillage
    for (int i = 0; i < 3; i++) {
        Object c;
        c.mesh = own(Geo::cone((1.5f - i * 0.3f) * s, (2.2f - i * 0.3f) * s, 7));
        c.mat = pineMat; c.castShadow = c.receiveShadow = true;
        c.base = g * glm::translate(mat4(1), vec3(0, (2.2f + i * 1.2f) * s, 0));
        c.model = c.base; c.animated = true; c.windIdx = windCounter++;
        objects.push_back(c);
    }
}

void App::createOakTree(float x, float z, float s) {
    mat4 g = glm::translate(mat4(1), vec3(x, 0, z));
    Object t;
    t.mesh = own(Geo::cylinder(0.4f * s, 0.7f * s, 4 * s, 9));
    t.mat = barkMat; t.castShadow = true;
    t.model = g * glm::translate(mat4(1), vec3(0, 2 * s, 0));
    objects.push_back(t);
    interactive.push_back({ t.mesh, t.model, "Chêne Majestueux" });

    Object c;
    c.mesh = own(Geo::dodecahedron(2.2f * s, 1));
    c.mat = oakMat; c.castShadow = true;
    c.base = g * glm::translate(mat4(1), vec3(0, 4.2f * s, 0));
    c.model = c.base; c.animated = true; c.windIdx = windCounter++;
    objects.push_back(c);
}

void App::createMushroomCluster(float x, float z, Material* capMat) {
    Mesh* stem = own(Geo::cylinder(0.03f, 0.05f, 0.4f, 8));
    Mesh* cap  = own(Geo::hemisphere(0.18f, 16, 12));
    for (int i = 0; i < 5; i++) {
        float sx = (rng.next() - 0.5f) * 0.8f, sz = (rng.next() - 0.5f) * 0.8f;
        float sc = 0.8f + rng.next() * 0.6f;
        mat4 g = glm::translate(mat4(1), vec3(x + sx, 0.2f, z + sz)) * glm::scale(mat4(1), vec3(sc));
        Object s; s.mesh = stem; s.mat = stemMat; s.model = g; objects.push_back(s);
        Object c; c.mesh = cap; c.mat = capMat; c.model = g * glm::translate(mat4(1), vec3(0, 0.2f, 0)); objects.push_back(c);
        interactive.push_back({ cap, c.model, "Champignons Bioluminescents" });
    }
}

void App::buildCampfire() {
    // pierres autour du feu
    Mesh* rock = own(Geo::dodecahedron(0.25f, 0));
    for (int i = 0; i < 8; i++) {
        float a = (i / 8.0f) * PI * 2;
        Object o; o.mesh = rock; o.mat = rockMat;
        o.model = glm::translate(mat4(1), vec3(std::cos(a) * 0.8f, 0.1f, std::sin(a) * 0.8f));
        objects.push_back(o);
    }
    // bûches
    Mesh* log = own(Geo::cylinder(0.08f, 0.08f, 1.2f, 6));
    for (int j = 0; j < 3; j++) {
        Object o; o.mesh = log; o.mat = barkMat;
        o.model = glm::translate(mat4(1), vec3(0, 0.2f, 0)) * eulerXYZ(0, (j * PI) / 1.5f, PI / 3);
        objects.push_back(o);
    }
    // flamme filaire émissive
    Object f; f.mesh = own(Geo::cone(0.35f, 0.9f, 8)); f.wire = true; f.wireColor = hexColor(0xff6600);
    f.model = glm::translate(mat4(1), vec3(0, 0.5f, 0));
    objects.push_back(f);
    interactive.push_back({ f.mesh, f.model, "Feu de Camp" });
}

void App::populateForest() {
    for (int i = 0; i < 35; i++) {
        float angle = rng.next() * PI * 2;
        float dist = 8 + rng.next() * 22;
        float x = std::cos(angle) * dist, z = std::sin(angle) * dist;
        float scale = 0.8f + rng.next() * 0.6f;
        if (rng.next() > 0.4f) createPineTree(x, z, scale);
        else                   createOakTree(x, z, scale);
    }
    createMushroomCluster(-6, 5, capCyan);
    createMushroomCluster(5, -4, capPurple);
}

void App::buildParticles() {
    fireflies.create(rng, 150, 35, 0.5f, 6.0f, 35);
    rain.create(rng, 1200, 40, 0.0f, 20.0f, 40);
    rain.visible = false;
}

// ---------------------------------------------------------------------------
//  Cibles de rendu
// ---------------------------------------------------------------------------
void App::createShadowTargets() {
    // Ombre directionnelle (profondeur 2048², comparaison matérielle)
    glGenTextures(1, &sunDepthTex);
    glBindTexture(GL_TEXTURE_2D, sunDepthTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, SUN_SIZE, SUN_SIZE, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float border[4] = { 1, 1, 1, 1 };
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glGenFramebuffers(1, &sunFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, sunFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, sunDepthTex, 0);
    glDrawBuffer(GL_NONE); glReadBuffer(GL_NONE);

    // Ombre omnidirectionnelle du feu (distance linéaire dans un cubemap R32F)
    glGenTextures(1, &cubeTex);
    glBindTexture(GL_TEXTURE_CUBE_MAP, cubeTex);
    for (int i = 0; i < 6; i++)
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_R32F, CUBE_SIZE, CUBE_SIZE, 0, GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glGenRenderbuffers(1, &cubeDepthRb);
    glBindRenderbuffer(GL_RENDERBUFFER, cubeDepthRb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, CUBE_SIZE, CUBE_SIZE);
    glGenFramebuffers(1, &cubeFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, cubeFbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, cubeDepthRb);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void App::createRenderTargets() {
    if (msFbo) {
        glDeleteFramebuffers(1, &msFbo); glDeleteRenderbuffers(1, &msColor); glDeleteRenderbuffers(1, &msDepth);
        glDeleteFramebuffers(1, &resFbo); glDeleteTextures(1, &resTex);
    }
    const int samples = 4;
    glGenRenderbuffers(1, &msColor);
    glBindRenderbuffer(GL_RENDERBUFFER, msColor);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA16F, fbW, fbH);
    glGenRenderbuffers(1, &msDepth);
    glBindRenderbuffer(GL_RENDERBUFFER, msDepth);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, fbW, fbH);
    glGenFramebuffers(1, &msFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, msFbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msColor);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, msDepth);

    glGenTextures(1, &resTex);
    glBindTexture(GL_TEXTURE_2D, resTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, fbW, fbH, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &resFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, resFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, resTex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    bloom.resize(fbW, fbH);
}

void App::resizeBuffers() {
    glfwGetFramebufferSize(window, &fbW, &fbH);
    glfwGetWindowSize(window, &winW, &winH);
    if (fbW < 1 || fbH < 1) return;
    createRenderTargets();
}

// ---------------------------------------------------------------------------
//  Mise à jour
// ---------------------------------------------------------------------------
void App::update(double time, float dt) {
    lights.update((float)time);
    cam.update(dt);

    // vent : rotation douce du feuillage
    if (windActive) {
        for (auto& o : objects) {
            if (!o.animated) continue;
            float w = std::sin((float)time * 2.0f + o.windIdx) * 0.05f;
            o.model = o.base * eulerXYZ(w * 0.5f, 0.0f, w);
        }
    }

    // lucioles
    if (fireflies.visible) {
        float k = dt * 60.0f;                       // les constantes du HTML supposent 60 FPS
        auto& p = fireflies.pos;
        for (size_t i = 0; i < p.size(); i += 3) {
            p[i + 1] += std::sin((float)time + p[i]) * 0.005f * k;
            p[i]     += std::cos((float)time + p[i + 2]) * 0.003f * k;
        }
        fireflies.sync();
    }
    // pluie
    if (rain.visible) {
        auto& p = rain.pos;
        for (size_t j = 1; j < p.size(); j += 3) {
            p[j] -= dt * 25.0f;
            if (p[j] < 0) p[j] = 20.0f;
        }
        rain.sync();
    }
}

// ---------------------------------------------------------------------------
//  Rendu : passes d'ombres
// ---------------------------------------------------------------------------
void App::renderShadows() {
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);                            // ombres via faces arrière (réduit l'acné)

    // ---- soleil / lune : projection orthographique ±30 ----
    mat4 lp = glm::ortho(-30.0f, 30.0f, -30.0f, 30.0f, 0.5f, 100.0f);
    mat4 lv = glm::lookAt(lights.sunPos, vec3(0), vec3(0, 1, 0));
    sunVP = lp * lv;
    glBindFramebuffer(GL_FRAMEBUFFER, sunFbo);
    glViewport(0, 0, SUN_SIZE, SUN_SIZE);
    glClear(GL_DEPTH_BUFFER_BIT);
    depth.use();
    depth.m4("uVP", sunVP);
    for (auto& o : objects) {
        if (!o.castShadow) continue;
        depth.m4("uModel", o.model);
        o.mesh->draw();
    }

    // ---- feu de camp : cubemap de distances ----
    const float farP = lights.pDist[0];
    vec3 lpos = lights.pPos[0];
    mat4 pp = glm::perspective(PI / 2, 1.0f, 0.5f, farP);
    static const vec3 dirs[6] = { {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1} };
    static const vec3 ups[6]  = { {0,-1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}, {0,-1,0}, {0,-1,0} };
    glBindFramebuffer(GL_FRAMEBUFFER, cubeFbo);
    glViewport(0, 0, CUBE_SIZE, CUBE_SIZE);
    cubeDepth.use();
    cubeDepth.f3("uLightPos", lpos); cubeDepth.f1("uFar", farP);
    for (int f = 0; f < 6; f++) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, cubeTex, 0);
        glClearColor(1, 1, 1, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        cubeDepth.m4("uVP", pp * glm::lookAt(lpos, lpos + dirs[f], ups[f]));
        for (auto& o : objects) {
            if (!o.castShadow) continue;
            cubeDepth.m4("uModel", o.model);
            o.mesh->draw();
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ---------------------------------------------------------------------------
//  Rendu : scène principale
// ---------------------------------------------------------------------------
void App::drawPBR(const Object& o) {
    const Material& m = *o.mat;
    pbr.m4("uModel", o.model);
    pbr.m3("uNormalMat", mat3(glm::transpose(glm::inverse(o.model))));
    pbr.f3("uColor", m.color);
    pbr.f3("uEmissive", m.emissive * m.emissiveIntensity);
    pbr.f1("uRough", m.roughness);
    pbr.f1("uMetal", m.metalness);
    pbr.i1("uFlat", m.flat);
    pbr.i1("uReceive", o.receiveShadow);
    pbr.i1("uHasMap", m.map != 0);
    pbr.i1("uHasNormalMap", m.normalMap != 0);
    pbr.f2("uMapRepeat", m.mapRepeat);
    pbr.f2("uNormalRepeat", m.normalRepeat);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, m.map);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, m.normalMap);
    o.mesh->draw();
}

void App::renderScene() {
    glBindFramebuffer(GL_FRAMEBUFFER, msFbo);
    glViewport(0, 0, fbW, fbH);
    glClearColor(lights.bg.r, lights.bg.g, lights.bg.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    float aspect = (float)fbW / (float)fbH;
    proj = glm::perspective(glm::radians(cam.fovDeg), aspect, 0.1f, 100.0f);
    view = cam.view();

    // ---------- objets PBR ----------
    pbr.use();
    pbr.m4("uView", view); pbr.m4("uProj", proj);
    pbr.f3("uCamPos", cam.pos);
    pbr.f3("uHemiSky", lights.hemiSky * lights.hemiIntensity);
    pbr.f3("uHemiGround", lights.hemiGround * lights.hemiIntensity);
    pbr.f3("uSunDir", glm::normalize(lights.sunPos));
    pbr.f3("uSunColor", lights.sunColor * lights.sunIntensity);
    vec3 pc[3];
    for (int i = 0; i < 3; i++) pc[i] = lights.pColor[i] * lights.pInt[i];
    glUniform3fv(pbr.u("uPointPos"), 3, &lights.pPos[0].x);
    glUniform3fv(pbr.u("uPointColor"), 3, &pc[0].x);
    glUniform1fv(pbr.u("uPointDist"), 3, lights.pDist);
    pbr.m4("uSunVP", sunVP);
    pbr.f1("uSunShadowSize", (float)SUN_SIZE);
    pbr.f1("uFireFar", lights.pDist[0]);
    pbr.f3("uFogColor", lights.fogColor); pbr.f1("uFogDensity", fogDensity);
    pbr.i1("uMap", 0); pbr.i1("uNormalMap", 1); pbr.i1("uSunShadow", 2); pbr.i1("uFireShadow", 3);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, sunDepthTex);
    glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_CUBE_MAP, cubeTex);
    for (auto& o : objects) if (!o.wire) drawPBR(o);

    // ---------- flamme filaire (MeshBasicMaterial wireframe) ----------
    basic.use();
    basic.m4("uView", view); basic.m4("uProj", proj);
    basic.f3("uFogColor", lights.fogColor); basic.f1("uFogDensity", fogDensity);
    glDisable(GL_CULL_FACE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    for (auto& o : objects) {
        if (!o.wire) continue;
        basic.m4("uModel", o.model);
        basic.m3("uNormalMat", mat3(1.0f));
        basic.f3("uColor", o.wireColor);
        o.mesh->draw();
    }
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    // ---------- particules ----------
    points.use();
    points.m4("uView", view); points.m4("uProj", proj);
    points.f1("uScale", fbH * 0.5f);
    points.f3("uFogColor", lights.fogColor); points.f1("uFogDensity", fogDensity);
    glEnable(GL_PROGRAM_POINT_SIZE);
    glEnable(GL_BLEND);
    if (fireflies.visible) {                              // lucioles : additif
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        points.f3("uColor", hexColor(0x34d399)); points.f1("uSize", 0.18f); points.f1("uOpacity", 0.8f);
        fireflies.draw();
    }
    if (rain.visible) {                                   // pluie : alpha classique
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        points.f3("uColor", hexColor(0x38bdf8)); points.f1("uSize", 0.08f); points.f1("uOpacity", 0.5f);
        rain.draw();
    }
    glDisable(GL_BLEND);

    // ---------- résolution MSAA -> texture ----------
    glBindFramebuffer(GL_READ_FRAMEBUFFER, msFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resFbo);
    glBlitFramebuffer(0, 0, fbW, fbH, 0, 0, fbW, fbH, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void App::renderPost() {
    glBindVertexArray(emptyVao);
    GLuint bloomTex = bloom.render(resTex);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, fbW, fbH);
    glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_BLEND);
    finalPass.use();
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, resTex);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, bloomTex);
    finalPass.i1("uScene", 0); finalPass.i1("uBloom", 1);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glActiveTexture(GL_TEXTURE0);
}

// ---------------------------------------------------------------------------
//  Interactions
// ---------------------------------------------------------------------------
void App::showToast(const std::string& t) {
    toastText = t;
    toastUntil = glfwGetTime() + 2.5;
    std::printf("[Forêt] %s\n", t.c_str());
    std::fflush(stdout);
}

void App::setTimeOfDay(const std::string& p) {
    currentTime = p;
    lights.setTimeOfDay(p);
    std::string up = p;
    for (auto& c : up) c = (char)std::toupper((unsigned char)c);
    showToast("Atmosphère : " + up);
}

static bool rayTriangle(const vec3& o, const vec3& d, const vec3& a, const vec3& b, const vec3& c, float& t) {
    const float EPS = 1e-7f;
    vec3 e1 = b - a, e2 = c - a, p = glm::cross(d, e2);
    float det = glm::dot(e1, p);
    if (std::fabs(det) < EPS) return false;
    float inv = 1.0f / det;
    vec3 s = o - a;
    float u = glm::dot(s, p) * inv;
    if (u < 0 || u > 1) return false;
    vec3 q = glm::cross(s, e1);
    float v = glm::dot(d, q) * inv;
    if (v < 0 || u + v > 1) return false;
    t = glm::dot(e2, q) * inv;
    return t > 0;
}

void App::pick(double mx, double my) {
    float nx = (float)(mx / winW) * 2 - 1, ny = -((float)(my / winH) * 2 - 1);
    mat4 inv = glm::inverse(proj * view);
    vec4 n4 = inv * vec4(nx, ny, -1, 1), f4 = inv * vec4(nx, ny, 1, 1);
    vec3 ro = vec3(n4) / n4.w, rf = vec3(f4) / f4.w;
    vec3 rd = glm::normalize(rf - ro);

    float best = 1e30f; const Interactive* hit = nullptr;
    for (auto& it : interactive) {
        mat4 im = glm::inverse(it.model);
        vec3 lo = vec3(im * vec4(ro, 1)), ld = glm::normalize(vec3(im * vec4(rd, 0)));
        for (size_t i = 0; i < it.mesh->idx.size(); i += 3) {
            float t;
            const vec3& a = it.mesh->verts[it.mesh->idx[i]].p;
            const vec3& b = it.mesh->verts[it.mesh->idx[i + 1]].p;
            const vec3& c = it.mesh->verts[it.mesh->idx[i + 2]].p;
            // test double face (comme Raycaster sur matériau visible)
            if (rayTriangle(lo, ld, a, b, c, t) || rayTriangle(lo, ld, a, c, b, t)) {
                vec3 wp = vec3(it.model * vec4(lo + ld * t, 1));
                float dist = glm::length(wp - ro);
                if (dist < best) { best = dist; hit = &it; }
            }
        }
    }
    if (hit) showToast("Interaction : " + hit->name);
}

void App::toggleFullscreen() {
    fullscreen = !fullscreen;
    if (fullscreen) {
        glfwGetWindowPos(window, &savedX, &savedY);
        glfwGetWindowSize(window, &savedW, &savedH);
        GLFWmonitor* mon = glfwGetPrimaryMonitor();
        const GLFWvidmode* vm = glfwGetVideoMode(mon);
        glfwSetWindowMonitor(window, mon, 0, 0, vm->width, vm->height, vm->refreshRate);
    } else {
        glfwSetWindowMonitor(window, nullptr, savedX, savedY, savedW, savedH, 0);
    }
}

void App::handleKey(int key) {
    auto clampf = [](float v, float a, float b) { return std::max(a, std::min(b, v)); };
    char buf[96];
    switch (key) {
        case GLFW_KEY_1: setTimeOfDay("day"); break;
        case GLFW_KEY_2: setTimeOfDay("sunset"); break;
        case GLFW_KEY_3: setTimeOfDay("night"); break;
        case GLFW_KEY_4: setTimeOfDay("dawn"); break;
        case GLFW_KEY_5: cam.setPreset("overview");  showToast("Vue Canopée"); break;
        case GLFW_KEY_6: cam.setPreset("trail");     showToast("Sentier"); break;
        case GLFW_KEY_7: cam.setPreset("campfire");  showToast("Feu de camp"); break;
        case GLFW_KEY_8: cam.setPreset("mushrooms"); showToast("Gros plan champignons"); break;
        case GLFW_KEY_W: windActive = !windActive; showToast(windActive ? "Vent : activé" : "Vent : désactivé"); break;
        case GLFW_KEY_L: fireflies.visible = !fireflies.visible; showToast(fireflies.visible ? "Lucioles : activées" : "Lucioles : désactivées"); break;
        case GLFW_KEY_R: rain.visible = !rain.visible; showToast(rain.visible ? "Pluie douce : activée" : "Pluie douce : désactivée"); break;
        case GLFW_KEY_UP:   lights.sunIntensity = clampf(std::round((lights.sunIntensity + 0.1f) * 10) / 10, 0.1f, 2.5f);
            std::snprintf(buf, sizeof buf, "Intensité lumière : %.1f", lights.sunIntensity); showToast(buf); break;
        case GLFW_KEY_DOWN: lights.sunIntensity = clampf(std::round((lights.sunIntensity - 0.1f) * 10) / 10, 0.1f, 2.5f);
            std::snprintf(buf, sizeof buf, "Intensité lumière : %.1f", lights.sunIntensity); showToast(buf); break;
        case GLFW_KEY_RIGHT: bloom.strength = clampf(std::round((bloom.strength + 0.1f) * 10) / 10, 0.0f, 2.5f);
            std::snprintf(buf, sizeof buf, "Bloom : %.1f", bloom.strength); showToast(buf); break;
        case GLFW_KEY_LEFT:  bloom.strength = clampf(std::round((bloom.strength - 0.1f) * 10) / 10, 0.0f, 2.5f);
            std::snprintf(buf, sizeof buf, "Bloom : %.1f", bloom.strength); showToast(buf); break;
        case GLFW_KEY_PAGE_UP:   fogDensity = clampf(std::round((fogDensity + 0.005f) * 1000) / 1000, 0.005f, 0.06f);
            std::snprintf(buf, sizeof buf, "Brouillard : %.3f", fogDensity); showToast(buf); break;
        case GLFW_KEY_PAGE_DOWN: fogDensity = clampf(std::round((fogDensity - 0.005f) * 1000) / 1000, 0.005f, 0.06f);
            std::snprintf(buf, sizeof buf, "Brouillard : %.3f", fogDensity); showToast(buf); break;
        case GLFW_KEY_F11: case GLFW_KEY_F: toggleFullscreen(); break;
        case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose(window, GLFW_TRUE); break;
        default: break;
    }
}

void App::updateTitle() {
    static const char* names[] = { "Jour", "Coucher", "Nuit", "Aube" };
    const char* tn = currentTime == "day" ? names[0] : currentTime == "sunset" ? names[1] : currentTime == "night" ? names[2] : names[3];
    char buf[256];
    if (glfwGetTime() < toastUntil)
        std::snprintf(buf, sizeof buf, "Forêt Enchantée 3D  |  %.0f FPS  |  %s  |  %s", fps, tn, toastText.c_str());
    else
        std::snprintf(buf, sizeof buf, "Forêt Enchantée 3D  |  %.0f FPS  |  %s  |  [H] aide", fps, tn);
    glfwSetWindowTitle(window, buf);
}

// ---------------------------------------------------------------------------
//  Callbacks GLFW
// ---------------------------------------------------------------------------
void App::onResize(GLFWwindow*, int, int) { if (gApp) gApp->resizeBuffers(); }
void App::onKey(GLFWwindow*, int key, int, int action, int) {
    if (!gApp || action == GLFW_RELEASE) return;
    bool slider = key == GLFW_KEY_UP || key == GLFW_KEY_DOWN || key == GLFW_KEY_LEFT || key == GLFW_KEY_RIGHT ||
                  key == GLFW_KEY_PAGE_UP || key == GLFW_KEY_PAGE_DOWN;
    if (action == GLFW_REPEAT && !slider) return;
    if (key == GLFW_KEY_H) {
        std::printf(
            "\n=== FORÊT ENCHANTÉE 3D : commandes ===\n"
            " Souris gauche : orbite   | Souris droite : déplacer | Molette : zoom | Clic : inspecter un objet\n"
            " 1 Jour  2 Coucher  3 Nuit  4 Aube\n"
            " 5 Canopée  6 Sentier  7 Feu de camp  8 Champignons\n"
            " W vent  L lucioles  R pluie\n"
            " Haut/Bas : lumière  |  Gauche/Droite : bloom  |  PgUp/PgDn : brouillard\n"
            " F / F11 : plein écran  |  Echap : quitter\n\n");
        return;
    }
    gApp->handleKey(key);
}
void App::onMouseButton(GLFWwindow* w, int button, int action, int) {
    if (!gApp) return;
    double x, y; glfwGetCursorPos(w, &x, &y);
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        gApp->lmb = (action == GLFW_PRESS);
        if (action == GLFW_PRESS) { gApp->pressX = x; gApp->pressY = y; }
        else if (std::hypot(x - gApp->pressX, y - gApp->pressY) < 5.0) gApp->pick(x, y);
    }
    if (button == GLFW_MOUSE_BUTTON_RIGHT || button == GLFW_MOUSE_BUTTON_MIDDLE) gApp->rmb = (action == GLFW_PRESS);
    gApp->lastX = x; gApp->lastY = y;
}
void App::onCursor(GLFWwindow*, double x, double y) {
    if (!gApp) return;
    float dx = (float)(x - gApp->lastX), dy = (float)(y - gApp->lastY);
    if (gApp->lmb) gApp->cam.rotate(dx, dy, (float)gApp->winH);
    else if (gApp->rmb) gApp->cam.pan(dx, dy, (float)gApp->winH);
    gApp->lastX = x; gApp->lastY = y;
}
void App::onScroll(GLFWwindow*, double, double yoff) { if (gApp) gApp->cam.zoom((float)yoff); }

// ---------------------------------------------------------------------------
//  Capture d'écran (option --screenshot)
// ---------------------------------------------------------------------------
void App::saveScreenshot(const std::string& path) {
    std::vector<unsigned char> px((size_t)fbW * fbH * 3);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, fbW, fbH, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { std::fprintf(stderr, "Impossible d'écrire %s\n", path.c_str()); return; }
    std::fprintf(f, "P6\n%d %d\n255\n", fbW, fbH);
    for (int y = fbH - 1; y >= 0; y--) std::fwrite(&px[(size_t)y * fbW * 3], 1, (size_t)fbW * 3, f);
    std::fclose(f);
    std::printf("Capture enregistrée : %s\n", path.c_str());
}

// ---------------------------------------------------------------------------
//  Point d'entrée de l'application
// ---------------------------------------------------------------------------
int App::run() {
    if (!glfwInit()) { std::fprintf(stderr, "Echec de glfwInit\n"); return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    window = glfwCreateWindow(opt.width, opt.height, "Forêt Enchantée 3D", nullptr, nullptr);
    if (!window) { std::fprintf(stderr, "Impossible de créer la fenêtre OpenGL 3.3\n"); glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    if (!gladLoadGL((GLADloadfunc)glfwGetProcAddress)) { std::fprintf(stderr, "Echec du chargement d'OpenGL (glad)\n"); return 1; }
    std::printf("OpenGL %s | %s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER));

    gApp = this;
    glfwSetFramebufferSizeCallback(window, onResize);
    glfwSetKeyCallback(window, onKey);
    glfwSetMouseButtonCallback(window, onMouseButton);
    glfwSetCursorPosCallback(window, onCursor);
    glfwSetScrollCallback(window, onScroll);

    // shaders
    pbr.build(Shaders::PBR_VS, Shaders::PBR_FS, "pbr");
    basic.build(Shaders::PBR_VS, Shaders::BASIC_FS, "basic");
    points.build(Shaders::POINTS_VS, Shaders::POINTS_FS, "points");
    depth.build(Shaders::DEPTH_VS, Shaders::DEPTH_FS, "depth");
    cubeDepth.build(Shaders::DEPTH_VS, Shaders::CUBE_DEPTH_FS, "cubedepth");
    finalPass.build(Shaders::FULLSCREEN_VS, Shaders::FINAL_FS, "final");
    bloom.initPrograms();
    glGenVertexArrays(1, &emptyVao);

    // scène
    buildMaterials();
    buildTerrain();
    buildCampfire();
    populateForest();
    buildParticles();
    createShadowTargets();
    glfwGetWindowSize(window, &winW, &winH);
    resizeBuffers();

    // état initial demandé en ligne de commande
    if (opt.time != "day") lights.setTimeOfDay(opt.time), currentTime = opt.time;
    if (!opt.preset.empty()) { cam.setPreset(opt.preset); cam.t = 1.0f; }
    if (opt.rain) rain.visible = true;

    std::printf("Forêt générée : %zu objets, %zu maillages. Appuyez sur [H] pour l'aide.\n", objects.size(), meshes.size());
    showToast("Bienvenue dans la forêt");

    double prev = glfwGetTime();
    int frame = 0;
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        double now = glfwGetTime();
        float dt = (float)std::min(now - prev, 0.1);
        prev = now;
        if (!opt.screenshot.empty()) dt = 1.0f / 60.0f;       // rendu déterministe pour les captures

        double simTime = opt.screenshot.empty() ? now : 3.0 + frame / 60.0;
        update(simTime, dt);
        if (fbW > 0 && fbH > 0) {
            renderShadows();
            renderScene();
            renderPost();
        }
        bool shot = !opt.screenshot.empty() && frame + 1 >= opt.frames;
        if (shot) saveScreenshot(opt.screenshot);       // lecture avant le swap
        glfwSwapBuffers(window);

        // FPS
        fpsFrames++;
        if (now - fpsTimer >= 0.5) {
            fps = fpsFrames / (now - fpsTimer);
            fpsFrames = 0; fpsTimer = now;
            updateTitle();
        }
        frame++;
        if (shot) break;
    }
    // libérer les ressources GL tant que le contexte existe
    objects.clear(); interactive.clear(); meshes.clear();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    Options o;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : ""; };
        if (a == "--screenshot") o.screenshot = next();
        else if (a == "--time") o.time = next();
        else if (a == "--preset") o.preset = next();
        else if (a == "--rain") o.rain = true;
        else if (a == "--frames") o.frames = std::atoi(next().c_str());
        else if (a == "--seed") o.seed = (uint32_t)std::strtoul(next().c_str(), nullptr, 10);
        else if (a == "--size") { std::string s = next(); std::sscanf(s.c_str(), "%dx%d", &o.width, &o.height); }
        else if (a == "--help") {
            std::printf("Usage: enchanted_forest [--size LxH] [--time day|sunset|night|dawn]\n"
                        "       [--preset overview|trail|campfire|mushrooms] [--rain] [--seed N]\n"
                        "       [--screenshot fichier.ppm [--frames N]]\n");
            return 0;
        }
    }
    App app(o);
    return app.run();
}
