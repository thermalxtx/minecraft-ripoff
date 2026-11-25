// Seeded terrain generator: height, biomes, caves, ores, water/lava, plants and trees.
#pragma once
#include "noise.h"
#include "chunk.h"

enum Biome : uint8_t { BIOME_OCEAN, BIOME_BEACH, BIOME_PLAINS, BIOME_FOREST, BIOME_DESERT, BIOME_TAIGA, BIOME_MOUNTAINS, BIOME_COUNT };
const char* biomeName(int b);

// Entity type ids shared by world generation, saving and the entity system
enum EntityType : uint8_t {
    ET_NONE = 0, ET_ITEM, ET_ARROW, ET_FALLING_BLOCK, ET_PIG, ET_COW, ET_SHEEP, ET_CHICKEN,
    ET_SHAMBLER, ET_MARROWSHOT, ET_FUSELING, ET_CRAGWEAVER, ET_TYPE_COUNT
};

struct ColumnInfo { int height; Biome biome; float mountain; float cont; };

struct TerrainGen {
    uint64_t seed;
    Noise nCont, nHill, nDetail, nMount, nMountMask, nTemp, nHum, nJitter, nCave1, nCave2, nCavern, nSurf;
    explicit TerrainGen(uint64_t s);
    ColumnInfo column(int x, int z) const;
    void generate(Chunk& c) const;
    // Places a tree of `type` (0 oak, 1 birch, 2 spruce) with its base at (x,y,z) using a block setter
    template <class SetFn, class GetFn>
    static void tree(int type, int x, int y, int z, Rng& r, SetFn set, GetFn get);
private:
    void carveCaves(Chunk& c, const ColumnInfo* cols) const;
    void placeOres(Chunk& c) const;
    void decorate(Chunk& c, const ColumnInfo* cols) const;
};

template <class SetFn, class GetFn>
void TerrainGen::tree(int type, int x, int y, int z, Rng& r, SetFn set, GetFn get) {
    uint16_t log = type == 0 ? OAK_LOG : (type == 1 ? BIRCH_LOG : SPRUCE_LOG);
    uint16_t leaf = type == 0 ? OAK_LEAVES : (type == 1 ? BIRCH_LEAVES : SPRUCE_LEAVES);
    auto leafAt = [&](int px, int py, int pz) {
        uint16_t b = get(px, py, pz);
        if (b == AIR || b == TALL_GRASS || b == FLOWER_RED || b == FLOWER_YELLOW || b == SNOW) set(px, py, pz, leaf);
    };
    if (type == 2) {  // spruce: tall cone
        int h = r.range(6, 9);
        int top = y + h;
        for (int ly = top; ly >= y + 2; ly--) {
            int k = top - ly;
            int radius = (k % 2 == 0) ? 1 : (k >= 5 ? 3 : 2);
            for (int dx = -radius; dx <= radius; dx++)
                for (int dz = -radius; dz <= radius; dz++) {
                    if (std::abs(dx) == radius && std::abs(dz) == radius) continue;
                    if (radius == 3 && std::abs(dx) + std::abs(dz) > 4) continue;
                    leafAt(x + dx, ly, z + dz);
                }
        }
        leafAt(x, top + 1, z);
        for (int ly = y; ly < top; ly++) set(x, ly, z, log);
        return;
    }
    int h = type == 1 ? r.range(5, 7) : r.range(4, 6);
    int top = y + h;
    for (int ly = top - 3; ly <= top; ly++) {
        int rad = ly >= top - 1 ? 1 : 2;
        for (int dx = -rad; dx <= rad; dx++)
            for (int dz = -rad; dz <= rad; dz++) {
                bool corner = std::abs(dx) == rad && std::abs(dz) == rad;
                if (corner && (ly == top || r.chance(0.5f))) continue;
                leafAt(x + dx, ly, z + dz);
            }
    }
    for (int ly = y; ly < top; ly++) set(x, ly, z, log);
}
