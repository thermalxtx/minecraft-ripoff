// Seeded gradient noise (improved Perlin) with fBm and ridged helpers. Replaces FastNoiseLite.
#pragma once
#include <cstdint>
#include <cmath>
#include "../core/math.h"

class Noise {
public:
    explicit Noise(uint64_t seed = 0) { reseed(seed); }
    void reseed(uint64_t seed) {
        Rng r(seed * 0x9E3779B97F4A7C15ull + 12345);
        for (int i = 0; i < 256; i++) p[i] = (uint8_t)i;
        for (int i = 255; i > 0; i--) { int j = r.nextInt(i + 1); uint8_t t = p[i]; p[i] = p[j]; p[j] = t; }
        for (int i = 0; i < 256; i++) p[256 + i] = p[i];
        ox = r.range(0.f, 256.f); oy = r.range(0.f, 256.f); oz = r.range(0.f, 256.f);
    }
    // Returns roughly -1..1
    float noise2(float x, float y) const {
        x += ox; y += oy;
        int X = ifloor(x), Y = ifloor(y);
        float xf = x - X, yf = y - Y;
        X &= 255; Y &= 255;
        float u = fade(xf), v = fade(yf);
        int aa = p[p[X] + Y], ab = p[p[X] + Y + 1], ba = p[p[X + 1] + Y], bb = p[p[X + 1] + Y + 1];
        float r = lerpf(lerpf(grad2(aa, xf, yf), grad2(ba, xf - 1, yf), u),
                        lerpf(grad2(ab, xf, yf - 1), grad2(bb, xf - 1, yf - 1), u), v);
        return r * 1.41f;
    }
    float noise3(float x, float y, float z) const {
        x += ox; y += oy; z += oz;
        int X = ifloor(x), Y = ifloor(y), Z = ifloor(z);
        float xf = x - X, yf = y - Y, zf = z - Z;
        X &= 255; Y &= 255; Z &= 255;
        float u = fade(xf), v = fade(yf), w = fade(zf);
        int A = p[X] + Y, AA = p[A] + Z, AB = p[A + 1] + Z, B = p[X + 1] + Y, BA = p[B] + Z, BB = p[B + 1] + Z;
        return lerpf(lerpf(lerpf(grad3(p[AA], xf, yf, zf), grad3(p[BA], xf - 1, yf, zf), u),
                           lerpf(grad3(p[AB], xf, yf - 1, zf), grad3(p[BB], xf - 1, yf - 1, zf), u), v),
                     lerpf(lerpf(grad3(p[AA + 1], xf, yf, zf - 1), grad3(p[BA + 1], xf - 1, yf, zf - 1), u),
                           lerpf(grad3(p[AB + 1], xf, yf - 1, zf - 1), grad3(p[BB + 1], xf - 1, yf - 1, zf - 1), u), v), w);
    }
    float fbm2(float x, float y, int oct, float lac = 2.0f, float gain = 0.5f) const {
        float s = 0, a = 1, n = 0;
        for (int i = 0; i < oct; i++) { s += noise2(x, y) * a; n += a; a *= gain; x *= lac; y *= lac; }
        return s / n;
    }
    float fbm3(float x, float y, float z, int oct) const {
        float s = 0, a = 1, n = 0;
        for (int i = 0; i < oct; i++) { s += noise3(x, y, z) * a; n += a; a *= 0.5f; x *= 2; y *= 2; z *= 2; }
        return s / n;
    }
    float ridged2(float x, float y, int oct) const {
        float s = 0, a = 1, n = 0;
        for (int i = 0; i < oct; i++) { float v = 1 - std::fabs(noise2(x, y)); s += v * v * a; n += a; a *= 0.5f; x *= 2; y *= 2; }
        return s / n;
    }
private:
    uint8_t p[512];
    float ox, oy, oz;
    static float fade(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }
    static float grad2(int h, float x, float y) {
        switch (h & 7) {
            case 0: return x + y; case 1: return -x + y; case 2: return x - y; case 3: return -x - y;
            case 4: return x; case 5: return -x; case 6: return y; default: return -y;
        }
    }
    static float grad3(int h, float x, float y, float z) {
        int hh = h & 15; float u = hh < 8 ? x : y, v = hh < 4 ? y : (hh == 12 || hh == 14 ? x : z);
        return ((hh & 1) ? -u : u) + ((hh & 2) ? -v : v);
    }
};
