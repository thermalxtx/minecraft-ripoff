// waveOut software mixer with positional sound effects and a procedural piano / cave-ambience music director.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include <thread>
#include <mutex>
#include <atomic>
#include <vector>
#include <cmath>
#include "audio.h"
#include "synth.h"

namespace {
const int CHANNELS = 2, BUF_FRAMES = 1024, NUM_BUFS = 4, MAX_VOICES = 48;
const float TAU = 6.2831853f;

struct Voice {
    const Wave* data = nullptr;
    double pos = 0, rate = 1;
    float volL = 0, volR = 0;
    bool active = false;
};
struct Note { double start; float freq, dur, vel; bool drone; };

HWAVEOUT g_out = nullptr;
HANDLE g_event = nullptr;
WAVEHDR g_hdr[NUM_BUFS];
std::vector<int16_t> g_bufs[NUM_BUFS];
std::thread g_thread;
std::atomic<bool> g_running{false};
std::mutex g_mutex;
std::vector<std::vector<Wave>> g_bank;
Voice g_voices[MAX_VOICES];
float g_master = 0.8f, g_sfx = 1.0f, g_music = 0.5f;
vec3 g_lpos, g_lfwd(0, 0, -1), g_lright(1, 0, 0);
Rng g_rng(99);
// music state (protected by g_mutex)
std::vector<Note> g_notes;
double g_musicTime = 0;      // seconds since the current piece started
float g_musicFade = 1;
bool g_piecePlaying = false;
// main-thread director state
float g_untilNext = 40;
bool g_wasUnderground = false;

float noteFreq(int midi) { return 440.0f * std::pow(2.0f, (midi - 69) / 12.0f); }

// Piano-ish tone: a few decaying harmonics with a soft hammer attack
float pianoSample(float f, float t, float dur) {
    if (t < 0) return 0;
    float a = std::min(1.0f, t / 0.006f);
    float rel = t > dur ? std::exp(-(t - dur) * 6.0f) : 1.0f;
    float v = std::sin(TAU * f * t) * std::exp(-t * 1.6f) + 0.45f * std::sin(TAU * 2 * f * t) * std::exp(-t * 2.8f) +
              0.2f * std::sin(TAU * 3 * f * t) * std::exp(-t * 4.0f) + 0.08f * std::sin(TAU * 4.01f * f * t) * std::exp(-t * 6.0f);
    return v * a * rel;
}
float droneSample(float f, float t, float dur) {
    if (t < 0 || t > dur + 3) return 0;
    float e = std::min(1.0f, t / 3.0f) * std::min(1.0f, std::max(0.0f, (dur + 3 - t) / 3.0f));
    return (std::sin(TAU * f * t) + 0.5f * std::sin(TAU * f * 1.006f * t + 1.3f) + 0.25f * std::sin(TAU * f * 2.0f * t)) * e * (0.7f + 0.3f * std::sin(t * 0.7f));
}

void composePiano() {
    static const int SCALES[3][7] = {{0, 2, 4, 7, 9, 12, 14}, {0, 3, 5, 7, 10, 12, 15}, {0, 2, 5, 7, 9, 12, 14}};
    int root = 48 + g_rng.range(0, 7);
    const int* sc = SCALES[g_rng.nextInt(3)];
    float beat = 60.0f / g_rng.range(56.f, 76.f);
    int bars = g_rng.range(12, 20);
    std::vector<Note> notes;
    int degree = g_rng.range(0, 4);
    static const int PROG[4][4] = {{0, 3, 4, 0}, {0, 5, 3, 4}, {0, 4, 5, 3}, {5, 3, 0, 4}};
    const int* prog = PROG[g_rng.nextInt(4)];
    for (int b = 0; b < bars; b++) {
        double t0 = b * 4 * beat;
        int chord = prog[b % 4] % 7;
        int bass = root - 12 + sc[chord % 5];
        notes.push_back({t0, noteFreq(bass), beat * 3.5f, 0.35f, false});
        notes.push_back({t0 + beat * 2, noteFreq(bass + 7), beat * 1.8f, 0.2f, false});
        // melody: a gentle random walk with rests
        for (int k = 0; k < 4; k++) {
            if (g_rng.chance(0.3f)) continue;
            degree = std::max(0, std::min(6, degree + g_rng.range(-2, 2)));
            float dur = beat * (g_rng.chance(0.3f) ? 2.0f : 1.0f);
            double nt = t0 + k * beat + (g_rng.chance(0.2f) ? beat * 0.5 : 0);
            notes.push_back({nt, noteFreq(root + 12 + sc[degree]), dur, g_rng.range(0.25f, 0.4f), false});
            if (g_rng.chance(0.15f)) notes.push_back({nt, noteFreq(root + 12 + sc[(degree + 2) % 7]), dur, 0.15f, false});
        }
    }
    std::lock_guard<std::mutex> l(g_mutex);
    g_notes = notes; g_musicTime = 0; g_musicFade = 1; g_piecePlaying = true;
}

void composeCave() {
    std::vector<Note> notes;
    float base = noteFreq(g_rng.range(31, 38));
    notes.push_back({0, base, 30, 0.35f, true});
    notes.push_back({6, base * 1.5f, 18, 0.18f, true});
    // drips
    for (int k = 0; k < 14; k++) notes.push_back({g_rng.range(1.f, 32.f), g_rng.range(1400.f, 2600.f), 0.05f, 0.12f, false});
    std::lock_guard<std::mutex> l(g_mutex);
    g_notes = notes; g_musicTime = 0; g_musicFade = 1; g_piecePlaying = true;
}

void fill(int16_t* out) {
    static float mixL[BUF_FRAMES], mixR[BUF_FRAMES];
    std::fill(mixL, mixL + BUF_FRAMES, 0.f); std::fill(mixR, mixR + BUF_FRAMES, 0.f);
    std::lock_guard<std::mutex> l(g_mutex);
    for (auto& v : g_voices) {
        if (!v.active) continue;
        const Wave& w = *v.data;
        for (int i = 0; i < BUF_FRAMES; i++) {
            size_t p = (size_t)v.pos;
            if (p + 1 >= w.size()) { v.active = false; break; }
            float frac = (float)(v.pos - p);
            float s = w[p] * (1 - frac) + w[p + 1] * frac;
            mixL[i] += s * v.volL; mixR[i] += s * v.volR;
            v.pos += v.rate;
        }
    }
    if (g_piecePlaying) {
        float mv = g_master * g_music * g_musicFade * 0.22f;
        double dt = 1.0 / SAMPLE_RATE;
        bool any = false;
        for (auto& n : g_notes) {
            double rel = g_musicTime - n.start;
            float tail = n.drone ? n.dur + 3 : n.dur + 3;
            if (rel > tail) continue;
            any = true;
            if (rel + BUF_FRAMES * dt < 0) continue;
            for (int i = 0; i < BUF_FRAMES; i++) {
                float t = (float)(rel + i * dt);
                float s = n.drone ? droneSample(n.freq, t, n.dur) : (n.dur < 0.1f ? std::sin(TAU * n.freq * (1 - std::min(1.0f, t * 8)) * t) * std::exp(-std::max(0.f, t) * 40) * (t >= 0) : pianoSample(n.freq, t, n.dur));
                s *= n.vel * mv;
                mixL[i] += s; mixR[i] += s;
            }
        }
        g_musicTime += BUF_FRAMES * dt;
        if (!any) g_piecePlaying = false;
    }
    for (int i = 0; i < BUF_FRAMES; i++) {
        float l = std::tanh(mixL[i]), r = std::tanh(mixR[i]);
        out[i * 2] = (int16_t)(l * 32000); out[i * 2 + 1] = (int16_t)(r * 32000);
    }
}

void mixerThread() {
    while (g_running) {
        WaitForSingleObject(g_event, 50);
        for (int b = 0; b < NUM_BUFS; b++) {
            if (!(g_hdr[b].dwFlags & WHDR_DONE)) continue;
            fill(g_bufs[b].data());
            g_hdr[b].dwFlags &= ~WHDR_DONE;
            waveOutWrite(g_out, &g_hdr[b], sizeof(WAVEHDR));
        }
    }
}

void startVoice(int id, float vl, float vr, float pitch) {
    if (id < 0 || id >= (int)g_bank.size() || g_bank[id].empty()) return;
    const Wave* w = &g_bank[id][g_rng.nextInt((int)g_bank[id].size())];
    std::lock_guard<std::mutex> l(g_mutex);
    Voice* slot = nullptr;
    for (auto& v : g_voices) if (!v.active) { slot = &v; break; }
    if (!slot) {  // steal the quietest voice
        slot = &g_voices[0];
        for (auto& v : g_voices) if (v.volL + v.volR < slot->volL + slot->volR) slot = &v;
    }
    slot->data = w; slot->pos = 0; slot->rate = pitch; slot->volL = vl; slot->volR = vr; slot->active = true;
}
}

namespace audio {
bool init() {
    synthesizeAll(g_bank);
    WAVEFORMATEX fmt = {};
    fmt.wFormatTag = WAVE_FORMAT_PCM; fmt.nChannels = CHANNELS; fmt.nSamplesPerSec = SAMPLE_RATE;
    fmt.wBitsPerSample = 16; fmt.nBlockAlign = (WORD)(CHANNELS * 2); fmt.nAvgBytesPerSec = SAMPLE_RATE * fmt.nBlockAlign;
    g_event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    if (waveOutOpen(&g_out, WAVE_MAPPER, &fmt, (DWORD_PTR)g_event, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) { g_out = nullptr; return false; }
    for (int b = 0; b < NUM_BUFS; b++) {
        g_bufs[b].assign(BUF_FRAMES * CHANNELS, 0);
        g_hdr[b] = {};
        g_hdr[b].lpData = (LPSTR)g_bufs[b].data();
        g_hdr[b].dwBufferLength = BUF_FRAMES * CHANNELS * 2;
        waveOutPrepareHeader(g_out, &g_hdr[b], sizeof(WAVEHDR));
        g_hdr[b].dwFlags |= WHDR_DONE;
    }
    g_running = true;
    g_thread = std::thread(mixerThread);
    return true;
}

void shutdown() {
    if (!g_out) return;
    g_running = false;
    SetEvent(g_event);
    if (g_thread.joinable()) g_thread.join();
    waveOutReset(g_out);
    for (int b = 0; b < NUM_BUFS; b++) waveOutUnprepareHeader(g_out, &g_hdr[b], sizeof(WAVEHDR));
    waveOutClose(g_out);
    g_out = nullptr;
    CloseHandle(g_event);
}

void setListener(vec3 pos, vec3 fwd, vec3 right) { g_lpos = pos; g_lfwd = fwd; g_lright = right; }
void setVolumes(float master, float sfx, float music) { g_master = master; g_sfx = sfx; g_music = music; }

void play3D(int id, vec3 pos, float volume, float pitch) {
    if (!g_out) return;
    vec3 d = pos - g_lpos;
    float dist = length(d);
    const float maxDist = 24.0f;
    if (dist > maxDist) return;
    float fall = 1.0f - dist / maxDist;
    float vol = volume * g_master * g_sfx * fall * fall;
    if (vol < 0.003f) return;
    float pan = dist > 0.5f ? clampf(dot(d / dist, g_lright), -1, 1) * 0.8f : 0;
    float vl = vol * std::sqrt((1 - pan) * 0.5f) * 1.41f, vr = vol * std::sqrt((1 + pan) * 0.5f) * 1.41f;
    startVoice(id, vl, vr, pitch);
}

void play2D(int id, float volume, float pitch) {
    if (!g_out) return;
    float v = volume * g_master * g_sfx;
    startVoice(id, v, v, pitch);
}

void updateMusic(float dt, bool inGame, bool underground, bool night) {
    if (!g_out) return;
    (void)night;
    bool playing;
    { std::lock_guard<std::mutex> l(g_mutex); playing = g_piecePlaying; }
    if (inGame && playing && underground != g_wasUnderground) {
        // fade out the current piece when moving between surface and caves
        std::lock_guard<std::mutex> l(g_mutex);
        g_musicFade = std::max(0.0f, g_musicFade - dt * 0.5f);
        if (g_musicFade <= 0) { g_piecePlaying = false; g_wasUnderground = underground; g_untilNext = 8; }
        return;
    }
    if (playing) return;
    g_wasUnderground = underground;
    g_untilNext -= dt;
    if (g_untilNext > 0) return;
    if (underground && inGame) { composeCave(); g_untilNext = g_rng.range(40.f, 90.f); }
    else { composePiano(); g_untilNext = g_rng.range(120.f, 300.f); }
}

void stopMusic() {
    std::lock_guard<std::mutex> l(g_mutex);
    g_piecePlaying = false;
    g_untilNext = 20;
}
}
