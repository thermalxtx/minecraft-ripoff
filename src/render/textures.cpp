// All block, item, UI and mob textures are drawn here in code (original pixel art).
#include "textures.h"
#include "../core/math.h"
#include <cstring>
#include <cmath>

namespace {
// ---------- small canvas helpers ----------
struct Img {
    uint8_t p[16 * 16 * 4];
    Img() { memset(p, 0, sizeof(p)); }
    void set(int x, int y, uint32_t rgb, int a = 255) {
        if (x < 0 || y < 0 || x > 15 || y > 15) return;
        uint8_t* q = p + (y * 16 + x) * 4;
        q[0] = (rgb >> 16) & 255; q[1] = (rgb >> 8) & 255; q[2] = rgb & 255; q[3] = (uint8_t)a;
    }
    uint32_t get(int x, int y) const { const uint8_t* q = p + (y * 16 + x) * 4; return (q[0] << 16) | (q[1] << 8) | q[2]; }
    int alpha(int x, int y) const { return p[(y * 16 + x) * 4 + 3]; }
    void fill(uint32_t c, int a = 255) { for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) set(x, y, c, a); }
    void rect(int x0, int y0, int x1, int y1, uint32_t c, int a = 255) { for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) set(x, y, c, a); }
};

uint32_t rgbMul(uint32_t c, float f) {
    int r = (int)(((c >> 16) & 255) * f), g = (int)(((c >> 8) & 255) * f), b = (int)((c & 255) * f);
    r = r > 255 ? 255 : (r < 0 ? 0 : r); g = g > 255 ? 255 : (g < 0 ? 0 : g); b = b > 255 ? 255 : (b < 0 ? 0 : b);
    return (r << 16) | (g << 8) | b;
}
uint32_t rgbMix(uint32_t a, uint32_t b, float t) {
    auto ch = [&](int s) { return (int)(((a >> s) & 255) * (1 - t) + ((b >> s) & 255) * t) & 255; };
    return (ch(16) << 16) | (ch(8) << 8) | ch(0);
}
// Tileable value noise on a 16x16 texture, cell = 16/period
float tnoise(int x, int y, int period, uint32_t seed) {
    float cs = 16.0f / period;
    float fx = x / cs, fy = y / cs;
    int ix = (int)std::floor(fx), iy = (int)std::floor(fy);
    float tx = fx - ix, ty = fy - iy;
    tx = tx * tx * (3 - 2 * tx); ty = ty * ty * (3 - 2 * ty);
    auto h = [&](int a, int b) { a = floorMod(a, period); b = floorMod(b, period); return (hashCoords(a, b, 7, seed) & 0xFFFF) / 65535.0f; };
    float v00 = h(ix, iy), v10 = h(ix + 1, iy), v01 = h(ix, iy + 1), v11 = h(ix + 1, iy + 1);
    return lerpf(lerpf(v00, v10, tx), lerpf(v01, v11, tx), ty);
}
float prand(int x, int y, uint32_t seed) { return (hashCoords(x, y, 3, seed) & 0xFFFF) / 65535.0f; }
// Pick from palette by a 0..1 value
uint32_t pal(const uint32_t* p, int n, float v) { int i = (int)(v * n); i = i < 0 ? 0 : (i >= n ? n - 1 : i); return p[i]; }

// Speckled fill: blend of smooth noise + per-pixel noise, quantised to a palette
void speckle(Img& im, const uint32_t* p, int n, uint32_t seed, float smooth = 0.5f, int period = 4) {
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) {
            float v = tnoise(x, y, period, seed) * smooth + prand(x, y, seed + 1) * (1 - smooth);
            im.set(x, y, pal(p, n, v));
        }
}

// ---------- block textures ----------
const uint32_t STONE_P[] = {0x6a6a6a, 0x767676, 0x7f7f7f, 0x888888, 0x939393};
const uint32_t DIRT_P[] = {0x5e3f26, 0x6d4a2d, 0x7a5434, 0x86603c, 0x6a4a35};
const uint32_t GRASS_P[] = {0x4a7f2a, 0x55902f, 0x5e9c35, 0x68a83b, 0x74b545};
const uint32_t SAND_P[] = {0xcdbb85, 0xd6c590, 0xdccc98, 0xe3d4a2, 0xc9b47c};

void stone(Img& im, uint32_t seed = 11) { speckle(im, STONE_P, 5, seed, 0.55f, 4); }
void dirt(Img& im, uint32_t seed = 21) {
    speckle(im, DIRT_P, 4, seed, 0.45f, 4);
    for (int i = 0; i < 6; i++) { int x = hashCoords(i, 1, 2, seed) % 16, y = hashCoords(i, 2, 3, seed) % 16; im.set(x, y, 0x9a8a78); }
}
void grassTop(Img& im) {
    speckle(im, GRASS_P, 5, 31, 0.4f, 4);
    for (int i = 0; i < 10; i++) { int x = hashCoords(i, 5, 2, 33) % 16, y = hashCoords(i, 6, 3, 33) % 16; im.set(x, y, 0x86c255); }
}
void grassSide(Img& im, bool snowy) {
    dirt(im);
    for (int x = 0; x < 16; x++) {
        int d = 3 + (int)(prand(x, 0, snowy ? 77 : 41) * 2.5f);
        if (x % 5 == 2) d += 1;
        for (int y = 0; y < d; y++) {
            static const uint32_t SNOWP[] = {0xdfe8ee, 0xeef4f8, 0xffffff};
            uint32_t c = snowy ? pal(SNOWP, 3, prand(x, y, 9))
                               : pal(GRASS_P, 5, prand(x, y, 43) * 0.8f + 0.1f);
            im.set(x, y, c);
        }
        if (!snowy) im.set(x, d, rgbMul(im.get(x, d), 0.8f));
    }
}
void sand(Img& im) {
    speckle(im, SAND_P, 5, 51, 0.35f, 8);
    for (int i = 0; i < 7; i++) { int x = hashCoords(i, 1, 1, 52) % 16, y = hashCoords(i, 2, 1, 52) % 16; im.set(x, y, 0xb39f6c); }
}
void gravel(Img& im) {
    const uint32_t P[] = {0x5d5754, 0x6f6965, 0x857f79, 0x9a938c, 0x7b6a5d, 0xa9a29a};
    im.fill(0x6a6461);
    for (int i = 0; i < 26; i++) {
        int cx = hashCoords(i, 0, 0, 61) % 16, cy = hashCoords(i, 1, 0, 61) % 16;
        uint32_t c = P[hashCoords(i, 2, 0, 61) % 6];
        int r = 1 + (hashCoords(i, 3, 0, 61) % 2);
        for (int dy = -r; dy <= r; dy++) for (int dx = -r; dx <= r; dx++)
            if (dx * dx + dy * dy <= r * r) {
                int x = floorMod(cx + dx, 16), y = floorMod(cy + dy, 16);
                im.set(x, y, (dy < 0 && dx < 0) ? rgbMul(c, 1.15f) : (dy > 0 ? rgbMul(c, 0.85f) : c));
            }
    }
}
void clay(Img& im) { const uint32_t P[] = {0x8e94a6, 0x979dae, 0xa0a6b6, 0x9ba0ad}; speckle(im, P, 4, 71, 0.7f, 4); }
// Voronoi-ish cobblestone
void cobbleBase(Img& im, uint32_t seed, const uint32_t* P, int n, uint32_t mortar) {
    float px[10], py[10];
    for (int i = 0; i < 10; i++) { px[i] = (hashCoords(i, 0, 0, seed) % 1600) / 100.f; py[i] = (hashCoords(i, 1, 0, seed) % 1600) / 100.f; }
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) {
            float best = 1e9, second = 1e9; int bi = 0;
            for (int i = 0; i < 10; i++)
                for (int oy = -1; oy <= 1; oy++) for (int ox = -1; ox <= 1; ox++) {
                    float dx = x + 0.5f - (px[i] + ox * 16), dy = y + 0.5f - (py[i] + oy * 16);
                    float d = std::sqrt(dx * dx + dy * dy);
                    if (d < best) { second = best; best = d; bi = i; } else if (d < second) second = d;
                }
            if (second - best < 1.1f) im.set(x, y, mortar);
            else {
                uint32_t c = P[hashCoords(bi, 9, 0, seed) % n];
                float shade = 1.0f + (best < 2.0f ? 0.08f : 0) - (prand(x, y, seed) * 0.12f);
                im.set(x, y, rgbMul(c, shade));
            }
        }
}
void cobble(Img& im) { const uint32_t P[] = {0x6e6e6e, 0x7e7e7e, 0x8e8e8e, 0x626262, 0x9a9a9a}; cobbleBase(im, 81, P, 5, 0x3f3f3f); }
void mossyCobble(Img& im) {
    cobble(im);
    for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++)
        if (tnoise(x, y, 4, 88) > 0.58f && im.get(x, y) != 0x3f3f3f) im.set(x, y, prand(x, y, 89) > 0.5f ? 0x4f7a32 : 0x5f8a3c);
}
void bedrock(Img& im) { const uint32_t P[] = {0x1e1e1e, 0x333333, 0x4a4a4a, 0x5c5c5c, 0x2a2a2a}; speckle(im, P, 5, 91, 0.3f, 8); }
void obsidian(Img& im) {
    const uint32_t P[] = {0x0f0a18, 0x170f24, 0x1f1530, 0x2a1d40};
    speckle(im, P, 4, 95, 0.6f, 4);
    for (int i = 0; i < 6; i++) { int x = hashCoords(i, 0, 0, 96) % 16, y = hashCoords(i, 1, 0, 96) % 16; im.set(x, y, 0x5a3d80); }
}
void logSide(Img& im, uint32_t base, uint32_t dark, uint32_t seed, bool birch) {
    for (int x = 0; x < 16; x++) {
        float col = prand(x, 0, seed);
        for (int y = 0; y < 16; y++) {
            float v = col * 0.6f + prand(x, y / 3, seed + 1) * 0.4f;
            uint32_t c = rgbMul(base, 0.85f + v * 0.3f);
            if (!birch && (x % 4 == 1 || (col > 0.75f))) c = rgbMix(c, dark, 0.6f);
            im.set(x, y, c);
        }
    }
    if (birch) {  // black dashes on white bark
        for (int i = 0; i < 9; i++) {
            int y = hashCoords(i, 0, 0, seed) % 16, x = hashCoords(i, 1, 0, seed) % 13, len = 2 + hashCoords(i, 2, 0, seed) % 3;
            for (int k = 0; k < len; k++) im.set(x + k, y, k == 0 || k == len - 1 ? 0x4a4a42 : 0x2a2a26);
        }
    }
}
void logTop(Img& im, uint32_t bark, uint32_t ringA, uint32_t ringB) {
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) {
            if (x == 0 || y == 0 || x == 15 || y == 15) { im.set(x, y, bark); continue; }
            float dx = x - 7.5f, dy = y - 7.5f, d = std::max(std::fabs(dx), std::fabs(dy)) * 0.7f + std::sqrt(dx * dx + dy * dy) * 0.3f;
            im.set(x, y, ((int)d % 2 == 0) ? ringA : ringB);
        }
}
void leaves(Img& im, const uint32_t* P, uint32_t seed) {
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) {
            float r = prand(x, y, seed);
            if (r < 0.2f) { im.set(x, y, 0, 0); continue; }
            float v = tnoise(x, y, 8, seed + 3) * 0.6f + prand(x, y, seed + 5) * 0.4f;
            im.set(x, y, pal(P, 4, v));
        }
}
void planks(Img& im, const uint32_t* P, uint32_t seed) {
    for (int y = 0; y < 16; y++) {
        int board = y / 4;
        int seam = (board * 7 + 3) % 16;
        for (int x = 0; x < 16; x++) {
            float g = prand(x / 3 + board * 17, y, seed) * 0.5f + tnoise(x, y, 8, seed) * 0.5f;
            uint32_t c = pal(P, 3, g);
            if (y % 4 == 3) c = P[3];
            if (x == seam) c = P[3];
            im.set(x, y, c);
        }
    }
}
void glass(Img& im) {
    im.fill(0, 0);
    for (int i = 0; i < 16; i++) { im.set(i, 0, 0xdde8ea); im.set(i, 15, 0xa9bcc0); im.set(0, i, 0xdde8ea); im.set(15, i, 0xa9bcc0); }
    for (int k = 0; k < 4; k++) { im.set(3 + k, 6 - k, 0xffffff, 200); im.set(4 + k, 6 - k, 0xffffff, 140); }
    im.set(10, 12, 0xffffff, 170); im.set(11, 11, 0xffffff, 170);
}
void ore(Img& im, uint32_t c1, uint32_t c2, uint32_t c3, uint32_t seed, int clusters) {
    stone(im, 11);
    for (int i = 0; i < clusters; i++) {
        int cx = 2 + hashCoords(i, 0, 0, seed) % 12, cy = 2 + hashCoords(i, 1, 0, seed) % 12;
        static const int OFF[6][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}, {-1, 0}, {0, -1}};
        int cnt = 3 + hashCoords(i, 2, 0, seed) % 3;
        for (int k = 0; k < cnt; k++) {
            int x = cx + OFF[k][0], y = cy + OFF[k][1];
            im.set(x, y, k == 0 ? c3 : (k % 2 ? c1 : c2));
        }
    }
}
void liquid(Img& im, int frame, bool lava) {
    const uint32_t WP[] = {0x1f4fbf, 0x2558c8, 0x2c62d2, 0x3a74e0, 0x5a90f0};
    const uint32_t LP[] = {0xb3300a, 0xd04a0c, 0xe8700f, 0xf59a1c, 0xffd040};
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) {
            float v = tnoise(floorMod(x + frame * 4, 16), floorMod(y + frame * 2, 16), 4, lava ? 301 : 201) * 0.6f
                    + tnoise(floorMod(x - frame * 4, 16), y, 2, lava ? 303 : 203) * 0.4f;
            im.set(x, y, pal(lava ? LP : WP, 5, v), lava ? 255 : 175);
        }
}
void torch(Img& im) {
    im.fill(0, 0);
    for (int y = 8; y < 16; y++) { im.set(7, y, 0x7a5530); im.set(8, y, 0x5d3f22); }
    im.set(7, 6, 0xfff2a0); im.set(8, 6, 0xffe070); im.set(7, 7, 0xffb030); im.set(8, 7, 0xff9020);
    im.set(7, 5, 0xffe9a0, 200); im.set(8, 5, 0xffd070, 200); im.set(6, 6, 0xffa020, 140); im.set(9, 6, 0xffa020, 140);
    im.set(7, 4, 0xffe0a0, 120); im.set(8, 4, 0xffe0a0, 90);
}
const uint32_t OAK_PL[] = {0x9c7a46, 0xa88553, 0xb39060, 0x6e5230};
const uint32_t BIRCH_PL[] = {0xc4b07a, 0xcfbb86, 0xd8c592, 0x9a8656};
const uint32_t SPRUCE_PL[] = {0x6a4b2a, 0x745433, 0x7e5d3b, 0x4a331c};
void craftTop(Img& im) {
    planks(im, OAK_PL, 401);
    for (int i = 1; i < 15; i++) { im.set(i, 1, 0x5a3f22); im.set(i, 14, 0x5a3f22); im.set(1, i, 0x5a3f22); im.set(14, i, 0x5a3f22); }
    for (int i = 2; i < 14; i++) { im.set(i, 5, 0x6e5230); im.set(i, 10, 0x6e5230); im.set(5, i, 0x6e5230); im.set(10, i, 0x6e5230); }
}
void craftSide(Img& im, bool front) {
    planks(im, OAK_PL, 402);
    for (int x = 0; x < 16; x++) { im.set(x, 0, 0x5a3f22); im.set(x, 1, 0x8a6a40); }
    if (front) {  // hammer
        for (int y = 4; y < 13; y++) im.set(5, y, 0x5d3f22);
        im.rect(3, 3, 7, 4, 0x9a9a9a); im.set(3, 3, 0xcfcfcf);
        // saw
        im.rect(9, 5, 13, 7, 0xb8b8b8); for (int x = 9; x <= 13; x += 2) im.set(x, 8, 0x8a8a8a);
        im.rect(9, 3, 10, 4, 0x5d3f22);
    } else {
        for (int y = 4; y < 13; y++) im.set(11, y, 0x5d3f22);
        im.rect(9, 3, 13, 5, 0x7a7a7a);
        im.rect(3, 6, 6, 11, 0x5a3f22); im.rect(4, 7, 5, 10, 0x8c8c8c);
    }
}
void furnaceSide(Img& im) {
    const uint32_t P[] = {0x6f6f6f, 0x787878, 0x828282, 0x8a8a8a};
    speckle(im, P, 4, 501, 0.5f, 4);
    for (int i = 0; i < 16; i++) { im.set(i, 0, 0x5a5a5a); im.set(i, 15, 0x4a4a4a); }
}
void furnaceFront(Img& im, bool lit) {
    furnaceSide(im);
    for (int x = 2; x < 14; x++) { im.set(x, 2, 0x4a4a4a); im.set(x, 6, 0x4a4a4a); }
    im.rect(4, 8, 11, 13, 0x2a2a2a);
    for (int x = 3; x <= 12; x++) im.set(x, 7, 0x5a5a5a);
    if (lit) {
        for (int y = 9; y <= 13; y++) for (int x = 4; x <= 11; x++) {
            float v = prand(x, y, 511) + (y - 9) * 0.15f;
            im.set(x, y, v > 0.9f ? 0xffd040 : (v > 0.6f ? 0xff8a1a : 0xc0400a));
        }
    } else {
        for (int x = 5; x <= 10; x++) im.set(x, 13, 0x1a1a1a);
    }
}
void chest(Img& im, int part) {  // 0 top, 1 side, 2 front
    const uint32_t P[] = {0x8a5a22, 0x9a682a, 0xa87532, 0x5e3c14};
    planks(im, P, 601);
    for (int i = 0; i < 16; i++) { im.set(i, 0, 0x3e2810); im.set(i, 15, 0x3e2810); im.set(0, i, 0x3e2810); im.set(15, i, 0x3e2810); }
    if (part != 0) for (int x = 0; x < 16; x++) im.set(x, 5, 0x3e2810);
    if (part == 2) { im.rect(7, 4, 8, 7, 0xc8c8c8); im.set(7, 7, 0x8a8a8a); im.set(8, 7, 0x8a8a8a); im.set(7, 4, 0xf0f0f0); }
}
void bed(Img& im, int part) {  // 0 top head,1 top foot,2 side head,3 side foot,4 end head,5 end foot
    const uint32_t RED[] = {0x9e2020, 0xb02828, 0xc03434};
    if (part == 0 || part == 1) {
        for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) im.set(x, y, pal(RED, 3, prand(x, y, 701) * 0.5f + 0.25f));
        if (part == 0) { im.rect(2, 1, 13, 6, 0xe8e8e8); im.rect(2, 6, 13, 6, 0xc8c8c8); }
        for (int y = 0; y < 16; y++) { im.set(0, y, 0x7a1818); im.set(15, y, 0x7a1818); }
        if (part == 1) for (int x = 0; x < 16; x++) im.set(x, 15, 0x7a1818);
    } else {
        im.fill(0, 0);
        for (int x = 0; x < 16; x++) for (int y = 7; y < 10; y++) im.set(x, y, pal(RED, 3, prand(x, y, 703)));
        if (part == 2 || part == 4) for (int x = 2; x < 14; x++) im.set(x, 7, 0xe8e8e8);
        for (int x = 0; x < 16; x++) for (int y = 10; y < 13; y++) im.set(x, y, pal(OAK_PL, 3, prand(x, y, 704)));
        bool legL = (part == 3 || part >= 4), legR = (part == 2 || part >= 4);
        if (legL) im.rect(0, 13, 2, 15, 0x6e5230);
        if (legR) im.rect(13, 13, 15, 15, 0x6e5230);
    }
}
void door(Img& im, bool top) {
    const uint32_t P[] = {0x8a6a3a, 0x957444, 0xa07e4c, 0x6a4e28};
    for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) im.set(x, y, pal(P, 3, prand(x / 2, y, 801) * 0.6f + 0.2f));
    for (int i = 0; i < 16; i++) { im.set(0, i, 0x5a4020); im.set(15, i, 0x5a4020); }
    if (top) {
        im.set(0, 0, 0x5a4020); for (int x = 0; x < 16; x++) im.set(x, 0, 0x5a4020);
        im.rect(3, 3, 6, 7, 0, 0); im.rect(9, 3, 12, 7, 0, 0);
        im.rect(3, 10, 12, 13, P[3]);
    } else {
        for (int x = 0; x < 16; x++) im.set(x, 15, 0x5a4020);
        im.rect(3, 2, 12, 6, P[3]); im.rect(3, 9, 12, 13, P[3]);
        im.rect(4, 3, 11, 5, P[1]); im.rect(4, 10, 11, 12, P[1]);
        im.set(13, 0, 0xb0b0b0); im.set(13, 1, 0x808080);
    }
}
void wheat(Img& im, int stage) {
    im.fill(0, 0);
    int h = 3 + stage * 12 / 7;
    bool ripe = stage >= 6;
    uint32_t stem = stage < 4 ? 0x3f8f2a : (ripe ? 0xb59a3a : 0x6fa030);
    for (int s = 0; s < 4; s++) {
        int x = 2 + s * 4 + (s % 2);
        for (int y = 16 - h; y < 16; y++) im.set(x + ((y + s) % 5 == 0 ? 1 : 0), y, stem);
        if (stage >= 5) for (int y = 16 - h; y < 16 - h + 4; y++) { im.set(x, y, ripe ? 0xd8b84a : 0x9ab040); im.set(x + 1, y + 1, ripe ? 0xb89838 : 0x8aa038); }
    }
}
void farmland(Img& im) {
    dirt(im, 911);
    for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) {
        uint32_t c = rgbMul(im.get(x, y), 0.75f);
        if (y % 4 == 0) c = rgbMul(c, 0.7f);
        im.set(x, y, c);
    }
}
void wool(Img& im, uint32_t base, uint32_t seed) {
    for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) {
        float v = tnoise(x, y, 8, seed) * 0.5f + prand(x, y, seed) * 0.5f;
        float f = 0.88f + v * 0.18f;
        if ((x + y * 3) % 7 == 0) f *= 0.92f;
        im.set(x, y, rgbMul(base, f));
    }
}
void snow(Img& im) { const uint32_t P[] = {0xe6eef2, 0xeef4f7, 0xf6fafc, 0xffffff}; speckle(im, P, 4, 1001, 0.5f, 4); }
void ice(Img& im) {
    const uint32_t P[] = {0x8ab4f0, 0x96bdf2, 0xa2c6f4, 0xb0d0f8};
    speckle(im, P, 4, 1011, 0.8f, 2);
    for (int k = 0; k < 7; k++) { im.set(3 + k, 2 + k / 2, 0xd8e8ff); im.set(12 - k / 2, 9 + k, 0xd0e2fc); }
}
void cactus(Img& im, bool top) {
    const uint32_t P[] = {0x2a6a20, 0x327a28, 0x3c8a30, 0x4a9a3a};
    if (top) {
        for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) {
            float dx = x - 7.5f, dy = y - 7.5f, d = std::sqrt(dx * dx + dy * dy);
            im.set(x, y, d > 7 ? 0x24561a : (((int)d % 3 == 0) ? P[0] : P[2]));
        }
        im.rect(7, 7, 8, 8, 0xc8d87a);
    } else {
        for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) im.set(x, y, pal(P, 4, prand(x, y, 1021) * 0.4f + (x % 4 == 1 ? 0.0f : 0.55f)));
        for (int y = 1; y < 16; y += 4) for (int x = 1; x < 16; x += 4) im.set(x + ((y / 4) % 2) * 2, y, 0xe8e8c0);
        for (int y = 0; y < 16; y++) { im.set(0, y, 0x24561a); im.set(15, y, 0x24561a); }
    }
}
void tallGrass(Img& im) {
    im.fill(0, 0);
    for (int b = 0; b < 7; b++) {
        int x = 1 + b * 2 + (int)(prand(b, 0, 1101) * 2), h = 6 + (int)(prand(b, 1, 1101) * 9);
        for (int y = 15; y > 15 - h; y--) {
            int xx = x + (y < 15 - h / 2 ? (b % 2 ? 1 : -1) : 0);
            im.set(xx, y, pal(GRASS_P, 5, prand(xx, y, 1102) * 0.6f + 0.2f));
        }
    }
}
void flower(Img& im, uint32_t petal, uint32_t center) {
    im.fill(0, 0);
    for (int y = 8; y < 16; y++) im.set(7, y, 0x3a8a2a);
    im.set(6, 12, 0x3a8a2a); im.set(5, 11, 0x3a8a2a); im.set(8, 13, 0x3a8a2a); im.set(9, 12, 0x3a8a2a);
    im.rect(6, 4, 8, 6, petal); im.set(7, 3, petal); im.set(5, 5, petal); im.set(9, 5, petal); im.set(7, 7, petal);
    im.set(7, 5, center); im.set(6, 4, rgbMul(petal, 1.2f));
}
void deadBush(Img& im) {
    im.fill(0, 0);
    for (int y = 9; y < 16; y++) im.set(7, y, 0x6a4a28);
    for (int k = 0; k < 5; k++) { im.set(7 - k, 9 - k, 0x7a5a30); im.set(8 + k, 10 - k, 0x7a5a30); }
    im.set(4, 4, 0x8a6a38); im.set(11, 4, 0x8a6a38); im.set(5, 8, 0x6a4a28); im.set(10, 9, 0x6a4a28); im.set(3, 7, 0x7a5a30);
}
void sandstone(Img& im, bool top) {
    sand(im);
    if (!top) {
        for (int x = 0; x < 16; x++) { im.set(x, 0, 0xb8a46c); im.set(x, 4, 0xc4b07a); im.set(x, 12, 0xb8a46c); im.set(x, 15, 0xa8945c); }
    } else for (int i = 0; i < 16; i++) { im.set(i, 0, 0xc4b07a); im.set(0, i, 0xc4b07a); }
}
void stoneBricks(Img& im) {
    stone(im, 1201);
    for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) {
        bool mortar = (y % 8 == 7) || (((y / 8) % 2 == 0) ? (x == 15) : (x == 7));
        if (mortar) im.set(x, y, 0x4e4e4e);
        else if (y % 8 == 0 || ((y / 8) % 2 == 0 ? x == 0 : x == 8)) im.set(x, y, rgbMul(im.get(x, y), 1.12f));
    }
}
void metalBlock(Img& im, uint32_t base, uint32_t dark, uint32_t light) {
    for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) im.set(x, y, rgbMix(base, light, prand(x, y, 1301) * 0.25f));
    for (int i = 0; i < 16; i++) { im.set(i, 15, dark); im.set(15, i, dark); im.set(i, 0, light); im.set(0, i, light); }
    for (int k = 2; k < 6; k++) im.set(k, k - 1, light);
    im.set(12, 11, dark); im.set(11, 12, dark);
}
void bricks(Img& im) {
    for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) {
        bool mortar = (y % 4 == 3) || (((y / 4) % 2 == 0) ? (x % 8 == 7) : (x % 8 == 3));
        static const uint32_t BP[] = {0x8a3a2a, 0x9a4432, 0xa84e3a, 0x7e3424};
        im.set(x, y, mortar ? 0xa89a8c : pal(BP, 4, prand(x / 2, y, 1401)));
    }
}
void ladder(Img& im) {
    im.fill(0, 0);
    for (int y = 0; y < 16; y++) { im.set(2, y, 0x7a5a32); im.set(3, y, 0x5e4224); im.set(12, y, 0x7a5a32); im.set(13, y, 0x5e4224); }
    for (int y = 2; y < 16; y += 4) for (int x = 2; x < 14; x++) { im.set(x, y, 0x8a6a3a); im.set(x, y + 1, 0x5e4224); }
}
void sapling(Img& im, int type) {
    im.fill(0, 0);
    uint32_t trunk = type == 1 ? 0xd8d8cc : 0x6a4a28;
    const uint32_t OAK_L[] = {0x3a7a22, 0x4a8a2a}, BIRCH_L[] = {0x6a9a3a, 0x7aaa44}, SPR_L[] = {0x2a5a3a, 0x356a44};
    const uint32_t* L = type == 0 ? OAK_L : (type == 1 ? BIRCH_L : SPR_L);
    for (int y = 9; y < 16; y++) im.set(7, y, trunk);
    if (type == 2) {
        for (int row = 0; row < 8; row++) { int w = row / 2 + 1; for (int x = 7 - w; x <= 7 + w; x++) im.set(x, 2 + row, L[(x + row) % 2]); }
    } else {
        for (int y = 2; y < 10; y++) for (int x = 3; x < 12; x++) {
            float dx = x - 7, dy = y - 5.5f;
            if (dx * dx + dy * dy * 1.3f < 14 && prand(x, y, 1501 + type) > 0.15f) im.set(x, y, L[(x * 3 + y) % 2]);
        }
    }
}
void crack(Img& im, int stage) {
    im.fill(0, 0);
    Rng r(4242);
    int lines = 2 + stage * 2;
    for (int l = 0; l < lines; l++) {
        float x = 8, y = 8, ang = r.range(0.f, 6.28f);
        int len = 2 + stage / 2 + r.nextInt(3);
        for (int k = 0; k < len; k++) {
            im.set((int)x, (int)y, 0x000000, 170);
            x += std::cos(ang) * 1.0f; y += std::sin(ang) * 1.0f; ang += r.range(-0.6f, 0.6f);
            x = clampf(x, 0, 15); y = clampf(y, 0, 15);
        }
    }
}

// ---------- items ----------
const uint32_t MAT[5][3] = {
    {0x5e4426, 0x8f6a3a, 0xb08850},   // wood
    {0x4e4e4e, 0x7a7a7a, 0xa0a0a0},   // stone
    {0x8a8a8a, 0xcfcfcf, 0xffffff},   // iron
    {0xa07c08, 0xe8c830, 0xfff59a},   // gold
    {0x168a80, 0x3ad8c8, 0xc0fff6},   // diamond
};
const uint32_t STICK_D = 0x4a3418, STICK_L = 0x7a5a30;
// Diagonal-axis coordinates: u along handle (bottom-left -> top-right), v perpendicular (toward top-left)
void handleLine(Img& im, int x0, int y0, int len) {
    for (int k = 0; k < len; k++) { im.set(x0 + k, y0 - k, STICK_L); im.set(x0 + k + 1, y0 - k, STICK_D); }
}
void tool(Img& im, int mat, int type) {
    const uint32_t* C = MAT[mat];
    im.fill(0, 0);
    const float s = 0.70710678f;
    auto paint = [&](auto inside, auto shadeFn) {
        for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) {
            float px = x + 0.5f, py = y + 0.5f;
            if (inside(px, py)) im.set(x, y, C[shadeFn(px, py)]);
        }
    };
    if (type == 0) {  // pickaxe: arc head centred at the handle end
        handleLine(im, 2, 13, 9);
        paint([&](float px, float py) { float dx = px - 2, dy = py - 14; float d = std::sqrt(dx * dx + dy * dy);
                  return d > 9.3f && d < 11.6f && dx > 0.5f && dy < -0.5f; },
              [&](float px, float py) { float dx = px - 2, dy = py - 14; float d = std::sqrt(dx * dx + dy * dy);
                  return d > 10.8f ? 2 : (d < 10.0f ? 0 : 1); });
    } else if (type == 1) {  // axe
        handleLine(im, 2, 13, 10);
        paint([&](float px, float py) { float rx = px - 11.5f, ry = py - 4.5f; float u = (rx - ry) * s, v = (-rx - ry) * s;
                  return v > 0.3f && v < 5.2f && u > -4.0f - 0.45f * v && u < 1.2f + 0.2f * v; },
              [&](float px, float py) { float rx = px - 11.5f, ry = py - 4.5f; float v = (-rx - ry) * s;
                  return v > 4.0f ? 2 : (v < 1.4f ? 0 : 1); });
    } else if (type == 2) {  // shovel
        handleLine(im, 2, 13, 8);
        paint([&](float px, float py) { float rx = px - 11.f, ry = py - 5.f; float u = (rx - ry) * s, v = (-rx - ry) * s;
                  return (u * u) / 9.0f + (v * v) / 4.8f < 1.0f; },
              [&](float px, float py) { float rx = px - 11.f, ry = py - 5.f; float u = (rx - ry) * s, v = (-rx - ry) * s;
                  return u > 1.5f ? 2 : (v < -0.8f ? 0 : 1); });
    } else if (type == 3) {  // sword
        paint([&](float px, float py) { float rx = px - 2, ry = py - 14; float u = (rx - ry) * s, v = (-rx - ry) * s;
                  return u > 4.6f && u < 16.2f && std::fabs(v) < 1.25f - (u > 14.8f ? (u - 14.8f) : 0); },
              [&](float px, float py) { float rx = px - 2, ry = py - 14; float v = (-rx - ry) * s;
                  return v > 0.35f ? 2 : (v < -0.4f ? 0 : 1); });
        for (int k = -2; k <= 2; k++) im.set(4 + k, 10 + k, k == 0 ? 0x3a2a14 : 0x5a4020);  // guard
        im.set(3, 12, STICK_L); im.set(2, 13, STICK_D); im.set(4, 11, STICK_L); im.set(1, 14, C[1]);
    } else {  // hoe
        handleLine(im, 2, 13, 10);
        paint([&](float px, float py) { float rx = px - 11.5f, ry = py - 4.0f; float u = (rx - ry) * s, v = (-rx - ry) * s;
                  return (v > -0.2f && v < 4.6f && u > -0.6f && u < 1.5f) || (v > 3.0f && v < 4.8f && u > -2.6f && u < 1.5f); },
              [&](float px, float py) { float rx = px - 11.5f, ry = py - 4.0f; float u = (rx - ry) * s; return u > 0.6f ? 2 : 1; });
    }
}
void stickItem(Img& im) { im.fill(0, 0); handleLine(im, 3, 12, 9); }
void blob(Img& im, float cx, float cy, float rx, float ry, const uint32_t* P, int n, uint32_t seed, float wobble = 0.15f) {
    for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) {
        float dx = (x + 0.5f - cx) / rx, dy = (y + 0.5f - cy) / ry;
        float d = dx * dx + dy * dy + (prand(x, y, seed) - 0.5f) * wobble * 2;
        if (d < 1) {
            float shade = (-dx - dy) * 0.25f + 0.5f + (prand(x, y, seed + 1) - 0.5f) * 0.3f;
            im.set(x, y, pal(P, n, shade));
        }
    }
}
void coalItem(Img& im) { im.fill(0, 0); const uint32_t P[] = {0x151515, 0x262626, 0x3a3a3a, 0x5a5a5a}; blob(im, 8, 8.5f, 5.5f, 5, P, 4, 2001, 0.3f); }
void ingot(Img& im, const uint32_t* C) {
    im.fill(0, 0);
    for (int y = 5; y <= 11; y++) for (int x = 1; x <= 14; x++) {
        int slant = (y - 5) / 2;
        if (x < 1 + (6 - slant) / 2 || x > 14 - slant / 2) continue;
        im.set(x, y, y <= 7 ? C[2] : (y == 11 ? C[0] : C[1]));
    }
}
void diamondItem(Img& im) {
    im.fill(0, 0);
    const uint32_t* C = MAT[4];
    for (int y = 2; y < 15; y++) for (int x = 1; x < 15; x++) {
        int w = y < 6 ? (y - 2) * 2 + 4 : (14 - y);
        if (std::abs(x * 2 + 1 - 16) < w) im.set(x, y, y < 6 ? C[2] : ((x + y) % 3 == 0 ? C[0] : C[1]));
    }
    im.set(5, 4, 0xffffff); im.set(6, 4, 0xffffff);
}
void appleItem(Img& im) {
    im.fill(0, 0); const uint32_t P[] = {0x8a1010, 0xb01818, 0xd02a20, 0xe85a40}; blob(im, 8, 9.5f, 5.5f, 5.2f, P, 4, 2101, 0.05f);
    im.set(8, 3, 0x5a3a18); im.set(8, 4, 0x5a3a18); im.set(9, 3, 0x3a8a2a); im.set(10, 2, 0x3a8a2a); im.set(5, 7, 0xffc0b0);
}
void meat(Img& im, const uint32_t* P, uint32_t fat, uint32_t seed, bool drumstick) {
    im.fill(0, 0);
    if (drumstick) {
        blob(im, 9.5f, 6.5f, 5, 4.6f, P, 4, seed, 0.1f);
        for (int k = 0; k < 4; k++) { im.set(4 - k, 10 + k, 0xf0ecd8); im.set(5 - k, 10 + k, 0xd8d0b8); }
        im.set(1, 13, 0xf8f4e8); im.set(0, 14, 0xf8f4e8); im.set(1, 14, 0xd8d0b8);
    } else {
        blob(im, 8, 8.5f, 6.5f, 4.8f, P, 4, seed, 0.2f);
        for (int x = 3; x < 13; x++) if (im.alpha(x, 7 + (x % 3 == 0)) && (x % 5 != 0)) im.set(x, 7 + (x % 3 == 0), fat);
    }
}
void breadItem(Img& im) {
    im.fill(0, 0); const uint32_t P[] = {0x8a5a20, 0xa8702a, 0xc08a3a, 0xd8a85a}; blob(im, 8, 9, 7, 3.6f, P, 4, 2201, 0.05f);
    for (int k = 0; k < 3; k++) { im.set(4 + k * 3, 7, 0xe8c888); im.set(5 + k * 3, 8, 0xe8c888); }
}
void wheatItem(Img& im) {
    im.fill(0, 0);
    for (int s = 0; s < 4; s++) for (int k = 0; k < 10; k++) im.set(3 + s * 2 + k / 4, 14 - k, k > 5 ? 0xd8b84a : 0xb09838);
    for (int x = 4; x < 12; x++) im.set(x, 10, 0x8a6a28);
}
void seedsItem(Img& im) {
    im.fill(0, 0);
    const int pts[7][2] = {{5, 6}, {9, 5}, {7, 9}, {11, 9}, {4, 11}, {8, 12}, {10, 7}};
    for (auto& p : pts) { im.set(p[0], p[1], 0x5a9a30); im.set(p[0] + 1, p[1], 0x3a7a20); im.set(p[0], p[1] + 1, 0x8ab040); }
}
void boneItem(Img& im) {
    im.fill(0, 0);
    for (int k = 0; k < 9; k++) { im.set(4 + k, 11 - k, 0xf0ecdc); im.set(4 + k, 12 - k, 0xc8c2b0); }
    im.rect(2, 11, 4, 13, 0xe8e4d4); im.rect(12, 2, 14, 4, 0xe8e4d4); im.set(3, 13, 0xb8b2a0); im.set(14, 4, 0xb8b2a0);
}
void arrowItem(Img& im) {
    im.fill(0, 0);
    for (int k = 0; k < 10; k++) im.set(3 + k, 12 - k, k % 2 ? 0x6a4a28 : 0x8a6a38);
    im.set(13, 2, 0x5a5a5a); im.set(12, 2, 0x8a8a8a); im.set(13, 3, 0x8a8a8a); im.set(11, 2, 0x5a5a5a); im.set(13, 4, 0x5a5a5a); im.set(14, 1, 0xa0a0a0);
    im.set(2, 12, 0xf0f0f0); im.set(3, 14, 0xf0f0f0); im.set(1, 12, 0xd0d0d0); im.set(3, 13, 0xd0d0d0); im.set(2, 13, 0xe0e0e0); im.set(1, 14, 0xc8c8c8);
}
void bowItem(Img& im, int pull) {
    im.fill(0, 0);
    // limb arc centred at bottom-right
    for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) {
        float dx = x + 0.5f - 14.5f, dy = y + 0.5f - 14.5f, d = std::sqrt(dx * dx + dy * dy);
        if (d > 11.2f && d < 12.8f && dx < -1 && dy < -1) im.set(x, y, d > 12.0f ? 0x5a3e1c : 0x8a6232);
    }
    // string from (3,13) to (13,3), pulled toward bottom-right
    float pp = pull * 1.2f;
    for (int k = 0; k <= 10; k++) {
        float t = k / 10.0f, bend = std::sin(t * PI) * pp;
        im.set((int)(3 + k + bend * 0.7f), (int)(13 - k + bend * 0.7f), 0xe0e0e0);
    }
    if (pull > 0) for (int k = 0; k < 9; k++) im.set(4 + k - pull, 11 - k + pull, 0x8a6a38);
}
void stringItem(Img& im) { im.fill(0, 0); for (int k = 0; k < 12; k++) im.set(2 + k, 8 + (int)(std::sin(k * 0.9f) * 2.5f), 0xe8e8e8); }
void gunpowderItem(Img& im) {
    im.fill(0, 0); const uint32_t P[] = {0x3a3a3a, 0x5a5a5a, 0x6e6e6e, 0x8a8a8a}; blob(im, 8, 10, 6, 4, P, 4, 2301, 0.4f);
    for (int i = 0; i < 6; i++) im.set(4 + hashCoords(i, 0, 0, 2302) % 8, 8 + hashCoords(i, 1, 0, 2302) % 5, 0x222222);
}
void featherItem(Img& im) {
    im.fill(0, 0);
    for (int k = 0; k < 12; k++) {
        int x = 3 + k, y = 13 - k;
        im.set(x, y, 0xb0b0b0);
        if (k > 2) { im.set(x - 1, y - 1, 0xf4f4f4); im.set(x - 2, y - 1, 0xe8e8e8); im.set(x + 1, y + 1, 0xdcdcdc); if (k > 4 && k < 10) im.set(x - 2, y - 2, 0xf8f8f8); }
    }
}
void leatherItem(Img& im) {
    im.fill(0, 0); const uint32_t P[] = {0x6a3a18, 0x804a22, 0x965a2c, 0xa86a38}; blob(im, 8, 8, 6.5f, 6, P, 4, 2401, 0.35f);
}
void clayBallItem(Img& im) { im.fill(0, 0); const uint32_t P[] = {0x7a8090, 0x8e94a6, 0xa0a6b6, 0xb8bccc}; blob(im, 8, 9, 5, 4.5f, P, 4, 2501, 0.05f); }
void brickItem(Img& im) { const uint32_t C[3] = {0x6e2a1c, 0x9a4432, 0xb85a44}; ingot(im, C); }
void flintItem(Img& im) {
    im.fill(0, 0); const uint32_t P[] = {0x1a1a1e, 0x2e2e34, 0x46464e, 0x6a6a74}; blob(im, 8, 8.5f, 4.5f, 6, P, 4, 2601, 0.5f);
}
void bedItem(Img& im) {
    im.fill(0, 0);
    im.rect(1, 7, 14, 9, 0xb02828); im.rect(1, 7, 4, 8, 0xe8e8e8); im.rect(1, 10, 14, 11, 0x8f6a3a);
    im.rect(1, 12, 2, 13, 0x6e5230); im.rect(13, 12, 14, 13, 0x6e5230);
}
void doorItem(Img& im) {
    im.fill(0, 0);
    im.rect(4, 1, 11, 14, 0x957444); im.rect(5, 2, 7, 5, 0, 0); im.rect(8, 2, 10, 5, 0, 0);
    im.rect(5, 8, 10, 13, 0x6a4e28); im.set(10, 8, 0xb0b0b0);
    for (int y = 1; y <= 14; y++) { im.set(4, y, 0x5a4020); im.set(11, y, 0x5a4020); }
}

// ---------- UI ----------
const char* HEART[8] = {
    ".XX.XX..",
    "XrrXrrX.",
    "XrwrrrX.",
    "XrrrrrX.",
    ".XrrrX..",
    "..XrX...",
    "...X....",
    "........"};
void heart(Img& im, int kind) {  // 0 full 1 half 2 empty
    im.fill(0, 0);
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) {
        char c = HEART[y][x];
        int px = x * 2 + 1, py = y * 2 + 2;
        uint32_t col; int a = 255;
        if (c == '.') continue;
        if (c == 'X') col = 0x1a0a0a;
        else {
            bool filled = kind == 0 || (kind == 1 && x < 4);
            col = filled ? (c == 'w' ? 0xffd0d0 : 0xd81818) : 0x3a1a1a;
        }
        im.rect(px, py, px + 1, py + 1, col, a);
    }
}
void food(Img& im, int kind) {
    im.fill(0, 0);
    bool full = kind == 0;
    for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) {
        float dx = x + 0.5f - 9.5f, dy = y + 0.5f - 6.5f;
        if (dx * dx + dy * dy < 22) {
            bool f = full || (kind == 1 && x >= 8);
            bool edge = dx * dx + dy * dy > 15;
            im.set(x, y, edge ? 0x2a1608 : (f ? ((x + y) % 4 == 0 ? 0xc87a3a : 0xa85a22) : 0x3a2a1a));
        }
    }
    for (int k = 0; k < 5; k++) { im.set(5 - k, 9 + k, 0x2a1608); im.set(6 - k, 9 + k, kind == 2 ? 0x5a5a50 : 0xf0e8d8); im.set(7 - k, 9 + k, 0x2a1608); }
    im.rect(1, 13, 2, 14, kind == 2 ? 0x5a5a50 : 0xf8f0e0);
}
void bubble(Img& im) {
    im.fill(0, 0);
    for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) {
        float dx = x + 0.5f - 8, dy = y + 0.5f - 8, d = std::sqrt(dx * dx + dy * dy);
        if (d < 6.5f) im.set(x, y, d > 5.2f ? 0x2050c0 : 0x80c0ff, d > 5.2f ? 255 : 120);
    }
    im.rect(5, 5, 6, 6, 0xffffff);
}
void particle(Img& im, int kind) {
    im.fill(0, 0);
    for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) {
        float dx = x + 0.5f - 8, dy = y + 0.5f - 8, d = std::sqrt(dx * dx + dy * dy);
        if (kind == 0 && d < 6) im.set(x, y, rgbMix(0xffe080, 0xff5010, d / 6), 255);
        if (kind == 1 && d < 7 && prand(x / 2, y / 2, 2701) > 0.25f) im.set(x, y, 0xffffff, 255);
        if (kind == 2 && (std::abs(x - 7) < 1 || std::abs(y - 7) < 1) && d < 7) im.set(x, y, 0xffffff);
    }
}

// ---------- mob skins (each face of a box shows a whole layer) ----------
void furFill(Img& im, uint32_t base, uint32_t seed, float var = 0.18f) {
    for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) {
        float v = tnoise(x, y, 4, seed) * 0.5f + prand(x, y, seed) * 0.5f;
        im.set(x, y, rgbMul(base, 1 - var + v * var * 2));
    }
}
void eyes(Img& im, int y, int x0, int x1, uint32_t white, uint32_t pupil) {
    im.rect(x0, y, x0 + 1, y + 1, white); im.rect(x1, y, x1 + 1, y + 1, white);
    im.set(x0 + 1, y + 1, pupil); im.set(x1, y + 1, pupil); im.set(x0 + 1, y, pupil); im.set(x1, y, pupil);
}
void pigSkin(Img& im, bool face) {
    furFill(im, 0xe8a0a0, 3001, 0.08f);
    if (face) {
        eyes(im, 4, 2, 12, 0xffffff, 0x201010);
        im.rect(4, 8, 11, 13, 0xf2b8b8); im.rect(5, 10, 6, 11, 0x9a5050); im.rect(9, 10, 10, 11, 0x9a5050);
        for (int x = 4; x <= 11; x++) im.set(x, 13, 0xc88080);
    }
}
void cowSkin(Img& im, bool face) {
    furFill(im, 0x5a3a22, 3101, 0.12f);
    for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) if (tnoise(x, y, 2, 3103) > 0.6f) im.set(x, y, rgbMul(0xece6dc, 0.95f + prand(x, y, 3) * 0.08f));
    if (face) {
        eyes(im, 4, 2, 12, 0xffffff, 0x100808);
        im.rect(3, 9, 12, 14, 0xd0b8a8); im.rect(5, 11, 6, 12, 0x5a3a30); im.rect(9, 11, 10, 12, 0x5a3a30);
    }
}
void sheepSkin(Img& im, int part) {  // 0 wool 1 face 2 bare skin
    if (part == 0) { furFill(im, 0xeeeae2, 3201, 0.1f); for (int i = 0; i < 20; i++) im.set(hashCoords(i, 0, 0, 3202) % 16, hashCoords(i, 1, 0, 3202) % 16, 0xd8d2c6); return; }
    furFill(im, 0xc8b8a0, 3203, 0.06f);
    if (part == 1) { eyes(im, 5, 2, 12, 0xffffff, 0x2a2010); im.rect(6, 10, 9, 11, 0xa89880); im.set(7, 12, 0x6a5a48); im.set(8, 12, 0x6a5a48); im.rect(0, 0, 15, 2, 0xeeeae2); }
}
void chickenSkin(Img& im, int part) {
    if (part == 2) { im.fill(0xe8a020); for (int y = 0; y < 16; y++) im.set(7, y, 0xc08010); return; }
    furFill(im, 0xf6f6f2, 3301, 0.05f);
    if (part == 1) {
        eyes(im, 4, 2, 12, 0x101010, 0x101010);
        im.rect(5, 7, 10, 9, 0xf0a020); im.rect(6, 10, 9, 12, 0xd02020);
    }
}
void shamblerSkin(Img& im, int part) {  // 0 skin 1 face 2 cloth 3 pants
    if (part == 0 || part == 1) {
        furFill(im, 0x4f6b5d, 3401, 0.15f);
        for (int i = 0; i < 6; i++) im.set(hashCoords(i, 0, 0, 3402) % 16, hashCoords(i, 1, 0, 3402) % 16, 0x2f4a3a);
        if (part == 1) {
            im.rect(2, 5, 5, 6, 0x101410); im.rect(10, 5, 13, 6, 0x101410);
            im.rect(3, 5, 4, 5, 0xffe040); im.rect(11, 5, 12, 5, 0xffe040);
            for (int x = 4; x <= 11; x++) im.set(x, 11 + (x % 2), 0x1a1010);
            im.rect(0, 0, 15, 1, 0x2a2a22);
        }
    } else if (part == 2) {
        furFill(im, 0x7a4a2a, 3403, 0.15f);
        for (int i = 0; i < 5; i++) { int x = hashCoords(i, 0, 0, 3404) % 14, y = hashCoords(i, 1, 0, 3404) % 14; im.rect(x, y, x + 1, y + 1, 0x4f6b5d); }
        for (int x = 0; x < 16; x += 2) im.set(x, 15, 0, 0);
    } else furFill(im, 0x3a4058, 3405, 0.12f);
}
void marrowSkin(Img& im, int part) {  // 0 bone 1 skull face 2 cloak
    if (part == 2) { furFill(im, 0x3a2050, 3501, 0.15f); for (int x = 0; x < 16; x++) im.set(x, 0, 0x2a1640); return; }
    furFill(im, 0xd8d2bc, 3502, 0.08f);
    if (part == 0) for (int y = 2; y < 16; y += 3) for (int x = 2; x < 14; x++) im.set(x, y, 0x8a8470);
    if (part == 1) {
        im.rect(2, 4, 6, 8, 0x1a1010); im.rect(9, 4, 13, 8, 0x1a1010);
        im.rect(3, 5, 4, 6, 0xff2020); im.rect(11, 5, 12, 6, 0xff2020);
        im.rect(7, 9, 8, 10, 0x2a2018);
        for (int x = 3; x <= 12; x++) im.set(x, 12, x % 2 ? 0x2a2018 : 0xb8b29c);
        im.rect(0, 0, 15, 2, 0x3a2050);
    }
}
void fuseSkin(Img& im, int part) {  // 0 body 1 face 2 cap side 3 cap top
    if (part == 2 || part == 3) {
        furFill(im, 0xb02a1a, 3601, 0.1f);
        int spots = part == 3 ? 7 : 4;
        for (int i = 0; i < spots; i++) {
            int x = 1 + hashCoords(i, 0, 0, 3602 + part) % 13, y = 1 + hashCoords(i, 1, 0, 3602 + part) % 13;
            im.rect(x, y, x + 1, y + 1, 0xf0eadc);
        }
        if (part == 2) for (int x = 0; x < 16; x++) im.set(x, 15, 0x6a1a10);
        return;
    }
    furFill(im, 0x6a8a3a, 3603, 0.14f);
    for (int x = 1; x < 16; x += 4) for (int y = 0; y < 16; y++) if (prand(x, y, 3604) > 0.3f) im.set(x, y, 0x4a6a2a);
    if (part == 1) {
        im.rect(3, 3, 5, 6, 0x101808); im.rect(10, 3, 12, 6, 0x101808); im.set(4, 4, 0xe8ff80); im.set(11, 4, 0xe8ff80);
        im.rect(5, 9, 10, 12, 0x101808); im.rect(6, 10, 9, 11, 0x3a1a10);
    }
}
void weaverSkin(Img& im, int part) {  // 0 body 1 face 2 leg
    if (part == 2) { furFill(im, 0x2e2622, 3701, 0.15f); for (int y = 0; y < 16; y += 4) for (int x = 0; x < 16; x++) im.set(x, y, 0x4a3e34); return; }
    furFill(im, 0x5a4e42, 3702, 0.2f);
    for (int i = 0; i < 10; i++) im.set(hashCoords(i, 0, 0, 3703) % 16, hashCoords(i, 1, 0, 3703) % 16, 0x7a6e5e);
    if (part == 1) {
        const int E[6][2] = {{3, 5}, {11, 5}, {5, 8}, {9, 8}, {2, 10}, {12, 10}};
        for (auto& e : E) { im.rect(e[0], e[1], e[0] + 1, e[1] + 1, 0xff8a10); im.set(e[0], e[1], 0xffe080); }
        im.rect(6, 12, 9, 13, 0x1a1410);
    }
}
void playerSkin(Img& im, int part) {  // 0 skin 1 face 2 hair 3 shirt 4 pants
    if (part == 2) { furFill(im, 0x3a2414, 3801, 0.1f); return; }
    if (part == 3) { furFill(im, 0x2a8a8a, 3802, 0.08f); for (int x = 0; x < 16; x++) im.set(x, 15, 0x1a6a6a); return; }
    if (part == 4) { furFill(im, 0x4a3a6a, 3803, 0.08f); return; }
    furFill(im, 0xd8a078, 3804, 0.04f);
    if (part == 1) {
        im.rect(0, 0, 15, 3, 0x3a2414); im.set(0, 4, 0x3a2414); im.set(15, 4, 0x3a2414);
        im.rect(3, 7, 5, 8, 0xffffff); im.rect(10, 7, 12, 8, 0xffffff); im.rect(4, 7, 5, 8, 0x3a50a0); im.rect(10, 7, 11, 8, 0x3a50a0);
        im.rect(6, 11, 9, 11, 0xa06050); im.rect(7, 10, 8, 10, 0xc88a68);
    }
}
}  // namespace

void generateTextures(std::vector<uint8_t>& out) {
    out.assign((size_t)TX_COUNT * 16 * 16 * 4, 0);
    auto put = [&](int layer, const Img& im) { memcpy(out.data() + (size_t)layer * 1024, im.p, 1024); };
    Img im;
#define T(layer, code) { im = Img(); code; put(layer, im); }
    T(TX_STONE, stone(im)); T(TX_DIRT, dirt(im)); T(TX_GRASS_TOP, grassTop(im)); T(TX_GRASS_SIDE, grassSide(im, false));
    T(TX_SAND, sand(im)); T(TX_GRAVEL, gravel(im)); T(TX_CLAY, clay(im)); T(TX_COBBLE, cobble(im)); T(TX_BEDROCK, bedrock(im));
    T(TX_OAK_LOG, logSide(im, 0x6a4e2e, 0x4a3420, 101, false)); T(TX_OAK_LOG_TOP, logTop(im, 0x5a4026, 0xb08a58, 0x9a7646));
    T(TX_BIRCH_LOG, logSide(im, 0xe2e0d6, 0xc8c4b4, 111, true)); T(TX_BIRCH_LOG_TOP, logTop(im, 0xdcdad0, 0xd0bc88, 0xbca874));
    T(TX_SPRUCE_LOG, logSide(im, 0x4a3420, 0x2e2014, 121, false)); T(TX_SPRUCE_LOG_TOP, logTop(im, 0x3e2a18, 0x8a6a42, 0x745636));
    { const uint32_t P[] = {0x2f6a1c, 0x3a7a22, 0x46892a, 0x55993a}; T(TX_OAK_LEAVES, leaves(im, P, 131)); }
    { const uint32_t P[] = {0x5a8a30, 0x689a3a, 0x76a844, 0x88b852}; T(TX_BIRCH_LEAVES, leaves(im, P, 141)); }
    { const uint32_t P[] = {0x1e4a30, 0x26563a, 0x2e6244, 0x3a704e}; T(TX_SPRUCE_LEAVES, leaves(im, P, 151)); }
    T(TX_OAK_PLANKS, planks(im, OAK_PL, 161)); T(TX_BIRCH_PLANKS, planks(im, BIRCH_PL, 171)); T(TX_SPRUCE_PLANKS, planks(im, SPRUCE_PL, 181));
    T(TX_GLASS, glass(im));
    T(TX_COAL_ORE, ore(im, 0x222222, 0x333333, 0x4a4a4a, 191, 5));
    T(TX_IRON_ORE, ore(im, 0xc89a78, 0xd8b090, 0xe8c8a8, 192, 4));
    T(TX_GOLD_ORE, ore(im, 0xe8c020, 0xf8d840, 0xfff080, 193, 4));
    T(TX_DIAMOND_ORE, ore(im, 0x30c8c0, 0x60e8e0, 0xc0fff8, 194, 4));
    for (int f = 0; f < 4; f++) { T(TX_WATER + f, liquid(im, f, false)); T(TX_LAVA + f, liquid(im, f, true)); }
    T(TX_TORCH, torch(im)); T(TX_CRAFT_TOP, craftTop(im)); T(TX_CRAFT_SIDE, craftSide(im, false)); T(TX_CRAFT_FRONT, craftSide(im, true));
    T(TX_FURNACE_SIDE, furnaceSide(im)); { im = Img(); furnaceSide(im); for (int y = 2; y < 14; y++) for (int x = 2; x < 14; x++) if (x == 2 || y == 2 || x == 13 || y == 13) im.set(x, y, 0x5a5a5a); put(TX_FURNACE_TOP, im); }
    T(TX_FURNACE_FRONT, furnaceFront(im, false)); T(TX_FURNACE_FRONT_LIT, furnaceFront(im, true));
    T(TX_CHEST_TOP, chest(im, 0)); T(TX_CHEST_SIDE, chest(im, 1)); T(TX_CHEST_FRONT, chest(im, 2));
    for (int i = 0; i < 6; i++) T(TX_BED_TOP_HEAD + i, bed(im, i));
    T(TX_DOOR_TOP, door(im, true)); T(TX_DOOR_BOTTOM, door(im, false));
    for (int i = 0; i < 8; i++) T(TX_WHEAT0 + i, wheat(im, i));
    T(TX_FARMLAND, farmland(im));
    T(TX_WOOL_WHITE, wool(im, 0xeeeeee, 1)); T(TX_WOOL_GRAY, wool(im, 0x6e6e6e, 2)); T(TX_WOOL_BLACK, wool(im, 0x262626, 3)); T(TX_WOOL_BROWN, wool(im, 0x6a4428, 4));
    T(TX_SNOW, snow(im)); T(TX_ICE, ice(im)); T(TX_CACTUS_SIDE, cactus(im, false)); T(TX_CACTUS_TOP, cactus(im, true));
    T(TX_TALL_GRASS, tallGrass(im)); T(TX_FLOWER_RED, flower(im, 0xd82020, 0xf0d020)); T(TX_FLOWER_YELLOW, flower(im, 0xf0d020, 0xc07010));
    T(TX_DEAD_BUSH, deadBush(im)); T(TX_SANDSTONE_SIDE, sandstone(im, false)); T(TX_SANDSTONE_TOP, sandstone(im, true));
    T(TX_STONE_BRICKS, stoneBricks(im));
    T(TX_IRON_BLOCK, metalBlock(im, 0xd0d0d0, 0x8a8a8a, 0xf4f4f4)); T(TX_GOLD_BLOCK, metalBlock(im, 0xf0d030, 0xb08a10, 0xfff6a0));
    T(TX_DIAMOND_BLOCK, metalBlock(im, 0x50e0d4, 0x20908a, 0xc8fff8)); T(TX_COAL_BLOCK, metalBlock(im, 0x1e1e1e, 0x0e0e0e, 0x3a3a3a));
    T(TX_BRICKS, bricks(im)); T(TX_MOSSY_COBBLE, mossyCobble(im)); T(TX_SNOWY_GRASS_SIDE, grassSide(im, true)); T(TX_LADDER, ladder(im));
    T(TX_OAK_SAPLING, sapling(im, 0)); T(TX_BIRCH_SAPLING, sapling(im, 1)); T(TX_SPRUCE_SAPLING, sapling(im, 2)); T(TX_OBSIDIAN, obsidian(im));
    for (int i = 0; i < 10; i++) T(TX_CRACK0 + i, crack(im, i));
    // items
    T(TX_STICK, stickItem(im)); T(TX_COAL, coalItem(im)); T(TX_IRON_INGOT, ingot(im, MAT[2])); T(TX_GOLD_INGOT, ingot(im, MAT[3]));
    T(TX_DIAMOND, diamondItem(im));
    for (int m = 0; m < 5; m++) for (int t = 0; t < 5; t++) T(TX_TOOLS_FIRST + m * 5 + t, tool(im, m, t));
    T(TX_APPLE, appleItem(im));
    { const uint32_t P[] = {0xc87070, 0xe08888, 0xf0a0a0, 0xf8c0c0}; T(TX_RAW_PORK, meat(im, P, 0xfff0f0, 2701, false)); }
    { const uint32_t P[] = {0x8a4a22, 0xa8602e, 0xc0783a, 0xd8945a}; T(TX_COOKED_PORK, meat(im, P, 0xf0d0a0, 2702, false)); }
    { const uint32_t P[] = {0x8a1a1a, 0xa82424, 0xc03232, 0xd04848}; T(TX_RAW_BEEF, meat(im, P, 0xf0e0e0, 2703, false)); }
    { const uint32_t P[] = {0x4a2410, 0x6a3418, 0x804424, 0x9a5a32}; T(TX_STEAK, meat(im, P, 0xb08060, 2704, false)); }
    { const uint32_t P[] = {0xe0a8a0, 0xf0bcb4, 0xf8d0c8, 0xfff0e8}; T(TX_RAW_CHICKEN, meat(im, P, 0xffffff, 2705, true)); }
    { const uint32_t P[] = {0x9a5a22, 0xb8742e, 0xd08e40, 0xe8b060}; T(TX_COOKED_CHICKEN, meat(im, P, 0xffffff, 2706, true)); }
    { const uint32_t P[] = {0xa02828, 0xb83434, 0xd04444, 0xe06060}; T(TX_RAW_MUTTON, meat(im, P, 0xf8e8e0, 2707, true)); }
    { const uint32_t P[] = {0x5a2a12, 0x7a3c1c, 0x8e4c28, 0xa86238}; T(TX_COOKED_MUTTON, meat(im, P, 0xe8d8c0, 2708, true)); }
    T(TX_BREAD, breadItem(im)); T(TX_WHEAT_ITEM, wheatItem(im)); T(TX_SEEDS, seedsItem(im));
    { const uint32_t P[] = {0x4a5a22, 0x6a6a2a, 0x7a5a30, 0x8a7a3a}; T(TX_ROTTEN_FLESH, meat(im, P, 0x5a7a2a, 2709, false)); }
    T(TX_BONE, boneItem(im)); T(TX_ARROW, arrowItem(im));
    for (int i = 0; i < 4; i++) T(TX_BOW + i, bowItem(im, i));
    T(TX_STRING, stringItem(im)); T(TX_GUNPOWDER, gunpowderItem(im)); T(TX_FEATHER, featherItem(im)); T(TX_LEATHER, leatherItem(im));
    T(TX_CLAY_BALL, clayBallItem(im)); T(TX_BRICK, brickItem(im)); T(TX_FLINT, flintItem(im)); T(TX_BED_ITEM, bedItem(im)); T(TX_DOOR_ITEM, doorItem(im));
    // UI & particles
    T(TX_HEART_FULL, heart(im, 0)); T(TX_HEART_HALF, heart(im, 1)); T(TX_HEART_EMPTY, heart(im, 2));
    T(TX_FOOD_FULL, food(im, 0)); T(TX_FOOD_HALF, food(im, 1)); T(TX_FOOD_EMPTY, food(im, 2)); T(TX_BUBBLE, bubble(im));
    T(TX_WHITE, im.fill(0xffffff)); T(TX_FLAME, particle(im, 0)); T(TX_SMOKE, particle(im, 1)); T(TX_SPARK, particle(im, 2));
    // mobs
    T(TX_PIG_SKIN, pigSkin(im, false)); T(TX_PIG_FACE, pigSkin(im, true));
    T(TX_COW_SKIN, cowSkin(im, false)); T(TX_COW_FACE, cowSkin(im, true));
    T(TX_SHEEP_WOOL, sheepSkin(im, 0)); T(TX_SHEEP_FACE, sheepSkin(im, 1)); T(TX_SHEEP_SKIN, sheepSkin(im, 2));
    T(TX_CHICKEN_BODY, chickenSkin(im, 0)); T(TX_CHICKEN_FACE, chickenSkin(im, 1)); T(TX_CHICKEN_LEG, chickenSkin(im, 2));
    for (int i = 0; i < 4; i++) T(TX_SHAMBLER_SKIN + i, shamblerSkin(im, i));
    for (int i = 0; i < 3; i++) T(TX_MARROW_BONE + i, marrowSkin(im, i));
    for (int i = 0; i < 4; i++) T(TX_FUSE_BODY + i, fuseSkin(im, i));
    for (int i = 0; i < 3; i++) T(TX_WEAVER_BODY + i, weaverSkin(im, i));
    for (int i = 0; i < 5; i++) T(TX_PLAYER_SKIN + i, playerSkin(im, i == 0 ? 0 : (i == 1 ? 1 : i)));
#undef T
}

// ---------- 5x7 bitmap font (ASCII 32..126), each row 5 bits, MSB = left ----------
static const uint8_t FONT57[95][7] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x04,0x04,0x04,0x04,0x04,0x00,0x04},{0x0A,0x0A,0x0A,0x00,0x00,0x00,0x00},
    {0x0A,0x0A,0x1F,0x0A,0x1F,0x0A,0x0A},{0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04},{0x18,0x19,0x02,0x04,0x08,0x13,0x03},
    {0x0C,0x12,0x14,0x08,0x15,0x12,0x0D},{0x04,0x04,0x08,0x00,0x00,0x00,0x00},{0x02,0x04,0x08,0x08,0x08,0x04,0x02},
    {0x08,0x04,0x02,0x02,0x02,0x04,0x08},{0x00,0x04,0x15,0x0E,0x15,0x04,0x00},{0x00,0x04,0x04,0x1F,0x04,0x04,0x00},
    {0x00,0x00,0x00,0x00,0x0C,0x04,0x08},{0x00,0x00,0x00,0x1F,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x0C,0x0C},
    {0x00,0x01,0x02,0x04,0x08,0x10,0x00},{0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},{0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},{0x1F,0x02,0x04,0x02,0x01,0x11,0x0E},{0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},
    {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},{0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},{0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},{0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},{0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00},
    {0x00,0x0C,0x0C,0x00,0x0C,0x04,0x08},{0x02,0x04,0x08,0x10,0x08,0x04,0x02},{0x00,0x00,0x1F,0x00,0x1F,0x00,0x00},
    {0x08,0x04,0x02,0x01,0x02,0x04,0x08},{0x0E,0x11,0x01,0x02,0x04,0x00,0x04},{0x0E,0x11,0x01,0x0D,0x15,0x15,0x0E},
    {0x0E,0x11,0x11,0x11,0x1F,0x11,0x11},{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},{0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
    {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C},{0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},{0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F},{0x11,0x11,0x11,0x1F,0x11,0x11,0x11},{0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},
    {0x07,0x02,0x02,0x02,0x02,0x12,0x0C},{0x11,0x12,0x14,0x18,0x14,0x12,0x11},{0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},{0x11,0x11,0x19,0x15,0x13,0x11,0x11},{0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},{0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},{0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E},{0x1F,0x04,0x04,0x04,0x04,0x04,0x04},{0x11,0x11,0x11,0x11,0x11,0x11,0x0E},
    {0x11,0x11,0x11,0x11,0x11,0x0A,0x04},{0x11,0x11,0x11,0x15,0x15,0x15,0x0A},{0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
    {0x11,0x11,0x11,0x0A,0x04,0x04,0x04},{0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},{0x0E,0x08,0x08,0x08,0x08,0x08,0x0E},
    {0x00,0x10,0x08,0x04,0x02,0x01,0x00},{0x0E,0x02,0x02,0x02,0x02,0x02,0x0E},{0x04,0x0A,0x11,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x1F},{0x08,0x04,0x02,0x00,0x00,0x00,0x00},{0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F},
    {0x10,0x10,0x16,0x19,0x11,0x11,0x1E},{0x00,0x00,0x0E,0x10,0x10,0x11,0x0E},{0x01,0x01,0x0D,0x13,0x11,0x11,0x0F},
    {0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E},{0x06,0x09,0x08,0x1C,0x08,0x08,0x08},{0x00,0x0F,0x11,0x11,0x0F,0x01,0x0E},
    {0x10,0x10,0x16,0x19,0x11,0x11,0x11},{0x04,0x00,0x0C,0x04,0x04,0x04,0x0E},{0x02,0x00,0x06,0x02,0x02,0x12,0x0C},
    {0x10,0x10,0x12,0x14,0x18,0x14,0x12},{0x0C,0x04,0x04,0x04,0x04,0x04,0x0E},{0x00,0x00,0x1A,0x15,0x15,0x11,0x11},
    {0x00,0x00,0x16,0x19,0x11,0x11,0x11},{0x00,0x00,0x0E,0x11,0x11,0x11,0x0E},{0x00,0x00,0x1E,0x11,0x1E,0x10,0x10},
    {0x00,0x00,0x0D,0x13,0x0F,0x01,0x01},{0x00,0x00,0x16,0x19,0x10,0x10,0x10},{0x00,0x00,0x0E,0x10,0x0E,0x01,0x1E},
    {0x08,0x08,0x1C,0x08,0x08,0x09,0x06},{0x00,0x00,0x11,0x11,0x11,0x13,0x0D},{0x00,0x00,0x11,0x11,0x11,0x0A,0x04},
    {0x00,0x00,0x11,0x11,0x15,0x15,0x0A},{0x00,0x00,0x11,0x0A,0x04,0x0A,0x11},{0x00,0x00,0x11,0x11,0x0F,0x01,0x0E},
    {0x00,0x00,0x1F,0x02,0x04,0x08,0x1F},{0x02,0x04,0x04,0x08,0x04,0x04,0x02},{0x04,0x04,0x04,0x04,0x04,0x04,0x04},
    {0x08,0x04,0x04,0x02,0x04,0x04,0x08},{0x00,0x00,0x08,0x15,0x02,0x00,0x00},
};

void generateFont(std::vector<uint8_t>& out, int& w, int& h, uint8_t glyphWidth[128]) {
    w = 128; h = 64;
    out.assign(w * h, 0);
    for (int c = 0; c < 128; c++) glyphWidth[c] = 6;
    for (int c = 32; c < 127; c++) {
        const uint8_t* g = FONT57[c - 32];
        int gx = (c % 16) * 8, gy = (c / 16) * 8;
        if (c == 'g' || c == 'j' || c == 'p' || c == 'q' || c == 'y') gy += 1;  // descenders hang below the baseline
        int maxCol = -1, minCol = 5;
        for (int r = 0; r < 7; r++)
            for (int col = 0; col < 5; col++)
                if (g[r] & (0x10 >> col)) {
                    out[(gy + r) * w + gx + col] = 255;
                    maxCol = std::max(maxCol, col); minCol = std::min(minCol, col);
                }
        glyphWidth[c] = (uint8_t)(maxCol < 0 ? 4 : maxCol + 2);
    }
}
