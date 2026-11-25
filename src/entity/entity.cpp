// Entity manager: physics, dropped items, arrows, falling blocks, combat, explosions, spawning, chunk saving.
#include "entity.h"
#include "../world/world.h"
#include "../game/player.h"
#include "../game/physics.h"
#include "../core/events.h"
#include "../audio/audio.h"
#include <algorithm>

static Rng g_erng(424242);

struct MobInfo { float halfW, height, health; };
static MobInfo mobInfo(uint8_t t) {
    switch (t) {
        case ET_ITEM: return {0.125f, 0.25f, 5};
        case ET_ARROW: return {0.15f, 0.3f, 1};
        case ET_FALLING_BLOCK: return {0.49f, 0.98f, 1};
        case ET_PIG: return {0.45f, 0.9f, 10};
        case ET_COW: return {0.45f, 1.4f, 10};
        case ET_SHEEP: return {0.45f, 1.3f, 8};
        case ET_CHICKEN: return {0.2f, 0.7f, 4};
        case ET_SHAMBLER: return {0.3f, 1.95f, 20};
        case ET_MARROWSHOT: return {0.3f, 1.99f, 20};
        case ET_FUSELING: return {0.3f, 1.6f, 20};
        case ET_CRAGWEAVER: return {0.7f, 0.9f, 16};
        default: return {0.3f, 1.0f, 10};
    }
}

Entity* EntityManager::spawn(uint8_t type, vec3 pos) {
    auto e = std::make_unique<Entity>();
    e->id = nextId++;
    e->type = type;
    e->pos = e->prevPos = pos;
    MobInfo mi = mobInfo(type);
    e->halfW = mi.halfW; e->height = mi.height; e->health = e->maxHealth = mi.health;
    e->yaw = e->prevYaw = e->headYaw = e->prevHeadYaw = g_erng.range(0.f, 6.28f);
    e->aiTimer = g_erng.range(20, 120);
    Entity* p = e.get();
    list.push_back(std::move(e));
    return p;
}

Entity* EntityManager::spawnItem(vec3 pos, const ItemStack& st, bool thrown, vec3 dir) {
    if (st.empty()) return nullptr;
    Entity* e = spawn(ET_ITEM, pos - vec3(0, 0.125f, 0));
    e->item = st;
    if (thrown) {
        e->vel = dir * 0.3f + vec3(g_erng.range(-0.02f, 0.02f), 0.1f, g_erng.range(-0.02f, 0.02f));
        e->pickupDelay = 40;
    } else {
        e->vel = vec3(g_erng.range(-0.1f, 0.1f), 0.2f, g_erng.range(-0.1f, 0.1f));
        e->pickupDelay = 10;
    }
    return e;
}

Entity* EntityManager::spawnArrow(vec3 pos, vec3 vel, bool fromPlayer, float damage, uint32_t owner) {
    Entity* e = spawn(ET_ARROW, pos);
    e->vel = vel; e->fromPlayer = fromPlayer; e->damage = damage; e->ownerId = owner;
    e->yaw = std::atan2(vel.x, vel.z); e->pitch = std::atan2(vel.y, std::sqrt(vel.x * vel.x + vel.z * vel.z));
    e->prevYaw = e->yaw;
    return e;
}

int EntityManager::countType(bool hostile) const {
    int n = 0;
    for (auto& e : list) if (e->isMob() && e->isHostile() == hostile && !e->removed) n++;
    return n;
}
int EntityManager::hostilesNear(vec3 p, float r) const {
    int n = 0;
    for (auto& e : list) if (e->isHostile() && e->alive() && length(e->pos - p) < r && !(e->type == ET_CRAGWEAVER && !e->hostileToPlayer && isDay)) n++;
    return n;
}

// ---------------- physics ----------------
void EntityManager::physics(Entity& e, float gravity, float drag) {
    AABB inner = e.box();
    inner.mn.y += 0.1f; inner.mx.y = std::max(inner.mn.y + 0.05f, inner.mx.y - 0.3f);
    e.inWater = world->isAreaLiquid(inner, WATER);
    e.inLava = world->isAreaLiquid(inner, LAVA);
    if (e.inWater || e.inLava) {
        e.vel *= e.inWater ? 0.8f : 0.5f;
        e.vel.y -= gravity * 0.25f;
        if (e.isMob() && e.type != ET_ITEM) e.vel.y += 0.03f;  // mobs paddle to stay afloat
        if (e.type == ET_ITEM) e.vel.y += 0.025f;
    } else e.vel.y -= gravity;
    vec3 d = e.vel;
    float before = e.pos.y;
    MoveResult r = moveBox(*world, e.pos, e.halfW, e.height, d, e.isMob() ? 0.6f : 0.0f);
    if (r.hitX) e.vel.x = 0;
    if (r.hitZ) e.vel.z = 0;
    if (r.hitY) e.vel.y = 0;
    e.hitWall = r.hitX || r.hitZ;
    e.onGround = r.onGround;
    float dy = e.pos.y - before;
    if (!e.onGround && dy < 0) e.fallDistance -= dy;
    if (e.inWater || e.type == ET_CHICKEN || e.type == ET_CRAGWEAVER) e.fallDistance = 0;
    if (e.onGround) {
        if (e.fallDistance > 3 && e.isMob()) hurt(&e, std::ceil(e.fallDistance - 3), vec3(0, 0, 0), false, 0);
        e.fallDistance = 0;
    }
    if (!e.inWater && !e.inLava) {
        float f = e.onGround ? 0.546f : 0.91f;
        e.vel.x *= f; e.vel.z *= f;
        e.vel.y *= drag;
    }
}

// ---------------- items ----------------
void EntityManager::tickItem(Entity& e) {
    if (e.pickupDelay > 0) e.pickupDelay--;
    physics(e, 0.04f, 0.98f);
    if (e.age > 6000) { e.removed = true; return; }
    if (e.inLava) { e.removed = true; g_events.sound(e.pos, SFX_FIZZ, 0.4f); return; }
    // merge with identical nearby stacks
    if (e.age % 10 == 0)
        for (auto& o : list) {
            if (o.get() == &e || o->removed || o->type != ET_ITEM || !o->item.sameItem(e.item)) continue;
            if (length(o->pos - e.pos) > 0.6f || o->item.count + e.item.count > e.item.maxStack()) continue;
            e.item.count = (uint8_t)(e.item.count + o->item.count);
            e.age = std::min(e.age, o->age);
            o->removed = true;
        }
    // drift toward and get picked up by the player
    if (player && !player->dead && e.pickupDelay == 0) {
        vec3 c = player->pos + vec3(0, 0.8f, 0);
        vec3 diff = c - (e.pos + vec3(0, 0.125f, 0));
        float dist = length(diff);
        if (dist < 2.5f) {
            e.vel += normalize(diff) * 0.04f;
            if (dist < 1.3f) {
                int left = player->inv.add(e.item);
                if (left < e.item.count) {
                    g_events.sound(e.pos, SFX_POP, 0.25f, g_erng.range(1.2f, 1.8f));
                    if (left == 0) e.removed = true; else e.item.count = (uint8_t)left;
                }
            }
        }
    }
}

// ---------------- arrows ----------------
void EntityManager::tickArrow(Entity& e) {
    if (e.stuck) {
        if (e.age > 1200) e.removed = true;
        // the block it stuck in was removed
        if (world->getBlock(ifloor(e.pos.x), ifloor(e.pos.y), ifloor(e.pos.z)) == AIR) { e.stuck = false; e.vel = vec3(0, -0.05f, 0); }
        if (e.fromPlayer && player && !player->dead && length(player->pos + vec3(0, 0.9f, 0) - e.pos) < 1.4f) {
            if (player->inv.add(ItemStack(ARROW, 1)) == 0) { e.removed = true; g_events.sound(e.pos, SFX_POP, 0.25f, 1.6f); }
        }
        return;
    }
    vec3 start = e.pos, step = e.vel;
    float len = length(step);
    // blocks
    RayHit bh = world->raycast(start, step, len);
    float blockT = bh.hit ? bh.dist : 1e9f;
    // entities and player
    float bestT = blockT;
    Entity* hitEnt = nullptr;
    bool hitPlayer = false;
    vec3 dir = len > 0 ? step / len : vec3(0, -1, 0);
    for (auto& o : list) {
        if (!o->isMob() || !o->alive() || o->id == e.ownerId) continue;
        float t = o->box().grow(0.3f).rayHit(start, dir, len);
        if (t >= 0 && t < bestT) { bestT = t; hitEnt = o.get(); }
    }
    if (player && !player->dead && !e.fromPlayer) {
        float t = player->box().grow(0.3f).rayHit(start, dir, len);
        if (t >= 0 && t < bestT) { bestT = t; hitEnt = nullptr; hitPlayer = true; }
    }
    float speed = len;
    float dmg = std::ceil(speed * e.damage * 0.5f + 0.5f);
    if (hitEnt) {
        hurt(hitEnt, dmg, dir, e.fromPlayer, 0.3f);
        g_events.sound(hitEnt->pos, SFX_ARROW_HIT, 0.6f);
        e.removed = true;
        return;
    }
    if (hitPlayer) {
        player->damage(dmg, "was shot by a Marrowshot", dir, 0.3f);
        g_events.sound(player->pos, SFX_ARROW_HIT, 0.6f);
        e.removed = true;
        return;
    }
    if (bh.hit) {
        e.pos = start + dir * std::max(0.0f, bh.dist - 0.05f);
        e.stuck = true; e.age = 0;
        e.vel = vec3(0, 0, 0);
        g_events.sound(e.pos, SFX_ARROW_HIT, 0.4f, 1.2f);
        return;
    }
    e.pos = start + step;
    bool water = world->getBlock(ifloor(e.pos.x), ifloor(e.pos.y), ifloor(e.pos.z)) == WATER;
    e.vel *= water ? 0.6f : 0.99f;
    e.vel.y -= 0.05f;
    e.prevYaw = e.yaw;
    e.yaw = std::atan2(e.vel.x, e.vel.z);
    e.pitch = std::atan2(e.vel.y, std::sqrt(e.vel.x * e.vel.x + e.vel.z * e.vel.z));
    if (e.age > 400) e.removed = true;
}

// ---------------- falling sand/gravel ----------------
void EntityManager::tickFalling(Entity& e) {
    physics(e, 0.04f, 0.98f);
    if (e.onGround || e.age > 600) {
        int x = ifloor(e.pos.x), y = ifloor(e.pos.y + 0.5f), z = ifloor(e.pos.z);
        uint16_t cur = world->getBlock(x, y, z);
        if (blockDef(cur).replaceable && e.age <= 600) {
            world->setBlock(x, y, z, e.blockId);
            g_events.sound(vec3(x + 0.5f, y + 0.5f, z + 0.5f), SFX_DIG_SAND + 0, 0.6f);
        } else g_events.drop(e.pos + vec3(0, 0.5f, 0), e.blockId, 1);
        e.removed = true;
    }
}

// ---------------- combat ----------------
void EntityManager::hurt(Entity* e, float amount, vec3 knockDir, bool byPlayer, float knockback) {
    if (!e || !e->alive() || e->invuln > 0) return;
    if (!e->isMob()) return;
    e->health -= amount;
    e->hurtTime = 10; e->invuln = 10;
    if (knockback > 0) {
        vec3 k = normalize(vec3(knockDir.x, 0, knockDir.z)) * knockback;
        e->vel.x += k.x; e->vel.z += k.z;
        if (e->onGround) e->vel.y = std::max(e->vel.y, 0.36f);
    }
    if (!e->isHostile()) { e->ai = AI_FLEE; e->aiTimer = 100; e->path.clear(); }
    if (e->type == ET_CRAGWEAVER && byPlayer) e->hostileToPlayer = true;
    if (e->isHostile()) e->hostileToPlayer = true;
    int snd = SFX_MOB_HURT;
    switch (e->type) {
        case ET_PIG: snd = SFX_PIG; break; case ET_COW: snd = SFX_COW; break; case ET_SHEEP: snd = SFX_SHEEP; break;
        case ET_CHICKEN: snd = SFX_CHICKEN; break; case ET_SHAMBLER: snd = SFX_SHAMBLER_HURT; break;
        case ET_MARROWSHOT: snd = SFX_MARROW; break; case ET_CRAGWEAVER: snd = SFX_WEAVER; break;
    }
    g_events.sound(e->pos + vec3(0, e->height * 0.5f, 0), snd, 0.8f, g_erng.range(1.1f, 1.3f));
    if (e->health <= 0) { e->health = 0; e->deathTime = 1; die(*e); }
}

void EntityManager::die(Entity& e) {
    auto drop = [&](uint16_t id, int mn, int mx) {
        int n = g_erng.range(mn, mx);
        if (n > 0) g_events.drop(e.pos + vec3(0, e.height * 0.5f, 0), id, n);
    };
    bool burned = e.fireTicks > 0;
    switch (e.type) {
        case ET_PIG: drop(burned ? COOKED_PORK : RAW_PORK, 1, 3); break;
        case ET_COW: drop(burned ? STEAK : RAW_BEEF, 1, 3); drop(LEATHER, 0, 2); break;
        case ET_SHEEP: {
            static const uint16_t wool[4] = {WOOL_WHITE, WOOL_GRAY, WOOL_BLACK, WOOL_BROWN};
            drop(wool[e.extra & 3], 1, 1); drop(burned ? COOKED_MUTTON : RAW_MUTTON, 1, 2); break;
        }
        case ET_CHICKEN: drop(FEATHER, 0, 2); drop(burned ? COOKED_CHICKEN : RAW_CHICKEN, 1, 1); break;
        case ET_SHAMBLER: drop(ROTTEN_FLESH, 0, 2); break;
        case ET_MARROWSHOT: drop(BONE, 0, 2); drop(ARROW, 0, 2); break;
        case ET_FUSELING: drop(GUNPOWDER, 0, 2); break;
        case ET_CRAGWEAVER: drop(STRING, 0, 2); break;
    }
    if (e.type == ET_SHAMBLER) g_events.sound(e.pos, SFX_SHAMBLER_HURT, 1.0f, 0.7f);
}

Entity* EntityManager::raycast(vec3 o, vec3 dir, float maxDist, float& tHit) {
    Entity* best = nullptr;
    float bt = maxDist;
    for (auto& e : list) {
        if (!e->isMob() || !e->alive()) continue;
        float t = e->box().grow(0.1f).rayHit(o, dir, bt);
        if (t >= 0 && t < bt) { bt = t; best = e.get(); }
    }
    tHit = bt;
    return best;
}

bool EntityManager::canSee(const Entity& e, vec3 target) const {
    vec3 eye = e.pos + vec3(0, e.height * 0.85f, 0);
    vec3 d = target - eye;
    float len = length(d);
    if (len < 0.01f) return true;
    RayHit h = world->raycast(eye, d / len, len);
    return !h.hit;
}

void EntityManager::explode(vec3 c, float power) {
    g_events.sound(c, SFX_EXPLODE, 1.0f, g_erng.range(0.85f, 1.05f));
    g_events.particles(c, PK_EXPLOSION, 30);
    float r = power * 1.2f;
    int ir = (int)std::ceil(r);
    int cx = ifloor(c.x), cy = ifloor(c.y), cz = ifloor(c.z);
    std::vector<ivec3> destroyed;
    for (int dx = -ir; dx <= ir; dx++)
        for (int dy = -ir; dy <= ir; dy++)
            for (int dz = -ir; dz <= ir; dz++) {
                float d = std::sqrt((float)(dx * dx + dy * dy + dz * dz));
                if (d > r * g_erng.range(0.7f, 1.0f)) continue;
                int x = cx + dx, y = cy + dy, z = cz + dz;
                uint16_t b = world->getBlock(x, y, z);
                if (b == AIR || blockDef(b).liquid) continue;
                if (blockDef(b).blastResist > 30 || blockDef(b).hardness < 0) continue;
                destroyed.push_back({x, y, z});
            }
    for (auto& p : destroyed) {
        uint16_t b = world->getBlock(p.x, p.y, p.z);
        uint8_t m = world->getMeta(p.x, p.y, p.z);
        if (b == AIR) continue;
        if (TileEntity* t = world->getTile(p.x, p.y, p.z)) {
            for (auto& s : t->slots) if (!s.empty()) g_events.drop(vec3(p.x + 0.5f, p.y + 0.5f, p.z + 0.5f), s.id, s.count, s.damage);
        }
        if (g_erng.chance(0.3f)) {
            std::vector<ItemStack> drops;
            blockDrops(b, m, ItemStack(toolItem(MAT_DIAMOND, KIND_PICKAXE), 1), g_erng, drops);
            for (auto& s : drops) g_events.drop(vec3(p.x + 0.5f, p.y + 0.5f, p.z + 0.5f), s.id, s.count);
        }
        world->setBlock(p.x, p.y, p.z, AIR);
    }
    // damage and knock back entities and the player
    float reach = power * 2.0f;
    for (auto& e : list) {
        if (!e->alive()) continue;
        vec3 ec = e->pos + vec3(0, e->height * 0.5f, 0);
        float d = length(ec - c);
        if (d > reach) continue;
        float impact = 1 - d / reach;
        vec3 dir = normalize(ec - c);
        if (e->type == ET_ITEM) { if (impact > 0.5f) e->removed = true; continue; }
        if (!e->isMob()) continue;
        e->invuln = 0;
        hurt(e.get(), impact * 22.0f, dir, false, 0);
        e->vel += dir * impact * 1.2f;
    }
    if (player && !player->dead) {
        vec3 pc = player->pos + vec3(0, 0.9f, 0);
        float d = length(pc - c);
        if (d < reach) {
            float impact = 1 - d / reach;
            vec3 dir = normalize(pc - c);
            player->invuln = 0;
            player->damage(std::ceil(impact * 22.0f), "was blown up by a Fuseling");
            player->vel += dir * impact * 1.2f;
        }
    }
}

// ---------------- main tick ----------------
void EntityManager::tick() {
    pathBudget = 3;  // A* searches allowed per tick
    for (size_t i = 0; i < list.size(); i++) {
        Entity& e = *list[i];
        if (e.removed) continue;
        e.prevPos = e.pos; e.prevYaw = e.yaw; e.prevHeadYaw = e.headYaw; e.prevLimbAmount = e.limbAmount;
        e.age++;
        if (!world->isLoaded(ifloor(e.pos.x), ifloor(e.pos.z))) continue;  // frozen at the edge of the world
        switch (e.type) {
            case ET_ITEM: tickItem(e); break;
            case ET_ARROW: tickArrow(e); break;
            case ET_FALLING_BLOCK: tickFalling(e); break;
            default: tickMob(e); break;
        }
        if (e.pos.y < -64) e.removed = true;
    }
    list.erase(std::remove_if(list.begin(), list.end(), [](const std::unique_ptr<Entity>& e) { return e->removed; }), list.end());
    if (mobSpawning) spawnMobs();
}

// ---------------- spawning ----------------
void EntityManager::spawnMobs() {
    if (!player || player->dead) return;
    vec3 pp = player->pos;
    // despawn distant monsters
    for (auto& e : list) {
        if (!e->isHostile() || e->removed) continue;
        float d = length(e->pos - pp);
        if (d > 128 || (d > 40 && g_erng.chance(1.0f / 600.0f))) e->removed = true;
        else if (isDay && e->type == ET_CRAGWEAVER && !e->hostileToPlayer && d > 32 && g_erng.chance(1.0f / 400.0f)) e->removed = true;
    }
    int maxDist = std::min(world->renderDistance * 16 - 8, 112);
    if (world->tickCount % 10 == 0 && countType(true) < 30) {
        for (int attempt = 0; attempt < 6; attempt++) {
            float ang = g_erng.range(0.f, 6.2832f), dist = g_erng.range(24.f, (float)std::max(28, maxDist));
            int x = ifloor(pp.x + std::cos(ang) * dist), z = ifloor(pp.z + std::sin(ang) * dist);
            Chunk* c = world->chunkAt(x, z);
            if (!c || c->state != CS_STITCHED) continue;
            int top = world->surfaceHeight(x, z);
            int y = g_erng.chance(0.55f) ? top : g_erng.range(6, std::max(7, top));
            // search down for a floor with two free blocks above
            int found = -1;
            for (int k = 0; k < 24 && y - k > 1; k++) {
                int yy = y - k;
                uint16_t floorB = world->getBlock(x, yy - 1, z);
                if (blockDef(floorB).opaque && floorB != BEDROCK && !blockDef(world->getBlock(x, yy, z)).solid &&
                    !blockDef(world->getBlock(x, yy + 1, z)).solid && !blockDef(world->getBlock(x, yy, z)).liquid &&
                    !blockDef(world->getBlock(x, yy + 1, z)).liquid) { found = yy; break; }
            }
            if (found < 0) continue;
            if (world->getLightLevel(x, found, z, dayFactor) >= 7) continue;
            float pd = length(vec3(x + 0.5f, (float)found, z + 0.5f) - pp);
            if (pd < 24) continue;
            int roll = g_erng.nextInt(100);
            uint8_t type = roll < 38 ? ET_SHAMBLER : (roll < 65 ? ET_MARROWSHOT : (roll < 83 ? ET_FUSELING : ET_CRAGWEAVER));
            int group = g_erng.range(1, type == ET_FUSELING ? 1 : 3);
            for (int g = 0; g < group; g++) {
                vec3 sp(x + 0.5f + g_erng.range(-1.5f, 1.5f), (float)found, z + 0.5f + g_erng.range(-1.5f, 1.5f));
                MobInfo mi = mobInfo(type);
                if (boxCollides(*world, boxAt(sp, mi.halfW, mi.height))) sp = vec3(x + 0.5f, (float)found, z + 0.5f);
                if (boxCollides(*world, boxAt(sp, mi.halfW, mi.height))) continue;
                spawn(type, sp);
            }
            break;
        }
    }
    // occasional daytime animal spawns on grass
    if (world->tickCount % 400 == 0 && isDay && countType(false) < 14) {
        float ang = g_erng.range(0.f, 6.2832f), dist = g_erng.range(28.f, (float)std::max(32, std::min(64, maxDist)));
        int x = ifloor(pp.x + std::cos(ang) * dist), z = ifloor(pp.z + std::sin(ang) * dist);
        Chunk* c = world->chunkAt(x, z);
        if (c && c->state == CS_STITCHED) {
            int y = world->surfaceHeight(x, z);
            uint16_t g = world->getBlock(x, y - 1, z);
            if ((g == GRASS || g == SNOWY_GRASS) && world->getBlock(x, y, z) == AIR && world->getSkyLight(x, y, z) >= 9) {
                static const uint8_t kinds[4] = {ET_PIG, ET_COW, ET_SHEEP, ET_CHICKEN};
                uint8_t k = kinds[g_erng.nextInt(4)];
                int n = g_erng.range(2, 3);
                for (int i = 0; i < n; i++) {
                    Entity* e = spawn(k, vec3(x + 0.5f + i * 0.4f, (float)y, z + 0.5f));
                    if (k == ET_SHEEP && g_erng.chance(0.15f)) e->extra = (uint8_t)g_erng.range(1, 3);
                }
            }
        }
    }
}

// ---------------- saving with chunks ----------------
void EntityManager::spawnFromChunk(Chunk* c) {
    for (auto& s : c->savedEntities) {
        if (s.type == ET_NONE || s.type >= ET_TYPE_COUNT || s.type >= ET_SHAMBLER || s.type == ET_ARROW || s.type == ET_FALLING_BLOCK) continue;
        Entity* e = spawn(s.type, vec3(s.x, s.y, s.z));
        e->yaw = e->prevYaw = s.yaw;
        if (s.health > 0) e->health = std::min((float)s.health, e->maxHealth);
        e->item = s.item; e->age = s.age; e->extra = s.extra;
        if (s.type == ET_ITEM && e->item.empty()) e->removed = true;
    }
    if (!c->savedEntities.empty()) c->modified = true;
    c->savedEntities.clear();
}

void EntityManager::collectForChunk(Chunk* c, bool remove) {
    c->savedEntities.clear();
    for (auto& e : list) {
        if (e->removed || !e->alive()) continue;
        if ((ifloor(e->pos.x) >> 4) != c->cx || (ifloor(e->pos.z) >> 4) != c->cz) continue;
        bool persist = e->type == ET_ITEM || (e->isMob() && !e->isHostile());
        if (persist) {
            SavedEntity s;
            s.type = e->type; s.x = e->pos.x; s.y = e->pos.y; s.z = e->pos.z; s.yaw = e->yaw;
            s.health = (int16_t)std::ceil(e->health); s.item = e->item; s.age = e->age; s.extra = e->extra;
            c->savedEntities.push_back(s);
        }
        if (remove) e->removed = true;
    }
    if (remove) list.erase(std::remove_if(list.begin(), list.end(), [](const std::unique_ptr<Entity>& e) { return e->removed; }), list.end());
}
