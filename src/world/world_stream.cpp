// Chunk streaming: generation/loading and meshing on worker threads, results handed back to the main thread.
#include "world.h"
#include "worldgen.h"
#include "../io/region.h"
#include "../core/jobs.h"
#include "../render/mesher.h"
#include <algorithm>

static uint32_t g_meshTicketCounter = 0;

int World::pendingJobs() const { return (int)genPending.size() + meshJobsInFlight; }

bool World::neighborsReady(Chunk* c) const {
    for (int dx = -1; dx <= 1; dx++)
        for (int dz = -1; dz <= 1; dz++) {
            Chunk* n = getChunk(c->cx + dx, c->cz + dz);
            if (!n || n->state != CS_STITCHED) return false;
        }
    return true;
}

bool World::isChunkReady(int cx, int cz) const {
    Chunk* c = getChunk(cx, cz);
    return c && c->everMeshed;
}

bool World::spawnAreaReady(int x, int z, int radius) const {
    int cx = x >> 4, cz = z >> 4;
    for (int dx = -radius; dx <= radius; dx++)
        for (int dz = -radius; dz <= radius; dz++) {
            Chunk* c = getChunk(cx + dx, cz + dz);
            if (!c || c->state != CS_STITCHED) return false;
        }
    return true;
}

void World::updateStreaming(vec3 playerPos) {
    int pcx = ifloor(playerPos.x) >> 4, pcz = ifloor(playerPos.z) >> 4;
    int loadR = renderDistance + 1;
    if (spiralRadius != loadR) {
        spiral.clear();
        for (int dx = -loadR; dx <= loadR; dx++)
            for (int dz = -loadR; dz <= loadR; dz++)
                if (dx * dx + dz * dz <= (loadR + 1) * (loadR + 1)) spiral.push_back({dx, 0, dz});
        std::sort(spiral.begin(), spiral.end(), [](const ivec3& a, const ivec3& b) { return a.x * a.x + a.z * a.z < b.x * b.x + b.z * b.z; });
        spiralRadius = loadR;
    }
    // Request missing chunks, nearest first
    int maxInFlight = std::max(4, jobs->threadCount() * 3);
    for (auto& o : spiral) {
        if ((int)genPending.size() >= maxInFlight) break;
        int cx = pcx + o.x, cz = pcz + o.z;
        int64_t key = chunkKey(cx, cz);
        if (chunks.count(key) || genPending.count(key)) continue;
        genPending.insert(key);
        TerrainGen* g = gen.get();
        RegionStore* rs = region.get();
        jobs->submit([this, g, rs, cx, cz] {
            Chunk* c = new Chunk();
            c->cx = cx; c->cz = cz;
            if (!rs->load(cx, cz, *c)) {
                memset(c->blocks, 0, sizeof(c->blocks));
                memset(c->meta, 0, sizeof(c->meta));
                c->tiles.clear(); c->savedEntities.clear();
                g->generate(*c);
            } else c->modified = true;
            lightChunkInternal(*c);
            std::lock_guard<std::mutex> l(resultMutex);
            genResults.push_back(c);
        });
    }
    // Unload far chunks
    int unloadR = loadR + 2;
    std::vector<int64_t> drop;
    for (auto& kv : chunks) {
        Chunk* c = kv.second;
        if (std::abs(c->cx - pcx) > unloadR || std::abs(c->cz - pcz) > unloadR) drop.push_back(kv.first);
    }
    for (int64_t k : drop) {
        Chunk* c = chunks[k];
        chunks.erase(k);
        unloadChunk(c);
    }
    lastCenterX = pcx; lastCenterZ = pcz;
}

void World::unloadChunk(Chunk* c) {
    if (onChunkUnload) onChunkUnload(callbackUser, c);
    if (c->modified || !c->savedEntities.empty()) region->store(*c);
    if (freeChunkGPU) freeChunkGPU(c);
    delete c;
}

void World::processJobResults(int maxGen, int maxMesh) {
    std::vector<Chunk*> gens;
    std::vector<MeshResult*> meshes;
    {
        std::lock_guard<std::mutex> l(resultMutex);
        int n = std::min((int)genResults.size(), maxGen);
        gens.assign(genResults.begin(), genResults.begin() + n);
        genResults.erase(genResults.begin(), genResults.begin() + n);
        int m = std::min((int)meshResults.size(), maxMesh);
        meshes.assign(meshResults.begin(), meshResults.begin() + m);
        meshResults.erase(meshResults.begin(), meshResults.begin() + m);
    }
    int unloadR = spiralRadius + 2;
    for (Chunk* c : gens) {
        int64_t key = chunkKey(c->cx, c->cz);
        genPending.erase(key);
        if (chunks.count(key) || std::abs(c->cx - lastCenterX) > unloadR || std::abs(c->cz - lastCenterZ) > unloadR) { delete c; continue; }
        chunks[key] = c;
        stitchChunk(c);
        newlyReadyChunks.push_back({key, c});
    }
    for (MeshResult* r : meshes) {
        meshJobsInFlight--;
        Chunk* c = getChunk(r->cx, r->cz);
        if (c) {
            c->sectionInFlight[r->sy] = false;
            if (c->meshTicket[r->sy] == r->ticket && uploadMesh) { uploadMesh(c, r->sy, *r); c->everMeshed = true; }
        }
        delete r;
    }
}

void World::buildSnapshot(Chunk* c, int sy, MeshSnapshot& s) const {
    s.cx = c->cx; s.cz = c->cz; s.sy = sy;
    Chunk* nb[3][3];
    for (int dx = -1; dx <= 1; dx++)
        for (int dz = -1; dz <= 1; dz++) nb[dx + 1][dz + 1] = getChunk(c->cx + dx, c->cz + dz);
    int baseY = sy * 16;
    for (int y = -1; y <= 16; y++) {
        int wy = baseY + y;
        for (int z = -1; z <= 16; z++)
            for (int x = -1; x <= 16; x++) {
                int si = MeshSnapshot::idx(x, y, z);
                if (wy < 0) { s.blocks[si] = BEDROCK; s.meta[si] = 0; s.light[si] = 0; continue; }
                if (wy >= CHUNK_H) { s.blocks[si] = AIR; s.meta[si] = 0; s.light[si] = 0xF0; continue; }
                int cxo = x < 0 ? 0 : (x > 15 ? 2 : 1), czo = z < 0 ? 0 : (z > 15 ? 2 : 1);
                Chunk* n = nb[cxo][czo];
                if (!n) { s.blocks[si] = STONE; s.meta[si] = 0; s.light[si] = 0xF0; continue; }
                int i = Chunk::idx(x & 15, wy, z & 15);
                s.blocks[si] = n->blocks[i]; s.meta[si] = n->meta[i]; s.light[si] = n->light[i];
            }
    }
}

static bool sectionIsEmpty(const Chunk* c, int sy) {
    const uint16_t* b = c->blocks + sy * 16 * 256;
    for (int i = 0; i < 4096; i++) if (b[i]) return false;
    return true;
}

void World::scheduleMeshes(vec3 cam) {
    int maxInFlight = std::max(4, jobs->threadCount() * 4);
    if (meshJobsInFlight >= maxInFlight) return;
    struct Cand { float d; Chunk* c; int sy; };
    std::vector<Cand> cands;
    for (auto& kv : chunks) {
        Chunk* c = kv.second;
        if (c->state != CS_STITCHED) continue;
        bool any = false;
        for (int s = 0; s < SECTIONS; s++) if (c->sectionDirty[s] && !c->sectionInFlight[s]) { any = true; break; }
        if (!any || !neighborsReady(c)) continue;
        float cxw = c->cx * 16 + 8 - cam.x, czw = c->cz * 16 + 8 - cam.z;
        for (int s = 0; s < SECTIONS; s++) {
            if (!c->sectionDirty[s] || c->sectionInFlight[s]) continue;
            if (sectionIsEmpty(c, s)) {
                c->sectionDirty[s] = false;
                MeshResult empty; empty.cx = c->cx; empty.cz = c->cz; empty.sy = s;
                if (uploadMesh) uploadMesh(c, s, empty);
                c->everMeshed = true;
                continue;
            }
            float cyw = s * 16 + 8 - cam.y;
            cands.push_back({cxw * cxw + czw * czw + cyw * cyw * 0.5f, c, s});
        }
    }
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.d < b.d; });
    for (auto& cd : cands) {
        if (meshJobsInFlight >= maxInFlight) break;
        MeshSnapshot* snap = new MeshSnapshot();
        buildSnapshot(cd.c, cd.sy, *snap);
        cd.c->meshTicket[cd.sy] = snap->ticket = ++g_meshTicketCounter;
        cd.c->sectionDirty[cd.sy] = false;
        cd.c->sectionInFlight[cd.sy] = true;
        meshJobsInFlight++;
        jobs->submit([this, snap] {
            MeshResult* r = new MeshResult();
            buildSectionMesh(*snap, *r);
            delete snap;
            std::lock_guard<std::mutex> l(resultMutex);
            meshResults.push_back(r);
        });
    }
}

void World::meshSectionNow(Chunk* c, int sy) {
    if (!c || c->state != CS_STITCHED || !neighborsReady(c)) return;
    MeshSnapshot* snap = new MeshSnapshot();
    buildSnapshot(c, sy, *snap);
    c->meshTicket[sy] = snap->ticket = ++g_meshTicketCounter;
    MeshResult r;
    buildSectionMesh(*snap, r);
    delete snap;
    c->sectionDirty[sy] = false;
    if (uploadMesh) uploadMesh(c, sy, r);
}

void World::saveAll() {
    for (auto& kv : chunks) {
        Chunk* c = kv.second;
        if (onChunkSave) onChunkSave(callbackUser, c);
        if (c->modified || !c->savedEntities.empty()) region->store(*c);
        c->savedEntities.clear();  // the live entities stay in the world
    }
    region->flush();
}
