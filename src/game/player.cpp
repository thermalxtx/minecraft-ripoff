#include "player.h"
#include "physics.h"
#include "crafting.h"
#include "../entity/entity.h"
#include "../core/events.h"
#include "../audio/audio.h"
#include <algorithm>

static Rng g_prng(20251125);

void Player::reset() {
    vel = vec3(0, 0, 0);
    health = 20; hunger = 20; saturation = 5; exhaustion = 0; air = 300;
    fireTicks = 0; deathTicks = 0; dead = false; fallDistance = 0; hurtTime = 0; invuln = 0;
    sleeping = false; usingItem = false; useTicks = 0; mining = false; mineProgress = 0;
}

vec3 Player::eyePos(float a) const { return lerp(prevPos, pos, a) + vec3(0, lerpf(prevEyeOffset, eyeOffset, a), 0); }
vec3 Player::lookDir() const {
    float cp = std::cos(pitch);
    return vec3(-std::sin(yaw) * cp, std::sin(pitch), -std::cos(yaw) * cp);
}
int Player::facingFromYaw() const {
    float tx = std::sin(yaw), tz = std::cos(yaw);
    if (std::fabs(tz) > std::fabs(tx)) return tz > 0 ? 0 : 2;
    return tx > 0 ? 3 : 1;
}
static int facingFromNormal(ivec3 n) {
    if (n.x > 0) return 3;
    if (n.x < 0) return 1;
    if (n.z > 0) return 0;
    return 2;
}

float Player::attackStrength(float alpha) const {
    const ItemStack& h = const_cast<Player*>(this)->inv.held();
    float cd = h.empty() ? 0.25f : itemDef(h.id).attackCooldown;
    if (h.empty() || itemDef(h.id).toolType == TOOL_NONE) cd = 0.25f;
    return clampf((ticksSinceAttack + alpha) / (cd * 20.0f), 0, 1);
}

void Player::updateTarget(World& w, EntityManager& em, float alpha) {
    vec3 eye = eyePos(alpha), dir = lookDir();
    target = w.raycast(eye, dir, 5.0f);
    float te = 0;
    Entity* e = em.raycast(eye, dir, 3.5f, te);
    targetEntity = (e && (!target.hit || te < target.dist)) ? e : nullptr;
}

// ---------------- harvesting ----------------
bool canHarvest(uint16_t block, const ItemStack& tool) {
    const BlockDef& d = blockDef(block);
    if (d.harvestLevel < 0) return true;
    if (tool.empty()) return false;
    const ItemDef& t = itemDef(tool.id);
    return t.toolType == TOOL_PICKAXE && t.toolTier >= d.harvestLevel;
}
float breakSpeed(uint16_t block, const ItemStack& tool) {
    if (tool.empty()) return 1;
    const ItemDef& t = itemDef(tool.id);
    const BlockDef& d = blockDef(block);
    if (t.toolType != TOOL_NONE && t.toolType == d.tool) return t.toolSpeed;
    if (t.toolType == TOOL_SWORD && (d.layer == LAYER_CUTOUT || block == CACTUS)) return 1.5f;
    return 1;
}
void blockDrops(uint16_t block, uint8_t meta, const ItemStack& tool, Rng& r, std::vector<ItemStack>& out) {
    (void)tool;
    switch (block) {
        case OAK_LEAVES: case BIRCH_LEAVES: case SPRUCE_LEAVES:
            if (r.chance(0.06f)) out.push_back(ItemStack(block == OAK_LEAVES ? OAK_SAPLING : (block == BIRCH_LEAVES ? BIRCH_SAPLING : SPRUCE_SAPLING), 1));
            if (block == OAK_LEAVES && r.chance(0.025f)) out.push_back(ItemStack(APPLE, 1));
            return;
        case TALL_GRASS: if (r.chance(0.15f)) out.push_back(ItemStack(SEEDS, 1)); return;
        case GRAVEL: out.push_back(r.chance(0.1f) ? ItemStack(FLINT, 1) : ItemStack(GRAVEL, 1)); return;
        case WHEAT:
            if (meta >= 7) { out.push_back(ItemStack(WHEAT_ITEM, 1)); out.push_back(ItemStack(SEEDS, r.range(1, 3))); }
            else out.push_back(ItemStack(SEEDS, 1));
            return;
        default: break;
    }
    const BlockDef& d = blockDef(block);
    if (d.drop == 0) return;
    uint16_t id = d.drop == 0xFFFF ? block : d.drop;
    int n = r.range(d.dropMin, d.dropMax);
    if (n > 0) out.push_back(ItemStack(id, n));
}

// ---------------- damage ----------------
void Player::damage(float amount, const char* cause, vec3 knockDir, float knock) {
    if (dead || amount <= 0) return;
    if (flying) return;  // debug fly mode is invulnerable
    if (invuln > 0) return;
    health -= amount;
    hurtTime = 10; invuln = 10;
    addExhaustion(0.1f);
    if (knock > 0) { vec3 k = normalize(vec3(knockDir.x, 0, knockDir.z)) * knock; vel.x += k.x; vel.z += k.z; vel.y = std::max(vel.y, 0.3f); }
    if (sleeping) sleeping = false;
    if (health <= 0) {
        health = 0; dead = true; deathTicks = 0;
        deathMessage = cause;
        g_events.uiSound(SFX_DEATH);
    } else g_events.uiSound(SFX_HURT, 1.0f, g_prng.range(0.9f, 1.1f));
}

void Player::dropHeld(EntityManager& em, bool wholeStack) {
    ItemStack& h = inv.held();
    if (h.empty()) return;
    ItemStack d = h; d.count = wholeStack ? h.count : 1;
    h.count = (uint8_t)(h.count - d.count);
    if (h.count == 0) h.clear();
    em.spawnItem(eyePos(1) - vec3(0, 0.3f, 0), d, true, lookDir());
    swing = swing > 0 ? swing : 0.01f;
}

void Player::dropAll(EntityManager& em) {
    for (auto& s : inv.slots) {
        if (s.empty()) continue;
        Entity* e = em.spawnItem(pos + vec3(0, 1, 0), s);
        if (e) { e->vel = vec3(g_prng.range(-0.25f, 0.25f), g_prng.range(0.1f, 0.35f), g_prng.range(-0.25f, 0.25f)); e->pickupDelay = 40; }
        s.clear();
    }
}

// ---------------- tick ----------------
void Player::tick(World& w, EntityManager& em, const PlayerInput& in, bool isNight) {
    prevPos = pos; prevSwing = swing; prevWalkDist = walkDist; prevBobAmt = bobAmt; prevEyeOffset = eyeOffset;
    if (actionMessageTicks > 0) actionMessageTicks--;
    if (dead) { deathTicks++; return; }
    if (sleeping) {
        sleepTicks++;
        vel = vec3(0, 0, 0);
        if (w.getBlock(bedPos.x, bedPos.y, bedPos.z) != BED) sleeping = false;
        return;
    }
    if (hurtTime > 0) hurtTime--;
    if (invuln > 0) invuln--;
    move(w, in);
    survivalTick(w);
    if (dead) return;
    miningTick(w, em, in);
    useTick(w, em, in, isNight);
    if (in.dropPressed) dropHeld(em, in.dropStack);
    if (swing > 0) { swing += 1.0f / 6.0f; if (swing >= 1) swing = 0; }
    ticksSinceAttack++;
}

void Player::move(World& w, const PlayerInput& in) {
    sneaking = in.sneak && !flying;
    float eyeTarget = sneaking ? SNEAK_EYE : EYE;
    eyeOffset += (eyeTarget - eyeOffset) * 0.5f;
    AABB b = box();
    AABB inner(b.mn + vec3(0.001f, 0.4f, 0.001f), b.mx - vec3(0.001f, 0.4f, 0.001f));
    inWater = w.isAreaLiquid(inner, WATER);
    inLava = w.isAreaLiquid(inner, LAVA);
    vec3 eye = pos + vec3(0, eyeOffset, 0);
    headInWater = w.isAreaLiquid(AABB(eye - vec3(0.01f), eye + vec3(0.01f)), WATER);
    uint16_t feet = w.getBlock(ifloor(pos.x), ifloor(pos.y), ifloor(pos.z));
    uint16_t body = w.getBlock(ifloor(pos.x), ifloor(pos.y + 0.6f), ifloor(pos.z));
    onLadder = blockDef(feet).climbable || blockDef(body).climbable;

    float fw = in.forward, st = in.strafe;
    float len = std::sqrt(fw * fw + st * st);
    if (len > 1) { fw /= len; st /= len; }
    if (in.sprint && fw > 0 && hunger > 6 && !sneaking && !usingItem) sprinting = true;
    if (fw <= 0 || hunger <= 6 || sneaking || hitWallLast) sprinting = false;
    vec3 f(-std::sin(yaw), 0, -std::cos(yaw)), r(std::cos(yaw), 0, -std::sin(yaw));
    vec3 wish = f * fw + r * st;
    float useSlow = usingItem ? 0.2f : 1.0f;
    wish *= useSlow;

    auto doMove = [&]() {
        vec3 d = vel;
        // sneaking stops you from walking off edges
        if (sneaking && onGround && !flying) {
            auto supported = [&](float dx, float dz) { return boxCollides(w, box().offset(vec3(dx, -0.6f, dz)).grow(-0.001f)); };
            const float s = 0.05f;
            while (d.x != 0 && !supported(d.x, 0)) { if (std::fabs(d.x) < s) d.x = 0; else d.x -= d.x > 0 ? s : -s; }
            while (d.z != 0 && !supported(0, d.z)) { if (std::fabs(d.z) < s) d.z = 0; else d.z -= d.z > 0 ? s : -s; }
            while (d.x != 0 && d.z != 0 && !supported(d.x, d.z)) {
                if (std::fabs(d.x) < s) d.x = 0; else d.x -= d.x > 0 ? s : -s;
                if (std::fabs(d.z) < s) d.z = 0; else d.z -= d.z > 0 ? s : -s;
            }
            vel.x = d.x; vel.z = d.z;
        }
        vec3 before = pos;
        vec3 wanted = d;
        MoveResult mr = moveBox(w, pos, HALF_W, HEIGHT, d, flying ? 0.0f : 0.6f);
        if (mr.hitX) vel.x = 0;
        if (mr.hitZ) vel.z = 0;
        if (mr.hitY) vel.y = 0;
        hitWallLast = mr.hitX || mr.hitZ;
        onGround = mr.onGround && wanted.y <= 0;
        float dy = pos.y - before.y;
        if (!onGround && dy < 0) fallDistance -= dy;
        if (inWater || onLadder || flying) fallDistance = 0;
        if (onGround) {
            if (fallDistance > 3.0f && !inWater) {
                float dmg = std::ceil(fallDistance - 3.0f);
                damage(dmg, "fell from a high place");
                g_events.sound(pos, SFX_FALL_BIG, std::min(1.0f, dmg / 4));
            }
            fallDistance = 0;
        }
        float hd = std::sqrt((pos.x - before.x) * (pos.x - before.x) + (pos.z - before.z) * (pos.z - before.z));
        if (onGround || inWater) {
            walkDist += hd * 0.6f;
            stepDist += hd;
            float stepLen = sprinting ? 2.0f : 1.6f;
            if (stepDist > stepLen && !flying) {
                stepDist = 0;
                if (inWater) g_events.sound(pos, SFX_SWIM, 0.3f, g_prng.range(0.8f, 1.2f));
                else {
                    uint16_t under = w.getBlock(ifloor(pos.x), ifloor(pos.y - 0.2f), ifloor(pos.z));
                    if (under != AIR && !sneaking) g_events.sound(pos, SFX_STEP_STONE + blockDef(under).sound, 0.35f, g_prng.range(0.9f, 1.1f));
                }
            }
            if (sprinting) addExhaustion(hd * 0.1f);
            if (inWater) addExhaustion(hd * 0.01f);
        }
        float target = onGround && !flying ? std::min(1.0f, hd * 4.0f) : 0.0f;
        bobAmt += (target - bobAmt) * 0.4f;
    };

    if (flying) {
        float sp = sprinting || in.sprint ? 1.6f : 0.7f;
        vel = wish * sp;
        vel.y = (in.jump ? 0.6f : 0) - (in.sneak ? 0.6f : 0);
        doMove();
        vel = vec3(0, 0, 0);
        return;
    }
    if (inWater || inLava) {
        float accel = inWater ? (sprinting ? 0.03f : 0.02f) : 0.02f;
        vel += wish * accel;
        if (in.jump) vel.y += 0.04f;
        if (sneaking) vel.y -= 0.02f;
        doMove();
        float drag = inWater ? 0.8f : 0.5f;
        vel *= drag;
        vel.y -= 0.02f;
        if (hitWallLast && in.jump) vel.y = 0.3f;
        if (inWater && !headInWater && vel.y > 0 && in.jump) vel.y = std::min(vel.y, 0.12f);
        return;
    }
    float speed = onGround ? 0.1f * (sprinting ? 1.3f : 1.0f) * (sneaking ? 0.3f : 1.0f) : 0.02f * (sprinting ? 1.3f : 1.0f);
    vel += wish * speed;
    if (in.jump && onGround && jumpTicks == 0) {
        vel.y = 0.42f;
        if (sprinting) vel += f * 0.2f;
        addExhaustion(sprinting ? 0.2f : 0.05f);
        jumpTicks = 10;
    }
    if (!in.jump) jumpTicks = 0; else if (jumpTicks > 0 && onGround) jumpTicks--;
    if (onLadder) {
        vel.x = clampf(vel.x, -0.15f, 0.15f); vel.z = clampf(vel.z, -0.15f, 0.15f);
        if (vel.y < -0.15f) vel.y = -0.15f;
        if (sneaking && vel.y < 0) vel.y = 0;
    }
    doMove();
    if (onLadder && (hitWallLast || in.jump)) vel.y = 0.2f;
    vel.y = (vel.y - 0.08f) * 0.98f;
    float fr = onGround ? 0.546f : 0.91f;
    vel.x *= fr; vel.z *= fr;
}

void Player::survivalTick(World& w) {
    if (exhaustion >= 4) {
        exhaustion -= 4;
        if (saturation > 0) saturation = std::max(0.0f, saturation - 1);
        else hunger = std::max(0.0f, hunger - 1);
    }
    foodTimer++;
    if (hunger >= 18 && health < 20) {
        if (foodTimer >= 80) { heal(1); addExhaustion(3.0f); foodTimer = 0; }
    } else if (hunger <= 0) {
        if (foodTimer >= 80) { if (health > 1) { invuln = 0; damage(1, "starved to death"); } foodTimer = 0; }
    } else foodTimer = 0;
    // air
    if (headInWater && !flying) {
        air--;
        if (air <= -20) { air = 0; invuln = 0; damage(2, "drowned"); }
    } else air = std::min(300, air + 5);
    // lava & fire
    if (inLava) { fireTicks = 300; if (invuln == 0) damage(4, "tried to swim in lava"); }
    if (fireTicks > 0) {
        if (inWater) fireTicks = 0;
        else { fireTicks--; if (fireTicks % 20 == 0 && !inLava) { invuln = 0; damage(1, "burned to death"); } }
    }
    // cactus
    AABB b = box().grow(0.02f);
    for (int x = ifloor(b.mn.x); x <= ifloor(b.mx.x); x++)
        for (int y = ifloor(b.mn.y); y <= ifloor(b.mx.y); y++)
            for (int z = ifloor(b.mn.z); z <= ifloor(b.mx.z); z++)
                if (w.getBlock(x, y, z) == CACTUS) {
                    AABB cb(vec3(x + 1 / 16.f, (float)y, z + 1 / 16.f), vec3(x + 15 / 16.f, (float)y + 1, z + 15 / 16.f));
                    if (cb.intersects(b)) damage(1, "was pricked to death");
                }
    if (pos.y < -40) { invuln = 0; damage(4, "fell out of the world"); }
}

void Player::breakBlock(World& w, ivec3 p) {
    uint16_t id = w.getBlock(p.x, p.y, p.z);
    uint8_t meta = w.getMeta(p.x, p.y, p.z);
    ItemStack& held = inv.held();
    vec3 c(p.x + 0.5f, p.y + 0.5f, p.z + 0.5f);
    // two-block structures
    if (id == DOOR) {
        int oy = (meta & 8) ? -1 : 1;
        if (w.getBlock(p.x, p.y + oy, p.z) == DOOR) w.setBlock(p.x, p.y + oy, p.z, AIR, 0, false);
        meta &= ~8;
    } else if (id == BED) {
        int f = meta & 3; bool head = meta & 4;
        int ox = head ? -facingDX(f) : facingDX(f), oz = head ? -facingDZ(f) : facingDZ(f);
        if (w.getBlock(p.x + ox, p.y, p.z + oz) == BED) w.setBlock(p.x + ox, p.y, p.z + oz, AIR, 0, false);
    }
    if (canHarvest(id, held)) {
        std::vector<ItemStack> drops;
        blockDrops(id, meta, held, g_prng, drops);
        for (auto& d : drops) g_events.drop(c, d.id, d.count, d.damage);
    }
    if (TileEntity* t = w.getTile(p.x, p.y, p.z)) {
        int n = t->type == TILE_CHEST ? 27 : 3;
        for (int i = 0; i < n; i++) if (!t->slots[i].empty()) g_events.drop(c, t->slots[i].id, t->slots[i].count, t->slots[i].damage);
    }
    GameEvent e{EV_BLOCK_BROKEN, c};
    e.a = id; e.b = meta; e.c = 1;
    g_events.post(e);
    w.setBlock(p.x, p.y, p.z, AIR);
    addExhaustion(0.005f);
    const ItemDef& td = itemDef(held.id);
    if (!held.empty() && td.maxDamage > 0 && blockDef(id).hardness > 0) {
        held.damage++;
        if (held.damage >= td.maxDamage) { held.clear(); g_events.uiSound(SFX_TOOL_BREAK); say("Your tool broke!"); }
    }
}

void Player::attack(EntityManager& em, Entity* e) {
    ItemStack& held = inv.held();
    const ItemDef& d = itemDef(held.id);
    float strength = attackStrength();
    float dmg = (held.empty() || d.toolType == TOOL_NONE) ? 1.0f : d.attackDamage;
    dmg *= 0.2f + 0.8f * strength * strength;
    bool crit = strength > 0.9f && fallDistance > 0 && !onGround && !inWater && !onLadder && vel.y < 0;
    if (crit) {
        dmg *= 1.5f;
        g_events.particles(e->pos + vec3(0, e->height * 0.7f, 0), PK_CRIT, 12);
        g_events.sound(e->pos, SFX_CRIT, 0.8f);
    } else g_events.sound(e->pos, SFX_ATTACK, 0.5f + 0.5f * strength);
    float kb = 0.35f + (sprinting && strength > 0.9f ? 0.5f : 0.0f);
    em.hurt(e, dmg, lookDir(), true, kb * (0.4f + 0.6f * strength));
    if (sprinting && strength > 0.9f) { sprinting = false; vel.x *= 0.6f; vel.z *= 0.6f; }
    if (!held.empty() && d.maxDamage > 0 && d.toolType != TOOL_NONE) {
        held.damage += d.toolType == TOOL_SWORD ? 1 : 2;
        if (held.damage >= d.maxDamage) { held.clear(); g_events.uiSound(SFX_TOOL_BREAK); say("Your tool broke!"); }
    }
    ticksSinceAttack = 0;
    addExhaustion(0.1f);
}

void Player::miningTick(World& w, EntityManager& em, const PlayerInput& in) {
    if (mineDelay > 0) mineDelay--;
    if (in.attackPressed) {
        swing = 0.01f;
        if (targetEntity && targetEntity->alive()) { attack(em, targetEntity); mining = false; mineProgress = 0; return; }
        if (!target.hit) ticksSinceAttack = 0;  // swinging at air resets the charge
    }
    if (in.attackHeld && target.hit && !targetEntity && !usingItem) {
        if (!mining || target.block != miningBlock) { mining = true; miningBlock = target.block; mineProgress = 0; digSoundTimer = 0; }
        uint16_t id = w.getBlock(miningBlock.x, miningBlock.y, miningBlock.z);
        const BlockDef& d = blockDef(id);
        if (id == AIR || d.hardness < 0 || mineDelay > 0) return;
        if (swing == 0) swing = 0.01f;
        ItemStack& held = inv.held();
        if (d.hardness == 0) mineProgress = 1;
        else {
            float sp = breakSpeed(id, held) / d.hardness / (canHarvest(id, held) ? 30.0f : 100.0f);
            if (headInWater) sp *= 0.2f;
            if (!onGround && !flying && !onLadder) sp *= 0.2f;
            mineProgress += sp;
        }
        if (++digSoundTimer % 4 == 1) {
            vec3 c(miningBlock.x + 0.5f, miningBlock.y + 0.5f, miningBlock.z + 0.5f);
            g_events.sound(c, SFX_DIG_STONE + d.sound, 0.3f, 0.7f);
            g_events.particles(c + vec3(target.normal.x * 0.55f, target.normal.y * 0.55f, target.normal.z * 0.55f), PK_BLOCK, 2, id);
        }
        if (mineProgress >= 1.0f) {
            breakBlock(w, miningBlock);
            mining = false; mineProgress = 0; mineDelay = 5;
        }
    } else { mining = false; mineProgress = 0; }
}

bool Player::interactBlock(World& w, ivec3 p, bool isNight, EntityManager& em) {
    uint16_t id = w.getBlock(p.x, p.y, p.z);
    uint8_t meta = w.getMeta(p.x, p.y, p.z);
    vec3 c(p.x + 0.5f, p.y + 0.5f, p.z + 0.5f);
    switch (id) {
    case CRAFTING_TABLE: pendingInteraction = INTERACT_CRAFTING; interactPos = p; return true;
    case FURNACE: case FURNACE_LIT:
        if (!w.getTile(p.x, p.y, p.z)) w.createTile(p.x, p.y, p.z, TILE_FURNACE);
        pendingInteraction = INTERACT_FURNACE; interactPos = p; return true;
    case CHEST:
        if (!w.getTile(p.x, p.y, p.z)) w.createTile(p.x, p.y, p.z, TILE_CHEST);
        pendingInteraction = INTERACT_CHEST; interactPos = p;
        g_events.sound(c, SFX_CHEST_OPEN, 0.6f);
        return true;
    case DOOR: {
        int by = (meta & 8) ? p.y - 1 : p.y;
        uint8_t bm = w.getMeta(p.x, by, p.z) ^ 4;
        w.setMeta(p.x, by, p.z, bm);
        if (w.getBlock(p.x, by + 1, p.z) == DOOR) w.setMeta(p.x, by + 1, p.z, (uint8_t)((bm & 7) | 8));
        g_events.sound(c, (bm & 4) ? SFX_DOOR_OPEN : SFX_DOOR_CLOSE, 0.7f);
        return true;
    }
    case BED: {
        ivec3 head = p;
        if (!(meta & 4)) head = ivec3(p.x + facingDX(meta & 3), p.y, p.z + facingDZ(meta & 3));
        spawnPoint = ivec3(head.x, head.y, head.z);
        bedSpawn = true;
        if (!isNight) { say("You can only sleep at night (respawn point set)"); return true; }
        if (em.hostilesNear(pos, 8) > 0) { say("You may not rest now, there are monsters nearby"); return true; }
        sleeping = true; sleepTicks = 0; bedPos = p;
        pos = vec3(head.x + 0.5f, head.y + 0.6f, head.z + 0.5f); prevPos = pos; vel = vec3(0, 0, 0);
        pendingInteraction = INTERACT_BED;
        say("Respawn point set");
        return true;
    }
    default: return false;
    }
}

bool Player::placeBlock(World& w, EntityManager& em, uint16_t block, ivec3 at, ivec3 against, ivec3 normal) {
    (void)against;
    if (at.y < 0 || at.y >= CHUNK_H) return false;
    if (!blockDef(w.getBlock(at.x, at.y, at.z)).replaceable) return false;
    uint8_t meta = 0;
    switch (block) {
    case TORCH:
        if (normal.y < 0) return false;
        if (normal.y > 0) meta = 0;
        else { meta = (uint8_t)(facingFromNormal(normal) + 1); if (!blockDef(w.getBlock(at.x - normal.x, at.y, at.z - normal.z)).opaque) return false; }
        if (meta == 0) { uint16_t b = w.getBlock(at.x, at.y - 1, at.z); if (!blockDef(b).opaque && b != FARMLAND && b != GLASS) return false; }
        break;
    case LADDER:
        if (normal.y != 0) return false;
        meta = (uint8_t)facingFromNormal(normal);
        if (!blockDef(w.getBlock(at.x - normal.x, at.y, at.z - normal.z)).opaque) return false;
        break;
    case FURNACE: case CHEST: meta = (uint8_t)facingFromYaw(); break;
    case DOOR: {
        if (at.y + 1 >= CHUNK_H || !blockDef(w.getBlock(at.x, at.y + 1, at.z)).replaceable) return false;
        if (!blockDef(w.getBlock(at.x, at.y - 1, at.z)).opaque) return false;
        int f = facingFromYaw();
        AABB pb = box();
        if (pb.intersects(AABB(vec3((float)at.x, (float)at.y, (float)at.z), vec3(at.x + 1.f, at.y + 2.f, at.z + 1.f)))) return false;
        w.setBlock(at.x, at.y, at.z, DOOR, (uint8_t)f, false);
        w.setBlock(at.x, at.y + 1, at.z, DOOR, (uint8_t)(f | 8), false);
        g_events.sound(vec3(at.x + 0.5f, at.y + 0.5f, at.z + 0.5f), SFX_DIG_WOOD, 0.8f, 0.8f);
        return true;
    }
    case BED: {
        int f = (facingFromYaw() + 2) & 3;  // head points away from the player
        ivec3 head(at.x + facingDX(f), at.y, at.z + facingDZ(f));
        if (!blockDef(w.getBlock(head.x, head.y, head.z)).replaceable) return false;
        if (!blockDef(w.getBlock(at.x, at.y - 1, at.z)).solid || !blockDef(w.getBlock(head.x, head.y - 1, head.z)).solid) return false;
        w.setBlock(at.x, at.y, at.z, BED, (uint8_t)f, false);
        w.setBlock(head.x, head.y, head.z, BED, (uint8_t)(f | 4), false);
        g_events.sound(vec3(at.x + 0.5f, at.y + 0.5f, at.z + 0.5f), SFX_DIG_WOOL, 0.8f, 0.8f);
        return true;
    }
    default:
        if (!w.canPlaceAt(block, at.x, at.y, at.z)) return false;
        break;
    }
    if (blockDef(block).solid) {
        AABB bb(vec3((float)at.x, (float)at.y, (float)at.z), vec3(at.x + 1.f, at.y + 1.f, at.z + 1.f));
        if (bb.intersects(box()) && !flying) return false;
        for (auto& e : em.list) if (e->isMob() && e->alive() && e->box().intersects(bb)) return false;
    }
    w.setBlock(at.x, at.y, at.z, block, meta);
    if (block == CHEST) w.createTile(at.x, at.y, at.z, TILE_CHEST);
    if (block == FURNACE) w.createTile(at.x, at.y, at.z, TILE_FURNACE);
    GameEvent e{EV_BLOCK_PLACED, vec3(at.x + 0.5f, at.y + 0.5f, at.z + 0.5f)};
    e.a = block;
    g_events.post(e);
    return true;
}

void Player::useTick(World& w, EntityManager& em, const PlayerInput& in, bool isNight) {
    if (placeDelay > 0) placeDelay--;
    ItemStack& held = inv.held();
    const ItemDef& d = itemDef(held.id);
    if (usingItem) {
        if (held.empty() || (d.foodHunger == 0 && held.id != BOW)) { usingItem = false; useTicks = 0; return; }
        if (!in.useHeld) {
            if (held.id == BOW && useTicks >= 3) {
                float p = std::min(1.0f, useTicks / 20.0f);
                p = (p * p + p * 2) / 3;
                if (inv.count(ARROW) > 0 || flying) {
                    vec3 dir = lookDir();
                    vec3 spawnPos = eyePos(1) + dir * 0.3f - vec3(0, 0.1f, 0);
                    float dmg = 2.0f + p * 4.0f;
                    em.spawnArrow(spawnPos, dir * (p * 3.0f) + vel * 0.5f, true, p >= 1.0f ? dmg + 1 : dmg, 0);
                    if (!flying) inv.removeItem(ARROW, 1);
                    held.damage++;
                    if (held.damage >= d.maxDamage) { held.clear(); g_events.uiSound(SFX_TOOL_BREAK); }
                    g_events.sound(pos, SFX_BOW, 0.8f, 1.0f + p * 0.2f);
                }
            }
            usingItem = false; useTicks = 0;
            return;
        }
        useTicks++;
        if (d.foodHunger > 0) {
            if (useTicks % 4 == 0 && useTicks > 6) {
                g_events.sound(pos, SFX_EAT, 0.5f, g_prng.range(0.8f, 1.2f));
                g_events.particles(eyePos(1) + lookDir() * 0.4f - vec3(0, 0.15f, 0), PK_BLOCK, 3, -(int)d.icon - 1);
            }
            if (useTicks >= 32) {
                hunger = std::min(20.0f, hunger + d.foodHunger);
                saturation = std::min(hunger, saturation + d.foodSaturation);
                held.count--;
                if (held.count == 0) held.clear();
                g_events.sound(pos, SFX_BURP, 0.5f, g_prng.range(0.9f, 1.1f));
                usingItem = false; useTicks = 0;
            }
        }
        return;
    }
    if (!(in.usePressed || (in.useHeld && placeDelay == 0))) return;
    placeDelay = 4;
    if (target.hit && !sneaking && in.usePressed && !targetEntity) {
        if (interactBlock(w, target.block, isNight, em)) { swing = 0.01f; return; }
    }
    if (held.empty()) return;
    if (d.foodHunger > 0 && in.usePressed) {
        if (hunger < 20) { usingItem = true; useTicks = 0; }
        return;
    }
    if (held.id == BOW && in.usePressed) {
        if (inv.count(ARROW) > 0 || flying) { usingItem = true; useTicks = 0; }
        return;
    }
    if (!target.hit || targetEntity) return;
    ivec3 tb = target.block;
    uint16_t tid = w.getBlock(tb.x, tb.y, tb.z);
    if (d.toolType == TOOL_HOE && (tid == GRASS || tid == DIRT || tid == SNOWY_GRASS) && target.normal.y >= 0 &&
        w.getBlock(tb.x, tb.y + 1, tb.z) == AIR) {
        w.setBlock(tb.x, tb.y, tb.z, FARMLAND);
        g_events.sound(vec3(tb.x + 0.5f, tb.y + 1.0f, tb.z + 0.5f), SFX_STEP_GRAVEL, 0.8f);
        held.damage++;
        if (held.damage >= d.maxDamage) { held.clear(); g_events.uiSound(SFX_TOOL_BREAK); }
        swing = 0.01f;
        return;
    }
    if (d.placesBlock) {
        ivec3 at = blockDef(tid).replaceable && tid != WATER && tid != LAVA ? tb : tb + target.normal;
        ivec3 n = target.normal;
        if (at == tb) n = ivec3(0, 1, 0);
        if (placeBlock(w, em, d.placesBlock, at, tb, n)) {
            held.count--;
            if (held.count == 0) held.clear();
            swing = 0.01f;
        }
    }
}
