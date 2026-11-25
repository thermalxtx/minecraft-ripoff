// world.dat and player.dat (versioned binary files written via temp file + rename).
#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "../core/math.h"

class Player;

struct WorldInfo {
    std::string name;       // display name
    std::string folder;     // folder under saves/
    uint64_t seed = 0;
    int64_t time = 0;       // ticks since creation (time of day = time % 24000)
    int64_t lastPlayed = 0; // unix-ish seconds
    ivec3 spawn{0, 80, 0};
    bool doDaylightCycle = true, doMobSpawning = true, keepInventory = false;
};

bool saveWorldInfo(const std::string& dir, const WorldInfo& w);
bool loadWorldInfo(const std::string& dir, WorldInfo& w);
bool savePlayer(const std::string& dir, const Player& p);
bool loadPlayer(const std::string& dir, Player& p);
std::vector<WorldInfo> listWorlds(const std::string& savesDir);
std::string makeFolderName(const std::string& savesDir, const std::string& name);
