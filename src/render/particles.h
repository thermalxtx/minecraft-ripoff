// Billboard particles: block breaking, smoke, flames, crits, splashes, explosions.
#pragma once
#include <vector>
#include "../core/math.h"

class World;
struct GeoBatch;
struct Camera;

struct Particle {
    vec3 pos, prevPos, vel;
    float life, maxLife, size;
    float layer, u0, v0, u1, v1;
    float r = 1, g = 1, b = 1, a = 1;
    float gravity = 0.04f;
    bool collide = true, fullbright = false, fade = false, grow = false;
};

class ParticleSystem {
public:
    std::vector<Particle> list;
    void spawn(int kind, vec3 pos, int count, int tex, Rng& rng);
    void tick(const World& w);
    void render(GeoBatch& b, const Camera& cam, const World& w, float daylight, float alpha);
    void clear() { list.clear(); }
};
