// Shared entity system: dropped items, arrows, falling blocks and every mob type.
#pragma once
#include <vector>
#include <memory>
#include "../core/math.h"
#include "../game/items.h"
#include "../world/worldgen.h"

class World;
class Player;
struct Chunk;
struct GeoBatch;
struct Camera;

enum AIState : uint8_t { AI_IDLE, AI_WANDER, AI_CHASE, AI_ATTACK, AI_FLEE };

struct Entity {
    uint32_t id = 0;
    uint8_t type = ET_NONE;
    vec3 pos, prevPos, vel;
    float yaw = 0, prevYaw = 0, pitch = 0, headYaw = 0, prevHeadYaw = 0;
    float halfW = 0.3f, height = 1.8f;
    bool onGround = false, inWater = false, inLava = false, hitWall = false, removed = false;
    int age = 0;
    float health = 10, maxHealth = 10;
    int hurtTime = 0, deathTime = 0, invuln = 0, fireTicks = 0;
    float fallDistance = 0;
    // animation
    float limbSwing = 0, limbAmount = 0, prevLimbAmount = 0;
    float attackAnim = 0;
    // AI
    uint8_t ai = AI_IDLE;
    int aiTimer = 0;
    vec3 moveTarget;
    bool hasMoveTarget = false;
    std::vector<ivec3> path;
    size_t pathIdx = 0;
    int repathTimer = 0;
    vec3 lastPathGoal;
    int attackCooldown = 0;
    bool hostileToPlayer = false;   // spiders when provoked
    int fuse = 0;                   // fuseling
    int jumpCooldown = 0;
    // type-specific data
    ItemStack item;                 // dropped item
    int pickupDelay = 0;
    uint16_t blockId = 0;           // falling block
    bool fromPlayer = false;        // arrow fired by the player (can be picked up)
    bool stuck = false;             // arrow in a block
    float damage = 2;
    uint32_t ownerId = 0;
    uint8_t extra = 0;              // sheep wool colour
    AABB box() const { return AABB(vec3(pos.x - halfW, pos.y, pos.z - halfW), vec3(pos.x + halfW, pos.y + height, pos.z + halfW)); }
    bool isMob() const { return type >= ET_PIG; }
    bool isHostile() const { return type >= ET_SHAMBLER; }
    bool alive() const { return !removed && deathTime == 0; }
};

class EntityManager {
public:
    std::vector<std::unique_ptr<Entity>> list;
    World* world = nullptr;
    Player* player = nullptr;
    float dayFactor = 1;       // set each tick by the game (sky brightness 0.2..1)
    bool isDay = true;
    bool mobSpawning = true;
    uint32_t nextId = 1;
    int pathBudget = 0;

    Entity* spawn(uint8_t type, vec3 pos);
    Entity* spawnItem(vec3 pos, const ItemStack& st, bool thrown = false, vec3 dir = vec3(0, 0, 0));
    Entity* spawnArrow(vec3 pos, vec3 vel, bool fromPlayer, float damage, uint32_t owner);
    void tick();
    void clear() { list.clear(); }
    int countType(bool hostile) const;
    // Saving with chunks
    void spawnFromChunk(Chunk* c);
    void collectForChunk(Chunk* c, bool remove);
    // Combat
    Entity* raycast(vec3 origin, vec3 dir, float maxDist, float& tHit);
    void hurt(Entity* e, float amount, vec3 knockDir, bool byPlayer, float knockback = 0.4f);
    void explode(vec3 pos, float power);
    void render(GeoBatch& b, const Camera& cam, float alpha);
    int hostilesNear(vec3 p, float r) const;
private:
    void tickItem(Entity& e);
    void tickArrow(Entity& e);
    void tickFalling(Entity& e);
    void tickMob(Entity& e);
    void aiPassive(Entity& e);
    void aiShambler(Entity& e);
    void aiMarrowshot(Entity& e);
    void aiFuseling(Entity& e);
    void aiWeaver(Entity& e);
    void moveToward(Entity& e, vec3 target, float speed);
    bool followPath(Entity& e, vec3 goal, float speed);
    void physics(Entity& e, float gravity, float drag);
    void die(Entity& e);
    void spawnMobs();
    bool canSee(const Entity& e, vec3 target) const;
};

// A* over walkable blocks (limited range and node count)
bool findPath(const World& w, ivec3 start, ivec3 goal, int height, bool climber, int maxNodes, std::vector<ivec3>& out);
// Mob models (box models with pixel-art skins)
void drawMobModel(GeoBatch& b, const Entity& e, vec3 camPos, float alpha, float light);
void drawPlayerArm(GeoBatch& b, const mat4& m, float light);
