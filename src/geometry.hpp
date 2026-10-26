// ============================================================================
//  geometry.hpp - Générateurs de géométries 
//  CylinderGeometry, ConeGeometry, DodecahedronGeometry, SphereGeometry (calotte),
//  PlaneGeometry (terrain ondulé).
// ============================================================================
#pragma once
#include <glad/gl.h>
#include <glm/glm.hpp>
#include <cmath>
#include <utility>
#include <vector>

struct Vertex {
    glm::vec3 p;
    glm::vec3 n;
    glm::vec2 uv;
};

struct Mesh {
    std::vector<Vertex>   verts;
    std::vector<unsigned> idx;
    GLuint vao = 0, vbo = 0, ebo = 0;
    GLsizei count = 0;

    void upload() {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glGenBuffers(1, &ebo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(Vertex), verts.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned), idx.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, p));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, n));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, uv));
        glBindVertexArray(0);
        count = (GLsizei)idx.size();
    }
    void draw() const {
        glBindVertexArray(vao);
        glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, nullptr);
    }
    ~Mesh() {
        if (vao) glDeleteVertexArrays(1, &vao);
        if (vbo) glDeleteBuffers(1, &vbo);
        if (ebo) glDeleteBuffers(1, &ebo);
    }
};

namespace Geo {

constexpr float PI = 3.14159265358979323846f;

// ---------------------------------------------------------------------------
// CylinderGeometry(radiusTop, radiusBottom, height, radialSegments, 1, false)
// Centré sur l'origine, axe Y. ConeGeometry(r,h,s) = Cylinder(0, r, h, s).
// ---------------------------------------------------------------------------
inline Mesh* cylinder(float rt, float rb, float height, int seg) {
    Mesh* m = new Mesh();
    const float half = height * 0.5f;
    const int hs = 1;
    unsigned index = 0;
    std::vector<std::vector<unsigned>> grid;

    const float slope = (rb - rt) / height;
    for (int y = 0; y <= hs; y++) {
        std::vector<unsigned> row;
        float v = (float)y / hs;
        float radius = v * (rb - rt) + rt;
        for (int x = 0; x <= seg; x++) {
            float u = (float)x / seg;
            float theta = u * 2.0f * PI;
            float s = std::sin(theta), c = std::cos(theta);
            Vertex vt;
            vt.p = { radius * s, -v * height + half, radius * c };
            vt.n = glm::normalize(glm::vec3(s, slope, c));
            vt.uv = { u, 1.0f - v };
            m->verts.push_back(vt);
            row.push_back(index++);
        }
        grid.push_back(row);
    }
    for (int x = 0; x < seg; x++) {
        for (int y = 0; y < hs; y++) {
            unsigned a = grid[y][x], b = grid[y + 1][x], c = grid[y + 1][x + 1], d = grid[y][x + 1];
            if (rt > 0 || y != 0)      { m->idx.push_back(a); m->idx.push_back(b); m->idx.push_back(d); }
            if (rb > 0 || y != hs - 1) { m->idx.push_back(b); m->idx.push_back(c); m->idx.push_back(d); }
        }
    }
    auto cap = [&](bool top) {
        float radius = top ? rt : rb;
        float sign = top ? 1.0f : -1.0f;
        unsigned centerStart = index;
        for (int x = 1; x <= seg; x++) {
            Vertex vt; vt.p = { 0, half * sign, 0 }; vt.n = { 0, sign, 0 }; vt.uv = { 0.5f, 0.5f };
            m->verts.push_back(vt); index++;
        }
        unsigned centerEnd = index;
        for (int x = 0; x <= seg; x++) {
            float u = (float)x / seg;
            float theta = u * 2.0f * PI;
            float s = std::sin(theta), c = std::cos(theta);
            Vertex vt;
            vt.p = { radius * s, half * sign, radius * c };
            vt.n = { 0, sign, 0 };
            vt.uv = { c * 0.5f + 0.5f, s * 0.5f * sign + 0.5f };
            m->verts.push_back(vt); index++;
        }
        for (int x = 0; x < seg; x++) {
            unsigned c = centerStart + x, i = centerEnd + x;
            if (top) { m->idx.push_back(i);     m->idx.push_back(i + 1); m->idx.push_back(c); }
            else     { m->idx.push_back(i + 1); m->idx.push_back(i);     m->idx.push_back(c); }
        }
    };
    if (rt > 0) cap(true);
    if (rb > 0) cap(false);
    m->upload();
    return m;
}

inline Mesh* cone(float r, float h, int seg) { return cylinder(0.0f, r, h, seg); }

// ---------------------------------------------------------------------------
// DodecahedronGeometry(radius, detail) - PolyhedronGeometry de Three.js.
// Normales de faces (rendu facetté pour les feuillages/rochers).
// ---------------------------------------------------------------------------
inline Mesh* dodecahedron(float radius, int detail) {
    const float t = (1.0f + std::sqrt(5.0f)) / 2.0f;
    const float r = 1.0f / t;
    const float V[] = {
        -1,-1,-1,  -1,-1, 1,  -1, 1,-1,  -1, 1, 1,
         1,-1,-1,   1,-1, 1,   1, 1,-1,   1, 1, 1,
         0,-r,-t,   0,-r, t,   0, r,-t,   0, r, t,
        -r,-t, 0,  -r, t, 0,   r,-t, 0,   r, t, 0,
        -t, 0,-r,   t, 0,-r,  -t, 0, r,   t, 0, r };
    const int I[] = {
        3,11,7,  3,7,15,  3,15,13,   7,19,17,  7,17,6,  7,6,15,
        17,4,8,  17,8,10, 17,10,6,   8,0,16,   8,16,2,  8,2,10,
        0,12,1,  0,1,18,  0,18,16,   6,10,2,   6,2,13,  6,13,15,
        2,16,18, 2,18,3,  2,3,13,    18,1,9,   18,9,11, 18,11,3,
        4,14,12, 4,12,0,  4,0,8,     11,9,5,   11,5,19, 11,19,7,
        19,5,14, 19,14,4, 19,4,17,   1,12,14,  1,14,5,  1,5,9 };

    Mesh* m = new Mesh();
    const int cols = detail + 1;
    auto vtx = [&](int i) { return glm::vec3(V[i * 3], V[i * 3 + 1], V[i * 3 + 2]); };

    for (int f = 0; f < 36; f++) {
        glm::vec3 a = vtx(I[f * 3]), b = vtx(I[f * 3 + 1]), c = vtx(I[f * 3 + 2]);
        std::vector<std::vector<glm::vec3>> v(cols + 1);
        for (int i = 0; i <= cols; i++) {
            glm::vec3 aj = glm::mix(a, c, (float)i / cols);
            glm::vec3 bj = glm::mix(b, c, (float)i / cols);
            int rows = cols - i;
            for (int j = 0; j <= rows; j++) {
                if (j == 0 && i == cols) v[i].push_back(aj);
                else v[i].push_back(glm::mix(aj, bj, (float)j / rows));
            }
        }
        auto pushTri = [&](glm::vec3 p0, glm::vec3 p1, glm::vec3 p2) {
            p0 = glm::normalize(p0) * radius; p1 = glm::normalize(p1) * radius; p2 = glm::normalize(p2) * radius;
            glm::vec3 n = glm::normalize(glm::cross(p1 - p0, p2 - p0));
            if (glm::dot(n, p0 + p1 + p2) < 0.0f) { std::swap(p1, p2); n = -n; }   // garantit des faces CCW vers l'extérieur
            unsigned base = (unsigned)m->verts.size();
            m->verts.push_back({ p0, n, {0, 0} });
            m->verts.push_back({ p1, n, {1, 0} });
            m->verts.push_back({ p2, n, {0, 1} });
            m->idx.push_back(base); m->idx.push_back(base + 1); m->idx.push_back(base + 2);
        };
        for (int i = 0; i < cols; i++) {
            for (int j = 0; j < 2 * (cols - i) - 1; j++) {
                int k = j / 2;
                if (j % 2 == 0) pushTri(v[i][k + 1], v[i + 1][k], v[i][k]);
                else            pushTri(v[i][k + 1], v[i + 1][k + 1], v[i + 1][k]);
            }
        }
    }
    m->upload();
    return m;
}

// ---------------------------------------------------------------------------
// SphereGeometry(radius, wSeg, hSeg, 0, 2PI, 0, PI/2) - calotte (chapeau de champignon)
// ---------------------------------------------------------------------------
inline Mesh* hemisphere(float radius, int ws, int hs) {
    Mesh* m = new Mesh();
    const float thetaLength = PI / 2.0f;
    std::vector<std::vector<unsigned>> grid;
    unsigned index = 0;
    for (int iy = 0; iy <= hs; iy++) {
        std::vector<unsigned> row;
        float v = (float)iy / hs;
        float uOff = (iy == 0) ? 0.5f / ws : 0.0f;
        for (int ix = 0; ix <= ws; ix++) {
            float u = (float)ix / ws;
            Vertex vt;
            vt.p = { -radius * std::cos(u * 2 * PI) * std::sin(v * thetaLength),
                      radius * std::cos(v * thetaLength),
                      radius * std::sin(u * 2 * PI) * std::sin(v * thetaLength) };
            vt.n = glm::normalize(vt.p);
            vt.uv = { u + uOff, 1.0f - v };
            m->verts.push_back(vt);
            row.push_back(index++);
        }
        grid.push_back(row);
    }
    for (int iy = 0; iy < hs; iy++) {
        for (int ix = 0; ix < ws; ix++) {
            unsigned a = grid[iy][ix + 1], b = grid[iy][ix], c = grid[iy + 1][ix], d = grid[iy + 1][ix + 1];
            if (iy != 0) { m->idx.push_back(a); m->idx.push_back(b); m->idx.push_back(d); }
            m->idx.push_back(b); m->idx.push_back(c); m->idx.push_back(d);
        }
    }
    m->upload();
    return m;
}

// ---------------------------------------------------------------------------
// Terrain : PlaneGeometry(80,80,64,64) déformé puis tourné de -PI/2 autour de X,
// positionné à y = -0.2 (identique au HTML).
// ---------------------------------------------------------------------------
inline Mesh* terrain(float size, int segs, float yOffset) {
    Mesh* m = new Mesh();
    const int gx1 = segs + 1;
    const float segW = size / segs;
    for (int iy = 0; iy <= segs; iy++) {
        float yPl = -(iy * segW - size * 0.5f);          // Plane: y = -(iy*seg - h/2)
        for (int ix = 0; ix <= segs; ix++) {
            float xPl = ix * segW - size * 0.5f;
            float h = std::sin(xPl * 0.1f) * std::cos(yPl * 0.1f) * 0.8f + std::sin(xPl * 0.05f) * 1.2f;
            Vertex vt;
            // rotation.x = -PI/2 : (x, y, z) -> (x, z, -y)
            vt.p = { xPl, h + yOffset, -yPl };
            vt.n = { 0, 1, 0 };
            vt.uv = { (float)ix / segs, 1.0f - (float)iy / segs };
            m->verts.push_back(vt);
        }
    }
    for (int iy = 0; iy < segs; iy++) {
        for (int ix = 0; ix < segs; ix++) {
            unsigned a = ix + gx1 * iy, b = ix + gx1 * (iy + 1), c = (ix + 1) + gx1 * (iy + 1), d = (ix + 1) + gx1 * iy;
            m->idx.push_back(a); m->idx.push_back(b); m->idx.push_back(d);
            m->idx.push_back(b); m->idx.push_back(c); m->idx.push_back(d);
        }
    }
    // computeVertexNormals()
    for (auto& v : m->verts) v.n = glm::vec3(0);
    for (size_t i = 0; i < m->idx.size(); i += 3) {
        Vertex &A = m->verts[m->idx[i]], &B = m->verts[m->idx[i + 1]], &C = m->verts[m->idx[i + 2]];
        glm::vec3 n = glm::cross(B.p - A.p, C.p - A.p);
        A.n += n; B.n += n; C.n += n;
    }
    for (auto& v : m->verts) v.n = glm::normalize(v.n);
    m->upload();
    return m;
}

} // namespace Geo
