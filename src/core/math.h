// Small vector/matrix library (replaces GLM). Column-major matrices like OpenGL.
#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>

constexpr float PI = 3.14159265358979f;

struct vec2 {
    float x = 0, y = 0;
    vec2() = default;
    constexpr vec2(float a, float b) : x(a), y(b) {}
    vec2 operator+(vec2 o) const { return {x + o.x, y + o.y}; }
    vec2 operator-(vec2 o) const { return {x - o.x, y - o.y}; }
    vec2 operator*(float s) const { return {x * s, y * s}; }
};

struct vec3 {
    float x = 0, y = 0, z = 0;
    vec3() = default;
    constexpr vec3(float a, float b, float c) : x(a), y(b), z(c) {}
    explicit constexpr vec3(float s) : x(s), y(s), z(s) {}
    vec3 operator+(vec3 o) const { return {x + o.x, y + o.y, z + o.z}; }
    vec3 operator-(vec3 o) const { return {x - o.x, y - o.y, z - o.z}; }
    vec3 operator*(vec3 o) const { return {x * o.x, y * o.y, z * o.z}; }
    vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
    vec3 operator-() const { return {-x, -y, -z}; }
    vec3& operator+=(vec3 o) { x += o.x; y += o.y; z += o.z; return *this; }
    vec3& operator-=(vec3 o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
    float operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }
    float& operator[](int i) { return i == 0 ? x : (i == 1 ? y : z); }
};
inline vec3 operator*(float s, vec3 v) { return v * s; }
inline float dot(vec3 a, vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline vec3 cross(vec3 a, vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float length(vec3 v) { return std::sqrt(dot(v, v)); }
inline vec3 normalize(vec3 v) { float l = length(v); return l > 1e-8f ? v / l : vec3(0, 0, 0); }
inline vec3 lerp(vec3 a, vec3 b, float t) { return a + (b - a) * t; }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
inline float smoothstepf(float a, float b, float x) { float t = clampf((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); }
inline int ifloor(float v) { int i = (int)v; return (v < (float)i) ? i - 1 : i; }
inline int ifloord(double v) { int i = (int)v; return (v < (double)i) ? i - 1 : i; }
inline int floorDiv(int a, int b) { return (a >= 0) ? a / b : ((a - b + 1) / b); }
inline int floorMod(int a, int b) { int m = a % b; return m < 0 ? m + b : m; }

struct vec4 {
    float x = 0, y = 0, z = 0, w = 0;
    vec4() = default;
    constexpr vec4(float a, float b, float c, float d) : x(a), y(b), z(c), w(d) {}
    vec4(vec3 v, float d) : x(v.x), y(v.y), z(v.z), w(d) {}
    vec3 xyz() const { return {x, y, z}; }
};

struct ivec3 {
    int x = 0, y = 0, z = 0;
    ivec3() = default;
    constexpr ivec3(int a, int b, int c) : x(a), y(b), z(c) {}
    ivec3 operator+(ivec3 o) const { return {x + o.x, y + o.y, z + o.z}; }
    ivec3 operator-(ivec3 o) const { return {x - o.x, y - o.y, z - o.z}; }
    bool operator==(ivec3 o) const { return x == o.x && y == o.y && z == o.z; }
    bool operator!=(ivec3 o) const { return !(*this == o); }
    vec3 toVec() const { return {(float)x, (float)y, (float)z}; }
};

struct mat4 {
    float m[16]; // column-major: m[col*4+row]
    mat4() { for (int i = 0; i < 16; i++) m[i] = (i % 5 == 0) ? 1.f : 0.f; }
    float& at(int r, int c) { return m[c * 4 + r]; }
    float at(int r, int c) const { return m[c * 4 + r]; }
    mat4 operator*(const mat4& o) const {
        mat4 r;
        for (int c = 0; c < 4; c++)
            for (int rr = 0; rr < 4; rr++) {
                float s = 0;
                for (int k = 0; k < 4; k++) s += at(rr, k) * o.at(k, c);
                r.at(rr, c) = s;
            }
        return r;
    }
    vec4 operator*(vec4 v) const {
        return {at(0,0)*v.x + at(0,1)*v.y + at(0,2)*v.z + at(0,3)*v.w,
                at(1,0)*v.x + at(1,1)*v.y + at(1,2)*v.z + at(1,3)*v.w,
                at(2,0)*v.x + at(2,1)*v.y + at(2,2)*v.z + at(2,3)*v.w,
                at(3,0)*v.x + at(3,1)*v.y + at(3,2)*v.z + at(3,3)*v.w};
    }
    vec3 transformPoint(vec3 p) const { vec4 r = (*this) * vec4(p, 1); return r.xyz(); }
    vec3 transformDir(vec3 p) const { vec4 r = (*this) * vec4(p, 0); return r.xyz(); }

    static mat4 translate(vec3 t) { mat4 r; r.at(0,3) = t.x; r.at(1,3) = t.y; r.at(2,3) = t.z; return r; }
    static mat4 scale(vec3 s) { mat4 r; r.at(0,0) = s.x; r.at(1,1) = s.y; r.at(2,2) = s.z; return r; }
    static mat4 rotateX(float a) { mat4 r; float c = std::cos(a), s = std::sin(a); r.at(1,1) = c; r.at(1,2) = -s; r.at(2,1) = s; r.at(2,2) = c; return r; }
    static mat4 rotateY(float a) { mat4 r; float c = std::cos(a), s = std::sin(a); r.at(0,0) = c; r.at(0,2) = s; r.at(2,0) = -s; r.at(2,2) = c; return r; }
    static mat4 rotateZ(float a) { mat4 r; float c = std::cos(a), s = std::sin(a); r.at(0,0) = c; r.at(0,1) = -s; r.at(1,0) = s; r.at(1,1) = c; return r; }
    static mat4 perspective(float fovyRad, float aspect, float n, float f) {
        mat4 r; float t = 1.0f / std::tan(fovyRad / 2);
        for (float& v : r.m) v = 0;
        r.at(0,0) = t / aspect; r.at(1,1) = t;
        r.at(2,2) = (f + n) / (n - f); r.at(2,3) = 2 * f * n / (n - f); r.at(3,2) = -1;
        return r;
    }
    static mat4 ortho(float l, float rr, float b, float t, float n, float f) {
        mat4 r;
        r.at(0,0) = 2 / (rr - l); r.at(1,1) = 2 / (t - b); r.at(2,2) = -2 / (f - n);
        r.at(0,3) = -(rr + l) / (rr - l); r.at(1,3) = -(t + b) / (t - b); r.at(2,3) = -(f + n) / (f - n);
        return r;
    }
    static mat4 lookAt(vec3 eye, vec3 target, vec3 up) {
        vec3 f = normalize(target - eye), s = normalize(cross(f, up)), u = cross(s, f);
        mat4 r;
        r.at(0,0) = s.x; r.at(0,1) = s.y; r.at(0,2) = s.z;
        r.at(1,0) = u.x; r.at(1,1) = u.y; r.at(1,2) = u.z;
        r.at(2,0) = -f.x; r.at(2,1) = -f.y; r.at(2,2) = -f.z;
        r.at(0,3) = -dot(s, eye); r.at(1,3) = -dot(u, eye); r.at(2,3) = dot(f, eye);
        return r;
    }
};

// Axis-aligned bounding box
struct AABB {
    vec3 mn, mx;
    AABB() = default;
    AABB(vec3 a, vec3 b) : mn(a), mx(b) {}
    bool intersects(const AABB& o) const {
        return mn.x < o.mx.x && mx.x > o.mn.x && mn.y < o.mx.y && mx.y > o.mn.y && mn.z < o.mx.z && mx.z > o.mn.z;
    }
    AABB offset(vec3 d) const { return {mn + d, mx + d}; }
    AABB expand(vec3 d) const {
        AABB r = *this;
        if (d.x < 0) r.mn.x += d.x; else r.mx.x += d.x;
        if (d.y < 0) r.mn.y += d.y; else r.mx.y += d.y;
        if (d.z < 0) r.mn.z += d.z; else r.mx.z += d.z;
        return r;
    }
    AABB grow(float g) const { return {mn - vec3(g), mx + vec3(g)}; }
    vec3 center() const { return (mn + mx) * 0.5f; }
    // Ray slab test; returns entry distance or -1
    float rayHit(vec3 o, vec3 d, float maxT) const {
        float tmin = 0, tmax = maxT;
        for (int i = 0; i < 3; i++) {
            if (std::fabs(d[i]) < 1e-9f) { if (o[i] < mn[i] || o[i] > mx[i]) return -1; continue; }
            float t1 = (mn[i] - o[i]) / d[i], t2 = (mx[i] - o[i]) / d[i];
            if (t1 > t2) std::swap(t1, t2);
            tmin = std::max(tmin, t1); tmax = std::min(tmax, t2);
            if (tmin > tmax) return -1;
        }
        return tmin;
    }
};

// View frustum from a view-projection matrix (Gribb/Hartmann)
struct Frustum {
    vec4 p[6];
    void build(const mat4& m) {
        auto row = [&](int r) { return vec4(m.at(r,0), m.at(r,1), m.at(r,2), m.at(r,3)); };
        vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
        auto add = [](vec4 a, vec4 b, float s) { return vec4(a.x + b.x * s, a.y + b.y * s, a.z + b.z * s, a.w + b.w * s); };
        p[0] = add(r3, r0, 1); p[1] = add(r3, r0, -1); p[2] = add(r3, r1, 1);
        p[3] = add(r3, r1, -1); p[4] = add(r3, r2, 1); p[5] = add(r3, r2, -1);
    }
    bool boxVisible(vec3 mn, vec3 mx) const {
        for (auto& pl : p) {
            vec3 v(pl.x >= 0 ? mx.x : mn.x, pl.y >= 0 ? mx.y : mn.y, pl.z >= 0 ? mx.z : mn.z);
            if (pl.x * v.x + pl.y * v.y + pl.z * v.z + pl.w < 0) return false;
        }
        return true;
    }
};

// Fast deterministic RNG (xorshift-ish / splitmix)
struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed = 0x12345678) : s(seed ? seed : 0x9E3779B97F4A7C15ull) {}
    uint64_t next() { uint64_t z = (s += 0x9E3779B97F4A7C15ull); z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull; return z ^ (z >> 31); }
    int nextInt(int n) { return n <= 0 ? 0 : (int)(next() % (uint64_t)n); }
    int range(int a, int b) { return a + nextInt(b - a + 1); } // inclusive
    float nextFloat() { return (next() >> 40) * (1.0f / 16777216.0f); }
    float range(float a, float b) { return a + (b - a) * nextFloat(); }
    bool chance(float p) { return nextFloat() < p; }
};
inline uint64_t hash64(uint64_t x) { x ^= x >> 33; x *= 0xff51afd7ed558ccdull; x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ull; x ^= x >> 33; return x; }
inline uint64_t hashCoords(int64_t a, int64_t b, int64_t c, uint64_t seed) {
    return hash64(seed ^ hash64((uint64_t)a * 0x9E3779B1ull ^ hash64((uint64_t)b * 0x85EBCA77ull ^ hash64((uint64_t)c * 0xC2B2AE3Dull))));
}
