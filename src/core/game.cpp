#include "game.h"
#include "platform.h"
#include "events.h"
#include "../world/worldgen.h"
#include "../game/items.h"
#include <cstdio>
#include <thread>
#include <cstdlib>

EventQueue g_events;
void tickFurnaceTile(World&, int, int, int, TileEntity&) {}

bool Game::init(int argc, char** argv) {
    if (!plat::init("Blockcraft", 1280, 720)) return false;
    initBlocks();
    initItems();
    if (!renderer.init()) { plat::showError("Failed to initialise the renderer (shaders)."); return false; }
    ui.init(&renderer);
    int hw = (int)std::thread::hardware_concurrency();
    jobs.start(std::max(1, std::min(6, hw - 1)));
    world.reset(new World());
    std::string dir = plat::exeDir() + "saves\\";
    plat::makeDir(dir);
    dir += "debug\\";
    plat::makeDir(dir);
    world->init(12345, dir, &jobs);
    world->freeChunkGPU = &Renderer::freeChunkGPU;
    world->uploadMesh = &Renderer::uploadMesh;
    world->renderDistance = 10;
    cam.pos = vec3(0.5f, 110, 0.5f);
    for (int i = 1; i + 5 < argc + 1; i++) if (std::string(argv[i]) == "-cam" && i + 5 < argc) { cam.pos = vec3(atof(argv[i+1]), atof(argv[i+2]), atof(argv[i+3])); cam.yaw = atof(argv[i+4]); cam.pitch = atof(argv[i+5]); }
    plat::setMouseCaptured(true);
    plat::setVsync(true);
    return true;
}

void Game::run() {
    double last = plat::timeSeconds();
    while (running) {
        if (!plat::pollEvents()) break;
        double now = plat::timeSeconds();
        float dt = (float)std::min(0.1, now - last);
        last = now;
        frame(dt);
        render(0);
        plat::swapBuffers();
        frames++;
        if (now - fpsTimer >= 1.0) { fps = frames; frames = 0; fpsTimer = now; }
    }
}

void Game::frame(float dt) {
    if (plat::keyPressed(KEY_ESCAPE)) plat::setMouseCaptured(!plat::mouseCaptured());
    if (plat::keyPressed(KEY_F3)) showDebug = !showDebug;
    if (plat::keyPressed(KEY_F11)) plat::setFullscreen(!plat::isFullscreen());
    if (plat::keyPressed(KEY_F9)) renderer.exportTexturesPNG(plat::exeDir() + "textures.png");
    if (plat::mouseCaptured()) {
        float dx, dy; plat::mouseDelta(dx, dy);
        cam.yaw -= dx * 0.0025f; cam.pitch -= dy * 0.0025f;
        cam.pitch = clampf(cam.pitch, -1.55f, 1.55f);
        float speed = plat::keyDown(KEY_CTRL) ? 60.f : 15.f;
        vec3 f(-std::sin(cam.yaw), 0, -std::cos(cam.yaw)), r(std::cos(cam.yaw), 0, -std::sin(cam.yaw));
        vec3 mv(0, 0, 0);
        if (plat::keyDown(KEY_W)) mv += f;
        if (plat::keyDown(KEY_S)) mv -= f;
        if (plat::keyDown(KEY_D)) mv += r;
        if (plat::keyDown(KEY_A)) mv -= r;
        if (plat::keyDown(KEY_SPACE)) mv.y += 1;
        if (plat::keyDown(KEY_SHIFT)) mv.y -= 1;
        cam.pos += normalize(mv) * speed * dt;
    }
    world->updateStreaming(cam.pos);
    world->processJobResults(6, 24);
    world->scheduleMeshes(cam.pos);
    g_events.events.clear();
}

void Game::render(float) {
    int w = plat::width(), h = plat::height();
    glViewport(0, 0, w, h);
    glClearColor(0.6f, 0.75f, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    cam.farZ = (world->renderDistance + 2) * 16.f * 1.5f;
    cam.update(w, h);
    FrameEnv env;
    env.time = (float)plat::timeSeconds();
    float rd = world->renderDistance * 16.f;
    env.fogStart = rd * 0.6f; env.fogEnd = rd;
    env.fogColor = env.horizon;
    renderer.drawSky(cam, env);
    renderer.drawWorld(*world, cam, env, LAYER_OPAQUE);
    renderer.drawWorld(*world, cam, env, LAYER_CUTOUT);
    renderer.drawWorld(*world, cam, env, LAYER_WATER);
    ui.begin(w, h, 0);
    ui.rect(ui.W / 2 - 4, ui.H / 2, 9, 1, 0xC0FFFFFF);
    ui.rect(ui.W / 2, ui.H / 2 - 4, 1, 9, 0xC0FFFFFF);
    if (showDebug) {
        char buf[256];
        snprintf(buf, sizeof(buf), "Blockcraft  %d fps  chunks %d  sections %d  jobs %d", fps, world->loadedChunkCount(), renderer.drawnSections, world->pendingJobs());
        ui.text(2, 2, buf);
        snprintf(buf, sizeof(buf), "XYZ %.1f %.1f %.1f", cam.pos.x, cam.pos.y, cam.pos.z);
        ui.text(2, 12, buf);
    }
    ui.flush();
}

void Game::shutdown() {
    world->shutdown();
    jobs.stop();
    renderer.shutdown();
    plat::shutdown();
}
