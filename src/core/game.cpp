// Game loop, fixed ticks, event dispatch and world lifecycle.
#include "game.h"
#include "platform.h"
#include "events.h"
#include "../world/worldgen.h"
#include "../game/crafting.h"
#include "../game/physics.h"
#include "../audio/audio.h"
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <thread>
#include <algorithm>

EventQueue g_events;
static Game* g_game = nullptr;

bool Game::init(int argc, char** argv) {
    g_game = this;
    baseDir = plat::exeDir();
    savesDir = baseDir + "saves\\";
    plat::makeDir(savesDir);
    settings.load(baseDir + "options.txt");
    if (!plat::init("Blockcraft", 1280, 720)) return false;
    initBlocks();
    initItems();
    initRecipes();
    if (!renderer.init()) { plat::showError("Failed to initialise the renderer (shader compile error)."); return false; }
    ui.init(&renderer);
    int hw = (int)std::thread::hardware_concurrency();
    jobs.start(std::max(2, std::min(6, hw - 1)));
    audioOk = audio::init();
    applySettings();
    rng = Rng((uint64_t)time(nullptr));
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "-cam" && i + 5 < argc) { for (int k = 1; k <= 5; k++) debugCam += std::string(argv[i + k]) + " "; i += 5; }
        else if (a == "-auto" && i + 1 < argc) autoKeys.push_back(argv[++i]);
    }
    // test hook: "-auto newworld" creates and enters a world without clicking
    for (auto& k : autoKeys) if (k == "newworld") createWorld("Test World", "12345");
    return true;
}

void Game::applySettings() {
    plat::setFullscreen(settings.fullscreen);
    plat::setVsync(settings.vsync);
    audio::setVolumes(settings.masterVolume, settings.sfxVolume, settings.musicVolume);
    if (world) world->renderDistance = settings.renderDistance;
}

void Game::run() {
    double last = plat::timeSeconds();
    while (running) {
        if (!plat::pollEvents()) break;
        double now = plat::timeSeconds();
        float dt = (float)std::min(0.25, now - last);
        last = now;
        frameTime = dt;
        frame(dt);
        render();
        plat::swapBuffers();
        frames++;
        if (now - fpsTimer >= 1.0) { fps = frames; frames = 0; fpsTimer = now; }
    }
    if (inWorld) { saveGame(); world->shutdown(); }
}

void Game::shutdown() {
    settings.save(baseDir + "options.txt");
    jobs.stop();
    audio::shutdown();
    renderer.shutdown();
    plat::shutdown();
}

float Game::daylight() const {
    float t = (float)(worldTime % 24000) / 24000.0f;
    float s = std::sin(t * 2 * PI);
    return 0.2f + 0.8f * smoothstepf(-0.18f, 0.22f, s);
}
bool Game::isNight() const {
    int64_t t = worldTime % 24000;
    return t > 12500 && t < 23500;
}

// ---------------- per-frame ----------------
void Game::frame(float dt) {
    titleTime += dt;
    screenAge++;
    if (plat::keyPressed(KEY_F11)) { settings.fullscreen = !settings.fullscreen; plat::setFullscreen(settings.fullscreen); }
    if (plat::keyPressed(KEY_F2)) takeScreenshot();
    if (plat::keyPressed(KEY_F9)) { renderer.exportTexturesPNG(baseDir + "textures.png"); statusMessage = "Saved textures.png"; }
    bool menuMusic = screen == Screen::TITLE || screen == Screen::WORLDS || screen == Screen::CREATE || screen == Screen::CONFIRM_DELETE ||
                     (screen == Screen::OPTIONS && !inWorld);
    if (inWorld) {
        bool underground = player.pos.y < 50 && world->getSkyLight(ifloor(player.pos.x), ifloor(player.pos.y + 1), ifloor(player.pos.z)) < 4;
        audio::updateMusic(dt, true, underground, isNight());
    } else if (menuMusic) audio::updateMusic(dt, false, false, false);

    if (!inWorld) return;
    world->renderDistance = settings.renderDistance;
    // stream the world around the player
    world->updateStreaming(player.pos);
    world->processJobResults(screen == Screen::LOADING ? 24 : 8, 32);
    for (auto& kc : world->newlyReadyChunks) entities.spawnFromChunk(kc.second);
    world->newlyReadyChunks.clear();
    world->scheduleMeshes(player.pos + vec3(0, 1.6f, 0));

    if (screen == Screen::LOADING) {
        loadingTicks++;
        int px = ifloor(player.pos.x), pz = ifloor(player.pos.z);
        if (world->spawnAreaReady(px, pz, 2) && world->isChunkReady(px >> 4, pz >> 4)) {
            placePlayerSafely();
            screen = Screen::PLAYING;
            plat::setMouseCaptured(true);
            tickAccum = 0;
        }
        return;
    }
    bool paused = screen == Screen::PAUSED || (screen == Screen::OPTIONS && inWorld);
    if (screen == Screen::PLAYING) {
        if (plat::keyPressed(KEY_ESCAPE)) { openScreen(Screen::PAUSED); return; }
        if (plat::keyPressed(KEY_E) && !player.dead) { openScreen(Screen::INVENTORY); }
        if (plat::keyPressed(KEY_F3)) showDebug = !showDebug;
        if (plat::keyPressed(KEY_F1)) hideHud = !hideHud;
        if (plat::keyPressed(KEY_F6)) { player.flying = !player.flying; player.say(player.flying ? "Debug fly ON" : "Debug fly OFF"); }
        if (plat::keyPressed(KEY_F7) && player.flying) {  // debug kit (only while debug-flying)
            const ItemStack kit[] = {{toolItem(MAT_DIAMOND, KIND_PICKAXE), 1}, {toolItem(MAT_IRON, KIND_SWORD), 1}, {BOW, 1}, {ARROW, 64},
                                     {TORCH, 64}, {OAK_PLANKS, 64}, {CRAFTING_TABLE, 1}, {FURNACE, 1}, {CHEST, 1}, {COBBLESTONE, 64},
                                     {COAL, 32}, {IRON_ORE, 16}, {STEAK, 16}, {BED_ITEM, 1}, {DOOR_ITEM, 4}, {LADDER, 16}, {SAND, 32}, {SEEDS, 16},
                                     {toolItem(MAT_STONE, KIND_HOE), 1}, {WOOL_WHITE, 8}, {OAK_LOG, 16}, {STICK, 16}, {GLASS, 16}};
            for (auto& k : kit) player.inv.add(k);
            player.say("Debug kit added");
        }
        // mouse look
        float dx, dy; plat::mouseDelta(dx, dy);
        if (!player.sleeping) {
            float s = 0.0022f * settings.sensitivity;
            player.yaw -= dx * s;
            player.pitch = clampf(player.pitch - dy * s, -1.5533f, 1.5533f);
        }
        // hotbar
        for (int i = 0; i < 9; i++) if (plat::keyPressed(KEY_1 + i)) player.inv.selected = i;
        float sc = plat::scrollDelta();
        if (sc != 0) player.inv.selected = floorMod(player.inv.selected - (sc > 0 ? 1 : -1) * std::max(1, (int)std::fabs(sc)), 9);
        if (player.inv.selected != lastSelected) { lastSelected = player.inv.selected; heldNameTimer = 2.0f; player.usingItem = false; player.mining = false; }
        if (plat::mousePressed(MOUSE_LEFT)) attackQueued = true;
        if (plat::mousePressed(MOUSE_RIGHT)) useQueued = true;
        if (plat::keyPressed(KEY_Q)) { dropQueued = true; dropStackQueued = plat::keyDown(KEY_CTRL); }
        player.updateTarget(*world, entities, alpha);
    }
    if (heldNameTimer > 0) heldNameTimer -= dt;
    if (hurtFlash > 0) hurtFlash -= dt;
    if (!paused) {
        tickAccum += dt;
        int n = 0;
        while (tickAccum >= 0.05 && n < 5) { tick(); tickAccum -= 0.05; n++; }
        if (n == 5) tickAccum = 0;
    }
    alpha = (float)(tickAccum / 0.05);
    if (alpha > 1) alpha = 1;
    // listener for 3D audio
    vec3 eye = player.eyePos(alpha);
    vec3 fwd = player.lookDir();
    audio::setListener(eye, fwd, normalize(cross(fwd, vec3(0, 1, 0))));
}

void Game::gatherInput() {
    PlayerInput in;
    bool active = screen == Screen::PLAYING && plat::focused();
    if (active) {
        if (plat::keyDown(KEY_W)) in.forward += 1;
        if (plat::keyDown(KEY_S)) in.forward -= 1;
        if (plat::keyDown(KEY_D)) in.strafe += 1;
        if (plat::keyDown(KEY_A)) in.strafe -= 1;
        in.jump = plat::keyDown(KEY_SPACE);
        in.sneak = plat::keyDown(KEY_SHIFT);
        in.sprint = plat::keyDown(KEY_CTRL);
        in.attackHeld = plat::mouseDown(MOUSE_LEFT);
        in.useHeld = plat::mouseDown(MOUSE_RIGHT);
    }
    in.attackPressed = attackQueued && active;
    in.usePressed = useQueued && active;
    in.dropPressed = dropQueued && active;
    in.dropStack = dropStackQueued;
    attackQueued = useQueued = dropQueued = false;
    input = in;
}

// ---------------- 20 Hz tick ----------------
void Game::tick() {
    gatherInput();
    float day = daylight();
    bool night = isNight();
    player.updateTarget(*world, entities, 1.0f);
    float healthBefore = player.health;
    player.tick(*world, entities, input, night);
    if (player.health < healthBefore) hurtFlash = 0.4f;
    // interactions that open screens
    if (player.pendingInteraction != INTERACT_NONE) {
        containerPos = player.interactPos;
        switch (player.pendingInteraction) {
            case INTERACT_CRAFTING: openScreen(Screen::CRAFTING); break;
            case INTERACT_FURNACE: openScreen(Screen::FURNACE); break;
            case INTERACT_CHEST: openScreen(Screen::CHEST); break;
            default: break;
        }
        player.pendingInteraction = INTERACT_NONE;
    }
    // splash when entering water
    if (player.inWater && !wasInWater && player.prevPos.y - player.pos.y > 0.15f) {
        g_events.sound(player.pos, SFX_SPLASH, 0.6f);
        g_events.particles(player.pos + vec3(0, 0.2f, 0), PK_SPLASH, 16);
    }
    wasInWater = player.inWater;
    if (player.headInWater && rng.chance(0.08f)) particles.spawn(PK_BUBBLE, player.eyePos(1) + player.lookDir() * 0.6f, 1, 0, rng);

    world->tick(player.pos);
    entities.dayFactor = day;
    entities.isDay = !night;
    entities.mobSpawning = info.doMobSpawning;
    entities.tick();
    particles.tick(*world);
    // ambient torch flames / smoke near the player
    for (int i = 0; i < 40; i++) {
        int x = ifloor(player.pos.x) + rng.range(-14, 14), y = ifloor(player.pos.y) + rng.range(-8, 8), z = ifloor(player.pos.z) + rng.range(-14, 14);
        uint16_t b = world->getBlock(x, y, z);
        if (b == TORCH) {
            uint8_t m = world->getMeta(x, y, z);
            vec3 p(x + 0.5f, y + 0.7f, z + 0.5f);
            if (m > 0) { int f = m - 1; p = vec3(x + 0.5f - facingDX(f) * 0.27f, y + 0.92f, z + 0.5f - facingDZ(f) * 0.27f); }
            particles.spawn(PK_FLAME, p, 1, 0, rng);
            particles.spawn(PK_SMOKE, p + vec3(0, 0.1f, 0), 1, 0, rng);
        } else if (b == FURNACE_LIT && rng.chance(0.5f)) {
            particles.spawn(PK_FLAME, vec3(x + 0.5f, y + 0.3f, z + 0.5f) + vec3(facingDX(world->getMeta(x, y, z)) * 0.52f, 0, facingDZ(world->getMeta(x, y, z)) * 0.52f), 1, 0, rng);
        }
    }
    // time of day
    if (info.doDaylightCycle) worldTime++;
    if (player.sleeping && player.sleepTicks >= 100) {
        worldTime = (worldTime / 24000 + 1) * 24000;
        player.sleeping = false;
        player.say("Good morning!");
    }
    // death
    if (player.dead && screen != Screen::DEATH) {
        if (screen != Screen::PLAYING) closeContainer();
        if (!info.keepInventory) player.dropAll(entities);
        screen = Screen::DEATH;
        plat::setMouseCaptured(false);
    }
    processEvents();
    if (++autosaveTicks >= 6000) { autosaveTicks = 0; saveGame(); player.say("World saved"); }
}

void Game::processEvents() {
    // events may post more events (e.g. explosions break blocks), so loop over a snapshot repeatedly
    for (int round = 0; round < 4 && !g_events.events.empty(); round++) {
        std::vector<GameEvent> evs;
        evs.swap(g_events.events);
        for (auto& e : evs) {
            switch (e.type) {
            case EV_BLOCK_BROKEN: {
                const BlockDef& d = blockDef((uint16_t)e.a);
                if (e.a == GLASS || e.a == ICE) audio::play3D(SFX_BREAK_GLASS, e.pos, 0.9f, rng.range(0.9f, 1.1f));
                else audio::play3D(SFX_DIG_STONE + d.sound, e.pos, 1.0f, rng.range(0.75f, 0.9f));
                particles.spawn(PK_BLOCK, e.pos, 20, e.a, rng);
                if (e.c == 0) {  // broken by the world (support lost, water)
                    std::vector<ItemStack> drops;
                    blockDrops((uint16_t)e.a, (uint8_t)e.b, ItemStack(), rng, drops);
                    for (auto& s : drops) entities.spawnItem(e.pos, s);
                }
                break;
            }
            case EV_BLOCK_PLACED:
                audio::play3D(SFX_DIG_STONE + blockDef((uint16_t)e.a).sound, e.pos, 0.9f, 0.8f);
                break;
            case EV_SOUND: audio::play3D(e.a, e.pos, e.f, e.g); break;
            case EV_SOUND_UI: audio::play2D(e.a, e.f, e.g); break;
            case EV_DROP_ITEM: entities.spawnItem(e.pos, ItemStack((uint16_t)e.a, e.b, e.c)); break;
            case EV_FALLING_BLOCK: {
                Entity* fb = entities.spawn(ET_FALLING_BLOCK, e.pos + vec3(0.5f, 0, 0.5f));
                fb->blockId = (uint16_t)e.a;
                break;
            }
            case EV_EXPLOSION: entities.explode(e.pos, e.f); break;
            case EV_PARTICLES: particles.spawn(e.a, e.pos, e.b, e.c, rng); break;
            default: break;
            }
        }
    }
    g_events.events.clear();
}

// ---------------- world lifecycle ----------------
ivec3 Game::findSpawn(uint64_t seed) {
    TerrainGen g(seed);
    for (int r = 0; r < 64; r++)
        for (int k = 0; k < std::max(1, r * 8); k++) {
            float a = k / (float)std::max(1, r * 8) * 2 * PI;
            int x = (int)(std::cos(a) * r * 16), z = (int)(std::sin(a) * r * 16);
            ColumnInfo ci = g.column(x, z);
            if (ci.biome != BIOME_OCEAN && ci.biome != BIOME_MOUNTAINS && ci.height >= SEA_LEVEL + 1 && ci.height < 110)
                return ivec3(x, ci.height + 1, z);
        }
    return ivec3(0, 90, 0);
}

static void cbUnload(void* u, Chunk* c) { ((EntityManager*)u)->collectForChunk(c, true); }
static void cbSave(void* u, Chunk* c) { ((EntityManager*)u)->collectForChunk(c, false); }

void Game::createWorld(const std::string& name, const std::string& seedText) {
    WorldInfo w;
    w.name = name.empty() ? "New World" : name;
    w.folder = makeFolderName(savesDir, w.name);
    if (seedText.empty()) w.seed = rng.next() & 0xFFFFFFFFFFFFull;
    else {
        bool numeric = !seedText.empty() && seedText.find_first_not_of("-0123456789") == std::string::npos && seedText.size() < 18;
        if (numeric) w.seed = (uint64_t)std::strtoll(seedText.c_str(), nullptr, 10);
        else { uint64_t h = 1469598103934665603ull; for (char c : seedText) { h ^= (uint8_t)c; h *= 1099511628211ull; } w.seed = h; }
    }
    w.spawn = findSpawn(w.seed);
    w.time = 1000;
    std::string dir = savesDir + w.folder + "\\";
    plat::makeDir(dir);
    saveWorldInfo(dir, w);
    loadWorld(w);
}

void Game::loadWorld(const WorldInfo& w) {
    info = w;
    worldDir = savesDir + w.folder + "\\";
    world.reset(new World());
    world->init(w.seed, worldDir, &jobs);
    world->renderDistance = settings.renderDistance;
    world->freeChunkGPU = &Renderer::freeChunkGPU;
    world->uploadMesh = &Renderer::uploadMesh;
    world->onChunkUnload = &cbUnload;
    world->onChunkSave = &cbSave;
    world->callbackUser = &entities;
    entities.clear();
    entities.world = world.get();
    entities.player = &player;
    particles.clear();
    player = Player();
    player.inv.clear();
    if (!loadPlayer(worldDir, player)) {
        player.pos = player.prevPos = vec3(w.spawn.x + 0.5f, (float)w.spawn.y, w.spawn.z + 0.5f);
        player.spawnPoint = w.spawn;
        player.yaw = 0.8f;
    }
    if (!debugCam.empty()) {
        float x, y, z, yw, pt;
        if (sscanf(debugCam.c_str(), "%f %f %f %f %f", &x, &y, &z, &yw, &pt) == 5) { player.pos = player.prevPos = vec3(x, y, z); player.yaw = yw; player.pitch = pt; player.flying = true; }
    }
    worldTime = w.time;
    for (auto& k : autoKeys) if (k == "night") worldTime = (worldTime / 24000) * 24000 + 14000;
    lastSelected = player.inv.selected;
    inWorld = true;
    autosaveTicks = 0;
    loadingTicks = 0;
    cursor.clear();
    screen = Screen::LOADING;
    respawnPending = player.dead;
    audio::stopMusic();
}

void Game::placePlayerSafely() {
    if (respawnPending) {  // the save was made while dead
        respawnPending = false;
        if (player.dead) { screen = Screen::DEATH; plat::setMouseCaptured(false); return; }
    }
    if (checkBed) {
        checkBed = false;
        ivec3 b = player.spawnPoint;
        if (player.bedSpawn && world->getBlock(b.x, b.y, b.z) != BED) {
            player.bedSpawn = false;
            player.spawnPoint = info.spawn;
            player.pos = player.prevPos = vec3(info.spawn.x + 0.5f, (float)info.spawn.y, info.spawn.z + 0.5f);
            player.say("Your bed was missing, so you woke up at the world spawn");
            screen = Screen::LOADING;
            return;
        }
    }
    if (player.flying) return;
    // push the player up out of any blocks
    for (int k = 0; k < 256 && boxCollides(*world, player.box()); k++) player.pos.y = std::floor(player.pos.y) + 1;
    player.prevPos = player.pos;
}

void Game::saveGame() {
    if (!inWorld) return;
    info.time = worldTime;
    world->saveAll();
    saveWorldInfo(worldDir, info);
    savePlayer(worldDir, player);
}

void Game::quitToTitle() {
    closeContainer();
    saveGame();
    world->shutdown();
    entities.clear();
    particles.clear();
    world.reset();
    inWorld = false;
    screen = Screen::TITLE;
    plat::setMouseCaptured(false);
    audio::stopMusic();
}

void Game::respawn() {
    bool bedOk = false;
    ivec3 sp = info.spawn;
    if (player.bedSpawn) {
        ivec3 b = player.spawnPoint;
        // the bed may be in an unloaded chunk; trust it and verify once loaded
        sp = b; bedOk = true;
    }
    float h = player.yaw;
    Inventory keep = player.inv;
    ivec3 spawnPt = player.spawnPoint;
    bool bed = player.bedSpawn;
    player = Player();
    if (info.keepInventory) player.inv = keep;
    player.spawnPoint = spawnPt; player.bedSpawn = bed;
    player.yaw = h;
    player.pos = player.prevPos = vec3(sp.x + 0.5f, (float)sp.y + (bedOk ? 0.6f : 0.0f), sp.z + 0.5f);
    screen = Screen::LOADING;
    loadingTicks = 0;
    checkBed = bedOk;
    plat::setMouseCaptured(false);
}

void Game::openScreen(Screen s) {
    screenAge = 0;
    if (s == Screen::PAUSED) { saveGame(); }
    screen = s;
    bool container = s == Screen::INVENTORY || s == Screen::CRAFTING || s == Screen::FURNACE || s == Screen::CHEST;
    plat::setMouseCaptured(s == Screen::PLAYING);
    if (container) { for (auto& c : craftGrid) c.clear(); craftResult.clear(); dragging = false; dragSlots.clear(); }
    player.mining = false;
    player.usingItem = false;
}

void Game::closeContainer() {
    bool container = screen == Screen::INVENTORY || screen == Screen::CRAFTING || screen == Screen::FURNACE || screen == Screen::CHEST;
    if (!container) return;
    for (auto& c : craftGrid) {
        if (c.empty()) continue;
        int left = player.inv.add(c);
        if (left > 0) { ItemStack d = c; d.count = (uint8_t)left; entities.spawnItem(player.eyePos(1), d, true, player.lookDir()); }
        c.clear();
    }
    if (!cursor.empty()) {
        int left = player.inv.add(cursor);
        if (left > 0) { ItemStack d = cursor; d.count = (uint8_t)left; entities.spawnItem(player.eyePos(1), d, true, player.lookDir()); }
        cursor.clear();
    }
    if (screen == Screen::CHEST) audio::play3D(SFX_CHEST_CLOSE, vec3(containerPos.x + 0.5f, containerPos.y + 0.5f, containerPos.z + 0.5f), 0.6f);
    craftResult.clear();
    screen = Screen::PLAYING;
    plat::setMouseCaptured(true);
}
