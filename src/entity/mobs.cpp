// Mob AI: state machines for passive animals and the four night monsters.
#include "entity.h"
#include "../world/world.h"
#include "../game/player.h"
#include "../core/events.h"
#include "../audio/audio.h"
#include <algorithm>

static Rng g_mrng(777);

static float angleTo(float from, float to) {
    float d = to - from;
    while (d > PI) d -= 2 * PI;
    while (d < -PI) d += 2 * PI;
    return d;
}

void EntityManager::moveToward(Entity& e, vec3 target, float speed) {
    vec3 d(target.x - e.pos.x, 0, target.z - e.pos.z);
    float len = length(d);
    if (len < 0.05f) return;
    d = d / len;
    float accel = e.onGround ? speed : (e.inWater ? speed * 0.6f : speed * 0.25f);
    e.vel.x += d.x * accel;
    e.vel.z += d.z * accel;
    float want = std::atan2(-d.x, -d.z);
    float diff = angleTo(e.yaw, want);
    e.yaw += clampf(diff, -0.35f, 0.35f);
    // jump over one-block steps
    if (e.hitWall && e.onGround && e.jumpCooldown <= 0) { e.vel.y = 0.42f; e.jumpCooldown = 8; }
}

bool EntityManager::followPath(Entity& e, vec3 goal, float speed) {
    e.repathTimer--;
    bool needPath = e.repathTimer <= 0 || length(goal - e.lastPathGoal) > 2.5f || e.pathIdx >= e.path.size();
    if (needPath && pathBudget > 0) {
        pathBudget--;
        e.repathTimer = 20 + g_mrng.nextInt(15);
        ivec3 start(ifloor(e.pos.x), ifloor(e.pos.y + 0.2f), ifloor(e.pos.z));
        ivec3 g(ifloor(goal.x), ifloor(goal.y + 0.2f), ifloor(goal.z));
        int h = (int)std::ceil(e.height);
        e.path.clear();
        findPath(*world, start, g, h, e.type == ET_CRAGWEAVER, 1200, e.path);
        e.pathIdx = e.path.size() > 1 ? 1 : 0;
        e.lastPathGoal = goal;
    }
    if (e.pathIdx < e.path.size()) {
        ivec3 n = e.path[e.pathIdx];
        vec3 nc(n.x + 0.5f, (float)n.y, n.z + 0.5f);
        float hd = std::sqrt((nc.x - e.pos.x) * (nc.x - e.pos.x) + (nc.z - e.pos.z) * (nc.z - e.pos.z));
        if (hd < 0.4f && std::fabs(e.pos.y - nc.y) < 1.3f) { e.pathIdx++; return true; }
        moveToward(e, nc, speed);
        if (n.y > ifloor(e.pos.y + 0.2f) && e.onGround && hd < 1.6f && e.jumpCooldown <= 0) { e.vel.y = 0.42f; e.jumpCooldown = 8; }
        return true;
    }
    return false;
}

// Pick a random nearby target that is not off a cliff or in lava
static bool pickWander(const World& w, const Entity& e, vec3& out, float range) {
    for (int t = 0; t < 6; t++) {
        int x = ifloor(e.pos.x + g_mrng.range(-range, range)), z = ifloor(e.pos.z + g_mrng.range(-range, range));
        int y = ifloor(e.pos.y + 0.2f);
        for (int k = 2; k >= -3; k--) {
            uint16_t below = w.getBlock(x, y + k - 1, z), at = w.getBlock(x, y + k, z), above = w.getBlock(x, y + k + 1, z);
            if (blockDef(below).solid && !blockDef(at).solid && !blockDef(above).solid && at != LAVA && below != CACTUS && below != LAVA) {
                if (!e.isHostile() && at == WATER) continue;
                out = vec3(x + 0.5f, (float)(y + k), z + 0.5f);
                return true;
            }
        }
    }
    return false;
}

void EntityManager::aiPassive(Entity& e) {
    if (e.ai == AI_FLEE) {
        if (--e.aiTimer <= 0) { e.ai = AI_IDLE; e.aiTimer = 40; e.hasMoveTarget = false; return; }
        if (!e.hasMoveTarget || e.aiTimer % 25 == 0) {
            vec3 away = e.pos;
            if (player) { vec3 d = e.pos - player->pos; d.y = 0; away = e.pos + normalize(d) * 6.0f; }
            e.moveTarget = away + vec3(g_mrng.range(-3.f, 3.f), 0, g_mrng.range(-3.f, 3.f));
            e.hasMoveTarget = true;
        }
        moveToward(e, e.moveTarget, 0.075f);
        return;
    }
    if (--e.aiTimer <= 0) {
        if (g_mrng.chance(0.55f) && pickWander(*world, e, e.moveTarget, 8)) { e.ai = AI_WANDER; e.hasMoveTarget = true; e.aiTimer = g_mrng.range(80, 200); }
        else { e.ai = AI_IDLE; e.hasMoveTarget = false; e.aiTimer = g_mrng.range(40, 160); }
    }
    if (e.ai == AI_WANDER && e.hasMoveTarget) {
        float d = std::sqrt((e.moveTarget.x - e.pos.x) * (e.moveTarget.x - e.pos.x) + (e.moveTarget.z - e.pos.z) * (e.moveTarget.z - e.pos.z));
        if (d < 0.6f) { e.ai = AI_IDLE; e.hasMoveTarget = false; }
        else moveToward(e, e.moveTarget, e.type == ET_CHICKEN ? 0.04f : 0.045f);
    }
    if (player && length(player->pos - e.pos) < 6) {
        vec3 d = player->pos - e.pos;
        e.headYaw += clampf(angleTo(e.headYaw, std::atan2(-d.x, -d.z)), -0.2f, 0.2f);
    } else e.headYaw += clampf(angleTo(e.headYaw, e.yaw), -0.2f, 0.2f);
    if (g_mrng.chance(1.0f / 400.0f)) {
        int snd = e.type == ET_PIG ? SFX_PIG : e.type == ET_COW ? SFX_COW : e.type == ET_SHEEP ? SFX_SHEEP : SFX_CHICKEN;
        g_events.sound(e.pos + vec3(0, e.height, 0), snd, 0.6f, g_mrng.range(0.9f, 1.1f));
    }
}

static float hdist(vec3 a, vec3 b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.z - b.z) * (a.z - b.z)); }

void EntityManager::aiShambler(Entity& e) {
    bool target = player && !player->dead && !player->flying && length(player->pos - e.pos) < 40;
    if (target) {
        e.ai = AI_CHASE;
        vec3 pp = player->pos;
        float d = hdist(pp, e.pos), dy = pp.y - e.pos.y;
        vec3 dd = pp - e.pos;
        e.headYaw = std::atan2(-dd.x, -dd.z);
        if (d < 2.5f && std::fabs(dy) < 1.6f && canSee(e, pp + vec3(0, 1.5f, 0))) moveToward(e, pp, 0.07f);
        else if (!followPath(e, pp, 0.07f)) moveToward(e, pp, 0.05f);
        if (d < 1.0f + e.halfW + 0.3f && std::fabs(dy) < 1.6f && e.attackCooldown <= 0) {
            player->damage(3, "was mauled by a Shambler", dd, 0.4f);
            e.attackCooldown = 20; e.attackAnim = 1;
        }
    } else {
        e.ai = AI_WANDER;
        if (--e.aiTimer <= 0) { e.hasMoveTarget = pickWander(*world, e, e.moveTarget, 10); e.aiTimer = g_mrng.range(100, 240); }
        if (e.hasMoveTarget) { if (hdist(e.moveTarget, e.pos) < 0.7f) e.hasMoveTarget = false; else moveToward(e, e.moveTarget, 0.035f); }
        e.headYaw += clampf(angleTo(e.headYaw, e.yaw), -0.2f, 0.2f);
    }
    if (g_mrng.chance(1.0f / 300.0f)) g_events.sound(e.pos + vec3(0, 1.6f, 0), SFX_SHAMBLER, 0.8f, g_mrng.range(0.85f, 1.1f));
}

void EntityManager::aiMarrowshot(Entity& e) {
    bool target = player && !player->dead && !player->flying && length(player->pos - e.pos) < 36;
    if (target) {
        vec3 pp = player->pos;
        vec3 dd = pp - e.pos;
        float d = hdist(pp, e.pos);
        e.headYaw = std::atan2(-dd.x, -dd.z);
        bool see = canSee(e, pp + vec3(0, 1.5f, 0));
        if (!see || d > 14) followPath(e, pp, 0.065f);
        else if (d < 6) { vec3 away = e.pos - normalize(vec3(dd.x, 0, dd.z)) * 3.0f; moveToward(e, away, 0.06f); e.yaw = e.headYaw; }
        else {
            // circle-strafe a little while shooting
            if (--e.aiTimer <= 0) { e.aiTimer = g_mrng.range(30, 70); e.extra ^= 1; }
            vec3 side = cross(normalize(vec3(dd.x, 0, dd.z)), vec3(0, 1, 0)) * (e.extra & 1 ? 1.0f : -1.0f);
            moveToward(e, e.pos + side * 2.0f, 0.03f);
            e.yaw += clampf(angleTo(e.yaw, e.headYaw), -0.3f, 0.3f);
        }
        if (see && d < 16 && e.attackCooldown <= 0) {
            vec3 eye = e.pos + vec3(0, 1.6f, 0);
            vec3 tgt = pp + vec3(0, 1.1f, 0);
            vec3 dv = tgt - eye;
            float h = std::sqrt(dv.x * dv.x + dv.z * dv.z);
            dv.y += h * 0.14f;
            vec3 dir = normalize(dv) + vec3(g_mrng.range(-0.04f, 0.04f), g_mrng.range(-0.03f, 0.03f), g_mrng.range(-0.04f, 0.04f));
            spawnArrow(eye + normalize(dir) * 0.6f, normalize(dir) * 1.6f, false, 2.0f, e.id);
            g_events.sound(eye, SFX_BOW, 0.8f, 0.9f);
            e.attackCooldown = g_mrng.range(35, 55);
            e.attackAnim = 1;
        }
    } else {
        if (--e.aiTimer <= 0) { e.hasMoveTarget = pickWander(*world, e, e.moveTarget, 10); e.aiTimer = g_mrng.range(100, 240); }
        if (e.hasMoveTarget) { if (hdist(e.moveTarget, e.pos) < 0.7f) e.hasMoveTarget = false; else moveToward(e, e.moveTarget, 0.035f); }
        e.headYaw += clampf(angleTo(e.headYaw, e.yaw), -0.2f, 0.2f);
    }
    if (g_mrng.chance(1.0f / 300.0f)) g_events.sound(e.pos + vec3(0, 1.6f, 0), SFX_MARROW, 0.7f, g_mrng.range(0.9f, 1.1f));
}

void EntityManager::aiFuseling(Entity& e) {
    bool target = player && !player->dead && !player->flying && length(player->pos - e.pos) < 24;
    if (target) {
        vec3 pp = player->pos;
        float d = length(pp - e.pos);
        vec3 dd = pp - e.pos;
        e.headYaw = std::atan2(-dd.x, -dd.z);
        bool see = canSee(e, pp + vec3(0, 1.5f, 0));
        if (d < 3.0f && see) {
            if (e.fuse == 0) g_events.sound(e.pos, SFX_FUSE, 1.0f);
            e.fuse++;
            e.yaw += clampf(angleTo(e.yaw, e.headYaw), -0.3f, 0.3f);
        } else {
            if (e.fuse > 0) e.fuse = std::max(0, e.fuse - (d > 7 ? 30 : 1));
            if (d < 2.5f && see) moveToward(e, pp, 0.06f);
            else if (!followPath(e, pp, 0.06f)) moveToward(e, pp, 0.04f);
        }
        if (e.fuse >= 30) {
            e.removed = true;
            explode(e.pos + vec3(0, e.height * 0.5f, 0), 3.0f);
        }
    } else {
        if (e.fuse > 0) e.fuse--;
        if (--e.aiTimer <= 0) { e.hasMoveTarget = pickWander(*world, e, e.moveTarget, 8); e.aiTimer = g_mrng.range(100, 240); }
        if (e.hasMoveTarget) { if (hdist(e.moveTarget, e.pos) < 0.7f) e.hasMoveTarget = false; else moveToward(e, e.moveTarget, 0.03f); }
        e.headYaw += clampf(angleTo(e.headYaw, e.yaw), -0.2f, 0.2f);
    }
}

void EntityManager::aiWeaver(Entity& e) {
    int light = world->getLightLevel(ifloor(e.pos.x), ifloor(e.pos.y + 0.5f), ifloor(e.pos.z), dayFactor);
    bool angry = e.hostileToPlayer || light < 9;
    if (isDay && e.hostileToPlayer && e.hurtTime == 0 && g_mrng.chance(1.0f / 600.0f)) e.hostileToPlayer = false;
    bool target = angry && player && !player->dead && !player->flying && length(player->pos - e.pos) < 24;
    if (target) {
        vec3 pp = player->pos;
        vec3 dd = pp - e.pos;
        float d = hdist(pp, e.pos), dy = pp.y - e.pos.y;
        e.headYaw = std::atan2(-dd.x, -dd.z);
        if (d < 3.5f && d > 1.6f && e.onGround && g_mrng.chance(0.1f)) {
            vec3 n = normalize(vec3(dd.x, 0, dd.z));
            e.vel.x = n.x * 0.45f; e.vel.z = n.z * 0.45f; e.vel.y = 0.38f;
        } else if (d < 3 && std::fabs(dy) < 2) moveToward(e, pp, 0.09f);
        else if (!followPath(e, pp, 0.09f)) moveToward(e, pp, 0.06f);
        if (d < 1.2f + e.halfW && std::fabs(dy) < 1.5f && e.attackCooldown <= 0) {
            player->damage(2, "was bitten by a Cragweaver", dd, 0.3f);
            e.attackCooldown = 20; e.attackAnim = 1;
        }
    } else {
        if (--e.aiTimer <= 0) { e.hasMoveTarget = pickWander(*world, e, e.moveTarget, 10); e.aiTimer = g_mrng.range(80, 200); }
        if (e.hasMoveTarget) { if (hdist(e.moveTarget, e.pos) < 0.8f) e.hasMoveTarget = false; else moveToward(e, e.moveTarget, 0.045f); }
        e.headYaw += clampf(angleTo(e.headYaw, e.yaw), -0.2f, 0.2f);
    }
    if (g_mrng.chance(1.0f / 350.0f)) g_events.sound(e.pos, SFX_WEAVER, 0.6f, g_mrng.range(0.9f, 1.1f));
}

void EntityManager::tickMob(Entity& e) {
    if (e.deathTime > 0) {
        e.deathTime++;
        physics(e, 0.08f, 0.98f);
        if (e.deathTime > 20) {
            e.removed = true;
            g_events.particles(e.pos + vec3(0, e.height * 0.5f, 0), PK_POOF, 12);
        }
        return;
    }
    if (e.hurtTime > 0) e.hurtTime--;
    if (e.invuln > 0) e.invuln--;
    if (e.attackCooldown > 0) e.attackCooldown--;
    if (e.jumpCooldown > 0) e.jumpCooldown--;
    if (e.attackAnim > 0) e.attackAnim = std::max(0.0f, e.attackAnim - 0.15f);
    // undead burn in sunlight
    if ((e.type == ET_SHAMBLER || e.type == ET_MARROWSHOT) && isDay && !e.inWater) {
        int hx = ifloor(e.pos.x), hy = ifloor(e.pos.y + e.height - 0.1f), hz = ifloor(e.pos.z);
        if (world->getSkyLight(hx, hy, hz) >= 15 && world->getBlock(hx, hy, hz) == AIR) e.fireTicks = std::max(e.fireTicks, 160);
    }
    if (e.inLava) { e.fireTicks = 300; hurt(&e, 4, vec3(0, 0, 0), false, 0); }
    if (e.fireTicks > 0) {
        if (e.inWater) e.fireTicks = 0;
        else {
            e.fireTicks--;
            if (e.fireTicks % 20 == 0) { e.invuln = 0; hurt(&e, 1, vec3(0, 0, 0), false, 0); }
            if (e.age % 3 == 0) g_events.particles(e.pos + vec3(g_mrng.range(-0.3f, 0.3f), g_mrng.range(0.f, e.height), g_mrng.range(-0.3f, 0.3f)), PK_FLAME, 1);
        }
        if (!e.alive()) return;
    }
    switch (e.type) {
        case ET_SHAMBLER: aiShambler(e); break;
        case ET_MARROWSHOT: aiMarrowshot(e); break;
        case ET_FUSELING: aiFuseling(e); break;
        case ET_CRAGWEAVER: aiWeaver(e); break;
        default: aiPassive(e); break;
    }
    if (e.removed) return;
    float gravity = (e.type == ET_CHICKEN && !e.onGround && e.vel.y < 0) ? 0.03f : 0.08f;
    vec3 before = e.pos;
    physics(e, gravity, 0.98f);
    if (e.type == ET_CHICKEN && e.vel.y < -0.12f) e.vel.y = -0.12f;
    if (e.type == ET_CRAGWEAVER && e.hitWall && (e.hostileToPlayer || e.ai != AI_IDLE)) e.vel.y = 0.2f;
    if (e.hitWall && e.onGround && e.jumpCooldown <= 0 && (std::fabs(e.vel.x) + std::fabs(e.vel.z)) > 0.001f) { e.vel.y = 0.42f; e.jumpCooldown = 8; }
    // push mobs apart
    for (auto& o : list) {
        if (o.get() == &e || !o->isMob() || o->removed) continue;
        vec3 d = e.pos - o->pos;
        float r = e.halfW + o->halfW;
        if (std::fabs(d.x) < r && std::fabs(d.z) < r && std::fabs(d.y) < 1.0f) {
            float l = std::max(0.05f, std::sqrt(d.x * d.x + d.z * d.z));
            e.vel.x += d.x / l * 0.02f; e.vel.z += d.z / l * 0.02f;
        }
    }
    float moved = hdist(e.pos, before);
    e.limbAmount += (std::min(1.0f, moved * 5.0f) - e.limbAmount) * 0.4f;
    e.limbSwing += moved * 3.2f;
}
