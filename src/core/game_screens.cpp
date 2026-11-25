// Menu screens: title, world list, create/delete, loading, pause, options, death.
#include "game.h"
#include "platform.h"
#include "../render/textures.h"
#include "../audio/audio.h"
#include <cstdio>
#include <ctime>

static bool clickSound(bool b) { if (b) audio::play2D(SFX_CLICK, 0.6f); return b; }

void Game::drawMenuBackground() {
    // dark tiled dirt, like a classic options background
    float t = 32;
    for (float y = 0; y < ui.H; y += t)
        for (float x = 0; x < ui.W; x += t) ui.image(x, y, t, t, TX_DIRT, rgba(0.25f, 0.25f, 0.25f));
}

void Game::screenTitle() {
    drawMenuBackground();
    float W = ui.W, H = ui.H;
    // logo built from tiny grass/stone blocks
    const char* logo = "BLOCKCRAFT";
    float size = std::min(5.0f, std::floor(W / 75));
    float lw = ui.textWidth(logo, size);
    float lx = W / 2 - lw / 2, ly = H * 0.18f;
    ui.text(lx + size, ly + size, logo, 0xFF101010, false, size);
    ui.text(lx, ly, logo, 0xFF7FC48A, false, size);
    ui.text(lx, ly, logo, 0x60FFFFFF, false, size);
    float pulse = 1.0f + std::sin(titleTime * 6) * 0.06f;
    ui.textCentered(W / 2 + lw * 0.32f, ly + 8 * size + 4, "Survive the night!", COL_YELLOW, true, pulse);
    float bw = 200, bh = 20, bx = W / 2 - bw / 2, by = H * 0.48f;
    if (clickSound(ui.button(bx, by, bw, bh, "Singleplayer"))) { worldList = listWorlds(savesDir); selectedWorld = worldList.empty() ? -1 : 0; screen = Screen::WORLDS; }
    if (clickSound(ui.button(bx, by + 24, bw, bh, "Options"))) { optionsReturn = Screen::TITLE; screen = Screen::OPTIONS; }
    if (clickSound(ui.button(bx, by + 48, bw, bh, "Quit Game"))) running = false;
    ui.text(2, H - 10, "Blockcraft 1.0", COL_GRAY);
    std::string c = "Original game - all art and sound made in code";
    ui.text(W - ui.textWidth(c) - 2, H - 10, c, COL_GRAY);
    if (plat::keyPressed(KEY_ESCAPE)) running = false;
}

void Game::screenWorlds() {
    drawMenuBackground();
    float W = ui.W, H = ui.H;
    ui.textCentered(W / 2, 12, "Select World");
    float lw = std::min(300.0f, W - 20), lx = W / 2 - lw / 2, ly = 28, rowH = 32;
    float listH = H - ly - 64;
    ui.rect(lx - 2, ly - 2, lw + 4, listH + 4, 0x80000000);
    int maxRows = (int)(listH / rowH);
    static int scroll = 0;
    float sc = plat::scrollDelta();
    if (sc != 0) scroll = std::max(0, std::min((int)worldList.size() - maxRows, scroll - (int)sc));
    if (scroll < 0) scroll = 0;
    if (worldList.empty()) ui.textCentered(W / 2, ly + 20, "No worlds yet - create one!", COL_GRAY);
    for (int i = scroll; i < (int)worldList.size() && i - scroll < maxRows; i++) {
        float y = ly + (i - scroll) * rowH;
        bool hov = ui.hovered(lx, y, lw, rowH - 2);
        if (i == selectedWorld) ui.frame(lx, y, lw, rowH - 2, 0xFFFFFFFF);
        else if (hov) ui.rect(lx, y, lw, rowH - 2, 0x30FFFFFF);
        const WorldInfo& wi = worldList[i];
        ui.text(lx + 4, y + 3, wi.name);
        char b[128];
        char date[32] = "";
        time_t lp = (time_t)wi.lastPlayed;
        if (lp > 0) strftime(date, sizeof(date), "%Y-%m-%d %H:%M", localtime(&lp));
        snprintf(b, sizeof(b), "%s (%s)", wi.folder.c_str(), date);
        ui.text(lx + 4, y + 12, b, COL_GRAY);
        snprintf(b, sizeof(b), "Seed %llu  -  Day %lld", (unsigned long long)wi.seed, (long long)(wi.time / 24000 + 1));
        ui.text(lx + 4, y + 21, b, COL_GRAY);
        if (hov && ui.clicked) {
            static double lastClick = 0; static int lastIdx = -1;
            double now = plat::timeSeconds();
            if (lastIdx == i && now - lastClick < 0.4) { loadWorld(wi); return; }
            lastClick = now; lastIdx = i; selectedWorld = i;
        }
    }
    float bw = 150, bx1 = W / 2 - 154, bx2 = W / 2 + 4, by = H - 52;
    bool sel = selectedWorld >= 0 && selectedWorld < (int)worldList.size();
    if (clickSound(ui.button(bx1, by, bw, 20, "Play Selected World", sel))) { loadWorld(worldList[selectedWorld]); return; }
    if (clickSound(ui.button(bx2, by, bw, 20, "Create New World"))) { newName = "New World"; newSeed.clear(); ui.focusedField = 1; screen = Screen::CREATE; }
    if (clickSound(ui.button(bx1, by + 24, bw, 20, "Delete", sel))) screen = Screen::CONFIRM_DELETE;
    if (clickSound(ui.button(bx2, by + 24, bw, 20, "Cancel")) || plat::keyPressed(KEY_ESCAPE)) screen = Screen::TITLE;
}

void Game::screenCreate() {
    drawMenuBackground();
    float W = ui.W, H = ui.H;
    ui.textCentered(W / 2, 20, "Create New World");
    float fw = 200, fx = W / 2 - fw / 2;
    ui.text(fx, H * 0.25f - 12, "World Name", COL_GRAY);
    ui.textField(1, fx, H * 0.25f, fw, 20, newName, 32);
    ui.text(fx, H * 0.25f + 34, "Seed for the world generator", COL_GRAY);
    ui.textField(2, fx, H * 0.25f + 46, fw, 20, newSeed, 32);
    ui.text(fx, H * 0.25f + 70, "Leave blank for a random seed", COL_DARK);
    if (plat::keyPressed(KEY_TAB)) ui.focusedField = ui.focusedField == 1 ? 2 : 1;
    if (clickSound(ui.button(W / 2 - 154, H - 40, 150, 20, "Create New World")) || plat::keyPressed(KEY_ENTER)) {
        ui.focusedField = -1;
        createWorld(newName, newSeed);
        return;
    }
    if (clickSound(ui.button(W / 2 + 4, H - 40, 150, 20, "Cancel")) || plat::keyPressed(KEY_ESCAPE)) { ui.focusedField = -1; screen = Screen::WORLDS; }
}

void Game::screenConfirmDelete() {
    drawMenuBackground();
    float W = ui.W, H = ui.H;
    if (selectedWorld < 0 || selectedWorld >= (int)worldList.size()) { screen = Screen::WORLDS; return; }
    ui.textCentered(W / 2, H * 0.3f, "Are you sure you want to delete this world?");
    ui.textCentered(W / 2, H * 0.3f + 14, "'" + worldList[selectedWorld].name + "' will be lost forever! (A long time!)", COL_GRAY);
    if (clickSound(ui.button(W / 2 - 154, H * 0.55f, 150, 20, "Delete"))) {
        plat::removeDirRecursive(savesDir + worldList[selectedWorld].folder);
        worldList = listWorlds(savesDir);
        selectedWorld = worldList.empty() ? -1 : 0;
        screen = Screen::WORLDS;
    }
    if (clickSound(ui.button(W / 2 + 4, H * 0.55f, 150, 20, "Cancel")) || plat::keyPressed(KEY_ESCAPE)) screen = Screen::WORLDS;
}

void Game::screenLoading() {
    drawMenuBackground();
    float W = ui.W, H = ui.H;
    ui.textCentered(W / 2, H / 2 - 20, loadingTicks < 2 ? "Loading world" : "Building terrain");
    int px = ifloor(player.pos.x) >> 4, pz = ifloor(player.pos.z) >> 4;
    int total = 0, ready = 0;
    for (int dx = -2; dx <= 2; dx++) for (int dz = -2; dz <= 2; dz++) { total++; if (world->isChunkReady(px + dx, pz + dz) || (world->getChunk(px + dx, pz + dz) && world->getChunk(px + dx, pz + dz)->state == CS_STITCHED)) ready++; }
    float f = (float)ready / total;
    ui.rect(W / 2 - 100, H / 2, 200, 4, 0xFF404040);
    ui.rect(W / 2 - 100, H / 2, 200 * f, 4, 0xFF60D060);
    char b[64]; snprintf(b, sizeof(b), "%d%%", (int)(f * 100));
    ui.textCentered(W / 2, H / 2 + 10, b, COL_GRAY);
}

void Game::screenPause() {
    ui.rect(0, 0, ui.W, ui.H, 0x90101010);
    float W = ui.W, H = ui.H;
    ui.textCentered(W / 2, H * 0.22f, "Game Menu");
    float bw = 200, bx = W / 2 - bw / 2, by = H * 0.35f;
    if (clickSound(ui.button(bx, by, bw, 20, "Back to Game")) || (screenAge > 1 && plat::keyPressed(KEY_ESCAPE))) { openScreen(Screen::PLAYING); return; }
    if (clickSound(ui.button(bx, by + 24, bw, 20, "Options"))) { optionsReturn = Screen::PAUSED; screen = Screen::OPTIONS; return; }
    if (clickSound(ui.button(bx, by + 48, bw, 20, "Save and Quit to Title"))) { quitToTitle(); return; }
    ui.textCentered(W / 2, by + 80, "Controls: WASD move, Space jump, Shift sneak, Ctrl sprint", COL_GRAY);
    ui.textCentered(W / 2, by + 90, "E inventory, Q drop, 1-9 / wheel hotbar, F3 debug, F2 screenshot", COL_GRAY);
}

void Game::screenOptions() {
    if (inWorld) ui.rect(0, 0, ui.W, ui.H, 0xB0101010); else drawMenuBackground();
    float W = ui.W, H = ui.H;
    ui.textCentered(W / 2, 14, "Options");
    float cw = 150, x1 = W / 2 - cw - 4, x2 = W / 2 + 4, y = 34, step = 24;
    char b[64];
    float rd = (float)settings.renderDistance;
    snprintf(b, sizeof(b), "Render Distance: %d chunks", settings.renderDistance);
    if (ui.slider(x1, y, cw, 20, b, rd, 4, 16)) settings.renderDistance = (int)(rd + 0.5f);
    snprintf(b, sizeof(b), "FOV: %d", (int)settings.fov);
    ui.slider(x2, y, cw, 20, b, settings.fov, 50, 110);
    y += step;
    snprintf(b, sizeof(b), "Sensitivity: %d%%", (int)(settings.sensitivity * 100));
    ui.slider(x1, y, cw, 20, b, settings.sensitivity, 0.2f, 3.0f);
    snprintf(b, sizeof(b), "Master Volume: %d%%", (int)(settings.masterVolume * 100));
    if (ui.slider(x2, y, cw, 20, b, settings.masterVolume, 0, 1)) audio::setVolumes(settings.masterVolume, settings.sfxVolume, settings.musicVolume);
    y += step;
    snprintf(b, sizeof(b), "Music: %d%%", (int)(settings.musicVolume * 100));
    if (ui.slider(x1, y, cw, 20, b, settings.musicVolume, 0, 1)) audio::setVolumes(settings.masterVolume, settings.sfxVolume, settings.musicVolume);
    snprintf(b, sizeof(b), "Sounds: %d%%", (int)(settings.sfxVolume * 100));
    if (ui.slider(x2, y, cw, 20, b, settings.sfxVolume, 0, 1)) audio::setVolumes(settings.masterVolume, settings.sfxVolume, settings.musicVolume);
    y += step;
    if (clickSound(ui.button(x1, y, cw, 20, std::string("View Bobbing: ") + (settings.viewBobbing ? "ON" : "OFF")))) settings.viewBobbing = !settings.viewBobbing;
    static const char* gs[5] = {"Auto", "1", "2", "3", "4"};
    if (clickSound(ui.button(x2, y, cw, 20, std::string("GUI Scale: ") + gs[settings.guiScale]))) settings.guiScale = (settings.guiScale + 1) % 5;
    y += step;
    if (clickSound(ui.button(x1, y, cw, 20, std::string("Fullscreen: ") + (settings.fullscreen ? "ON" : "OFF")))) { settings.fullscreen = !settings.fullscreen; plat::setFullscreen(settings.fullscreen); }
    if (clickSound(ui.button(x2, y, cw, 20, std::string("VSync: ") + (settings.vsync ? "ON" : "OFF")))) { settings.vsync = !settings.vsync; plat::setVsync(settings.vsync); }
    y += step;
    if (inWorld) {
        if (clickSound(ui.button(x1, y, cw, 20, std::string("Daylight Cycle: ") + (info.doDaylightCycle ? "ON" : "OFF")))) info.doDaylightCycle = !info.doDaylightCycle;
        if (clickSound(ui.button(x2, y, cw, 20, std::string("Keep Inventory: ") + (info.keepInventory ? "ON" : "OFF")))) info.keepInventory = !info.keepInventory;
        y += step;
    }
    if (clickSound(ui.button(W / 2 - 100, std::max(y + 10, H - 30), 200, 20, "Done")) || plat::keyPressed(KEY_ESCAPE)) {
        settings.save(baseDir + "options.txt");
        screen = optionsReturn;
    }
}

void Game::screenDeath() {
    ui.rect(0, 0, ui.W, ui.H, rgba(0.5f, 0.0f, 0.0f, 0.45f));
    float W = ui.W, H = ui.H;
    ui.textCentered(W / 2, H * 0.25f, "You died!", COL_WHITE, true, 2);
    ui.textCentered(W / 2, H * 0.25f + 24, "Player " + player.deathMessage, COL_WHITE);
    if (player.deathTicks < 20) return;
    if (clickSound(ui.button(W / 2 - 100, H * 0.45f, 200, 20, "Respawn"))) { respawn(); return; }
    if (clickSound(ui.button(W / 2 - 100, H * 0.45f + 24, 200, 20, "Title Screen"))) { player.reset(); player.dead = false; respawn(); quitToTitle(); return; }
}
