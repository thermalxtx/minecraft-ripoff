// Game: owns every system and runs the fixed-tick loop.
#pragma once
#include <memory>
#include <string>
#include "../render/renderer.h"
#include "../ui/ui.h"
#include "../world/world.h"
#include "jobs.h"

class Game {
public:
    bool init(int argc, char** argv);
    void run();
    void shutdown();
private:
    Renderer renderer;
    UI ui;
    JobSystem jobs;
    std::unique_ptr<World> world;
    Camera cam;
    bool running = true;
    bool showDebug = true;
    double fpsTimer = 0; int frames = 0; int fps = 0;
    void frame(float dt);
    void render(float alpha);
};
