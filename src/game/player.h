// The player: movement physics, mining/placing, combat, health, hunger, air and the inventory.
#pragma once
#include "../core/math.h"
#include "inventory.h"
#include "../world/world.h"
#include <string>
#include <vector>

class EntityManager;
struct Entity;

struct PlayerInput {
    float forward = 0, strafe = 0;
    bool jump = false, sneak = false, sprint = false;
    bool attackHeld = false, attackPressed = false;
    bool useHeld = false, usePressed = false;
    bool dropPressed = false, dropStack = false;
};

// What the player clicked on that needs a UI screen
enum Interaction { INTERACT_NONE, INTERACT_CRAFTING, INTERACT_FURNACE, INTERACT_CHEST, INTERACT_BED };

class Player {
public:
    vec3 pos, prevPos, vel;
    float yaw = 0, pitch = 0;
    static constexpr float HALF_W = 0.3f, HEIGHT = 1.8f, EYE = 1.62f, SNEAK_EYE = 1.5f;
    bool onGround = false, inWater = false, headInWater = false, inLava = false, onLadder = false;
    bool sneaking = false, sprinting = false, flying = false;
    bool hitWallLast = false;
    int jumpTicks = 0;
    float fallDistance = 0;
    // survival
    float health = 20, hunger = 20, saturation = 5, exhaustion = 0;
    int air = 300, foodTimer = 0, hurtTime = 0, invuln = 0, fireTicks = 0, deathTicks = 0;
    bool dead = false;
    std::string deathMessage;
    Inventory inv;
    ivec3 spawnPoint{0, 64, 0};
    bool bedSpawn = false;
    // mining
    bool mining = false;
    ivec3 miningBlock{0, -1, 0};
    float mineProgress = 0;
    int mineDelay = 0, digSoundTimer = 0;
    // arm swing / attack cooldown
    float swing = 0, prevSwing = 0;
    int ticksSinceAttack = 100;
    // using items (eating, bow)
    int useTicks = 0;
    bool usingItem = false;
    int placeDelay = 0;
    // view bobbing
    float walkDist = 0, prevWalkDist = 0, bobAmt = 0, prevBobAmt = 0;
    float eyeOffset = EYE, prevEyeOffset = EYE;
    float stepDist = 0;
    // sleeping
    bool sleeping = false;
    int sleepTicks = 0;
    ivec3 bedPos;
    // target (updated each frame)
    RayHit target;
    Entity* targetEntity = nullptr;
    Interaction pendingInteraction = INTERACT_NONE;
    ivec3 interactPos;
    std::string actionMessage;
    int actionMessageTicks = 0;

    void reset();
    AABB box() const { return AABB(vec3(pos.x - HALF_W, pos.y, pos.z - HALF_W), vec3(pos.x + HALF_W, pos.y + HEIGHT, pos.z + HALF_W)); }
    vec3 eyePos(float alpha) const;
    vec3 lookDir() const;
    void updateTarget(World& w, EntityManager& em, float alpha);
    void tick(World& w, EntityManager& em, const PlayerInput& in, bool isNight);
    void damage(float amount, const char* cause, vec3 knockDir = vec3(0, 0, 0), float knock = 0);
    void heal(float amount) { health = std::min(20.0f, health + amount); }
    void addExhaustion(float e) { exhaustion += e; }
    float attackStrength(float alpha = 0) const;
    void dropHeld(EntityManager& em, bool wholeStack);
    void dropAll(EntityManager& em);
    void say(const std::string& s) { actionMessage = s; actionMessageTicks = 60; }
    int facingFromYaw() const;   // 0=+Z,1=-X,2=-Z,3=+X; direction from a placed block toward the player
private:
    void move(World& w, const PlayerInput& in);
    void survivalTick(World& w);
    void miningTick(World& w, EntityManager& em, const PlayerInput& in);
    void useTick(World& w, EntityManager& em, const PlayerInput& in, bool isNight);
    bool placeBlock(World& w, EntityManager& em, uint16_t block, ivec3 at, ivec3 against, ivec3 normal);
    bool interactBlock(World& w, ivec3 p, bool isNight, EntityManager& em);
    void breakBlock(World& w, ivec3 p);
    void attack(EntityManager& em, Entity* e);
};

// Mining helpers shared with explosions etc.
bool canHarvest(uint16_t block, const ItemStack& tool);
float breakSpeed(uint16_t block, const ItemStack& tool);
void blockDrops(uint16_t block, uint8_t meta, const ItemStack& tool, Rng& r, std::vector<ItemStack>& out);
