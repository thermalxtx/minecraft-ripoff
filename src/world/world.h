// The World owns all chunks, lighting, block updates and chunk streaming.
#pragma once
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <deque>
#include <map>
#include <mutex>
#include <memory>
#include <string>
#include "chunk.h"
#include "../core/math.h"

class JobSystem;
class RegionStore;
struct TerrainGen;

struct RayHit {
    bool hit = false;
    ivec3 block;      // block that was hit
    ivec3 normal;     // face normal (place position = block + normal)
    float dist = 0;
};

struct MeshSnapshot;
struct MeshResult;

class World {
public:
    uint64_t seed = 0;
    std::string saveDir;            // folder of this world (with trailing slash)
    std::unordered_map<int64_t, Chunk*> chunks;
    int64_t tickCount = 0;
    int renderDistance = 10;
    bool unloading = false;

    World();
    ~World();
    void init(uint64_t seed, const std::string& dir, JobSystem* jobs);
    void shutdown();   // waits for jobs, saves and frees everything

    // ---- block access (world coordinates) ----
    Chunk* getChunk(int cx, int cz) const {
        auto it = chunks.find(chunkKey(cx, cz));
        return it == chunks.end() ? nullptr : it->second;
    }
    Chunk* chunkAt(int x, int z) const { return getChunk(x >> 4, z >> 4); }
    bool isLoaded(int x, int z) const { return chunkAt(x, z) != nullptr; }
    uint16_t getBlock(int x, int y, int z) const {
        if (y < 0 || y >= CHUNK_H) return AIR;
        Chunk* c = chunkAt(x, z);
        return c ? c->blocks[Chunk::idx(x & 15, y, z & 15)] : AIR;
    }
    uint8_t getMeta(int x, int y, int z) const {
        if (y < 0 || y >= CHUNK_H) return 0;
        Chunk* c = chunkAt(x, z);
        return c ? c->meta[Chunk::idx(x & 15, y, z & 15)] : 0;
    }
    int getSkyLight(int x, int y, int z) const;
    int getBlockLight(int x, int y, int z) const;
    // 0..15 brightness with the day factor applied to sky light
    int getLightLevel(int x, int y, int z, float dayFactor) const;
    int surfaceHeight(int x, int z) const;  // y of the first light-blocking block from the top + 1
    // Place a block: updates light, marks meshes dirty, triggers neighbour updates
    void setBlock(int x, int y, int z, uint16_t id, uint8_t meta = 0, bool notify = true);
    void setMeta(int x, int y, int z, uint8_t meta);
    TileEntity* getTile(int x, int y, int z);
    TileEntity* createTile(int x, int y, int z, uint8_t type);
    void removeTile(int x, int y, int z);

    // ---- queries ----
    RayHit raycast(vec3 origin, vec3 dir, float maxDist, bool hitLiquids = false) const;
    void getCollisionBoxes(const AABB& area, std::vector<AABB>& out) const;
    static void blockBoxes(uint16_t id, uint8_t meta, std::vector<AABB>& out);  // in block-local space
    static bool selectionBox(uint16_t id, uint8_t meta, AABB& box);              // in block-local space
    bool isAreaLiquid(const AABB& box, uint16_t liquid) const;

    // ---- streaming (main thread, every frame) ----
    void updateStreaming(vec3 playerPos);
    void processJobResults(int maxGen, int maxMesh);
    void scheduleMeshes(vec3 camPos);
    void meshSectionNow(Chunk* c, int sy);  // synchronous remesh for player edits
    int loadedChunkCount() const { return (int)chunks.size(); }
    int pendingJobs() const;
    bool isChunkReady(int cx, int cz) const;
    bool spawnAreaReady(int x, int z, int radius) const;
    void saveAll();   // writes modified chunks to region files

    // ---- simulation (20 ticks per second) ----
    void tick(vec3 playerPos);
    void scheduleUpdate(int x, int y, int z, int delay);
    void neighborChanged(int x, int y, int z);
    bool canPlaceAt(uint16_t id, int x, int y, int z) const;
    void growTree(int x, int y, int z, int type);   // used by saplings

    // callbacks into other systems (set by Game) for chunk load/unload of entities
    std::vector<std::pair<int64_t, Chunk*>> newlyReadyChunks;
    void (*onChunkUnload)(void* user, Chunk* c) = nullptr;  // move entities into c->savedEntities
    void (*onChunkSave)(void* user, Chunk* c) = nullptr;    // copy entities into c->savedEntities
    void* callbackUser = nullptr;

    // GPU cleanup hook (set by renderer)
    void (*freeChunkGPU)(Chunk* c) = nullptr;
    void (*uploadMesh)(Chunk* c, int sy, MeshResult& r) = nullptr;

private:
    JobSystem* jobs = nullptr;
    std::unique_ptr<RegionStore> region;
    std::unique_ptr<TerrainGen> gen;
    std::unordered_set<int64_t> genPending;
    std::mutex resultMutex;
    std::vector<Chunk*> genResults;
    std::vector<MeshResult*> meshResults;
    std::vector<ivec3> spiral;
    int spiralRadius = -1;
    int lastCenterX = 0, lastCenterZ = 0;
    int meshJobsInFlight = 0;
    std::multimap<int64_t, ivec3> scheduled;
    std::unordered_set<uint64_t> scheduledSet;

    void markDirty(int x, int y, int z);
    void lightAdd(std::vector<ivec3>& queue, bool skyChannel);
    void lightRemove(std::vector<std::pair<ivec3, int>>& rq, std::vector<ivec3>& addQ, bool skyChannel);
    void relightAt(int x, int y, int z);
    void stitchChunk(Chunk* c);
    void unloadChunk(Chunk* c);
    bool neighborsReady(Chunk* c) const;
    void buildSnapshot(Chunk* c, int sy, MeshSnapshot& s) const;
    void randomTick(int x, int y, int z, uint16_t id);
    void updateLiquid(int x, int y, int z);
    void blockUpdate(int x, int y, int z);
};

int liquidLevel(uint16_t id, uint8_t meta);
// Implemented by the crafting module: advances a furnace by one tick
void tickFurnaceTile(World& w, int x, int y, int z, TileEntity& t);  // 0 = source/full .. 7, falling = 0
