// World simulation: scheduled block updates, water/lava flow, random ticks (grass, crops, saplings), support checks.
#include "world.h"
#include "worldgen.h"
#include "../core/events.h"
#include "../audio/audio.h"
#include <algorithm>

static const int HDIRS[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
static uint64_t posKey(int x, int y, int z) {
    return ((uint64_t)(uint32_t)(x & 0x3FFFFFF) << 38) | ((uint64_t)(uint32_t)(z & 0x3FFFFFF) << 12) | (uint64_t)(y & 0xFFF);
}
static Rng g_tickRng(987654321);

void World::scheduleUpdate(int x, int y, int z, int delay) {
    if (y < 0 || y >= CHUNK_H || !isLoaded(x, z)) return;
    uint64_t k = posKey(x, y, z);
    if (scheduledSet.count(k)) return;
    scheduledSet.insert(k);
    scheduled.insert({tickCount + std::max(1, delay), ivec3(x, y, z)});
}

void World::neighborChanged(int x, int y, int z) {
    static const int D[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (auto& d : D) {
        int nx = x + d[0], ny = y + d[1], nz = z + d[2];
        uint16_t b = getBlock(nx, ny, nz);
        if (b == AIR) continue;
        const BlockDef& bd = g_blocks[b];
        if (bd.liquid) scheduleUpdate(nx, ny, nz, b == WATER ? 5 : 30);
        else if (bd.needsSupport || bd.gravity || b == BED) scheduleUpdate(nx, ny, nz, 1);
    }
}

static bool plantSoil(uint16_t b) { return b == GRASS || b == DIRT || b == SNOWY_GRASS || b == FARMLAND; }

bool World::canPlaceAt(uint16_t id, int x, int y, int z) const {
    uint16_t below = getBlock(x, y - 1, z);
    switch (id) {
        case TALL_GRASS: case FLOWER_RED: case FLOWER_YELLOW: case OAK_SAPLING: case BIRCH_SAPLING: case SPRUCE_SAPLING:
            return plantSoil(below);
        case DEAD_BUSH: return below == SAND;
        case WHEAT: return below == FARMLAND;
        case CACTUS: return below == SAND || below == CACTUS;
        case DOOR: return g_blocks[below].opaque;
        default: return true;
    }
}

// Is a support-needing block still held up?
static bool isSupported(const World& w, uint16_t id, uint8_t meta, int x, int y, int z) {
    switch (id) {
        case TORCH: {
            if (meta == 0) { uint16_t b = w.getBlock(x, y - 1, z); return g_blocks[b].opaque || b == FARMLAND || b == GLASS; }
            int f = meta - 1;
            return g_blocks[w.getBlock(x - facingDX(f), y, z - facingDZ(f))].opaque;
        }
        case LADDER: { int f = meta & 3; return g_blocks[w.getBlock(x - facingDX(f), y, z - facingDZ(f))].opaque; }
        case DOOR:
            if (meta & 8) return w.getBlock(x, y - 1, z) == DOOR;
            return w.getBlock(x, y + 1, z) == DOOR && g_blocks[w.getBlock(x, y - 1, z)].opaque;
        default: return w.canPlaceAt(id, x, y, z);
    }
}

void World::blockUpdate(int x, int y, int z) {
    uint16_t id = getBlock(x, y, z);
    if (id == AIR) return;
    const BlockDef& d = g_blocks[id];
    uint8_t meta = getMeta(x, y, z);
    if (d.liquid) { updateLiquid(x, y, z); return; }
    if (d.needsSupport && !isSupported(*this, id, meta, x, y, z)) {
        bool dropIt = !(id == DOOR && (meta & 8));
        GameEvent e{EV_BLOCK_BROKEN, vec3(x + 0.5f, y + 0.5f, z + 0.5f)};
        e.a = id; e.b = meta; e.c = dropIt ? 0 : 2;
        g_events.post(e);
        setBlock(x, y, z, AIR);
        return;
    }
    if (id == BED) {
        int f = meta & 3; bool head = meta & 4;
        int ox = head ? -facingDX(f) : facingDX(f), oz = head ? -facingDZ(f) : facingDZ(f);
        if (getBlock(x + ox, y, z + oz) != BED) { setBlock(x, y, z, AIR); }
        return;
    }
    if (d.gravity) {
        uint16_t below = getBlock(x, y - 1, z);
        if (y > 0 && (below == AIR || g_blocks[below].liquid || (g_blocks[below].replaceable && !g_blocks[below].solid))) {
            GameEvent e{EV_FALLING_BLOCK, vec3((float)x, (float)y, (float)z)};
            e.a = id;
            g_events.post(e);
            setBlock(x, y, z, AIR);
        }
    }
}

static bool flowable(uint16_t b) {
    return b == AIR || b == TALL_GRASS || b == FLOWER_RED || b == FLOWER_YELLOW || b == DEAD_BUSH || b == TORCH || b == WHEAT ||
           b == OAK_SAPLING || b == BIRCH_SAPLING || b == SPRUCE_SAPLING;
}

// Simplified classic liquid model: meta = level 0 (source) .. 7, bit 3 = falling.
// Flowing blocks recompute their level from their neighbours; liquid only spreads into empty space.
void World::updateLiquid(int x, int y, int z) {
    uint16_t id = getBlock(x, y, z);
    if (!g_blocks[id].liquid) return;
    bool water = id == WATER;
    uint16_t other = water ? LAVA : WATER;
    int step = water ? 1 : 2;
    int delay = water ? 5 : 30;
    uint8_t meta = getMeta(x, y, z);
    // lava touching water hardens
    if (!water) {
        bool touch = getBlock(x, y + 1, z) == WATER;
        for (auto& d : HDIRS) if (getBlock(x + d[0], y, z + d[1]) == WATER) touch = true;
        if (touch) {
            setBlock(x, y, z, (meta & 15) == 0 ? OBSIDIAN : COBBLESTONE);
            g_events.sound(vec3(x + 0.5f, y + 0.5f, z + 0.5f), SFX_FIZZ, 0.8f);
            g_events.particles(vec3(x + 0.5f, y + 1.0f, z + 0.5f), PK_SMOKE, 8);
            return;
        }
    }
    int level = (meta & 8) ? 8 : (meta & 7);  // 8 = falling
    if (level != 0) {
        int best = 100, sources = 0;
        for (auto& d : HDIRS) {
            if (getBlock(x + d[0], y, z + d[1]) != id) continue;
            uint8_t nm = getMeta(x + d[0], y, z + d[1]);
            int nl = (nm & 8) ? 0 : (nm & 7);
            if ((nm & 15) == 0) sources++;
            best = std::min(best, nl);
        }
        int want = best + step;
        if (want >= 8 || best == 100) want = -1;
        if (getBlock(x, y + 1, z) == id) want = 8;
        if (water && sources >= 2) {
            uint16_t below = getBlock(x, y - 1, z);
            if (g_blocks[below].solid || (below == WATER && (getMeta(x, y - 1, z) & 15) == 0)) want = 0;
        }
        if (want != level) {
            if (want < 0) { setBlock(x, y, z, AIR); return; }
            setBlock(x, y, z, id, (uint8_t)want, false);
            level = want;
            scheduleUpdate(x, y, z, delay);
            static const int D6[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
            for (auto& d : D6) if (getBlock(x + d[0], y + d[1], z + d[2]) == id) scheduleUpdate(x + d[0], y + d[1], z + d[2], delay);
        }
    }
    // spread down, else sideways
    uint16_t below = y > 0 ? getBlock(x, y - 1, z) : BEDROCK;
    if (below == other) {
        if (water) { setBlock(x, y - 1, z, (getMeta(x, y - 1, z) & 15) == 0 ? OBSIDIAN : STONE); g_events.sound(vec3(x + 0.5f, y - 0.5f, z + 0.5f), SFX_FIZZ, 0.8f); }
        return;
    }
    if (flowable(below)) {
        if (below != AIR) { GameEvent e{EV_BLOCK_BROKEN, vec3(x + 0.5f, y - 0.5f, z + 0.5f)}; e.a = below; e.c = 0; g_events.post(e); }
        setBlock(x, y - 1, z, id, 8);
        return;
    }
    if (below == id && level != 0) return;  // flowing water resting on liquid does not spread sideways
    int nl = (level >= 8) ? step : level + step;
    if (nl > 7) return;
    for (auto& d : HDIRS) {
        int nx = x + d[0], nz = z + d[1];
        if (!isLoaded(nx, nz)) continue;
        uint16_t nb = getBlock(nx, y, nz);
        if (flowable(nb)) {
            if (nb != AIR) { GameEvent e{EV_BLOCK_BROKEN, vec3(nx + 0.5f, y + 0.5f, nz + 0.5f)}; e.a = nb; e.c = 0; g_events.post(e); }
            setBlock(nx, y, nz, id, (uint8_t)nl);
        } else if (nb == other && water) {
            uint8_t om = getMeta(nx, y, nz);
            setBlock(nx, y, nz, (om & 15) == 0 ? OBSIDIAN : COBBLESTONE);
            g_events.sound(vec3(nx + 0.5f, y + 0.5f, nz + 0.5f), SFX_FIZZ, 0.8f);
        }
    }
}

void World::growTree(int x, int y, int z, int type) {
    int h = type == 2 ? 9 : 7;
    for (int k = 1; k <= h; k++) {
        uint16_t b = getBlock(x, y + k, z);
        if (b != AIR && !g_blocks[b].replaceable && b != OAK_LEAVES && b != BIRCH_LEAVES && b != SPRUCE_LEAVES) return;
    }
    if (y + h + 2 >= CHUNK_H) return;
    setBlock(x, y, z, AIR, 0, false);
    Rng r(hashCoords(x, y, z, seed ^ (uint64_t)tickCount));
    auto set = [&](int px, int py, int pz, uint16_t b) { if (isLoaded(px, pz)) setBlock(px, py, pz, b, 0, false); };
    auto get = [&](int px, int py, int pz) { return getBlock(px, py, pz); };
    TerrainGen::tree(type, x, y, z, r, set, get);
    if (getBlock(x, y - 1, z) == GRASS || getBlock(x, y - 1, z) == SNOWY_GRASS) setBlock(x, y - 1, z, DIRT, 0, false);
}

void World::randomTick(int x, int y, int z, uint16_t id) {
    switch (id) {
    case GRASS: case SNOWY_GRASS: {
        uint16_t above = getBlock(x, y + 1, z);
        if (g_blocks[above].opaque || g_blocks[above].liquid) { setBlock(x, y, z, DIRT); return; }
        if (getLightLevel(x, y + 1, z, 1.0f) >= 9) {
            int nx = x + g_tickRng.range(-1, 1), ny = y + g_tickRng.range(-3, 1), nz = z + g_tickRng.range(-1, 1);
            if (getBlock(nx, ny, nz) == DIRT) {
                uint16_t a = getBlock(nx, ny + 1, nz);
                if (!g_blocks[a].opaque && !g_blocks[a].liquid && getLightLevel(nx, ny + 1, nz, 1.0f) >= 4) setBlock(nx, ny, nz, GRASS);
            }
        }
        break;
    }
    case WHEAT: {
        uint8_t m = getMeta(x, y, z);
        if (m < 7 && getLightLevel(x, y, z, 1.0f) >= 9 && g_tickRng.chance(0.35f)) setMeta(x, y, z, m + 1);
        break;
    }
    case OAK_SAPLING: case BIRCH_SAPLING: case SPRUCE_SAPLING: {
        if (getLightLevel(x, y + 1, z, 1.0f) < 9 || !g_tickRng.chance(0.15f)) break;
        uint8_t m = getMeta(x, y, z);
        if (m == 0) setMeta(x, y, z, 1);
        else growTree(x, y, z, id == OAK_SAPLING ? 0 : (id == BIRCH_SAPLING ? 1 : 2));
        break;
    }
    case FARMLAND: {
        bool wet = false;
        for (int dx = -4; dx <= 4 && !wet; dx++)
            for (int dz = -4; dz <= 4 && !wet; dz++)
                for (int dy = 0; dy <= 1; dy++) if (getBlock(x + dx, y + dy, z + dz) == WATER) { wet = true; break; }
        setMeta(x, y, z, wet ? 7 : 0);
        if (!wet && getBlock(x, y + 1, z) != WHEAT && g_tickRng.chance(0.2f)) setBlock(x, y, z, DIRT);
        break;
    }
    case ICE: if (getBlockLight(x, y + 1, z) > 11 || getBlockLight(x + 1, y, z) > 11 || getBlockLight(x - 1, y, z) > 11) setBlock(x, y, z, WATER); break;
    case LAVA:
        if (g_tickRng.chance(0.03f) && getBlock(x, y + 1, z) == AIR) {
            g_events.particles(vec3(x + 0.5f, y + 1.0f, z + 0.5f), PK_LAVA, 1);
            g_events.sound(vec3(x + 0.5f, y + 1.0f, z + 0.5f), SFX_LAVA_POP, 0.5f);
        }
        break;
    default: break;
    }
}

void World::tick(vec3 playerPos) {
    tickCount++;
    // scheduled updates
    int budget = 4000;
    while (!scheduled.empty() && budget-- > 0) {
        auto it = scheduled.begin();
        if (it->first > tickCount) break;
        ivec3 p = it->second;
        scheduled.erase(it);
        scheduledSet.erase(posKey(p.x, p.y, p.z));
        if (isLoaded(p.x, p.z)) blockUpdate(p.x, p.y, p.z);
    }
    // random ticks and furnaces within simulation distance
    int pcx = ifloor(playerPos.x) >> 4, pcz = ifloor(playerPos.z) >> 4;
    const int SIM = std::min(renderDistance, 7);
    for (int dx = -SIM; dx <= SIM; dx++)
        for (int dz = -SIM; dz <= SIM; dz++) {
            Chunk* c = getChunk(pcx + dx, pcz + dz);
            if (!c || c->state != CS_STITCHED) continue;
            int maxH = 0;
            for (int k = 0; k < 256; k++) maxH = std::max(maxH, (int)c->height[k]);
            int sections = std::min(SECTIONS, maxH / 16 + 1);
            for (int s = 0; s < sections; s++)
                for (int k = 0; k < 3; k++) {
                    uint32_t r = (uint32_t)g_tickRng.next();
                    int x = r & 15, y = s * 16 + ((r >> 4) & 15), z = (r >> 8) & 15;
                    uint16_t id = c->blocks[Chunk::idx(x, y, z)];
                    if (id == GRASS || id == SNOWY_GRASS || id == WHEAT || id == OAK_SAPLING || id == BIRCH_SAPLING ||
                        id == SPRUCE_SAPLING || id == FARMLAND || id == ICE || id == LAVA)
                        randomTick(c->cx * 16 + x, y, c->cz * 16 + z, id);
                }
            for (auto& kv : c->tiles)
                if (kv.second.type == TILE_FURNACE) {
                    int i = kv.first;
                    tickFurnaceTile(*this, c->cx * 16 + (i & 15), i >> 8, c->cz * 16 + ((i >> 4) & 15), kv.second);
                }
        }
}
