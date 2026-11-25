// OpenGL renderer: texture array, chunk passes, sky, and batched dynamic geometry.
#pragma once
#include <vector>
#include <string>
#include <unordered_map>
#include "../core/gl.h"
#include "../core/math.h"

class World;
struct Chunk;
struct MeshResult;

struct Camera {
    vec3 pos;
    float yaw = 0, pitch = 0, fov = 70, aspect = 1.6f, nearZ = 0.05f, farZ = 600;
    vec3 forward, right, up;
    mat4 view, proj, viewProj;  // view is rotation-only: geometry is sent camera-relative
    Frustum frustum;
    void update(int w, int h);
};

// Lighting / fog / sky parameters for a frame
struct FrameEnv {
    vec3 fogColor = {0.6f, 0.75f, 1.0f};
    float fogStart = 100, fogEnd = 160;
    float daylight = 1.0f;
    vec3 skyTint = {1, 1, 1};
    vec3 zenith = {0.3f, 0.5f, 0.9f}, horizon = {0.7f, 0.82f, 1.0f}, sunset = {1, 0.5f, 0.3f};
    float sunsetAmt = 0, stars = 0, moonPhase = 0.5f;
    vec3 sunDir = {0, 1, 0};
    float time = 0;
};

struct Shader {
    GLuint prog = 0;
    std::unordered_map<std::string, GLint> locs;
    bool build(const char* vs, const char* fs, const char* name);
    GLint loc(const char* n);
    void use() const { glUseProgram(prog); }
    void set(const char* n, float v) { glUniform1f(loc(n), v); }
    void set(const char* n, int v) { glUniform1i(loc(n), v); }
    void set(const char* n, vec3 v) { glUniform3f(loc(n), v.x, v.y, v.z); }
    void set(const char* n, const mat4& m) { glUniformMatrix4fv(loc(n), 1, GL_FALSE, m.m); }
};

struct GVert {
    float x, y, z;
    float u, v, layer;
    uint8_t r, g, b, a;
};

// CPU-side batch of dynamic triangles (positions are camera-relative)
struct GeoBatch {
    std::vector<GVert> v;
    void clear() { v.clear(); }
    void vert(vec3 p, float u, float vv, float layer, uint32_t rgba);
    // quad corners in CCW order; uv rect in 0..1
    void quad(vec3 a, vec3 b, vec3 c, vec3 d, float u0, float v0, float u1, float v1, float layer, uint32_t rgba);
    // Box from local min/max transformed by m; tex per face (+X,-X,+Y,-Y,+Z,-Z); light multiplies colour
    void box(const mat4& m, vec3 mn, vec3 mx, const uint16_t tex[6], float light, uint32_t tint = 0xFFFFFFFF, bool shade = true,
             const float* uvRects = nullptr);
    void billboard(vec3 c, float size, vec3 right, vec3 up, float layer, float u0, float v0, float u1, float v1, uint32_t rgba);
};
inline uint32_t rgba(float r, float g, float b, float a = 1) {
    auto c = [](float v) { return (uint32_t)(clampf(v, 0, 1) * 255.f + 0.5f); };
    return c(r) | (c(g) << 8) | (c(b) << 16) | (c(a) << 24);
}

class Renderer {
public:
    GLuint texArray = 0, fontTex = 0;
    uint8_t glyphW[128];
    Shader chunkSh, genericSh, skySh, uiSh;
    int drawnSections = 0;
    bool init();
    void shutdown();
    void drawSky(const Camera& cam, const FrameEnv& env);
    void drawWorld(World& w, const Camera& cam, const FrameEnv& env, int layer);
    enum DrawMode { DRAW_OPAQUE, DRAW_CUTOUT, DRAW_BLEND, DRAW_LINES, DRAW_ADDITIVE };
    void drawBatch(GeoBatch& b, const mat4& viewProj, const FrameEnv& env, DrawMode mode, bool depthTest = true, bool depthWrite = true);
    static void uploadMesh(Chunk* c, int sy, MeshResult& r);
    static void freeChunkGPU(Chunk* c);
    bool exportTexturesPNG(const std::string& path);
private:
    GLuint skyVAO = 0, genVAO = 0, genVBO = 0;
    size_t genCap = 0;
    std::vector<uint8_t> texPixels;
};

extern GLuint g_quadEBO;
bool writePNG(const std::string& path, int w, int h, const uint8_t* rgba);
