#include "worldgen.h"
#include <cmath>

const char* biomeName(int b) {
    static const char* n[] = {"Ocean", "Beach", "Plains", "Forest", "Desert", "Snowy Taiga", "Mountains"};
    return b >= 0 && b < BIOME_COUNT ? n[b] : "?";
}

TerrainGen::TerrainGen(uint64_t s)
    : seed(s), nCont(s + 1), nHill(s + 2), nDetail(s + 3), nMount(s + 4), nMountMask(s + 5), nTemp(s + 6),
      nHum(s + 7), nJitter(s + 8), nCave1(s + 9), nCave2(s + 10), nCavern(s + 11), nSurf(s + 12) {}

ColumnInfo TerrainGen::column(int x, int z) const {
    ColumnInfo ci;
    float fx = (float)x, fz = (float)z;
    // Step 1: height from continents, hills, detail and rare mountain ranges
    float c = nCont.fbm2(fx / 700.f, fz / 700.f, 4) * 1.6f + 0.2f;
    float h = c < 0 ? 63 + c * 60 : 63 + c * 22;
    float land = smoothstepf(-0.1f, 0.25f, c);
    h += nHill.fbm2(fx / 160.f, fz / 160.f, 4) * (4 + 16 * land);
    h += nDetail.fbm2(fx / 40.f, fz / 40.f, 3) * (1.5f + 2.5f * land);
    float mm = smoothstepf(0.12f, 0.38f, nMountMask.fbm2(fx / 1100.f, fz / 1100.f, 3)) * smoothstepf(-0.02f, 0.2f, c);
    float peak = nMount.ridged2(fx / 230.f, fz / 230.f, 5);
    h += mm * std::pow(peak, 1.6f) * 105.f;
    ci.height = (int)clampf(h, 4, 245);
    ci.mountain = mm;
    ci.cont = c;
    // Step 2: biomes from temperature/humidity, dithered a little at the edges
    float jit = nJitter.noise2(fx / 6.f, fz / 6.f) * 0.04f;
    float t = nTemp.fbm2(fx / 520.f, fz / 520.f, 3) * 1.6f + jit;
    float u = nHum.fbm2(fx / 470.f, fz / 470.f, 3) * 1.6f - jit;
    int hh = ci.height;
    if (hh < SEA_LEVEL - 1) ci.biome = BIOME_OCEAN;
    else if (hh <= SEA_LEVEL + 2 && c < 0.08f + jit && t > -0.3f) ci.biome = BIOME_BEACH;
    else if (mm > 0.45f && hh > 92) ci.biome = BIOME_MOUNTAINS;
    else if (t > 0.18f && u < 0.12f) ci.biome = BIOME_DESERT;
    else if (t < -0.32f) ci.biome = BIOME_TAIGA;
    else if (u > 0.15f) ci.biome = BIOME_FOREST;
    else ci.biome = BIOME_PLAINS;
    return ci;
}

void TerrainGen::generate(Chunk& c) const {
    ColumnInfo cols[256];
    int bx = c.cx * 16, bz = c.cz * 16;
    for (int z = 0; z < 16; z++)
        for (int x = 0; x < 16; x++) cols[z * 16 + x] = column(bx + x, bz + z);
    // Fill stone and surface layers
    for (int z = 0; z < 16; z++)
        for (int x = 0; x < 16; x++) {
            const ColumnInfo& ci = cols[z * 16 + x];
            int h = ci.height;
            int wx = bx + x, wz = bz + z;
            float sn = nSurf.noise2(wx / 9.f, wz / 9.f);
            int depth = 3 + (int)((sn + 1) * 1.5f);
            uint64_t hsh = hashCoords(wx, 0, wz, seed);
            for (int y = 0; y <= h; y++) {
                uint16_t b = STONE;
                int d = h - y;
                if (y == 0 || (y <= 3 && (int)((hsh >> (y * 4)) & 3) >= y)) b = BEDROCK;
                else if (d < depth) {
                    switch (ci.biome) {
                    case BIOME_OCEAN:
                        b = (h < 50 && sn > 0.2f) ? GRAVEL : (sn < -0.45f && d == 0 ? CLAY : (h < 45 ? GRAVEL : SAND));
                        break;
                    case BIOME_BEACH: b = SAND; break;
                    case BIOME_DESERT: b = d < depth - 1 ? SAND : SANDSTONE; if (d >= 3) b = SANDSTONE; break;
                    case BIOME_MOUNTAINS:
                        if (h > 150 + (int)(sn * 6)) b = (d == 0) ? SNOW : STONE;
                        else if (h > 118 + (int)(sn * 10)) b = STONE;
                        else b = (d == 0) ? GRASS : DIRT;
                        break;
                    case BIOME_TAIGA: b = (d == 0) ? (h >= SEA_LEVEL ? SNOWY_GRASS : DIRT) : DIRT; break;
                    default:
                        b = (d == 0) ? (h >= SEA_LEVEL ? GRASS : (sn > 0 ? SAND : DIRT)) : DIRT;
                        if (h < SEA_LEVEL - 1 && d == 0) b = sn > 0.3f ? GRAVEL : DIRT;
                        break;
                    }
                }
                c.blocks[Chunk::idx(x, y, z)] = b;
            }
        }
    // Step 3: caves
    carveCaves(c, cols);
    // Step 5: water to sea level above the ground; lava at the bottom of caves
    for (int z = 0; z < 16; z++)
        for (int x = 0; x < 16; x++) {
            const ColumnInfo& ci = cols[z * 16 + x];
            for (int y = ci.height + 1; y <= SEA_LEVEL; y++) {
                int i = Chunk::idx(x, y, z);
                if (c.blocks[i] == AIR) c.blocks[i] = (ci.biome == BIOME_TAIGA && y == SEA_LEVEL) ? ICE : WATER;
            }
            for (int y = 1; y <= 10; y++) {
                int i = Chunk::idx(x, y, z);
                if (c.blocks[i] == AIR) c.blocks[i] = LAVA;
            }
        }
    // Step 4: ores
    placeOres(c);
    // Step 6: plants and trees
    decorate(c, cols);
    c.recomputeAllHeights();
}

void TerrainGen::carveCaves(Chunk& c, const ColumnInfo* cols) const {
    // Sample 3D noise on a coarse 4x4x4 grid and interpolate
    const int GX = 5, GY = 33;  // y step 8? we use 4 horizontal, 4 vertical over 0..128
    static_assert(GY > 0, "");
    int bx = c.cx * 16, bz = c.cz * 16;
    float w1[GX][GY][GX], w2[GX][GY][GX], cav[GX][GY][GX];
    for (int ix = 0; ix < GX; ix++)
        for (int iz = 0; iz < GX; iz++)
            for (int iy = 0; iy < GY; iy++) {
                float x = (float)(bx + ix * 4), y = (float)(iy * 4), z = (float)(bz + iz * 4);
                w1[ix][iy][iz] = nCave1.noise3(x / 48.f, y / 28.f, z / 48.f);
                w2[ix][iy][iz] = nCave2.noise3(x / 48.f, y / 28.f, z / 48.f);
                cav[ix][iy][iz] = nCavern.fbm3(x / 90.f, y / 36.f, z / 90.f, 2);
            }
    for (int x = 0; x < 16; x++)
        for (int z = 0; z < 16; z++) {
            int h = cols[z * 16 + x].height;
            bool wet = h < SEA_LEVEL + 3;
            int maxY = wet ? std::min(h - 6, 127) : std::min(h + 1, 127);
            int ix = x >> 2, iz = z >> 2;
            float fx = (x & 3) / 4.f, fz = (z & 3) / 4.f;
            for (int y = 5; y <= maxY; y++) {
                int iy = y >> 2; float fy = (y & 3) / 4.f;
                auto tri = [&](float (&a)[GX][GY][GX]) {
                    float c00 = lerpf(a[ix][iy][iz], a[ix + 1][iy][iz], fx), c10 = lerpf(a[ix][iy + 1][iz], a[ix + 1][iy + 1][iz], fx);
                    float c01 = lerpf(a[ix][iy][iz + 1], a[ix + 1][iy][iz + 1], fx), c11 = lerpf(a[ix][iy + 1][iz + 1], a[ix + 1][iy + 1][iz + 1], fx);
                    return lerpf(lerpf(c00, c10, fy), lerpf(c01, c11, fy), fz);
                };
                float a = tri(w1), b = tri(w2);
                // tunnels get thinner near the surface so only a few open to the sky
                float width = 0.075f * (y > h - 8 ? 0.6f : 1.0f);
                bool carve = std::fabs(a) < width && std::fabs(b) < width;
                if (!carve && y < 48 && y > 8) {
                    float cv = tri(cav);
                    carve = cv > 0.38f + (y < 14 ? (14 - y) * 0.03f : 0);
                }
                if (carve) {
                    int i = Chunk::idx(x, y, z);
                    uint16_t cur = c.blocks[i];
                    if (cur != BEDROCK && cur != WATER) {
                        c.blocks[i] = AIR;
                        // expose dirt below grass if the cave reaches the surface
                        if (y == h) { /* top removed */ }
                    }
                }
            }
        }
}

void TerrainGen::placeOres(Chunk& c) const {
    Rng r(hashCoords(c.cx, 77, c.cz, seed));
    struct OreDef { uint16_t block; int veins, minSize, maxSize, minY, maxY; };
    const OreDef ores[] = {
        {COAL_ORE, 20, 6, 14, 5, 128}, {IRON_ORE, 14, 4, 8, 5, 64}, {GOLD_ORE, 3, 4, 8, 5, 32},
        {DIAMOND_ORE, 1, 3, 7, 5, 16}, {GRAVEL, 6, 12, 28, 5, 100}, {DIRT, 8, 12, 28, 5, 120}, {CLAY, 1, 6, 12, 40, 62},
    };
    for (auto& o : ores) {
        int veins = o.veins;
        if (o.block == DIAMOND_ORE && r.chance(0.5f)) veins++;
        for (int v = 0; v < veins; v++) {
            float x = (float)r.range(0, 15), y = (float)r.range(o.minY, o.maxY), z = (float)r.range(0, 15);
            int size = r.range(o.minSize, o.maxSize);
            float dx = r.range(-1.f, 1.f), dy = r.range(-0.5f, 0.5f), dz = r.range(-1.f, 1.f);
            for (int k = 0; k < size; k++) {
                for (int j = 0; j < 2; j++) {
                    int px = (int)x + (j ? r.range(-1, 1) : 0), py = (int)y + (j ? r.range(-1, 1) : 0), pz = (int)z + (j ? r.range(-1, 1) : 0);
                    if (px < 0 || px > 15 || pz < 0 || pz > 15 || py < 1 || py > 254) continue;
                    int i = Chunk::idx(px, py, pz);
                    if (c.blocks[i] == STONE) c.blocks[i] = o.block;
                }
                x += dx * 0.8f + r.range(-0.4f, 0.4f); y += dy * 0.6f + r.range(-0.3f, 0.3f); z += dz * 0.8f + r.range(-0.4f, 0.4f);
            }
        }
    }
}

void TerrainGen::decorate(Chunk& c, const ColumnInfo* cols) const {
    int bx = c.cx * 16, bz = c.cz * 16;
    // Plants on the surface of this chunk
    for (int z = 0; z < 16; z++)
        for (int x = 0; x < 16; x++) {
            const ColumnInfo& ci = cols[z * 16 + x];
            int h = ci.height;
            if (h < SEA_LEVEL || h >= 250) continue;
            uint16_t ground = c.blocks[Chunk::idx(x, h, z)];
            if (c.blocks[Chunk::idx(x, h + 1, z)] != AIR) continue;
            uint64_t hs = hashCoords(bx + x, h, bz + z, seed ^ 0x5151);
            float rnd = (hs & 0xFFFF) / 65535.f;
            uint16_t plant = AIR;
            if (ground == GRASS) {
                float fl = nSurf.noise2((bx + x) / 30.f, (bz + z) / 30.f);
                if (ci.biome == BIOME_PLAINS) { if (rnd < 0.18f) plant = TALL_GRASS; else if (rnd < 0.18f + (fl > 0.3f ? 0.06f : 0.01f)) plant = ((hs >> 20) & 1) ? FLOWER_RED : FLOWER_YELLOW; }
                else if (ci.biome == BIOME_FOREST) { if (rnd < 0.1f) plant = TALL_GRASS; else if (rnd < 0.115f) plant = ((hs >> 20) & 1) ? FLOWER_RED : FLOWER_YELLOW; }
                else if (rnd < 0.06f) plant = TALL_GRASS;
            } else if (ground == SNOWY_GRASS && rnd < 0.03f) plant = TALL_GRASS;
            else if (ground == SAND && ci.biome == BIOME_DESERT && rnd < 0.008f) plant = DEAD_BUSH;
            else if (ground == SAND && ci.biome == BIOME_DESERT && rnd < 0.014f) {  // cactus
                int ch = 1 + (int)((hs >> 24) % 3);
                bool clear = true;
                for (int k : {0, 1, 2, 3}) {
                    static const int D[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                    int nx = x + D[k][0], nz = z + D[k][1];
                    if (nx >= 0 && nx < 16 && nz >= 0 && nz < 16 && c.blocks[Chunk::idx(nx, h + 1, nz)] != AIR) clear = false;
                }
                if (clear) for (int k = 1; k <= ch; k++) c.blocks[Chunk::idx(x, h + k, z)] = CACTUS;
                continue;
            }
            if (plant) c.blocks[Chunk::idx(x, h + 1, z)] = plant;
        }
    // Trees: candidates from this chunk and its neighbours, placed only where they overlap us
    auto set = [&](int wx, int wy, int wz, uint16_t b) {
        int lx = wx - bx, lz = wz - bz;
        if (lx < 0 || lx > 15 || lz < 0 || lz > 15 || wy < 1 || wy > 254) return;
        c.blocks[Chunk::idx(lx, wy, lz)] = b;
    };
    auto get = [&](int wx, int wy, int wz) -> uint16_t {
        int lx = wx - bx, lz = wz - bz;
        if (lx < 0 || lx > 15 || lz < 0 || lz > 15 || wy < 0 || wy > 255) return AIR;
        return c.blocks[Chunk::idx(lx, wy, lz)];
    };
    for (int scx = c.cx - 1; scx <= c.cx + 1; scx++)
        for (int scz = c.cz - 1; scz <= c.cz + 1; scz++) {
            Rng r(hashCoords(scx, 991, scz, seed));
            for (int k = 0; k < 14; k++) {
                int lx = r.range(0, 15), lz = r.range(0, 15);
                float roll = r.nextFloat();
                uint64_t treeSeed = r.next();
                int wx = scx * 16 + lx, wz = scz * 16 + lz;
                if (wx < bx - 3 || wx > bx + 18 || wz < bz - 3 || wz > bz + 18) continue;
                ColumnInfo ci = column(wx, wz);
                if (ci.height < SEA_LEVEL || ci.height > 200) continue;
                float chance = 0; int type = 0;
                switch (ci.biome) {
                    case BIOME_FOREST: chance = 0.75f; type = (treeSeed % 4 == 0) ? 1 : 0; break;
                    case BIOME_PLAINS: chance = 0.035f; type = 0; break;
                    case BIOME_TAIGA: chance = 0.5f; type = 2; break;
                    case BIOME_MOUNTAINS: chance = ci.height < 118 ? 0.18f : 0; type = 2; break;
                    default: chance = 0;
                }
                if (roll >= chance) continue;
                // Only on grass-type ground (snow-capped mountains excluded)
                if (ci.biome == BIOME_MOUNTAINS && ci.height > 118) continue;
                Rng tr(treeSeed);
                TerrainGen::tree(type, wx, ci.height + 1, wz, tr, set, get);
                // the block under the trunk becomes dirt
                if (wx >= bx && wx < bx + 16 && wz >= bz && wz < bz + 16) set(wx, ci.height, wz, DIRT);
            }
        }
    // Passive animals: a few groups on grassy chunks
    Rng ar(hashCoords(c.cx, 4242, c.cz, seed));
    if (ar.chance(0.1f)) {
        int x = ar.range(2, 13), z = ar.range(2, 13);
        const ColumnInfo& ci = cols[z * 16 + x];
        uint16_t g = c.blocks[Chunk::idx(x, ci.height, z)];
        if ((g == GRASS || g == SNOWY_GRASS) && ci.height >= SEA_LEVEL) {
            static const uint8_t kinds[4] = {ET_PIG, ET_COW, ET_SHEEP, ET_CHICKEN};
            uint8_t kind = kinds[ar.nextInt(4)];
            int n = ar.range(2, 4);
            for (int i = 0; i < n; i++) {
                SavedEntity e;
                e.type = kind;
                e.x = bx + x + ar.range(-1.5f, 1.5f) + 0.5f; e.z = bz + z + ar.range(-1.5f, 1.5f) + 0.5f;
                e.x = clampf(e.x, (float)bx + 0.5f, (float)bx + 15.5f); e.z = clampf(e.z, (float)bz + 0.5f, (float)bz + 15.5f);
                e.y = (float)ci.height + 1.5f;
                e.yaw = ar.range(0.f, 6.28f);
                e.extra = (uint8_t)(ar.chance(0.1f) ? (1 + ar.nextInt(3)) : 0);
                c.savedEntities.push_back(e);
            }
        }
    }
}
