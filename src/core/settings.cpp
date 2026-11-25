#include "settings.h"
#include "math.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

void Settings::load(const std::string& path) {
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        const char* k = line; float v = (float)atof(eq + 1);
        if (!strcmp(k, "renderDistance")) renderDistance = (int)v;
        else if (!strcmp(k, "fov")) fov = v;
        else if (!strcmp(k, "sensitivity")) sensitivity = v;
        else if (!strcmp(k, "brightness")) brightness = v;
        else if (!strcmp(k, "masterVolume")) masterVolume = v;
        else if (!strcmp(k, "musicVolume")) musicVolume = v;
        else if (!strcmp(k, "sfxVolume")) sfxVolume = v;
        else if (!strcmp(k, "viewBobbing")) viewBobbing = v != 0;
        else if (!strcmp(k, "fullscreen")) fullscreen = v != 0;
        else if (!strcmp(k, "vsync")) vsync = v != 0;
        else if (!strcmp(k, "guiScale")) guiScale = (int)v;
    }
    fclose(f);
    renderDistance = std::max(4, std::min(16, renderDistance));
    fov = clampf(fov, 50, 110); sensitivity = clampf(sensitivity, 0.1f, 4); brightness = clampf(brightness, 0, 1);
    masterVolume = clampf(masterVolume, 0, 1); musicVolume = clampf(musicVolume, 0, 1); sfxVolume = clampf(sfxVolume, 0, 1);
    guiScale = std::max(0, std::min(4, guiScale));
}

void Settings::save(const std::string& path) const {
    FILE* f = fopen(path.c_str(), "w");
    if (!f) return;
    fprintf(f, "renderDistance=%d\nfov=%.1f\nsensitivity=%.3f\nbrightness=%.3f\nmasterVolume=%.3f\nmusicVolume=%.3f\nsfxVolume=%.3f\n",
            renderDistance, fov, sensitivity, brightness, masterVolume, musicVolume, sfxVolume);
    fprintf(f, "viewBobbing=%d\nfullscreen=%d\nvsync=%d\nguiScale=%d\n", viewBobbing ? 1 : 0, fullscreen ? 1 : 0, vsync ? 1 : 0, guiScale);
    fclose(f);
}
