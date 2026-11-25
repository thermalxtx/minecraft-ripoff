// Simple event queue: systems post events instead of calling each other directly.
#pragma once
#include <vector>
#include "math.h"

enum EventType : uint8_t {
    EV_BLOCK_BROKEN,    // pos = block centre, a = block id, b = meta; c = 1 if by player
    EV_BLOCK_PLACED,    // pos, a = block id
    EV_SOUND,           // pos, a = sound id, f = volume, g = pitch
    EV_SOUND_UI,        // non-positional sound, a = sound id
    EV_DROP_ITEM,       // pos, a = item id, b = count, c = damage
    EV_FALLING_BLOCK,   // pos (block corner), a = block id
    EV_EXPLOSION,       // pos, f = power
    EV_PARTICLES,       // pos, a = particle kind, b = count, c = texture layer / block id
    EV_PLAYER_HURT,     // f = amount
};

struct GameEvent {
    EventType type;
    vec3 pos;
    int a = 0, b = 0, c = 0;
    float f = 1.0f, g = 1.0f;
};

struct EventQueue {
    std::vector<GameEvent> events;
    void post(const GameEvent& e) { events.push_back(e); }
    void sound(vec3 p, int id, float vol = 1, float pitch = 1) { GameEvent e{EV_SOUND, p}; e.a = id; e.f = vol; e.g = pitch; post(e); }
    void uiSound(int id, float vol = 1, float pitch = 1) { GameEvent e{EV_SOUND_UI, {}}; e.a = id; e.f = vol; e.g = pitch; post(e); }
    void drop(vec3 p, int item, int count, int damage = 0) { GameEvent e{EV_DROP_ITEM, p}; e.a = item; e.b = count; e.c = damage; post(e); }
    void particles(vec3 p, int kind, int count, int tex = 0) { GameEvent e{EV_PARTICLES, p}; e.a = kind; e.b = count; e.c = tex; post(e); }
};
extern EventQueue g_events;

// Particle kinds
enum ParticleKind { PK_BLOCK = 0, PK_SMOKE, PK_FLAME, PK_CRIT, PK_SPLASH, PK_EXPLOSION, PK_POOF, PK_HEART, PK_BUBBLE, PK_LAVA };
