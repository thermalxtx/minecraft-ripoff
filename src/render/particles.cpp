#include "particles.h"
#include "renderer.h"
#include "textures.h"
#include "../core/events.h"
#include "../world/world.h"

void ParticleSystem::spawn(int kind, vec3 pos, int count, int tex, Rng& r) {
    if (list.size() > 4000) return;
    for (int i = 0; i < count; i++) {
        Particle p;
        p.pos = p.prevPos = pos;
        p.u0 = 0; p.v0 = 0; p.u1 = 1; p.v1 = 1;
        switch (kind) {
        case PK_BLOCK: {
            // a random 4x4 piece of the block (or item) texture
            int layer = tex < 0 ? (-tex - 1) : blockDef((uint16_t)tex).tex[FACE_PZ];
            if (tex == GRASS) layer = TX_DIRT;
            p.layer = (float)layer;
            float px = r.range(0, 12) / 16.0f, py = r.range(0, 12) / 16.0f;
            p.u0 = px; p.v0 = py; p.u1 = px + 4 / 16.f; p.v1 = py + 4 / 16.f;
            p.pos += vec3(r.range(-0.3f, 0.3f), r.range(-0.3f, 0.3f), r.range(-0.3f, 0.3f));
            p.vel = vec3(r.range(-0.12f, 0.12f), r.range(0.0f, 0.2f), r.range(-0.12f, 0.12f));
            p.maxLife = p.life = (float)r.range(12, 30);
            p.size = r.range(0.08f, 0.14f);
            p.gravity = 0.04f;
            break;
        }
        case PK_SMOKE: case PK_POOF:
            p.layer = TX_SMOKE;
            p.vel = kind == PK_POOF ? vec3(r.range(-0.08f, 0.08f), r.range(0.0f, 0.1f), r.range(-0.08f, 0.08f)) : vec3(r.range(-0.02f, 0.02f), r.range(0.02f, 0.06f), r.range(-0.02f, 0.02f));
            p.pos += vec3(r.range(-0.3f, 0.3f), r.range(-0.3f, 0.3f), r.range(-0.3f, 0.3f));
            p.maxLife = p.life = (float)r.range(15, 35);
            p.size = r.range(0.15f, 0.3f);
            p.gravity = -0.002f; p.collide = false; p.fade = true; p.grow = true;
            { float gcol = kind == PK_POOF ? 0.9f : 0.35f; p.r = p.g = p.b = gcol; }
            break;
        case PK_FLAME:
            p.layer = TX_FLAME;
            p.vel = vec3(r.range(-0.01f, 0.01f), r.range(0.01f, 0.03f), r.range(-0.01f, 0.01f));
            p.maxLife = p.life = (float)r.range(8, 16);
            p.size = r.range(0.1f, 0.18f);
            p.gravity = 0; p.collide = false; p.fullbright = true; p.fade = true;
            break;
        case PK_CRIT:
            p.layer = TX_SPARK;
            p.vel = vec3(r.range(-0.3f, 0.3f), r.range(0.05f, 0.4f), r.range(-0.3f, 0.3f));
            p.maxLife = p.life = (float)r.range(10, 20);
            p.size = 0.12f; p.gravity = 0.03f; p.fullbright = true;
            p.r = 1; p.g = 0.9f; p.b = 0.5f;
            break;
        case PK_SPLASH:
            p.layer = TX_WATER; p.u1 = 0.25f; p.v1 = 0.25f;
            p.pos += vec3(r.range(-0.5f, 0.5f), 0, r.range(-0.5f, 0.5f));
            p.vel = vec3(r.range(-0.08f, 0.08f), r.range(0.1f, 0.25f), r.range(-0.08f, 0.08f));
            p.maxLife = p.life = (float)r.range(10, 25);
            p.size = 0.1f; p.gravity = 0.05f;
            break;
        case PK_BUBBLE:
            p.layer = TX_BUBBLE;
            p.vel = vec3(r.range(-0.02f, 0.02f), r.range(0.04f, 0.08f), r.range(-0.02f, 0.02f));
            p.maxLife = p.life = (float)r.range(10, 30);
            p.size = 0.1f; p.gravity = -0.002f; p.collide = false;
            break;
        case PK_LAVA:
            p.layer = TX_FLAME;
            p.vel = vec3(r.range(-0.08f, 0.08f), r.range(0.2f, 0.35f), r.range(-0.08f, 0.08f));
            p.maxLife = p.life = (float)r.range(20, 40);
            p.size = 0.12f; p.gravity = 0.04f; p.fullbright = true;
            break;
        case PK_EXPLOSION:
            p.layer = TX_SMOKE;
            p.pos += vec3(r.range(-2.f, 2.f), r.range(-1.5f, 2.f), r.range(-2.f, 2.f));
            p.vel = vec3(r.range(-0.1f, 0.1f), r.range(0.0f, 0.1f), r.range(-0.1f, 0.1f));
            p.maxLife = p.life = (float)r.range(10, 25);
            p.size = r.range(0.6f, 1.4f); p.gravity = -0.003f; p.collide = false; p.fade = true; p.grow = true;
            p.fullbright = true;
            { float gg = r.range(0.6f, 0.95f); p.r = p.g = p.b = gg; }
            break;
        case PK_HEART:
            p.layer = TX_HEART_FULL; p.vel = vec3(0, 0.05f, 0); p.maxLife = p.life = 20; p.size = 0.3f; p.gravity = 0; p.collide = false;
            break;
        default: return;
        }
        list.push_back(p);
    }
}

void ParticleSystem::tick(const World& w) {
    for (auto& p : list) {
        p.prevPos = p.pos;
        p.life -= 1;
        p.vel.y -= p.gravity;
        vec3 np = p.pos + p.vel;
        if (p.collide) {
            uint16_t b = w.getBlock(ifloor(np.x), ifloor(np.y - p.size * 0.5f), ifloor(np.z));
            if (blockDef(b).solid && blockDef(b).opaque) { np.y = p.pos.y; p.vel.y = 0; p.vel.x *= 0.7f; p.vel.z *= 0.7f; }
            uint16_t bx = w.getBlock(ifloor(np.x), ifloor(np.y), ifloor(p.pos.z));
            if (blockDef(bx).opaque) { np.x = p.pos.x; p.vel.x = 0; }
            uint16_t bz = w.getBlock(ifloor(np.x), ifloor(np.y), ifloor(np.z));
            if (blockDef(bz).opaque) { np.z = p.pos.z; p.vel.z = 0; }
        }
        p.pos = np;
        p.vel *= 0.96f;
    }
    list.erase(std::remove_if(list.begin(), list.end(), [](const Particle& p) { return p.life <= 0; }), list.end());
}

void ParticleSystem::render(GeoBatch& b, const Camera& cam, const World& w, float daylight, float a) {
    for (auto& p : list) {
        vec3 pos = lerp(p.prevPos, p.pos, a) - cam.pos;
        if (dot(pos, cam.forward) < 0) continue;
        float light = 1;
        if (!p.fullbright) {
            vec3 wp = pos + cam.pos;
            int x = ifloor(wp.x), y = ifloor(wp.y), z = ifloor(wp.z);
            light = std::max(std::pow(0.8f, 15.0f - w.getSkyLight(x, y, z)) * daylight, std::pow(0.8f, 15.0f - w.getBlockLight(x, y, z)));
            light = std::max(liftLight(light), 0.06f);
        }
        float t = 1 - p.life / p.maxLife;
        float alpha = p.fade ? (1 - t) * p.a : p.a;
        float size = p.grow ? p.size * (0.6f + t * 0.8f) : p.size;
        b.billboard(pos, size, cam.right, cam.up, p.layer, p.u0, p.v0, p.u1, p.v1, rgba(p.r * light, p.g * light, p.b * light, alpha));
    }
}
