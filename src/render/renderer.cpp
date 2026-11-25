#include "renderer.h"
#include "textures.h"
#include "mesher.h"
#include "../world/world.h"
#include <cstdio>
#include <algorithm>
#include <zlib.h>

extern "C" const char sh_chunk_vert[], sh_chunk_frag[], sh_generic_vert[], sh_generic_frag[];
extern "C" const char sh_sky_vert[], sh_sky_frag[], sh_ui_vert[], sh_ui_frag[];

GLuint g_quadEBO = 0;
static const int MAX_QUADS = 98304;

// ---------- camera ----------
void Camera::update(int w, int h) {
    aspect = h > 0 ? (float)w / (float)h : 1.0f;
    float cp = std::cos(pitch), sp = std::sin(pitch);
    forward = vec3(-std::sin(yaw) * cp, sp, -std::cos(yaw) * cp);
    right = normalize(cross(forward, vec3(0, 1, 0)));
    up = cross(right, forward);
    view = mat4::lookAt(vec3(0, 0, 0), forward, vec3(0, 1, 0));
    proj = mat4::perspective(fov * PI / 180.f, aspect, nearZ, farZ);
    viewProj = proj * view;
    frustum.build(viewProj);
}

// ---------- shaders ----------
static GLuint compile(GLenum type, const char* src, const char* name) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048]; glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        fprintf(stderr, "Shader %s failed:\n%s\n", name, log);
        return 0;
    }
    return s;
}
bool Shader::build(const char* vs, const char* fs, const char* name) {
    GLuint v = compile(GL_VERTEX_SHADER, vs, name), f = compile(GL_FRAGMENT_SHADER, fs, name);
    if (!v || !f) return false;
    prog = glCreateProgram();
    glAttachShader(prog, v); glAttachShader(prog, f);
    glLinkProgram(prog);
    glDeleteShader(v); glDeleteShader(f);
    GLint ok = 0; glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) { char log[2048]; glGetProgramInfoLog(prog, sizeof(log), nullptr, log); fprintf(stderr, "Link %s failed:\n%s\n", name, log); return false; }
    return true;
}
GLint Shader::loc(const char* n) {
    auto it = locs.find(n);
    if (it != locs.end()) return it->second;
    GLint l = glGetUniformLocation(prog, n);
    locs[n] = l;
    return l;
}

// ---------- batches ----------
void GeoBatch::vert(vec3 p, float u, float vv, float layer, uint32_t c) {
    GVert g; g.x = p.x; g.y = p.y; g.z = p.z; g.u = u; g.v = vv; g.layer = layer;
    g.r = c & 255; g.g = (c >> 8) & 255; g.b = (c >> 16) & 255; g.a = (c >> 24) & 255;
    v.push_back(g);
}
void GeoBatch::quad(vec3 a, vec3 b, vec3 c, vec3 d, float u0, float v0, float u1, float v1, float layer, uint32_t col) {
    vert(a, u0, v1, layer, col); vert(b, u1, v1, layer, col); vert(c, u1, v0, layer, col);
    vert(a, u0, v1, layer, col); vert(c, u1, v0, layer, col); vert(d, u0, v0, layer, col);
}
void GeoBatch::box(const mat4& m, vec3 mn, vec3 mx, const uint16_t tex[6], float light, uint32_t tint, bool shade, const float* uvRects) {
    static const int FC[6][4][3] = {
        {{1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}}, {{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}},
        {{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}}, {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}},
        {{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}, {{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}}};
    static const float SH[6] = {0.62f, 0.62f, 1.0f, 0.5f, 0.82f, 0.82f};
    float tr = (tint & 255) / 255.f, tg = ((tint >> 8) & 255) / 255.f, tb = ((tint >> 16) & 255) / 255.f, ta = ((tint >> 24) & 255) / 255.f;
    for (int f = 0; f < 6; f++) {
        if (tex[f] == 0xFFFF) continue;
        vec3 p[4];
        for (int c = 0; c < 4; c++) p[c] = m.transformPoint(vec3(FC[f][c][0] ? mx.x : mn.x, FC[f][c][1] ? mx.y : mn.y, FC[f][c][2] ? mx.z : mn.z));
        float s = (shade ? SH[f] : 1.0f) * light;
        uint32_t col = rgba(tr * s, tg * s, tb * s, ta);
        if (uvRects) quad(p[0], p[1], p[2], p[3], uvRects[f * 4], uvRects[f * 4 + 1], uvRects[f * 4 + 2], uvRects[f * 4 + 3], tex[f], col);
        else quad(p[0], p[1], p[2], p[3], 0, 0, 1, 1, tex[f], col);
    }
}
void GeoBatch::billboard(vec3 c, float size, vec3 r, vec3 u, float layer, float u0, float v0, float u1, float v1, uint32_t col) {
    vec3 hr = r * (size * 0.5f), hu = u * (size * 0.5f);
    quad(c - hr - hu, c + hr - hu, c + hr + hu, c - hr + hu, u0, v0, u1, v1, layer, col);
}

// ---------- init ----------
bool Renderer::init() {
    if (!chunkSh.build(sh_chunk_vert, sh_chunk_frag, "chunk")) return false;
    if (!genericSh.build(sh_generic_vert, sh_generic_frag, "generic")) return false;
    if (!skySh.build(sh_sky_vert, sh_sky_frag, "sky")) return false;
    if (!uiSh.build(sh_ui_vert, sh_ui_frag, "ui")) return false;

    // Texture array with mipmaps (nearest filtering keeps pixels crisp)
    generateTextures(texPixels);
    glGenTextures(1, &texArray);
    glBindTexture(GL_TEXTURE_2D_ARRAY, texArray);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, 16, 16, TX_COUNT, 0, GL_RGBA, GL_UNSIGNED_BYTE, texPixels.data());
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, 4);
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);

    std::vector<uint8_t> font; int fw, fh;
    generateFont(font, fw, fh, glyphW);
    glGenTextures(1, &fontTex);
    glBindTexture(GL_TEXTURE_2D, fontTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, fw, fh, 0, GL_RED, GL_UNSIGNED_BYTE, font.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // shared index buffer for chunk quads (0,1,2, 0,2,3)
    std::vector<uint32_t> idx((size_t)MAX_QUADS * 6);
    for (uint32_t q = 0; q < (uint32_t)MAX_QUADS; q++) {
        uint32_t b = q * 4;
        idx[q * 6 + 0] = b; idx[q * 6 + 1] = b + 1; idx[q * 6 + 2] = b + 2;
        idx[q * 6 + 3] = b; idx[q * 6 + 4] = b + 2; idx[q * 6 + 5] = b + 3;
    }
    glGenBuffers(1, &g_quadEBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_quadEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * 4, idx.data(), GL_STATIC_DRAW);

    glGenVertexArrays(1, &skyVAO);
    glGenVertexArrays(1, &genVAO);
    glGenBuffers(1, &genVBO);
    glBindVertexArray(genVAO);
    glBindBuffer(GL_ARRAY_BUFFER, genVBO);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GVert), (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(GVert), (void*)12);
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(GVert), (void*)24);
    glBindVertexArray(0);

    chunkSh.use(); chunkSh.set("uTex", 0);
    genericSh.use(); genericSh.set("uTex", 0);
    uiSh.use(); uiSh.set("uTex", 0); uiSh.set("uFont", 1);
    return true;
}

void Renderer::shutdown() {
    if (texArray) glDeleteTextures(1, &texArray);
    if (fontTex) glDeleteTextures(1, &fontTex);
}

// ---------- chunk GPU data ----------
void Renderer::uploadMesh(Chunk* c, int sy, MeshResult& r) {
    for (int l = 0; l < 3; l++) {
        auto& v = r.verts[l];
        int quads = (int)(v.size() / 8);
        if (quads > MAX_QUADS) quads = MAX_QUADS;
        c->quads[sy][l] = quads;
        if (quads == 0) continue;
        if (!c->vao[sy][l]) {
            glGenVertexArrays(1, &c->vao[sy][l]);
            glGenBuffers(1, &c->vbo[sy][l]);
            glBindVertexArray(c->vao[sy][l]);
            glBindBuffer(GL_ARRAY_BUFFER, c->vbo[sy][l]);
            glEnableVertexAttribArray(0);
            glVertexAttribIPointer(0, 2, GL_UNSIGNED_INT, 8, (void*)0);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_quadEBO);
            glBindVertexArray(0);
        }
        glBindBuffer(GL_ARRAY_BUFFER, c->vbo[sy][l]);
        size_t bytes = (size_t)quads * 32;
        if ((int)bytes > c->vboCap[sy][l]) { glBufferData(GL_ARRAY_BUFFER, bytes, v.data(), GL_DYNAMIC_DRAW); c->vboCap[sy][l] = (int)bytes; }
        else glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, v.data());
    }
    c->sectionEmpty[sy] = c->quads[sy][0] == 0 && c->quads[sy][1] == 0 && c->quads[sy][2] == 0;
}

void Renderer::freeChunkGPU(Chunk* c) {
    for (int s = 0; s < SECTIONS; s++)
        for (int l = 0; l < 3; l++) {
            if (c->vao[s][l]) glDeleteVertexArrays(1, &c->vao[s][l]);
            if (c->vbo[s][l]) glDeleteBuffers(1, &c->vbo[s][l]);
            c->vao[s][l] = c->vbo[s][l] = 0;
        }
}

// ---------- drawing ----------
void Renderer::drawSky(const Camera& cam, const FrameEnv& env) {
    glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE); glDisable(GL_BLEND);
    skySh.use();
    // inverse of (proj * rotation view): view is orthonormal so use transpose; proj inverse built by hand
    mat4 invView = cam.view;
    for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) invView.at(r, c) = cam.view.at(c, r);
    mat4 invProj;
    for (float& v : invProj.m) v = 0;
    const mat4& p = cam.proj;
    invProj.at(0, 0) = 1.0f / p.at(0, 0); invProj.at(1, 1) = 1.0f / p.at(1, 1);
    invProj.at(2, 3) = -1.0f; invProj.at(3, 2) = 1.0f / p.at(2, 3); invProj.at(3, 3) = p.at(2, 2) / p.at(2, 3);
    skySh.set("uInvViewProj", invView * invProj);
    skySh.set("uSunDir", env.sunDir);
    skySh.set("uZenith", env.zenith); skySh.set("uHorizon", env.horizon); skySh.set("uSunset", env.sunset);
    skySh.set("uSunsetAmt", env.sunsetAmt); skySh.set("uStars", env.stars); skySh.set("uMoonPhase", env.moonPhase);
    glBindVertexArray(skyVAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDepthMask(GL_TRUE);
}

void Renderer::drawWorld(World& w, const Camera& cam, const FrameEnv& env, int layer) {
    chunkSh.use();
    chunkSh.set("uViewProj", cam.viewProj);
    chunkSh.set("uCamPos", cam.pos);
    chunkSh.set("uTime", env.time);
    chunkSh.set("uDaylight", env.daylight);
    chunkSh.set("uSkyTint", env.skyTint);
    chunkSh.set("uFogColor", env.fogColor);
    chunkSh.set("uFogStart", env.fogStart);
    chunkSh.set("uFogEnd", env.fogEnd);
    chunkSh.set("uAlphaTest", layer == LAYER_WATER ? 0.01f : 0.5f);
    chunkSh.set("uAlphaMul", 1.0f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, texArray);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    if (layer == LAYER_OPAQUE) { glEnable(GL_CULL_FACE); glCullFace(GL_BACK); glDisable(GL_BLEND); glDepthMask(GL_TRUE); }
    else if (layer == LAYER_CUTOUT) { glDisable(GL_CULL_FACE); glDisable(GL_BLEND); glDepthMask(GL_TRUE); }
    else { glDisable(GL_CULL_FACE); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE); }

    struct Item { float d; Chunk* c; int s; };
    std::vector<Item> list;
    list.reserve(2048);
    float maxDist = (w.renderDistance + 0.5f) * 16.f;
    for (auto& kv : w.chunks) {
        Chunk* c = kv.second;
        float ox = c->cx * 16.f - cam.pos.x, oz = c->cz * 16.f - cam.pos.z;
        float dx = ox + 8, dz = oz + 8;
        if (dx * dx + dz * dz > (maxDist + 12) * (maxDist + 12)) continue;
        for (int s = 0; s < SECTIONS; s++) {
            if (c->quads[s][layer] == 0) continue;
            float oy = s * 16.f - cam.pos.y;
            if (!cam.frustum.boxVisible(vec3(ox, oy, oz), vec3(ox + 16, oy + 16, oz + 16))) continue;
            float dy = oy + 8;
            list.push_back({dx * dx + dy * dy + dz * dz, c, s});
        }
    }
    if (layer == LAYER_WATER) std::sort(list.begin(), list.end(), [](const Item& a, const Item& b) { return a.d > b.d; });
    else std::sort(list.begin(), list.end(), [](const Item& a, const Item& b) { return a.d < b.d; });
    GLint offLoc = chunkSh.loc("uOffset");
    for (auto& it : list) {
        Chunk* c = it.c;
        glUniform3f(offLoc, c->cx * 16.f - cam.pos.x, it.s * 16.f - cam.pos.y, c->cz * 16.f - cam.pos.z);
        glBindVertexArray(c->vao[it.s][layer]);
        glDrawElements(GL_TRIANGLES, c->quads[it.s][layer] * 6, GL_UNSIGNED_INT, nullptr);
    }
    if (layer == LAYER_OPAQUE) drawnSections = (int)list.size(); else drawnSections += (int)list.size();
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
}

void Renderer::drawBatch(GeoBatch& b, const mat4& viewProj, const FrameEnv& env, DrawMode mode, bool depthTest, bool depthWrite) {
    if (b.v.empty()) return;
    genericSh.use();
    genericSh.set("uViewProj", viewProj);
    genericSh.set("uFogColor", env.fogColor);
    genericSh.set("uFogStart", env.fogStart);
    genericSh.set("uFogEnd", env.fogEnd);
    genericSh.set("uAlphaTest", mode == DRAW_CUTOUT || mode == DRAW_OPAQUE ? 0.5f : 0.01f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, texArray);
    if (depthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    glDepthMask(depthWrite ? GL_TRUE : GL_FALSE);
    glDisable(GL_CULL_FACE);
    if (mode == DRAW_BLEND || mode == DRAW_LINES) { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); }
    else if (mode == DRAW_ADDITIVE) { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE); }
    else glDisable(GL_BLEND);
    glBindVertexArray(genVAO);
    glBindBuffer(GL_ARRAY_BUFFER, genVBO);
    size_t bytes = b.v.size() * sizeof(GVert);
    if (bytes > genCap) { genCap = bytes * 2; glBufferData(GL_ARRAY_BUFFER, genCap, nullptr, GL_STREAM_DRAW); }
    glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, b.v.data());
    glDrawArrays(mode == DRAW_LINES ? GL_LINES : GL_TRIANGLES, 0, (GLsizei)b.v.size());
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

// ---------- PNG export (debug key) ----------
bool writePNG(const std::string& path, int w, int h, const uint8_t* px) {
    std::vector<uint8_t> raw;
    raw.reserve((size_t)(w * 4 + 1) * h);
    for (int y = 0; y < h; y++) { raw.push_back(0); raw.insert(raw.end(), px + (size_t)y * w * 4, px + (size_t)(y + 1) * w * 4); }
    uLongf clen = compressBound((uLong)raw.size());
    std::vector<uint8_t> comp(clen);
    if (compress2(comp.data(), &clen, raw.data(), (uLong)raw.size(), 9) != Z_OK) return false;
    comp.resize(clen);
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    auto be32 = [&](uint32_t v) { uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v}; fwrite(b, 1, 4, f); };
    auto chunk = [&](const char* type, const std::vector<uint8_t>& data) {
        be32((uint32_t)data.size());
        std::vector<uint8_t> td(type, type + 4); td.insert(td.end(), data.begin(), data.end());
        fwrite(td.data(), 1, td.size(), f);
        be32((uint32_t)crc32(0, td.data(), (uInt)td.size()));
    };
    const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    fwrite(sig, 1, 8, f);
    std::vector<uint8_t> ihdr = {(uint8_t)(w >> 24), (uint8_t)(w >> 16), (uint8_t)(w >> 8), (uint8_t)w,
                                 (uint8_t)(h >> 24), (uint8_t)(h >> 16), (uint8_t)(h >> 8), (uint8_t)h, 8, 6, 0, 0, 0};
    chunk("IHDR", ihdr);
    chunk("IDAT", comp);
    chunk("IEND", {});
    fclose(f);
    return true;
}

bool Renderer::exportTexturesPNG(const std::string& path) {
    // lay layers out in a 16-wide grid, scaled 4x for easy viewing
    const int S = 4, cols = 16, rows = (TX_COUNT + cols - 1) / cols;
    int w = cols * 16 * S, h = rows * 16 * S;
    std::vector<uint8_t> img((size_t)w * h * 4, 0);
    for (int l = 0; l < TX_COUNT; l++) {
        int gx = (l % cols) * 16 * S, gy = (l / cols) * 16 * S;
        for (int y = 0; y < 16 * S; y++)
            for (int x = 0; x < 16 * S; x++) {
                const uint8_t* s = &texPixels[((size_t)l * 256 + (y / S) * 16 + (x / S)) * 4];
                uint8_t* d = &img[((size_t)(gy + y) * w + gx + x) * 4];
                bool checker = ((x / 8) + (y / 8)) & 1;
                uint8_t bg = checker ? 60 : 90;
                float a = s[3] / 255.f;
                for (int k = 0; k < 3; k++) d[k] = (uint8_t)(s[k] * a + bg * (1 - a));
                d[3] = 255;
            }
    }
    return writePNG(path, w, h, img.data());
}
