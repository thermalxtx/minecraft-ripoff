// Procedural sound effect synthesis (platform independent). Every sound is generated in code at startup.
#pragma once
#include <vector>

constexpr int SAMPLE_RATE = 44100;
using Wave = std::vector<float>;

// Builds all variants of every SoundId into bank[id]
void synthesizeAll(std::vector<std::vector<Wave>>& bank);
