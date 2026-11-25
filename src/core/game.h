// Game: owns every system, runs the fixed 20-tick loop and the screen state machine.
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "../render/renderer.h"
#include "../render/particles.h"
#include "../ui/ui.h"
#include "../world/world.h"
#include "../entity/entity.h"
#include "../game/player.h"
#include "../io/save.h"
#include "settings.h"
#include "jobs.h"

enum class Screen {
    TITLE, WORLDS, CREATE, CONFIRM_DELETE, LOADING, PLAYING, PAUSED, OPTIONS,
    INVENTORY, CRAFTING, FURNACE, CHEST, DEATH
};

struct SlotRef {
    ItemStack* st;
    float x, y;
    int kind;   // 0 normal, 1 craft result, 2 output only
    int group;  // 0 hotbar, 1 main inventory, 2 container / crafting grid, 3 furnace input, 4 furnace fuel
};

class Game {
public:
    bool init(int argc, char** argv);
    void run();
    void shutdown();

private:
    // systems
    Settings settings;
    Renderer renderer;
    UI ui;
    JobSystem jobs;
    std::unique_ptr<World> world;
    EntityManager entities;
    Player player;
    ParticleSystem particles;
    Camera cam;
    Rng rng{12345};
    bool audioOk = false;
    // paths
    std::string baseDir, savesDir, worldDir;
    WorldInfo info;
    // state
    Screen screen = Screen::TITLE;
    Screen optionsReturn = Screen::TITLE;
    bool running = true;
    bool inWorld = false;
    double tickAccum = 0;
    float alpha = 0;
    int64_t worldTime = 0;   // copy of info.time while playing
    int autosaveTicks = 0;
    bool showDebug = false, hideHud = false;
    double fpsTimer = 0; int frames = 0, fps = 0;
    float frameTime = 0;
    PlayerInput input;
    bool attackQueued = false, useQueued = false, dropQueued = false, dropStackQueued = false;
    int lastSelected = 0;
    float heldNameTimer = 0;
    bool wasInWater = false;
    float hurtFlash = 0;
    int loadingTicks = 0;
    int screenAge = 0;   // frames since the current screen opened
    bool respawnPending = false;
    bool checkBed = false;
    std::string debugCam;   // test hook: "-cam" args
    std::vector<std::string> autoKeys;
    // container UI
    ItemStack craftGrid[9];
    ItemStack craftResult;
    ItemStack cursor;
    ivec3 containerPos;
    std::vector<int> dragSlots;
    bool dragging = false;
    int dragButton = 0;
    // menus
    std::vector<WorldInfo> worldList;
    int selectedWorld = -1;
    std::string newName = "New World", newSeed;
    float titleTime = 0;
    std::string statusMessage;

    // game.cpp
    void frame(float dt);
    void tick();
    void gatherInput();
    void processEvents();
    void applySettings();
    float daylight() const;
    bool isNight() const;
    // world lifecycle
    void createWorld(const std::string& name, const std::string& seedText);
    void loadWorld(const WorldInfo& w);
    void saveGame();
    void quitToTitle();
    void respawn();
    ivec3 findSpawn(uint64_t seed);
    void placePlayerSafely();
    void openScreen(Screen s);
    void closeContainer();
    // game_render.cpp
    void render();
    void renderWorld();
    void renderHand(const FrameEnv& env);
    void renderHUD();
    void renderDebug();
    FrameEnv makeEnv() const;
    void takeScreenshot();
    // game_screens.cpp
    void screenTitle();
    void screenWorlds();
    void screenCreate();
    void screenConfirmDelete();
    void screenLoading();
    void screenPause();
    void screenOptions();
    void screenDeath();
    void drawMenuBackground();
    // game_inventory.cpp
    void screenContainer();
    void buildSlots(std::vector<SlotRef>& slots, float ox, float oy);
    void slotClick(std::vector<SlotRef>& slots, int idx, int button, bool shift);
    void shiftMove(std::vector<SlotRef>& slots, int idx);
    void updateCraftResult();
    bool moveInto(ItemStack& st, std::vector<SlotRef>& slots, int group);
};
