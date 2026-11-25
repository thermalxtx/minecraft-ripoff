#include "synth.h"
#include "audio.h"
#include "../world/block.h"
#include <cmath>
#include <algorithm>

namespace {
const float TAU = 6.2831853f;
struct S {
    Rng r;
    explicit S(uint64_t seed) : r(seed) {}
    float noise() { return r.range(-1.f, 1.f); }
};
Wave make(float secs) { return Wave((size_t)(secs * SAMPLE_RATE), 0.f); }
float env(float t, float attack, float decay) {  // quick attack, exponential decay
    if (t < attack) return t / attack;
    return std::exp(-(t - attack) / decay);
}
// simple biquad band-pass applied in place
void bandpass(Wave& w, float freq, float q) {
    float w0 = TAU * freq / SAMPLE_RATE, alpha = std::sin(w0) / (2 * q);
    float b0 = alpha, b2 = -alpha, a0 = 1 + alpha, a1 = -2 * std::cos(w0), a2 = 1 - alpha;
    float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    for (auto& s : w) {
        float y = (b0 * s + b2 * x2 - a1 * y1 - a2 * y2) / a0;
        x2 = x1; x1 = s; y2 = y1; y1 = y; s = y;
    }
}
void lowpass(Wave& w, float freq) {
    float a = 1 - std::exp(-TAU * freq / SAMPLE_RATE), y = 0;
    for (auto& s : w) { y += a * (s - y); s = y; }
}
void highpass(Wave& w, float freq) {
    float a = 1 - std::exp(-TAU * freq / SAMPLE_RATE), y = 0;
    for (auto& s : w) { y += a * (s - y); s = s - y; }
}
void normalize(Wave& w, float peak) {
    float m = 0;
    for (float s : w) m = std::max(m, std::fabs(s));
    if (m > 1e-6f) for (auto& s : w) s *= peak / m;
}
void fadeEdges(Wave& w) {
    size_t n = std::min<size_t>(w.size() / 4, 64);
    for (size_t i = 0; i < n; i++) { float f = (float)i / n; w[i] *= f; w[w.size() - 1 - i] *= f; }
}
// Noise burst shaped by envelope and filtered
Wave noiseHit(S& s, float len, float attack, float decay, float bpFreq, float q) {
    Wave w = make(len);
    for (size_t i = 0; i < w.size(); i++) w[i] = s.noise() * env(i / (float)SAMPLE_RATE, attack, decay);
    if (bpFreq > 0) bandpass(w, bpFreq, q);
    return w;
}
// Grainy crunch: many tiny noise clicks
Wave crunch(S& s, float len, int grains, float bp, float q) {
    Wave w = make(len);
    for (int g = 0; g < grains; g++) {
        size_t start = (size_t)(s.r.range(0.f, len * 0.8f) * SAMPLE_RATE);
        float amp = s.r.range(0.3f, 1.0f);
        size_t gl = (size_t)(s.r.range(0.004f, 0.015f) * SAMPLE_RATE);
        for (size_t i = 0; i < gl && start + i < w.size(); i++) w[start + i] += s.noise() * amp * (1 - (float)i / gl);
    }
    for (size_t i = 0; i < w.size(); i++) w[i] *= env(i / (float)SAMPLE_RATE, 0.002f, len * 0.5f);
    bandpass(w, bp, q);
    return w;
}
Wave tone(float len, float f0, float f1, float attack, float decay, int shape /*0 sine 1 saw 2 square*/, float vibHz = 0, float vibAmt = 0) {
    Wave w = make(len);
    float ph = 0;
    for (size_t i = 0; i < w.size(); i++) {
        float t = i / (float)SAMPLE_RATE, k = t / len;
        float f = f0 + (f1 - f0) * k;
        if (vibHz > 0) f *= 1 + std::sin(TAU * vibHz * t) * vibAmt;
        ph += f / SAMPLE_RATE;
        ph -= std::floor(ph);
        float v = shape == 0 ? std::sin(TAU * ph) : (shape == 1 ? (2 * ph - 1) : (ph < 0.5f ? 1.f : -1.f));
        w[i] = v * env(t, attack, decay);
    }
    return w;
}
void mix(Wave& dst, const Wave& src, float gain, size_t offset = 0) {
    if (dst.size() < src.size() + offset) dst.resize(src.size() + offset, 0);
    for (size_t i = 0; i < src.size(); i++) dst[i + offset] += src[i] * gain;
}
// Vocal-ish creature sound: saw with formant filter sweep
Wave voice(S& s, float len, float f0, float f1, float formant0, float formant1, float vib, float am, float noiseAmt) {
    Wave w = make(len);
    float ph = 0;
    for (size_t i = 0; i < w.size(); i++) {
        float t = i / (float)SAMPLE_RATE, k = t / len;
        float f = (f0 + (f1 - f0) * k) * (1 + std::sin(TAU * 5.5f * t) * vib);
        ph += f / SAMPLE_RATE; ph -= std::floor(ph);
        float v = (2 * ph - 1) + s.noise() * noiseAmt;
        if (am > 0) v *= 0.6f + 0.4f * std::sin(TAU * am * t);
        float e = std::min(1.0f, t / 0.03f) * std::min(1.0f, (len - t) / 0.08f);
        w[i] = v * e;
    }
    // sweep the formant by filtering in segments
    Wave out = make(len);
    const int SEG = 8;
    for (int sgm = 0; sgm < SEG; sgm++) {
        Wave part = w;
        float fr = formant0 + (formant1 - formant0) * (sgm + 0.5f) / SEG;
        bandpass(part, fr, 2.5f);
        size_t a = w.size() * sgm / SEG, b = w.size() * (sgm + 1) / SEG;
        for (size_t i = a; i < b; i++) out[i] = part[i];
    }
    lowpass(out, formant1 * 2.5f);
    return out;
}

Wave makeSound(int id, int variant) {
    S s(1000 + id * 31 + variant * 7);
    float pv = 1.0f + (variant - 1) * 0.07f;  // slight pitch change per variant
    Wave w;
    auto stepLike = [&](int mat, bool dig) {
        float L = dig ? 0.16f : 0.09f;
        switch (mat) {
        case SND_STONE: w = noiseHit(s, L, 0.001f, L * 0.25f, 1800 * pv, 1.2f); mix(w, noiseHit(s, 0.03f, 0.0005f, 0.005f, 3500, 1.0f), 0.6f); break;
        case SND_WOOD: w = tone(L, 210 * pv, 160 * pv, 0.002f, L * 0.3f, 0); mix(w, noiseHit(s, L, 0.001f, 0.02f, 900, 1.5f), 0.7f); break;
        case SND_GRASS: w = crunch(s, L + 0.03f, dig ? 40 : 22, 2200 * pv, 0.8f); break;
        case SND_SAND: w = noiseHit(s, L + 0.04f, 0.01f, L * 0.4f, 3500 * pv, 0.6f); break;
        case SND_GRAVEL: w = crunch(s, L + 0.05f, dig ? 30 : 18, 1300 * pv, 1.0f); break;
        case SND_GLASS: w = tone(0.12f, 2600 * pv, 2500 * pv, 0.001f, 0.04f, 0); mix(w, tone(0.12f, 3900 * pv, 3800, 0.001f, 0.03f, 0), 0.5f); break;
        case SND_WOOL: w = noiseHit(s, L, 0.01f, L * 0.35f, 700 * pv, 0.7f); break;
        case SND_SNOW: w = crunch(s, L + 0.04f, 26, 3000 * pv, 0.6f); break;
        case SND_METAL: w = tone(0.25f, 900 * pv, 880 * pv, 0.001f, 0.08f, 0); mix(w, tone(0.25f, 1370 * pv, 1360, 0.001f, 0.06f, 0), 0.6f); mix(w, noiseHit(s, 0.03f, 0.0005f, 0.006f, 4000, 1), 0.4f); break;
        }
    };
    if (id >= SFX_STEP_STONE && id <= SFX_STEP_METAL) stepLike(id - SFX_STEP_STONE, false);
    else if (id >= SFX_DIG_STONE && id <= SFX_DIG_METAL) stepLike(id - SFX_DIG_STONE, true);
    else switch (id) {
    case SFX_BREAK_GLASS:
        w = noiseHit(s, 0.45f, 0.001f, 0.08f, 3000, 0.7f);
        for (int k = 0; k < 9; k++) mix(w, tone(0.1f, s.r.range(2500.f, 5500.f), s.r.range(2400.f, 5400.f), 0.001f, 0.03f, 0), 0.25f, (size_t)(s.r.range(0.f, 0.25f) * SAMPLE_RATE));
        break;
    case SFX_HURT: w = voice(s, 0.22f, 230 * pv, 140 * pv, 800, 500, 0.01f, 0, 0.15f); break;
    case SFX_DEATH: w = voice(s, 0.7f, 240, 90, 900, 400, 0.02f, 0, 0.1f); break;
    case SFX_PIG: w = voice(s, 0.35f, 120 * pv, 95 * pv, 600, 450, 0.0f, 28, 0.25f); break;
    case SFX_COW: w = voice(s, 0.9f, 118 * pv, 92 * pv, 380, 900, 0.01f, 0, 0.05f); break;
    case SFX_SHEEP: w = voice(s, 0.55f, 310 * pv, 280 * pv, 1100, 900, 0.09f, 0, 0.05f); break;
    case SFX_CHICKEN:
        w = make(0.3f);
        for (int k = 0; k < 3; k++) mix(w, tone(0.06f, 950 * pv + k * 60, 750 * pv, 0.003f, 0.02f, 2), 0.4f, (size_t)(k * 0.08f * SAMPLE_RATE));
        bandpass(w, 1200, 1.2f);
        break;
    case SFX_SHAMBLER: w = voice(s, 1.1f, 85 * pv, 70 * pv, 350, 650, 0.03f, 4, 0.35f); break;
    case SFX_SHAMBLER_HURT: w = voice(s, 0.35f, 120 * pv, 80 * pv, 700, 400, 0.04f, 0, 0.4f); break;
    case SFX_MARROW:
        w = make(0.45f);
        for (int k = 0; k < 14; k++) mix(w, noiseHit(s, 0.025f, 0.0005f, 0.006f, s.r.range(1500.f, 3000.f), 3.0f), 0.8f, (size_t)(k * 0.028f * SAMPLE_RATE + s.r.range(0.f, 0.01f) * SAMPLE_RATE));
        break;
    case SFX_FUSE: {
        w = make(1.5f);
        for (size_t i = 0; i < w.size(); i++) { float t = i / (float)SAMPLE_RATE; w[i] = s.noise() * std::min(1.0f, t * 4) * (0.6f + 0.4f * t / 1.5f); }
        highpass(w, 2500);
        break;
    }
    case SFX_EXPLODE: {
        w = make(1.8f);
        for (size_t i = 0; i < w.size(); i++) { float t = i / (float)SAMPLE_RATE; w[i] = s.noise() * env(t, 0.003f, 0.35f); }
        lowpass(w, 900);
        mix(w, tone(1.0f, 60, 30, 0.002f, 0.25f, 0), 1.2f);
        mix(w, crunch(s, 1.2f, 60, 600, 0.8f), 0.6f);
        break;
    }
    case SFX_WEAVER:
        w = make(0.5f);
        for (int k = 0; k < 18; k++) mix(w, tone(0.015f, s.r.range(2000.f, 3500.f), 1500, 0.001f, 0.005f, 2), 0.3f, (size_t)(k * 0.025f * SAMPLE_RATE));
        mix(w, noiseHit(s, 0.5f, 0.05f, 0.2f, 4000, 1.0f), 0.3f);
        break;
    case SFX_SPLASH: case SFX_SWIM: {
        float L = id == SFX_SPLASH ? 0.5f : 0.28f;
        w = noiseHit(s, L, 0.005f, L * 0.3f, 1100 * pv, 0.8f);
        for (size_t i = 0; i < w.size(); i++) w[i] *= 0.7f + 0.3f * std::sin(i * 0.004f + s.r.range(0.f, 0.3f));
        for (int k = 0; k < 5; k++) mix(w, tone(0.04f, s.r.range(500.f, 1100.f), s.r.range(900.f, 1600.f), 0.002f, 0.015f, 0), 0.3f, (size_t)(s.r.range(0.f, L * 0.7f) * SAMPLE_RATE));
        break;
    }
    case SFX_BOW: w = tone(0.25f, 330 * pv, 210 * pv, 0.002f, 0.08f, 1); lowpass(w, 1200); mix(w, noiseHit(s, 0.08f, 0.001f, 0.02f, 2500, 1), 0.5f); break;
    case SFX_ARROW_HIT: w = tone(0.12f, 160 * pv, 110, 0.001f, 0.03f, 0); mix(w, noiseHit(s, 0.05f, 0.0005f, 0.01f, 2000, 1.5f), 0.7f); break;
    case SFX_EAT: w = crunch(s, 0.12f, 25, 1800 * pv, 0.9f); break;
    case SFX_BURP: w = voice(s, 0.35f, 95, 80, 500, 350, 0.05f, 30, 0.1f); break;
    case SFX_CLICK: w = tone(0.035f, 1300, 1200, 0.001f, 0.01f, 2); lowpass(w, 3000); break;
    case SFX_DOOR_OPEN: w = voice(s, 0.35f, 190 * pv, 260 * pv, 900, 1300, 0.08f, 12, 0.2f); mix(w, noiseHit(s, 0.08f, 0.001f, 0.02f, 600, 1), 0.6f, (size_t)(0.25f * SAMPLE_RATE)); break;
    case SFX_DOOR_CLOSE: w = tone(0.18f, 140 * pv, 90, 0.001f, 0.05f, 0); mix(w, noiseHit(s, 0.12f, 0.001f, 0.03f, 500, 1), 0.9f); break;
    case SFX_CHEST_OPEN: w = voice(s, 0.3f, 160, 210, 700, 1000, 0.05f, 15, 0.2f); break;
    case SFX_CHEST_CLOSE: w = tone(0.15f, 120, 80, 0.001f, 0.04f, 0); mix(w, noiseHit(s, 0.1f, 0.001f, 0.03f, 450, 1), 0.9f); break;
    case SFX_POP: w = tone(0.07f, 420, 950, 0.002f, 0.03f, 0); break;
    case SFX_FIZZ: { w = make(0.6f); for (size_t i = 0; i < w.size(); i++) w[i] = s.noise() * env(i / (float)SAMPLE_RATE, 0.005f, 0.18f); highpass(w, 3000); break; }
    case SFX_LAVA_POP: w = tone(0.09f, 140 * pv, 60, 0.002f, 0.03f, 0); mix(w, noiseHit(s, 0.05f, 0.001f, 0.01f, 700, 1), 0.5f); break;
    case SFX_ATTACK: {
        w = make(0.16f);
        for (size_t i = 0; i < w.size(); i++) { float t = i / (float)SAMPLE_RATE; w[i] = s.noise() * std::sin(PI * t / 0.16f); }
        bandpass(w, 1500 * pv, 0.9f);
        break;
    }
    case SFX_CRIT: w = tone(0.2f, 1500, 1400, 0.001f, 0.05f, 2); lowpass(w, 4000); mix(w, noiseHit(s, 0.06f, 0.0005f, 0.015f, 3000, 1), 0.7f); break;
    case SFX_MOB_HURT: w = noiseHit(s, 0.15f, 0.002f, 0.04f, 800 * pv, 1.0f); mix(w, tone(0.12f, 300 * pv, 180, 0.002f, 0.04f, 1), 0.3f); break;
    case SFX_FALL_BIG: w = tone(0.3f, 70, 45, 0.002f, 0.08f, 0); mix(w, noiseHit(s, 0.2f, 0.002f, 0.05f, 300, 1), 0.9f); break;
    case SFX_TOOL_BREAK:
        w = tone(0.35f, 1100, 1000, 0.001f, 0.08f, 0); mix(w, tone(0.35f, 1650, 1600, 0.001f, 0.06f, 0), 0.5f); mix(w, crunch(s, 0.3f, 20, 2500, 1), 0.8f);
        break;
    case SFX_FURNACE: w = crunch(s, 1.0f, 50, 1500, 0.7f); break;
    case SFX_WATER_AMBIENT: w = noiseHit(s, 1.0f, 0.3f, 0.5f, 600, 0.5f); break;
    default: w = tone(0.1f, 440, 440, 0.01f, 0.05f, 0); break;
    }
    fadeEdges(w);
    normalize(w, 0.8f);
    return w;
}
}

void synthesizeAll(std::vector<std::vector<Wave>>& bank) {
    bank.assign(SFX_COUNT, {});
    for (int id = 0; id < SFX_COUNT; id++) {
        bool varied = id <= SFX_DIG_METAL || id == SFX_HURT || id == SFX_PIG || id == SFX_COW || id == SFX_SHEEP || id == SFX_SHAMBLER ||
                      id == SFX_EAT || id == SFX_SPLASH || id == SFX_SWIM || id == SFX_MOB_HURT || id == SFX_ARROW_HIT;
        int n = varied ? 3 : 1;
        for (int v = 0; v < n; v++) bank[id].push_back(makeSound(id, v));
    }
}
