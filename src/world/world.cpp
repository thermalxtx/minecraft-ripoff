// World core: block access, lighting (flood fill across chunk borders), raycasts and collision.
#include "world.h"
#include "worldgen.h"
#include "../io/region.h"
#include "../core/jobs.h"
#include "../core/events.h"
#include "../render/mesher.h"
#include <algorithm>

static const int DIRS[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};

World::World() {}
World::~World() { shutdown(); }

void World::init(uint64_t s, const std::string& dir, JobSystem* j) {
    seed = s; saveDir = dir; jobs = j;
    region.reset(new RegionStore(dir));
    gen.reset(new TerrainGen(s));
}

void World::shutdown() {
    if (!region) return;
    if (jobs) jobs->waitIdle();
    // drop finished results
    {
        std::lock_guard<std::mutex> l(resultMutex);
        for (Chunk* c : genResults) delete c;
        genResults.clear();
        for (MeshResult* r : meshResults) delete r;
        meshResults.clear();
    }
    unloading = true;
    for (auto& kv : chunks) unloadChunk(kv.second);
    chunks.clear();
    region->flush();
    region.reset();
    gen.reset();
    genPending.clear();
    scheduled.clear(); scheduledSet.clear();
    unloading = false;
}

// ---------------- light queries ----------------
int World::getSkyLight(int x, int y, int z) const {
    if (y >= CHUNK_H) return 15;
    if (y < 0) return 0;
    Chunk* c = chunkAt(x, z);
    return c ? c->sky(Chunk::idx(x & 15, y, z & 15)) : 15;
}
int World::getBlockLight(int x, int y, int z) const {
    if (y < 0 || y >= CHUNK_H) return 0;
    Chunk* c = chunkAt(x, z);
    return c ? c->blk(Chunk::idx(x & 15, y, z & 15)) : 0;
}
int World::getLightLevel(int x, int y, int z, float dayFactor) const {
    int s = (int)(getSkyLight(x, y, z) * dayFactor + 0.5f);
    return std::max(s, getBlockLight(x, y, z));
}
int World::surfaceHeight(int x, int z) const {
    Chunk* c = chunkAt(x, z);
    return c ? c->height[(z & 15) * 16 + (x & 15)] : 0;
}

// Marks every section whose mesh can see block (x,y,z)
void World::markDirty(int x, int y, int z) {
    for (int dx = -1; dx <= 1; dx++) {
        if (dx != 0 && ((x + dx) >> 4) == (x >> 4)) continue;
        for (int dz = -1; dz <= 1; dz++) {
            if (dz != 0 && ((z + dz) >> 4) == (z >> 4)) continue;
            Chunk* c = chunkAt(x + dx, z + dz);
            if (!c) continue;
            for (int dy = -1; dy <= 1; dy++) {
                int yy = y + dy;
                if (yy < 0 || yy >= CHUNK_H) continue;
                if (dy != 0 && (yy >> 4) == (y >> 4)) continue;
                c->sectionDirty[yy >> 4] = true;
            }
        }
    }
}

// ---------------- lighting ----------------
void lightChunkInternal(Chunk& c) {
    c.recomputeAllHeights();
    std::vector<int> q;
    q.reserve(8192);
    // sky: straight down from the top, then spread sideways
    for (int z = 0; z < 16; z++)
        for (int x = 0; x < 16; x++) {
            int l = 15;
            for (int y = CHUNK_H - 1; y >= 0; y--) {
                int i = Chunk::idx(x, y, z);
                int op = g_blocks[c.blocks[i]].lightOpacity;
                if (op >= 15) l = 0; else l = std::max(0, l - op);
                c.light[i] = (uint8_t)(l << 4);
                if (l == 0 && y < c.height[z * 16 + x]) {
                    // everything below is dark until spread fills it
                    for (int yy = y - 1; yy >= 0; yy--) c.light[Chunk::idx(x, yy, z)] = 0;
                    break;
                }
            }
        }
    // seed spread: lit cells next to darker non-opaque cells
    for (int z = 0; z < 16; z++)
        for (int x = 0; x < 16; x++) {
            int top = c.height[z * 16 + x];
            for (int y = 0; y <= std::min(top + 1, CHUNK_H - 1); y++) {
                int i = Chunk::idx(x, y, z);
                if (c.sky(i) > 1) q.push_back(i);
            }
        }
    auto spread = [&](bool sky) {
        size_t head = 0;
        while (head < q.size()) {
            int i = q[head++];
            int x = i & 15, z = (i >> 4) & 15, y = i >> 8;
            int L = sky ? c.sky(i) : c.blk(i);
            if (L <= 1) continue;
            for (auto& d : DIRS) {
                int nx = x + d[0], ny = y + d[1], nz = z + d[2];
                if (nx < 0 || nx > 15 || nz < 0 || nz > 15 || ny < 0 || ny >= CHUNK_H) continue;
                int ni = Chunk::idx(nx, ny, nz);
                int op = g_blocks[c.blocks[ni]].lightOpacity;
                if (op >= 15) continue;
                int nl = L - 1 - op;
                if (sky && d[1] == -1 && L == 15 && op == 0) nl = 15;
                int cur = sky ? c.sky(ni) : c.blk(ni);
                if (nl > cur) { if (sky) c.setSky(ni, nl); else c.setBlk(ni, nl); q.push_back(ni); }
            }
        }
    };
    spread(true);
    // block light from emitters
    q.clear();
    for (int i = 0; i < CHUNK_VOL; i++) {
        int e = g_blocks[c.blocks[i]].lightEmit;
        if (e) { c.setBlk(i, e); q.push_back(i); }
    }
    spread(false);
}

void World::lightAdd(std::vector<ivec3>& queue, bool sky) {
    size_t head = 0;
    Chunk* cache = nullptr; int ccx = INT32_MIN, ccz = INT32_MIN;
    auto chunkFor = [&](int x, int z) -> Chunk* {
        if ((x >> 4) != ccx || (z >> 4) != ccz) { ccx = x >> 4; ccz = z >> 4; cache = getChunk(ccx, ccz); }
        return cache;
    };
    while (head < queue.size()) {
        ivec3 p = queue[head++];
        Chunk* c = chunkFor(p.x, p.z);
        if (!c) continue;
        int i = Chunk::idx(p.x & 15, p.y, p.z & 15);
        int L = sky ? c->sky(i) : c->blk(i);
        if (L <= 1) continue;
        for (auto& d : DIRS) {
            int nx = p.x + d[0], ny = p.y + d[1], nz = p.z + d[2];
            if (ny < 0 || ny >= CHUNK_H) continue;
            Chunk* nc = chunkFor(nx, nz);
            if (!nc) continue;
            int ni = Chunk::idx(nx & 15, ny, nz & 15);
            int op = g_blocks[nc->blocks[ni]].lightOpacity;
            if (op >= 15) continue;
            int nl = L - 1 - op;
            if (sky && d[1] == -1 && L == 15 && op == 0) nl = 15;
            int cur = sky ? nc->sky(ni) : nc->blk(ni);
            if (nl > cur) {
                if (sky) nc->setSky(ni, nl); else nc->setBlk(ni, nl);
                markDirty(nx, ny, nz);
                queue.push_back({nx, ny, nz});
            }
        }
    }
    queue.clear();
}

void World::lightRemove(std::vector<std::pair<ivec3, int>>& rq, std::vector<ivec3>& addQ, bool sky) {
    size_t head = 0;
    while (head < rq.size()) {
        ivec3 p = rq[head].first; int oldL = rq[head].second; head++;
        for (auto& d : DIRS) {
            int nx = p.x + d[0], ny = p.y + d[1], nz = p.z + d[2];
            if (ny < 0 || ny >= CHUNK_H) continue;
            Chunk* nc = chunkAt(nx, nz);
            if (!nc) continue;
            int ni = Chunk::idx(nx & 15, ny, nz & 15);
            int nl = sky ? nc->sky(ni) : nc->blk(ni);
            if (nl == 0) continue;
            bool dependent = nl < oldL || (sky && d[1] == -1 && oldL == 15 && nl == 15);
            if (dependent) {
                if (sky) nc->setSky(ni, 0); else nc->setBlk(ni, 0);
                markDirty(nx, ny, nz);
                int emit = g_blocks[nc->blocks[ni]].lightEmit;
                if (!sky && emit > 0) { nc->setBlk(ni, emit); addQ.push_back({nx, ny, nz}); }
                rq.push_back({{nx, ny, nz}, nl});
            } else addQ.push_back({nx, ny, nz});
        }
    }
    rq.clear();
}

void World::relightAt(int x, int y, int z) {
    Chunk* c = chunkAt(x, z);
    if (!c) return;
    int i = Chunk::idx(x & 15, y, z & 15);
    std::vector<std::pair<ivec3, int>> rq;
    std::vector<ivec3> addQ;
    for (int ch = 0; ch < 2; ch++) {
        bool sky = ch == 0;
        int old = sky ? c->sky(i) : c->blk(i);
        if (sky) c->setSky(i, 0); else c->setBlk(i, 0);
        markDirty(x, y, z);
        if (old > 0) { rq.push_back({{x, y, z}, old}); lightRemove(rq, addQ, sky); }
        const BlockDef& d = g_blocks[c->blocks[i]];
        if (!sky && d.lightEmit) { c->setBlk(i, d.lightEmit); addQ.push_back({x, y, z}); }
        if (sky && y == CHUNK_H - 1 && d.lightOpacity < 15) { c->setSky(i, 15 - d.lightOpacity); addQ.push_back({x, y, z}); }
        for (auto& dd : DIRS) {
            int ny = y + dd[1];
            if (ny >= 0 && ny < CHUNK_H) addQ.push_back({x + dd[0], ny, z + dd[2]});
        }
        lightAdd(addQ, sky);
    }
}

void World::stitchChunk(Chunk* c) {
    std::vector<ivec3> qs, qb;
    for (int dir = 0; dir < 4; dir++) {
        int ox = dir == 0 ? 1 : (dir == 1 ? -1 : 0), oz = dir == 2 ? 1 : (dir == 3 ? -1 : 0);
        Chunk* n = getChunk(c->cx + ox, c->cz + oz);
        if (!n) continue;
        int maxY = 0;
        for (int k = 0; k < 256; k++) maxY = std::max(maxY, (int)std::max(c->height[k], n->height[k]));
        maxY = std::min(CHUNK_H - 1, maxY + 2);
        for (int k = 0; k < 16; k++) {
            int cx_ = ox == 1 ? 15 : (ox == -1 ? 0 : k), cz_ = oz == 1 ? 15 : (oz == -1 ? 0 : k);
            int nx_ = ox == 1 ? 0 : (ox == -1 ? 15 : k), nz_ = oz == 1 ? 0 : (oz == -1 ? 15 : k);
            for (int y = 0; y <= maxY; y++) {
                int ci = Chunk::idx(cx_, y, cz_), ni = Chunk::idx(nx_, y, nz_);
                ivec3 cp(c->cx * 16 + cx_, y, c->cz * 16 + cz_), np(n->cx * 16 + nx_, y, n->cz * 16 + nz_);
                int cs = c->sky(ci), ns = n->sky(ni), cb = c->blk(ci), nb = n->blk(ni);
                if (cs > ns + 1) qs.push_back(cp); else if (ns > cs + 1) qs.push_back(np);
                if (cb > nb + 1) qb.push_back(cp); else if (nb > cb + 1) qb.push_back(np);
            }
        }
    }
    lightAdd(qs, true);
    lightAdd(qb, false);
    c->state = CS_STITCHED;
}

// ---------------- block edits ----------------
void World::setBlock(int x, int y, int z, uint16_t id, uint8_t meta, bool notify) {
    if (y < 0 || y >= CHUNK_H) return;
    Chunk* c = chunkAt(x, z);
    if (!c) return;
    int lx = x & 15, lz = z & 15;
    int i = Chunk::idx(lx, y, lz);
    uint16_t old = c->blocks[i];
    uint8_t oldMeta = c->meta[i];
    if (old == id && oldMeta == meta) return;
    c->blocks[i] = id;
    c->meta[i] = meta;
    c->modified = true;
    if (old != id) {
        auto it = c->tiles.find(i);
        if (it != c->tiles.end() && !((old == FURNACE || old == FURNACE_LIT) && (id == FURNACE || id == FURNACE_LIT))) c->tiles.erase(it);
    }
    // heightmap
    int hi = lz * 16 + lx;
    if (g_blocks[id].lightOpacity > 0) { if (y >= c->height[hi]) c->height[hi] = (uint8_t)std::min(255, y + 1); }
    else if (y + 1 >= c->height[hi]) c->recomputeHeight(lx, lz);
    markDirty(x, y, z);
    const BlockDef& od = g_blocks[old];
    const BlockDef& nd = g_blocks[id];
    if (od.lightOpacity != nd.lightOpacity || od.lightEmit != nd.lightEmit) relightAt(x, y, z);
    if (notify) {
        neighborChanged(x, y, z);
        if (nd.liquid) scheduleUpdate(x, y, z, id == WATER ? 5 : 30);
        if (nd.gravity || nd.needsSupport) scheduleUpdate(x, y, z, 1);
    }
}

void World::setMeta(int x, int y, int z, uint8_t meta) {
    if (y < 0 || y >= CHUNK_H) return;
    Chunk* c = chunkAt(x, z);
    if (!c) return;
    int i = Chunk::idx(x & 15, y, z & 15);
    if (c->meta[i] == meta) return;
    c->meta[i] = meta;
    c->modified = true;
    markDirty(x, y, z);
}

TileEntity* World::getTile(int x, int y, int z) {
    Chunk* c = chunkAt(x, z);
    if (!c || y < 0 || y >= CHUNK_H) return nullptr;
    auto it = c->tiles.find(Chunk::idx(x & 15, y, z & 15));
    return it == c->tiles.end() ? nullptr : &it->second;
}
TileEntity* World::createTile(int x, int y, int z, uint8_t type) {
    Chunk* c = chunkAt(x, z);
    if (!c || y < 0 || y >= CHUNK_H) return nullptr;
    TileEntity& t = c->tiles[Chunk::idx(x & 15, y, z & 15)];
    t = TileEntity(); t.type = type;
    c->modified = true;
    return &t;
}
void World::removeTile(int x, int y, int z) {
    Chunk* c = chunkAt(x, z);
    if (!c || y < 0 || y >= CHUNK_H) return;
    c->tiles.erase(Chunk::idx(x & 15, y, z & 15));
}

// ---------------- shapes & queries ----------------
static AABB px16(float x0, float y0, float z0, float x1, float y1, float z1) { return AABB(vec3(x0, y0, z0) / 16.f, vec3(x1, y1, z1) / 16.f); }

static AABB doorBox(uint8_t meta) {
    int f = meta & 3; bool open = meta & 4;
    int dir = open ? ((f + 1) & 3) : f;
    float x0 = 0, x1 = 16, z0 = 0, z1 = 16;
    int dx = facingDX(dir), dz = facingDZ(dir);
    if (dz > 0) z1 = 3; else if (dz < 0) z0 = 13; else if (dx > 0) x1 = 3; else x0 = 13;
    return px16(x0, 0, z0, x1, 16, z1);
}

void World::blockBoxes(uint16_t id, uint8_t meta, std::vector<AABB>& out) {
    const BlockDef& d = g_blocks[id];
    if (!d.solid) return;
    switch (d.shape) {
        case SHAPE_CHEST: out.push_back(px16(1, 0, 1, 15, 14, 15)); break;
        case SHAPE_BED: out.push_back(px16(0, 0, 0, 16, 9, 16)); break;
        case SHAPE_FARMLAND: out.push_back(px16(0, 0, 0, 16, 15, 16)); break;
        case SHAPE_CACTUS: out.push_back(px16(1, 0, 1, 15, 16, 15)); break;
        case SHAPE_DOOR: out.push_back(doorBox(meta)); break;
        default: out.push_back(AABB(vec3(0, 0, 0), vec3(1, 1, 1))); break;
    }
}

bool World::selectionBox(uint16_t id, uint8_t meta, AABB& box) {
    const BlockDef& d = g_blocks[id];
    if (id == AIR || d.liquid) return false;
    switch (d.shape) {
        case SHAPE_TORCH:
            if (meta == 0) box = px16(6, 0, 6, 10, 10, 10);
            else { int f = meta - 1; float ox = -facingDX(f) * 5.f, oz = -facingDZ(f) * 5.f; box = px16(5.5f + ox, 3, 5.5f + oz, 10.5f + ox, 13, 10.5f + oz); }
            return true;
        case SHAPE_CROSS: box = px16(2, 0, 2, 14, 13, 14); return true;
        case SHAPE_CROP: box = px16(0, 0, 0, 16, 2 + meta * 2, 16); return true;
        case SHAPE_LADDER: {
            int f = meta & 3; int wdx = -facingDX(f), wdz = -facingDZ(f);
            if (wdx > 0) box = px16(13, 0, 0, 16, 16, 16); else if (wdx < 0) box = px16(0, 0, 0, 3, 16, 16);
            else if (wdz > 0) box = px16(0, 0, 13, 16, 16, 16); else box = px16(0, 0, 0, 16, 16, 3);
            return true;
        }
        default: {
            std::vector<AABB> b; blockBoxes(id, meta, b);
            if (b.empty()) box = AABB(vec3(0, 0, 0), vec3(1, 1, 1)); else box = b[0];
            return true;
        }
    }
}

RayHit World::raycast(vec3 o, vec3 dir, float maxDist, bool hitLiquids) const {
    RayHit hit;
    dir = normalize(dir);
    int x = ifloor(o.x), y = ifloor(o.y), z = ifloor(o.z);
    int sx = dir.x > 0 ? 1 : -1, sy = dir.y > 0 ? 1 : -1, sz = dir.z > 0 ? 1 : -1;
    auto tdelta = [](float d) { return std::fabs(d) < 1e-9f ? 1e30f : std::fabs(1.0f / d); };
    float tdx = tdelta(dir.x), tdy = tdelta(dir.y), tdz = tdelta(dir.z);
    auto tmax0 = [](float o, float d, int i) { if (std::fabs(d) < 1e-9f) return 1e30f; float b = d > 0 ? (i + 1 - o) : (o - i); return b / std::fabs(d); };
    float tmx = tmax0(o.x, dir.x, x), tmy = tmax0(o.y, dir.y, y), tmz = tmax0(o.z, dir.z, z);
    ivec3 n(0, 0, 0);
    float t = 0;
    for (int step = 0; step < 256 && t <= maxDist; step++) {
        uint16_t id = getBlock(x, y, z);
        if (id != AIR && (!g_blocks[id].liquid || (hitLiquids && (getMeta(x, y, z) & 7) == 0))) {
            AABB box;
            bool sel = g_blocks[id].liquid ? true : selectionBox(id, getMeta(x, y, z), box);
            if (g_blocks[id].liquid) box = AABB(vec3(0, 0, 0), vec3(1, 1, 1));
            if (sel) {
                AABB wb = box.offset(vec3((float)x, (float)y, (float)z));
                float th = wb.rayHit(o, dir, maxDist);
                if (th >= 0) {
                    hit.hit = true; hit.block = {x, y, z}; hit.dist = th;
                    // face normal from the hit point
                    vec3 hp = o + dir * th;
                    float best = 1e9; ivec3 bn = n;
                    const float eps = 1e-3f;
                    if (std::fabs(hp.x - wb.mn.x) < eps) { best = 0; bn = {-1, 0, 0}; }
                    else if (std::fabs(hp.x - wb.mx.x) < eps) { best = 0; bn = {1, 0, 0}; }
                    else if (std::fabs(hp.y - wb.mn.y) < eps) { best = 0; bn = {0, -1, 0}; }
                    else if (std::fabs(hp.y - wb.mx.y) < eps) { best = 0; bn = {0, 1, 0}; }
                    else if (std::fabs(hp.z - wb.mn.z) < eps) { best = 0; bn = {0, 0, -1}; }
                    else if (std::fabs(hp.z - wb.mx.z) < eps) { best = 0; bn = {0, 0, 1}; }
                    (void)best;
                    if (bn == ivec3(0, 0, 0)) bn = n;
                    hit.normal = bn;
                    return hit;
                }
            }
        }
        if (tmx < tmy && tmx < tmz) { x += sx; t = tmx; tmx += tdx; n = {-sx, 0, 0}; }
        else if (tmy < tmz) { y += sy; t = tmy; tmy += tdy; n = {0, -sy, 0}; }
        else { z += sz; t = tmz; tmz += tdz; n = {0, 0, -sz}; }
    }
    return hit;
}

void World::getCollisionBoxes(const AABB& a, std::vector<AABB>& out) const {
    int x0 = ifloor(a.mn.x), x1 = ifloor(a.mx.x), y0 = ifloor(a.mn.y) - 1, y1 = ifloor(a.mx.y), z0 = ifloor(a.mn.z), z1 = ifloor(a.mx.z);
    std::vector<AABB> tmp;
    for (int x = x0; x <= x1; x++)
        for (int z = z0; z <= z1; z++) {
            Chunk* c = chunkAt(x, z);
            for (int y = y0; y <= y1; y++) {
                if (y < 0) { out.push_back(AABB(vec3((float)x, (float)y, (float)z), vec3((float)x + 1, (float)y + 1, (float)z + 1))); continue; }
                if (y >= CHUNK_H) continue;
                if (!c) {  // unloaded chunks act as walls so nothing falls out of the world
                    out.push_back(AABB(vec3((float)x, (float)y, (float)z), vec3((float)x + 1, (float)y + 1, (float)z + 1)));
                    continue;
                }
                int i = Chunk::idx(x & 15, y, z & 15);
                uint16_t id = c->blocks[i];
                if (!g_blocks[id].solid) continue;
                tmp.clear();
                blockBoxes(id, c->meta[i], tmp);
                for (auto& b : tmp) out.push_back(b.offset(vec3((float)x, (float)y, (float)z)));
            }
        }
}

bool World::isAreaLiquid(const AABB& a, uint16_t liquid) const {
    int x0 = ifloor(a.mn.x), x1 = ifloor(a.mx.x - 0.001f), y0 = ifloor(a.mn.y), y1 = ifloor(a.mx.y - 0.001f), z0 = ifloor(a.mn.z), z1 = ifloor(a.mx.z - 0.001f);
    for (int x = x0; x <= x1; x++)
        for (int y = y0; y <= y1; y++)
            for (int z = z0; z <= z1; z++) {
                if (getBlock(x, y, z) != liquid) continue;
                int lvl = getMeta(x, y, z) & 7;
                if (getMeta(x, y, z) & 8) lvl = 0;
                float top = y + 1 - (lvl + 1) / 9.0f;
                if (getBlock(x, y + 1, z) == liquid) top = (float)y + 1;
                if (a.mn.y < top) return true;
            }
    return false;
}

int liquidLevel(uint16_t id, uint8_t meta) {
    if (!g_blocks[id].liquid) return -1;
    return (meta & 8) ? 0 : (meta & 7);
}
