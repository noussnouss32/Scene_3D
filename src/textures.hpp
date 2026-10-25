// ============================================================================
//  textures.hpp - Textures procédurales (équivalent du TextureGenerator JS)
//  Écorce, normal-map d'écorce et sol moussu, dessinés sur un "canvas" CPU.
//  NB : comme dans le HTML, les textures sont lues comme des données LINÉAIRES
//  (CanvasTexture sans colorSpace sRGB) -> on les envoie en GL_RGBA8 simple.
// ============================================================================
#pragma once
#include <glad/gl.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <random>
#include <vector>

class Rng {
public:
    explicit Rng(uint32_t seed = 1337) : eng(seed), dist(0.0f, 1.0f) {}
    float next() { return dist(eng); }                       // Math.random()
    float range(float a, float b) { return a + (b - a) * next(); }
private:
    std::mt19937 eng;
    std::uniform_real_distribution<float> dist;
};

struct Canvas {
    int w, h;
    std::vector<uint8_t> px;   // RGBA
    Canvas(int W, int H) : w(W), h(H), px((size_t)W * H * 4, 255) {}

    static glm::ivec3 hex(uint32_t c) { return { (c >> 16) & 255, (c >> 8) & 255, c & 255 }; }

    void fillRect(float fx, float fy, float fw, float fh, uint32_t col) {
        glm::ivec3 c = hex(col);
        int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
        int x1 = (int)std::ceil(fx + fw), y1 = (int)std::ceil(fy + fh);
        for (int y = std::max(y0, 0); y < std::min(y1, h); y++)
            for (int x = std::max(x0, 0); x < std::min(x1, w); x++) set(x, y, c);
    }
    void fillCircle(float cx, float cy, float r, uint32_t col) {
        glm::ivec3 c = hex(col);
        int x0 = (int)std::floor(cx - r), x1 = (int)std::ceil(cx + r);
        int y0 = (int)std::floor(cy - r), y1 = (int)std::ceil(cy + r);
        for (int y = std::max(y0, 0); y < std::min(y1, h); y++)
            for (int x = std::max(x0, 0); x < std::min(x1, w); x++) {
                float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
                if (dx * dx + dy * dy <= r * r) set(x, y, c);
            }
    }
    void set(int x, int y, const glm::ivec3& c) {
        size_t i = ((size_t)y * w + x) * 4;
        px[i] = (uint8_t)c.x; px[i + 1] = (uint8_t)c.y; px[i + 2] = (uint8_t)c.z; px[i + 3] = 255;
    }
    GLuint toTexture() const {
        GLuint t;
        glGenTextures(1, &t);
        glBindTexture(GL_TEXTURE_2D, t);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        return t;
    }
};

struct BarkTextures { GLuint albedo = 0, normal = 0; };

namespace TextureGen {

inline BarkTextures createBark(Rng& rng) {
    BarkTextures out;
    {   // ---- Albedo 512x512 ----
        Canvas c(512, 512);
        c.fillRect(0, 0, 512, 512, 0x2c1d11);                        // fond d'écorce sombre
        for (int i = 0; i < 300; i++)                                 // stries de bois
            c.fillRect(rng.next() * 512, 0, rng.next() * 12 + 4, 512, 0x1a1008);
        for (int j = 0; j < 5000; j++)                                // bruit + mousse
            c.fillRect(rng.next() * 512, rng.next() * 512, rng.next() * 4 + 1, rng.next() * 8 + 1,
                       rng.next() > 0.8f ? 0x3a4f28 : 0x1f130b);
        out.albedo = c.toTexture();
    }
    {   // ---- Normal map 256x256 ----
        Canvas c(256, 256);
        c.fillRect(0, 0, 256, 256, 0x8080ff);
        for (int k = 0; k < 400; k++)
            c.fillRect(rng.next() * 256, 0, rng.next() * 6 + 2, 256, rng.next() > 0.5f ? 0xa080ff : 0x6080ff);
        out.normal = c.toTexture();
    }
    return out;
}

inline GLuint createMossyGround(Rng& rng) {
    Canvas c(512, 512);
    c.fillRect(0, 0, 512, 512, 0x1b1712);                            // terre humide
    for (int i = 0; i < 2000; i++) {                                  // taches de mousse
        float x = rng.next() * 512, y = rng.next() * 512, r = rng.next() * 20 + 5;
        c.fillCircle(x, y, r, rng.next() > 0.3f ? 0x233818 : 0x14240e);
    }
    for (int j = 0; j < 300; j++)                                     // feuilles mortes
        c.fillRect(rng.next() * 512, rng.next() * 512, rng.next() * 6 + 2, rng.next() * 4 + 2, 0x523117);
    return c.toTexture();
}

} // namespace TextureGen
