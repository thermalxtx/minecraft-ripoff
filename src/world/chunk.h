// A chunk is a 16x16x256 column of blocks, with metadata and two light channels.
#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
#include <unordered_map>
#include "block.h"
#include "../game/items.h"

constexpr int CHUNK_W = 16;
constexpr int CHUNK_H = 256;
constexpr int SECTIONS = CHUNK_H / 16;
constexpr int CHUNK_VOL = CHUNK_W * CHUNK_W * CHUNK_H;
constexpr int SEA_LEVEL = 62;

enum ChunkState : uint8_t { CS_LOADED = 0, CS_STITCHED = 1 };

// Containers that live in blocks (chest, furnace)
enum TileType : uint8_t { TILE_NONE = 0, TILE_CHEST, TILE_FURNACE };
struct TileEntity {
    uint8_t type = TILE_NONE;
    ItemStack slots[27];        // chest: 27; furnace: 0 input, 1 fuel, 2 output
    int16_t burnTime = 0, burnTotal = 0, cookTime = 0;
};

// Entities stored with a chunk in the save file (spawned back when it loads)
struct SavedEntity {
    uint8_t type = 0;
    float x = 0, y = 0, z = 0, yaw = 0;
    int16_t health = 0;
    ItemStack item;  // for dropped items
    int32_t age = 0;
    uint8_t extra = 0; // e.g. sheep wool color
};

struct Chunk {
    int cx = 0, cz = 0;
    uint16_t blocks[CHUNK_VOL];
    uint8_t meta[CHUNK_VOL];
    uint8_t light[CHUNK_VOL];   // high nibble = sky light, low nibble = block light
    uint8_t height[256];        // first y above the highest light-blocking block, per column
    uint8_t state = CS_LOADED;
    bool modified = false;      // changed since generation, must be saved
    bool entitiesSpawned = false;
    bool sectionDirty[SECTIONS];
    bool sectionInFlight[SECTIONS];
    bool everMeshed = false;
    // GPU data (main thread only): per section, per render layer
    uint32_t vao[SECTIONS][3] = {};
    uint32_t vbo[SECTIONS][3] = {};
    int quads[SECTIONS][3] = {};
    int vboCap[SECTIONS][3] = {};
    bool sectionEmpty[SECTIONS];
    uint32_t meshTicket[SECTIONS] = {};  // only the newest mesh job result is accepted
    std::unordered_map<int, TileEntity> tiles;
    std::vector<SavedEntity> savedEntities;

    Chunk() {
        memset(blocks, 0, sizeof(blocks)); memset(meta, 0, sizeof(meta)); memset(light, 0, sizeof(light));
        memset(height, 0, sizeof(height));
        for (int i = 0; i < SECTIONS; i++) { sectionDirty[i] = true; sectionInFlight[i] = false; sectionEmpty[i] = false; }
    }
    static int idx(int x, int y, int z) { return (y << 8) | (z << 4) | x; }
    uint16_t get(int x, int y, int z) const { return blocks[idx(x, y, z)]; }
    int sky(int i) const { return light[i] >> 4; }
    int blk(int i) const { return light[i] & 15; }
    void setSky(int i, int v) { light[i] = (uint8_t)((light[i] & 0x0F) | (v << 4)); }
    void setBlk(int i, int v) { light[i] = (uint8_t)((light[i] & 0xF0) | v); }
    void recomputeHeight(int x, int z) {
        int y = CHUNK_H - 1;
        while (y >= 0 && blockDef(blocks[idx(x, y, z)]).lightOpacity == 0) y--;
        height[z * 16 + x] = (uint8_t)std::min(255, y + 1);
    }
    void recomputeAllHeights() { for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) recomputeHeight(x, z); }
};

inline int64_t chunkKey(int cx, int cz) { return ((int64_t)(uint32_t)cx << 32) | (uint32_t)cz; }

// Light inside a single chunk (no neighbours); run on worker threads after generation/loading
void lightChunkInternal(Chunk& c);
