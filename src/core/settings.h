// User options, saved as options.txt (key=value) next to the .exe.
#pragma once
#include <string>

struct Settings {
    int renderDistance = 10;     // chunks (4..16)
    float fov = 70;              // degrees
    float sensitivity = 1.0f;    // mouse look multiplier
    float brightness = 0.5f;     // 0 moody .. 1 bright
    float masterVolume = 0.8f;
    float musicVolume = 0.5f;
    float sfxVolume = 1.0f;
    bool viewBobbing = true;
    bool fullscreen = false;
    bool vsync = true;
    int guiScale = 0;            // 0 = auto
    void load(const std::string& path);
    void save(const std::string& path) const;
};
