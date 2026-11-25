// Builds packed 8-byte vertices for one 16x16x16 section. Hidden faces are skipped;
// full cubes get smooth lighting + ambient occlusion from the 3 blocks around each corner.
#include "mesher.h"
#include "textures.h"
#include "../core/math.h"
#include <cstring>

namespace {
// Unit-cube corners for each face, CCW seen from outside (see block.h face order)
const int FACE_CORNERS[6][4][3] = {
    {{1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}},  // +X
    {{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}},  // -X
    {{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}},  // +Y
    {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}},  // -Y
    {{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}},  // +Z
    {{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}},  // -Z
};
const int FACE_DIR[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
const float AO_UNUSED = 0;

struct Builder {
    const MeshSnapshot& s;
    MeshResult& out;
    Builder(const MeshSnapshot& snap, MeshResult& o) : s(snap), out(o) {}

    uint16_t B(int x, int y, int z) const { return s.blocks[MeshSnapshot::idx(x, y, z)]; }
    uint8_t M(int x, int y, int z) const { return s.meta[MeshSnapshot::idx(x, y, z)]; }
    uint8_t L(int x, int y, int z) const { return s.light[MeshSnapshot::idx(x, y, z)]; }
    bool opq(int x, int y, int z) const { return g_blocks[B(x, y, z)].opaque; }

    static uint32_t packA(int x, int y, int z, int n, int ao) {
        return (uint32_t)(x & 511) | ((uint32_t)(y & 511) << 9) | ((uint32_t)(z & 511) << 18) | ((uint32_t)n << 27) | ((uint32_t)ao << 30);
    }
    static uint32_t packB(int layer, int u, int v, int sky, int blk, int flags) {
        return (uint32_t)(layer & 1023) | ((uint32_t)(u & 31) << 10) | ((uint32_t)(v & 31) << 15) |
               ((uint32_t)(sky & 15) << 20) | ((uint32_t)(blk & 15) << 24) | ((uint32_t)(flags & 15) << 28);
    }
    void vert(int layer, int px, int py, int pz, int n, int ao, int tex, int u, int v, int sky, int blk, int flags) {
        auto& vv = out.verts[layer];
        vv.push_back(packA(px, py, pz, n, ao));
        vv.push_back(packB(tex, u, v, sky, blk, flags));
    }
    static void faceUV(int f, int x, int y, int z, int& u, int& v) {
        switch (f) {
            case FACE_PX: u = 16 - z; v = 16 - y; break;
            case FACE_NX: u = z; v = 16 - y; break;
            case FACE_PY: u = x; v = z; break;
            case FACE_NY: u = x; v = 16 - z; break;
            case FACE_PZ: u = x; v = 16 - y; break;
            default: u = 16 - x; v = 16 - y; break;
        }
    }
    static void rotUV(int rot, int& u, int& v) {
        for (int i = 0; i < (rot & 3); i++) { int t = u; u = 16 - v; v = t; }
    }

    // Full cube face with smooth lighting and AO
    void cubeFace(int layer, int bx, int by, int bz, int f, int tex, int flags) {
        int nx = bx + FACE_DIR[f][0], ny = by + FACE_DIR[f][1], nz = bz + FACE_DIR[f][2];
        int axisN = f / 2;
        int t1 = (axisN + 1) % 3, t2 = (axisN + 2) % 3;
        int aoV[4], skyV[4], blkV[4];
        for (int c = 0; c < 4; c++) {
            const int* cc = FACE_CORNERS[f][c];
            int o1[3] = {0, 0, 0}, o2[3] = {0, 0, 0};
            o1[t1] = cc[t1] ? 1 : -1;
            o2[t2] = cc[t2] ? 1 : -1;
            bool s1 = opq(nx + o1[0], ny + o1[1], nz + o1[2]);
            bool s2 = opq(nx + o2[0], ny + o2[1], nz + o2[2]);
            bool cr = opq(nx + o1[0] + o2[0], ny + o1[1] + o2[1], nz + o1[2] + o2[2]);
            aoV[c] = (s1 && s2) ? 0 : 3 - ((int)s1 + (int)s2 + (int)cr);
            int sumS = 0, sumB = 0, cnt = 0;
            auto addL = [&](int x, int y, int z) { uint8_t l = L(x, y, z); sumS += l >> 4; sumB += l & 15; cnt++; };
            addL(nx, ny, nz);
            if (!s1) addL(nx + o1[0], ny + o1[1], nz + o1[2]);
            if (!s2) addL(nx + o2[0], ny + o2[1], nz + o2[2]);
            if (!cr && !(s1 && s2)) addL(nx + o1[0] + o2[0], ny + o1[1] + o2[1], nz + o1[2] + o2[2]);
            skyV[c] = (sumS * 2 + cnt) / (cnt * 2);
            blkV[c] = (sumB * 2 + cnt) / (cnt * 2);
        }
        int order[4] = {0, 1, 2, 3};
        if (aoV[0] + aoV[2] < aoV[1] + aoV[3]) { order[0] = 1; order[1] = 2; order[2] = 3; order[3] = 0; }
        for (int k = 0; k < 4; k++) {
            int c = order[k];
            const int* cc = FACE_CORNERS[f][c];
            int u, v; faceUV(f, cc[0] * 16, cc[1] * 16, cc[2] * 16, u, v);
            vert(layer, (bx + cc[0]) * 16, (by + cc[1]) * 16, (bz + cc[2]) * 16, f, aoV[c], tex, u, v, skyV[c], blkV[c], flags);
        }
    }

    // Axis-aligned box (in 1/16 units relative to the block); flat lighting from `lightSrc`
    void box(int layer, int bx, int by, int bz, int x0, int y0, int z0, int x1, int y1, int z1, const uint16_t tex[6],
             unsigned faceMask, uint8_t light, int flags = 0, int topRot = 0, bool shade = true) {
        int mn[3] = {x0, y0, z0}, mx[3] = {x1, y1, z1};
        for (int f = 0; f < 6; f++) {
            if (!(faceMask & (1u << f))) continue;
            for (int c = 0; c < 4; c++) {
                const int* cc = FACE_CORNERS[f][c];
                int p[3];
                for (int a = 0; a < 3; a++) p[a] = cc[a] ? mx[a] : mn[a];
                int u, v; faceUV(f, p[0], p[1], p[2], u, v);
                if (f == FACE_PY && topRot) rotUV(topRot, u, v);
                vert(layer, bx * 16 + p[0], by * 16 + p[1], bz * 16 + p[2], shade ? f : 6, 3, tex[f], u, v, light >> 4, light & 15, flags);
            }
        }
    }

    // A single quad from explicit corner positions (1/16 units, section space)
    void quad(int layer, const int p[4][3], const int uv[4][2], int tex, uint8_t light, int n, int flagsTop, int flagsBottom) {
        for (int c = 0; c < 4; c++)
            vert(layer, p[c][0], p[c][1], p[c][2], n, 3, tex, uv[c][0], uv[c][1], light >> 4, light & 15, (c >= 2) ? flagsTop : flagsBottom);
    }

    void cross(int layer, int bx, int by, int bz, int tex, uint8_t light, int flags, int inset = 2) {
        int X = bx * 16, Y = by * 16, Z = bz * 16;
        int a = inset, b = 16 - inset;
        const int uv[4][2] = {{0, 16}, {16, 16}, {16, 0}, {0, 0}};
        int q1[4][3] = {{X + a, Y, Z + a}, {X + b, Y, Z + b}, {X + b, Y + 16, Z + b}, {X + a, Y + 16, Z + a}};
        int q2[4][3] = {{X + a, Y, Z + b}, {X + b, Y, Z + a}, {X + b, Y + 16, Z + a}, {X + a, Y + 16, Z + b}};
        quad(layer, q1, uv, tex, light, 6, flags, 0);
        quad(layer, q2, uv, tex, light, 6, flags, 0);
    }

    void crop(int bx, int by, int bz, int tex, uint8_t light) {
        int X = bx * 16, Y = by * 16, Z = bz * 16;
        const int uv[4][2] = {{0, 16}, {16, 16}, {16, 0}, {0, 0}};
        for (int k : {4, 12}) {
            int qa[4][3] = {{X + k, Y, Z}, {X + k, Y, Z + 16}, {X + k, Y + 16, Z + 16}, {X + k, Y + 16, Z}};
            int qb[4][3] = {{X, Y, Z + k}, {X + 16, Y, Z + k}, {X + 16, Y + 16, Z + k}, {X, Y + 16, Z + k}};
            quad(LAYER_CUTOUT, qa, uv, tex, light, 6, VF_SWAY, 0);
            quad(LAYER_CUTOUT, qb, uv, tex, light, 6, VF_SWAY, 0);
        }
    }

    int liquidHeightAt(int x, int y, int z, uint16_t id) const {  // in 1/16 units; -1 if not this liquid
        uint16_t b = B(x, y, z);
        if (b != id) return -1;
        if (B(x, y + 1, z) == id) return 16;
        int lvl = M(x, y, z) & 7;
        if (M(x, y, z) & 8) lvl = 0;
        return 14 - lvl * 12 / 8;
    }
    int cornerHeight(int x, int y, int z, int dx, int dz, uint16_t id) const {
        int sum = 0, cnt = 0;
        for (int ox = dx - 1; ox <= dx; ox++)
            for (int oz = dz - 1; oz <= dz; oz++) {
                if (B(x + ox, y + 1, z + oz) == id) return 16;
                int h = liquidHeightAt(x + ox, y, z + oz, id);
                if (h >= 0) { sum += h; cnt++; }
            }
        return cnt ? sum / cnt : 14;
    }
    void liquid(int bx, int by, int bz, uint16_t id) {
        bool water = id == WATER;
        int layer = water ? LAYER_WATER : LAYER_OPAQUE;
        int tex = water ? TX_WATER : TX_LAVA;
        int flags = VF_ANIM | (water ? 0 : VF_EMISSIVE);
        int h[2][2];
        for (int dx = 0; dx < 2; dx++) for (int dz = 0; dz < 2; dz++) h[dx][dz] = cornerHeight(bx, by, bz, dx, dz, id);
        uint8_t own = L(bx, by, bz);
        int X = bx * 16, Y = by * 16, Z = bz * 16;
        // top
        uint16_t above = B(bx, by + 1, bz);
        if (above != id) {
            uint8_t lt = std::max(own, L(bx, by + 1, bz));
            int p[4][3] = {{X, Y + h[0][1], Z + 16}, {X + 16, Y + h[1][1], Z + 16}, {X + 16, Y + h[1][0], Z}, {X, Y + h[0][0], Z}};
            const int uv[4][2] = {{0, 16}, {16, 16}, {16, 0}, {0, 0}};
            for (int c = 0; c < 4; c++) vert(layer, p[c][0], p[c][1], p[c][2], FACE_PY, 3, tex, uv[c][0], uv[c][1], lt >> 4, lt & 15, flags | (water ? VF_WAVE : 0));
        }
        // sides
        for (int f : {FACE_PX, FACE_NX, FACE_PZ, FACE_NZ}) {
            int nx = bx + FACE_DIR[f][0], nz = bz + FACE_DIR[f][2];
            uint16_t nb = B(nx, by, nz);
            if (nb == id || g_blocks[nb].opaque) continue;
            if (water && nb == ICE) continue;
            uint8_t lt = std::max(own, L(nx, by, nz));
            int p[4][3];
            for (int c = 0; c < 4; c++) {
                const int* cc = FACE_CORNERS[f][c];
                p[c][0] = X + cc[0] * 16; p[c][2] = Z + cc[2] * 16;
                p[c][1] = Y + (cc[1] ? h[cc[0]][cc[2]] : 0);
            }
            for (int c = 0; c < 4; c++) {
                int u, v; faceUV(f, p[c][0] - X, p[c][1] - Y, p[c][2] - Z, u, v);
                vert(layer, p[c][0], p[c][1], p[c][2], f, 3, tex, u, v, lt >> 4, lt & 15, flags);
            }
        }
        // bottom
        uint16_t below = B(bx, by - 1, bz);
        if (below != id && !g_blocks[below].opaque) {
            uint16_t t[6] = {(uint16_t)tex, (uint16_t)tex, (uint16_t)tex, (uint16_t)tex, (uint16_t)tex, (uint16_t)tex};
            box(layer, bx, by, bz, 0, 0, 0, 16, 16, 16, t, 1u << FACE_NY, own, flags);
        }
    }

    void block(int x, int y, int z) {
        uint16_t id = B(x, y, z);
        if (id == AIR) return;
        const BlockDef& d = g_blocks[id];
        uint8_t meta = M(x, y, z);
        uint8_t own = L(x, y, z);
        switch (d.shape) {
        case SHAPE_CUBE: {
            uint16_t tex[6]; memcpy(tex, d.tex, sizeof(tex));
            if (id == FURNACE || id == FURNACE_LIT) {
                static const int frontFace[4] = {FACE_PZ, FACE_NX, FACE_NZ, FACE_PX};
                tex[frontFace[meta & 3]] = id == FURNACE_LIT ? TX_FURNACE_FRONT_LIT : TX_FURNACE_FRONT;
            }
            bool isLeaves = id == OAK_LEAVES || id == BIRCH_LEAVES || id == SPRUCE_LEAVES;
            for (int f = 0; f < 6; f++) {
                int nx = x + FACE_DIR[f][0], ny = y + FACE_DIR[f][1], nz = z + FACE_DIR[f][2];
                uint16_t nb = B(nx, ny, nz);
                if (g_blocks[nb].opaque) continue;
                if (!d.opaque && nb == id && !isLeaves) continue;  // glass next to glass
                if (d.opaque) cubeFace(d.layer, x, y, z, f, tex[f], 0);
                else {
                    cubeFace(d.layer, x, y, z, f, tex[f], 0);
                }
            }
            break;
        }
        case SHAPE_LIQUID: liquid(x, y, z, id); break;
        case SHAPE_CROSS: cross(LAYER_CUTOUT, x, y, z, d.tex[0], own, (id == TALL_GRASS || id == FLOWER_RED || id == FLOWER_YELLOW) ? VF_SWAY : 0); break;
        case SHAPE_CROP: crop(x, y, z, TX_WHEAT0 + std::min(7, (int)meta), own); break;
        case SHAPE_TORCH: {
            int m = meta;
            uint16_t t[6]; for (auto& tt : t) tt = TX_TORCH;
            if (m == 0) box(LAYER_CUTOUT, x, y, z, 7, 0, 7, 9, 10, 9, t, 0x3F, own, VF_EMISSIVE);
            else {
                int f = m - 1;  // torch points toward facing f, wall is behind
                int ox = -facingDX(f) * 5, oz = -facingDZ(f) * 5;
                box(LAYER_CUTOUT, x, y, z, 7 + ox, 3, 7 + oz, 9 + ox, 13, 9 + oz, t, 0x3F, own, VF_EMISSIVE);
            }
            break;
        }
        case SHAPE_LADDER: {
            int f = meta & 3;  // ladder faces f; wall is on the opposite side
            int X = x * 16, Y = y * 16, Z = z * 16;
            int wdx = -facingDX(f), wdz = -facingDZ(f);
            const int uv[4][2] = {{0, 16}, {16, 16}, {16, 0}, {0, 0}};
            int p[4][3];
            if (wdx != 0) {
                int px = X + (wdx > 0 ? 15 : 1);
                int q[4][3] = {{px, Y, Z}, {px, Y, Z + 16}, {px, Y + 16, Z + 16}, {px, Y + 16, Z}};
                memcpy(p, q, sizeof(p));
            } else {
                int pz = Z + (wdz > 0 ? 15 : 1);
                int q[4][3] = {{X, Y, pz}, {X + 16, Y, pz}, {X + 16, Y + 16, pz}, {X, Y + 16, pz}};
                memcpy(p, q, sizeof(p));
            }
            quad(LAYER_CUTOUT, p, uv, TX_LADDER, own, 6, 0, 0);
            break;
        }
        case SHAPE_CACTUS: {
            uint16_t t[6]; memcpy(t, d.tex, sizeof(t));
            unsigned mask = 0x0F ^ 0x0C;  // sides only
            mask = (1u << FACE_PX) | (1u << FACE_NX) | (1u << FACE_PZ) | (1u << FACE_NZ);
            box(LAYER_CUTOUT, x, y, z, 1, 0, 0, 15, 16, 16, t, (1u << FACE_PX) | (1u << FACE_NX), own);
            box(LAYER_CUTOUT, x, y, z, 0, 0, 1, 16, 16, 15, t, (1u << FACE_PZ) | (1u << FACE_NZ), own);
            (void)mask;
            if (B(x, y + 1, z) != CACTUS) box(LAYER_CUTOUT, x, y, z, 1, 0, 1, 15, 16, 15, t, 1u << FACE_PY, std::max(own, L(x, y + 1, z)));
            if (!opq(x, y - 1, z) && B(x, y - 1, z) != CACTUS) box(LAYER_CUTOUT, x, y, z, 1, 0, 1, 15, 16, 15, t, 1u << FACE_NY, own);
            break;
        }
        case SHAPE_FARMLAND: {
            uint16_t t[6]; memcpy(t, d.tex, sizeof(t));
            unsigned mask = 0;
            for (int f = 0; f < 6; f++) {
                int nx = x + FACE_DIR[f][0], ny = y + FACE_DIR[f][1], nz = z + FACE_DIR[f][2];
                if (f == FACE_PY || !opq(nx, ny, nz)) mask |= 1u << f;
            }
            box(LAYER_OPAQUE, x, y, z, 0, 0, 0, 16, 15, 16, t, mask, std::max(own, L(x, y + 1, z)));
            break;
        }
        case SHAPE_CHEST: {
            uint16_t t[6]; memcpy(t, d.tex, sizeof(t));
            static const int frontFace[4] = {FACE_PZ, FACE_NX, FACE_NZ, FACE_PX};
            t[frontFace[meta & 3]] = TX_CHEST_FRONT;
            box(LAYER_OPAQUE, x, y, z, 1, 0, 1, 15, 14, 15, t, 0x3F, own);
            break;
        }
        case SHAPE_DOOR: {
            bool top = meta & 8, open = meta & 4;
            int f = meta & 3;
            int dir = open ? ((f + 1) & 3) : f;
            uint16_t t[6]; for (auto& tt : t) tt = top ? TX_DOOR_TOP : TX_DOOR_BOTTOM;
            int x0 = 0, x1 = 16, z0 = 0, z1 = 16;
            int ddx = facingDX(dir), ddz = facingDZ(dir);
            if (ddz > 0) z1 = 3; else if (ddz < 0) z0 = 13; else if (ddx > 0) x1 = 3; else x0 = 13;
            box(LAYER_CUTOUT, x, y, z, x0, 0, z0, x1, 16, z1, t, 0x3F, own);
            break;
        }
        case SHAPE_BED: {
            int f = meta & 3; bool head = meta & 4;
            uint16_t t[6];
            int endFace, backFace;
            static const int faceOfDir[4] = {FACE_PZ, FACE_NX, FACE_NZ, FACE_PX};
            endFace = faceOfDir[f];              // toward head direction
            backFace = faceOfDir[(f + 2) & 3];   // toward foot direction
            for (int k = 0; k < 6; k++) t[k] = head ? TX_BED_SIDE_HEAD : TX_BED_SIDE_FOOT;
            t[FACE_PY] = head ? TX_BED_TOP_HEAD : TX_BED_TOP_FOOT;
            t[FACE_NY] = TX_OAK_PLANKS;
            unsigned mask = 0x3F;
            if (head) { t[endFace] = TX_BED_END_HEAD; mask &= ~(1u << backFace); }
            else { t[backFace] = TX_BED_END_FOOT; mask &= ~(1u << endFace); }
            static const int rotForDir[4] = {2, 3, 0, 1};
            box(LAYER_CUTOUT, x, y, z, 0, 0, 0, 16, 9, 16, t, mask, own, 0, rotForDir[f]);
            break;
        }
        default: break;
        }
    }
};
}  // namespace

void buildSectionMesh(const MeshSnapshot& s, MeshResult& out) {
    out.cx = s.cx; out.cz = s.cz; out.sy = s.sy; out.ticket = s.ticket;
    for (auto& v : out.verts) v.clear();
    Builder b(s, out);
    for (int y = 0; y < 16; y++)
        for (int z = 0; z < 16; z++)
            for (int x = 0; x < 16; x++) b.block(x, y, z);
    (void)AO_UNUSED;
}
