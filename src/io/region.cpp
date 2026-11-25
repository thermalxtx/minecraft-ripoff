#include "region.h"
#include "../world/chunk.h"
#include "../core/math.h"
#include "../core/platform.h"
#include <zlib.h>
#include <cstdio>
#include <cstring>
#include <map>

namespace {
const uint32_t REGION_MAGIC = 0x31524342;  // "BCR1"
const uint32_t REGION_VERSION = 1;
const uint16_t CHUNK_VERSION = 1;

struct Writer {
    std::vector<uint8_t>& b;
    void u8(uint8_t v) { b.push_back(v); }
    void u16(uint16_t v) { b.push_back(v & 255); b.push_back(v >> 8); }
    void u32(uint32_t v) { for (int i = 0; i < 4; i++) b.push_back((v >> (i * 8)) & 255); }
    void f32(float f) { uint32_t v; memcpy(&v, &f, 4); u32(v); }
    void bytes(const void* p, size_t n) { const uint8_t* q = (const uint8_t*)p; b.insert(b.end(), q, q + n); }
};
struct Reader {
    const uint8_t* p; size_t n, i = 0; bool ok = true;
    bool need(size_t k) { if (i + k > n) { ok = false; return false; } return true; }
    uint8_t u8() { if (!need(1)) return 0; return p[i++]; }
    uint16_t u16() { if (!need(2)) return 0; uint16_t v = p[i] | (p[i + 1] << 8); i += 2; return v; }
    uint32_t u32() { if (!need(4)) return 0; uint32_t v = p[i] | (p[i + 1] << 8) | (p[i + 2] << 16) | ((uint32_t)p[i + 3] << 24); i += 4; return v; }
    float f32() { uint32_t v = u32(); float f; memcpy(&f, &v, 4); return f; }
    void bytes(void* d, size_t k) { if (!need(k)) return; memcpy(d, p + i, k); i += k; }
};
void writeStack(Writer& w, const ItemStack& s) { w.u16(s.id); w.u8(s.count); w.u16(s.damage); }
ItemStack readStack(Reader& r) { ItemStack s; s.id = r.u16(); s.count = r.u8(); s.damage = r.u16(); if (s.id >= ITEM_END) s.clear(); return s; }
}

bool zcompress(const std::vector<uint8_t>& in, std::vector<uint8_t>& out) {
    uLongf n = compressBound((uLong)in.size());
    out.resize(n + 4);
    uint32_t raw = (uint32_t)in.size();
    memcpy(out.data(), &raw, 4);
    if (compress2(out.data() + 4, &n, in.data(), (uLong)in.size(), 6) != Z_OK) return false;
    out.resize(n + 4);
    return true;
}
bool zdecompress(const uint8_t* in, size_t n, std::vector<uint8_t>& out) {
    if (n < 4) return false;
    uint32_t raw; memcpy(&raw, in, 4);
    if (raw > 64u * 1024 * 1024) return false;
    out.resize(raw);
    uLongf dl = raw;
    if (uncompress(out.data(), &dl, in + 4, (uLong)(n - 4)) != Z_OK || dl != raw) return false;
    return true;
}

void serializeChunk(const Chunk& c, std::vector<uint8_t>& out) {
    out.clear();
    out.reserve(CHUNK_VOL * 3 + 1024);
    Writer w{out};
    w.u16(CHUNK_VERSION);
    w.u32((uint32_t)c.cx); w.u32((uint32_t)c.cz);
    w.bytes(c.blocks, sizeof(c.blocks));
    w.bytes(c.meta, sizeof(c.meta));
    w.u32((uint32_t)c.tiles.size());
    for (auto& kv : c.tiles) {
        w.u32((uint32_t)kv.first); w.u8(kv.second.type);
        w.u16((uint16_t)kv.second.burnTime); w.u16((uint16_t)kv.second.burnTotal); w.u16((uint16_t)kv.second.cookTime);
        for (auto& s : kv.second.slots) writeStack(w, s);
    }
    w.u32((uint32_t)c.savedEntities.size());
    for (auto& e : c.savedEntities) {
        w.u8(e.type); w.f32(e.x); w.f32(e.y); w.f32(e.z); w.f32(e.yaw); w.u16((uint16_t)e.health);
        writeStack(w, e.item); w.u32((uint32_t)e.age); w.u8(e.extra);
    }
}

bool deserializeChunk(const std::vector<uint8_t>& in, Chunk& c) {
    Reader r{in.data(), in.size()};
    uint16_t ver = r.u16();
    if (ver < 1 || ver > CHUNK_VERSION) return false;
    r.u32(); r.u32();
    r.bytes(c.blocks, sizeof(c.blocks));
    r.bytes(c.meta, sizeof(c.meta));
    for (auto& b : c.blocks) if (b >= BLOCK_COUNT) b = AIR;
    uint32_t nt = r.u32();
    for (uint32_t i = 0; i < nt && r.ok; i++) {
        int key = (int)r.u32();
        TileEntity t; t.type = r.u8();
        t.burnTime = (int16_t)r.u16(); t.burnTotal = (int16_t)r.u16(); t.cookTime = (int16_t)r.u16();
        for (auto& s : t.slots) s = readStack(r);
        if (key >= 0 && key < CHUNK_VOL) c.tiles[key] = t;
    }
    uint32_t ne = r.u32();
    for (uint32_t i = 0; i < ne && r.ok && i < 4096; i++) {
        SavedEntity e;
        e.type = r.u8(); e.x = r.f32(); e.y = r.f32(); e.z = r.f32(); e.yaw = r.f32(); e.health = (int16_t)r.u16();
        e.item = readStack(r); e.age = (int32_t)r.u32(); e.extra = r.u8();
        c.savedEntities.push_back(e);
    }
    return r.ok;
}

RegionStore::RegionStore(const std::string& worldDir) : dir(worldDir + "region") { plat::makeDir(dir); }

std::string RegionStore::regionPath(int rx, int rz) const {
    char b[64]; snprintf(b, sizeof(b), "\\r.%d.%d.bcr", rx, rz);
    return dir + b;
}

bool RegionStore::readBlob(int cx, int cz, std::vector<uint8_t>& out) {
    int rx = floorDiv(cx, 32), rz = floorDiv(cz, 32);
    FILE* f = fopen(regionPath(rx, rz).c_str(), "rb");
    if (!f) return false;
    uint32_t hdr[2];
    bool ok = false;
    if (fread(hdr, 4, 2, f) == 2 && hdr[0] == REGION_MAGIC && hdr[1] <= REGION_VERSION) {
        int slot = floorMod(cx, 32) + floorMod(cz, 32) * 32;
        uint32_t ent[2];
        if (fseek(f, 8 + slot * 8, SEEK_SET) == 0 && fread(ent, 4, 2, f) == 2 && ent[0] && ent[1]) {
            out.resize(ent[1]);
            if (fseek(f, (long)ent[0], SEEK_SET) == 0 && fread(out.data(), 1, ent[1], f) == ent[1]) ok = true;
        }
    }
    fclose(f);
    return ok;
}

bool RegionStore::load(int cx, int cz, Chunk& c) {
    std::vector<uint8_t> blob, raw;
    {
        std::lock_guard<std::mutex> l(m);
        auto it = pending.find(chunkKey(cx, cz));
        if (it != pending.end()) blob = it->second;
        else if (!readBlob(cx, cz, blob)) return false;
    }
    if (!zdecompress(blob.data(), blob.size(), raw)) return false;
    return deserializeChunk(raw, c);
}

void RegionStore::store(const Chunk& c) {
    std::vector<uint8_t> raw, comp;
    serializeChunk(c, raw);
    if (!zcompress(raw, comp)) return;
    std::lock_guard<std::mutex> l(m);
    pending[chunkKey(c.cx, c.cz)] = std::move(comp);
}

void RegionStore::flush() {
    std::lock_guard<std::mutex> l(m);
    if (pending.empty()) return;
    // group by region
    std::map<std::pair<int, int>, std::vector<int64_t>> byRegion;
    for (auto& kv : pending) {
        int cx = (int)(kv.first >> 32), cz = (int)(uint32_t)kv.first;
        byRegion[{floorDiv(cx, 32), floorDiv(cz, 32)}].push_back(kv.first);
    }
    for (auto& rg : byRegion) {
        int rx = rg.first.first, rz = rg.first.second;
        // gather existing blobs
        std::vector<std::vector<uint8_t>> blobs(1024);
        std::string path = regionPath(rx, rz);
        FILE* f = fopen(path.c_str(), "rb");
        if (f) {
            uint32_t hdr[2], table[2048];
            if (fread(hdr, 4, 2, f) == 2 && hdr[0] == REGION_MAGIC && fread(table, 4, 2048, f) == 2048) {
                for (int s = 0; s < 1024; s++) {
                    if (!table[s * 2] || !table[s * 2 + 1]) continue;
                    blobs[s].resize(table[s * 2 + 1]);
                    if (fseek(f, (long)table[s * 2], SEEK_SET) != 0 || fread(blobs[s].data(), 1, blobs[s].size(), f) != blobs[s].size()) blobs[s].clear();
                }
            }
            fclose(f);
        }
        for (int64_t key : rg.second) {
            int cx = (int)(key >> 32), cz = (int)(uint32_t)key;
            blobs[floorMod(cx, 32) + floorMod(cz, 32) * 32] = pending[key];
        }
        std::string tmp = path + ".tmp";
        FILE* o = fopen(tmp.c_str(), "wb");
        if (!o) continue;
        uint32_t hdr[2] = {REGION_MAGIC, REGION_VERSION};
        uint32_t table[2048] = {};
        uint32_t off = 8 + sizeof(table);
        for (int s = 0; s < 1024; s++) {
            if (blobs[s].empty()) continue;
            table[s * 2] = off; table[s * 2 + 1] = (uint32_t)blobs[s].size(); off += (uint32_t)blobs[s].size();
        }
        bool ok = fwrite(hdr, 4, 2, o) == 2 && fwrite(table, 4, 2048, o) == 2048;
        for (int s = 0; s < 1024 && ok; s++) if (!blobs[s].empty()) ok = fwrite(blobs[s].data(), 1, blobs[s].size(), o) == blobs[s].size();
        fflush(o);
        fclose(o);
        if (ok) plat::renameFile(tmp, path);
    }
    pending.clear();
}
