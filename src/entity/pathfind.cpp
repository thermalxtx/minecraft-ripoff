// A* search over walkable block positions, limited in range and node count.
#include "entity.h"
#include "../world/world.h"
#include <queue>
#include <unordered_map>
#include <algorithm>

namespace {
bool passable(const World& w, int x, int y, int z) {
    uint16_t b = w.getBlock(x, y, z);
    if (b == LAVA || b == CACTUS) return false;
    const BlockDef& d = blockDef(b);
    if (b == DOOR) return (w.getMeta(x, y, z) & 4) != 0;  // open doors only
    return !d.solid;
}
bool standable(const World& w, int x, int y, int z, int h, bool climber) {
    if (y <= 0 || y + h >= CHUNK_H) return false;
    if (!w.isLoaded(x, z)) return false;
    for (int k = 0; k < h; k++) if (!passable(w, x, y + k, z)) return false;
    uint16_t below = w.getBlock(x, y - 1, z);
    if (below == LAVA || below == CACTUS) return false;
    if (blockDef(below).solid || below == WATER || blockDef(w.getBlock(x, y, z)).climbable) return true;
    if (w.getBlock(x, y, z) == WATER) return true;
    if (climber) {  // wall climbers can hang next to a solid block
        static const int D[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (auto& d : D) if (blockDef(w.getBlock(x + d[0], y, z + d[1])).solid) return true;
    }
    return false;
}
struct Node { ivec3 p; float g, f; };
struct Cmp { bool operator()(const Node& a, const Node& b) const { return a.f > b.f; } };
uint64_t key(ivec3 p) { return ((uint64_t)(uint32_t)(p.x & 0xFFFFF) << 40) | ((uint64_t)(uint32_t)(p.z & 0xFFFFF) << 20) | (uint64_t)(p.y & 0xFFFFF); }
float heur(ivec3 a, ivec3 b) { return (float)(std::abs(a.x - b.x) + std::abs(a.y - b.y) + std::abs(a.z - b.z)); }
}

bool findPath(const World& w, ivec3 start, ivec3 goal, int h, bool climber, int maxNodes, std::vector<ivec3>& out) {
    out.clear();
    if (heur(start, goal) > 48) return false;
    // drop the start down to the floor if we're mid-air
    for (int k = 0; k < 3 && !standable(w, start.x, start.y, start.z, h, climber); k++) start.y--;
    if (!standable(w, start.x, start.y, start.z, h, climber)) start.y += 3;
    std::priority_queue<Node, std::vector<Node>, Cmp> open;
    std::unordered_map<uint64_t, std::pair<ivec3, float>> came;  // node -> (parent, g)
    came.reserve(maxNodes * 2);
    open.push({start, 0, heur(start, goal)});
    came[key(start)] = {start, 0};
    ivec3 best = start; float bestH = heur(start, goal);
    int expanded = 0;
    static const int D[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    while (!open.empty() && expanded < maxNodes) {
        Node n = open.top(); open.pop();
        auto it = came.find(key(n.p));
        if (it != came.end() && n.g > it->second.second + 1e-4f) continue;
        expanded++;
        float hh = heur(n.p, goal);
        if (hh < bestH) { bestH = hh; best = n.p; }
        if (n.p == goal || (std::abs(n.p.x - goal.x) <= 0 && std::abs(n.p.z - goal.z) <= 0 && std::abs(n.p.y - goal.y) <= 1)) { best = n.p; break; }
        if (std::abs(n.p.x - start.x) > 32 || std::abs(n.p.z - start.z) > 32) continue;
        for (int di = 0; di < 8; di++) {
            int dx = D[di][0], dz = D[di][1];
            bool diag = dx != 0 && dz != 0;
            if (diag) {  // no corner cutting
                if (!passable(w, n.p.x + dx, n.p.y, n.p.z) || !passable(w, n.p.x, n.p.y, n.p.z + dz)) continue;
                if (h > 1 && (!passable(w, n.p.x + dx, n.p.y + 1, n.p.z) || !passable(w, n.p.x, n.p.y + 1, n.p.z + dz))) continue;
            }
            int nx = n.p.x + dx, nz = n.p.z + dz;
            ivec3 cand; float cost = diag ? 1.414f : 1.0f; bool ok = false;
            if (standable(w, nx, n.p.y, nz, h, climber)) { cand = {nx, n.p.y, nz}; ok = true; }
            else if (!diag && standable(w, nx, n.p.y + 1, nz, h, climber) && passable(w, n.p.x, n.p.y + h, n.p.z)) { cand = {nx, n.p.y + 1, nz}; cost += 1.0f; ok = true; }
            else if (!diag) {
                for (int drop = 1; drop <= 3; drop++)
                    if (standable(w, nx, n.p.y - drop, nz, h, climber)) {
                        bool clear = true;
                        for (int k = 0; k < drop; k++) if (!passable(w, nx, n.p.y - k + h - 1, nz) && !passable(w, nx, n.p.y - k, nz)) clear = false;
                        if (clear) { cand = {nx, n.p.y - drop, nz}; cost += drop * 0.5f; ok = true; }
                        break;
                    }
            }
            if (!ok && climber && !diag && standable(w, n.p.x, n.p.y + 1, n.p.z, h, true)) { cand = {n.p.x, n.p.y + 1, n.p.z}; cost = 1.5f; ok = true; }
            if (!ok) continue;
            if (w.getBlock(cand.x, cand.y, cand.z) == WATER) cost += 2.0f;
            float g = n.g + cost;
            uint64_t k = key(cand);
            auto f = came.find(k);
            if (f != came.end() && f->second.second <= g) continue;
            came[k] = {n.p, g};
            open.push({cand, g, g + heur(cand, goal) * 1.1f});
        }
    }
    // reconstruct toward the best node reached
    ivec3 cur = best;
    int guard = 0;
    while (guard++ < 4096) {
        out.push_back(cur);
        if (cur == start) break;
        auto it = came.find(key(cur));
        if (it == came.end()) break;
        cur = it->second.first;
    }
    std::reverse(out.begin(), out.end());
    return best == goal;
}
