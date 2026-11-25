#include "save.h"
#include "../game/player.h"
#include "../core/platform.h"
#include <cstdio>
#include <cstring>
#include <ctime>
#include <algorithm>

namespace {
const uint32_t WORLD_MAGIC = 0x31574342;   // "BCW1"
const uint32_t PLAYER_MAGIC = 0x31504342;  // "BCP1"
const uint32_t WORLD_VERSION = 1, PLAYER_VERSION = 1;

struct Out {
    std::vector<uint8_t> b;
    void raw(const void* p, size_t n) { const uint8_t* q = (const uint8_t*)p; b.insert(b.end(), q, q + n); }
    template <class T> void put(T v) { raw(&v, sizeof(T)); }
    void str(const std::string& s) { put<uint16_t>((uint16_t)s.size()); raw(s.data(), s.size()); }
};
struct In {
    std::vector<uint8_t> b; size_t i = 0; bool ok = true;
    template <class T> T get() { T v{}; if (i + sizeof(T) > b.size()) { ok = false; return v; } memcpy(&v, &b[i], sizeof(T)); i += sizeof(T); return v; }
    std::string str() { uint16_t n = get<uint16_t>(); if (i + n > b.size()) { ok = false; return ""; } std::string s((const char*)&b[i], n); i += n; return s; }
};
bool writeAtomic(const std::string& path, const std::vector<uint8_t>& data) {
    std::string tmp = path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
    fflush(f);
    fclose(f);
    return ok && plat::renameFile(tmp, path);
}
bool readAll(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > 16 * 1024 * 1024) { fclose(f); return false; }
    out.resize((size_t)n);
    bool ok = fread(out.data(), 1, (size_t)n, f) == (size_t)n;
    fclose(f);
    return ok;
}
}

bool saveWorldInfo(const std::string& dir, const WorldInfo& w) {
    Out o;
    o.put(WORLD_MAGIC); o.put(WORLD_VERSION);
    o.str(w.name); o.put(w.seed); o.put(w.time); o.put((int64_t)time(nullptr));
    o.put(w.spawn.x); o.put(w.spawn.y); o.put(w.spawn.z);
    o.put<uint8_t>(w.doDaylightCycle); o.put<uint8_t>(w.doMobSpawning); o.put<uint8_t>(w.keepInventory);
    return writeAtomic(dir + "world.dat", o.b);
}

bool loadWorldInfo(const std::string& dir, WorldInfo& w) {
    In in;
    if (!readAll(dir + "world.dat", in.b)) return false;
    if (in.get<uint32_t>() != WORLD_MAGIC) return false;
    uint32_t ver = in.get<uint32_t>();
    if (ver < 1 || ver > WORLD_VERSION) return false;
    w.name = in.str(); w.seed = in.get<uint64_t>(); w.time = in.get<int64_t>(); w.lastPlayed = in.get<int64_t>();
    w.spawn.x = in.get<int>(); w.spawn.y = in.get<int>(); w.spawn.z = in.get<int>();
    w.doDaylightCycle = in.get<uint8_t>() != 0; w.doMobSpawning = in.get<uint8_t>() != 0; w.keepInventory = in.get<uint8_t>() != 0;
    return in.ok;
}

bool savePlayer(const std::string& dir, const Player& p) {
    Out o;
    o.put(PLAYER_MAGIC); o.put(PLAYER_VERSION);
    o.put(p.pos.x); o.put(p.pos.y); o.put(p.pos.z); o.put(p.yaw); o.put(p.pitch);
    o.put(p.health); o.put(p.hunger); o.put(p.saturation); o.put(p.exhaustion); o.put(p.air); o.put(p.fireTicks);
    o.put(p.spawnPoint.x); o.put(p.spawnPoint.y); o.put(p.spawnPoint.z); o.put<uint8_t>(p.bedSpawn);
    o.put<uint8_t>(p.flying); o.put<int32_t>(p.inv.selected);
    o.put<uint8_t>(p.dead);
    for (auto& s : p.inv.slots) { o.put(s.id); o.put(s.count); o.put(s.damage); }
    return writeAtomic(dir + "player.dat", o.b);
}

bool loadPlayer(const std::string& dir, Player& p) {
    In in;
    if (!readAll(dir + "player.dat", in.b)) return false;
    if (in.get<uint32_t>() != PLAYER_MAGIC) return false;
    uint32_t ver = in.get<uint32_t>();
    if (ver < 1 || ver > PLAYER_VERSION) return false;
    p.pos.x = in.get<float>(); p.pos.y = in.get<float>(); p.pos.z = in.get<float>();
    p.yaw = in.get<float>(); p.pitch = in.get<float>();
    p.health = in.get<float>(); p.hunger = in.get<float>(); p.saturation = in.get<float>(); p.exhaustion = in.get<float>();
    p.air = in.get<int>(); p.fireTicks = in.get<int>();
    p.spawnPoint.x = in.get<int>(); p.spawnPoint.y = in.get<int>(); p.spawnPoint.z = in.get<int>(); p.bedSpawn = in.get<uint8_t>() != 0;
    p.flying = in.get<uint8_t>() != 0; p.inv.selected = std::max(0, std::min(8, (int)in.get<int32_t>()));
    p.dead = in.get<uint8_t>() != 0;
    for (auto& s : p.inv.slots) {
        s.id = in.get<uint16_t>(); s.count = in.get<uint8_t>(); s.damage = in.get<uint16_t>();
        if (s.id >= ITEM_END || s.count == 0) s.clear();
    }
    p.prevPos = p.pos;
    return in.ok;
}

std::vector<WorldInfo> listWorlds(const std::string& savesDir) {
    std::vector<WorldInfo> out;
    for (auto& d : plat::listDirs(savesDir)) {
        WorldInfo w;
        if (loadWorldInfo(savesDir + d + "\\", w)) { w.folder = d; out.push_back(w); }
    }
    std::sort(out.begin(), out.end(), [](const WorldInfo& a, const WorldInfo& b) { return a.lastPlayed > b.lastPlayed; });
    return out;
}

std::string makeFolderName(const std::string& savesDir, const std::string& name) {
    std::string base;
    for (char c : name) base += (isalnum((unsigned char)c) || c == '-' || c == '_') ? c : '_';
    if (base.empty()) base = "World";
    auto dirs = plat::listDirs(savesDir);
    std::string n = base;
    for (int k = 2; std::find(dirs.begin(), dirs.end(), n) != dirs.end(); k++) n = base + "_" + std::to_string(k);
    return n;
}
