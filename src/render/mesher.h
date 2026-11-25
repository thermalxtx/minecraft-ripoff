// Chunk section meshing. Runs on worker threads from a copied snapshot of the section plus a 1-block border.
#pragma once
#include <cstdint>
#include <vector>
#include "../world/chunk.h"

constexpr int SNAP = 18;  // 16 + border on each side
struct MeshSnapshot {
    int cx, cz, sy;
    uint32_t ticket;
    uint16_t blocks[SNAP * SNAP * SNAP];
    uint8_t meta[SNAP * SNAP * SNAP];
    uint8_t light[SNAP * SNAP * SNAP];
    static int idx(int x, int y, int z) { return ((y + 1) * SNAP + (z + 1)) * SNAP + (x + 1); }
};

struct MeshResult {
    int cx, cz, sy;
    uint32_t ticket;
    std::vector<uint32_t> verts[3];  // per render layer, 2 uint32 per vertex, 4 vertices per quad
};

// Vertex flags
enum { VF_SWAY = 1, VF_WAVE = 2, VF_ANIM = 4, VF_EMISSIVE = 8 };

void buildSectionMesh(const MeshSnapshot& s, MeshResult& out);
