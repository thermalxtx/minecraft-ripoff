// Frame rendering: sky, world passes, entities, particles, first-person hand, HUD and the F3 overlay.
#include "game.h"
#include "platform.h"
#include "../render/textures.h"
#include "../world/worldgen.h"
#include <cstdio>
#include <ctime>

FrameEnv Game::makeEnv() const {
    FrameEnv env;
    float t = (float)(worldTime % 24000) / 24000.0f;
    float a = t * 2 * PI;
    env.sunDir = normalize(vec3(std::cos(a), std::sin(a), 0.12f));
    float s = env.sunDir.y;
    float day = smoothstepf(-0.18f, 0.22f, s);
    env.daylight = daylight();
    vec3 dayZen(0.30f, 0.52f, 0.95f), dayHor(0.66f, 0.80f, 1.0f), nightZen(0.008f, 0.012f, 0.035f), nightHor(0.035f, 0.045f, 0.09f);
    env.zenith = lerp(nightZen, dayZen, day);
    env.horizon = lerp(nightHor, dayHor, day);
    env.sunsetAmt = std::exp(-(s / 0.22f) * (s / 0.22f)) * 0.85f;
    env.sunset = vec3(1.0f, 0.42f, 0.22f);
    env.stars = 1 - smoothstepf(-0.25f, 0.05f, s);
    env.moonPhase = (float)((worldTime / 24000) % 8) / 8.0f;
    env.skyTint = lerp(vec3(0.55f, 0.65f, 1.0f), vec3(1, 1, 1), day);
    env.skyTint = lerp(env.skyTint, vec3(1.0f, 0.78f, 0.62f), env.sunsetAmt * 0.5f);
    // fog follows the horizon, tinted toward the sunset when looking at it
    vec3 sunFlat = normalize(vec3(env.sunDir.x, 0, env.sunDir.z));
    float look = std::max(0.0f, dot(normalize(vec3(cam.forward.x, 0, cam.forward.z)), sunFlat));
    env.fogColor = lerp(env.horizon, env.sunset, env.sunsetAmt * look * look * 0.6f);
    float rd = settings.renderDistance * 16.0f;
    env.fogStart = rd * 0.55f; env.fogEnd = rd * 0.95f;
    env.time = (float)plat::timeSeconds();
    // caves get a darker fog so the far tunnels fade into black instead of sky colour
    if (inWorld) {
        int sl = world->getSkyLight(ifloor(cam.pos.x), ifloor(cam.pos.y), ifloor(cam.pos.z));
        float cave = 1.0f - sl / 15.0f;
        env.fogColor = lerp(env.fogColor, vec3(0.02f, 0.02f, 0.03f), cave * 0.85f);
    }
    if (player.headInWater) {
        env.fogColor = vec3(0.05f, 0.12f, 0.35f) * (0.3f + 0.7f * env.daylight);
        env.fogStart = 0; env.fogEnd = 22;
    }
    if (player.inLava) { env.fogColor = vec3(0.8f, 0.25f, 0.02f); env.fogStart = 0; env.fogEnd = 2.5f; }
    return env;
}

void Game::render() {
    int w = plat::width(), h = plat::height();
    if (w <= 0 || h <= 0) return;
    glViewport(0, 0, w, h);
    bool worldVisible = inWorld && screen != Screen::LOADING;
    if (worldVisible) renderWorld();
    else { glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); }
    ui.begin(w, h, settings.guiScale);
    if (worldVisible && !hideHud) renderHUD();
    switch (screen) {
        case Screen::TITLE: screenTitle(); break;
        case Screen::WORLDS: screenWorlds(); break;
        case Screen::CREATE: screenCreate(); break;
        case Screen::CONFIRM_DELETE: screenConfirmDelete(); break;
        case Screen::LOADING: screenLoading(); break;
        case Screen::PAUSED: screenPause(); break;
        case Screen::OPTIONS: screenOptions(); break;
        case Screen::DEATH: screenDeath(); break;
        case Screen::INVENTORY: case Screen::CRAFTING: case Screen::FURNACE: case Screen::CHEST: screenContainer(); break;
        default: break;
    }
    if (!statusMessage.empty()) { ui.text(4, ui.H - 12, statusMessage, COL_YELLOW); }
    ui.flush();
}

void Game::renderWorld() {
    int w = plat::width(), h = plat::height();
    // camera
    cam.pos = player.eyePos(alpha);
    cam.yaw = player.yaw; cam.pitch = player.pitch;
    float fov = settings.fov;
    if (player.sprinting) fov *= 1.1f;
    if (player.usingItem && player.inv.held().id == BOW) fov *= 1.0f - 0.15f * std::min(1.0f, player.useTicks / 20.0f);
    cam.fov = fov;
    cam.farZ = (settings.renderDistance + 2) * 16.0f * 1.5f;
    cam.update(w, h);
    if (settings.viewBobbing && !player.flying) {
        float wd = lerpf(player.prevWalkDist, player.walkDist, alpha) * 2.2f;
        float bob = lerpf(player.prevBobAmt, player.bobAmt, alpha);
        cam.pos += cam.right * (std::sin(wd) * 0.035f * bob) + vec3(0, -std::fabs(std::cos(wd)) * 0.06f * bob, 0);
    }
    if (player.dead) { cam.pos.y -= std::min(1.0f, player.deathTicks / 20.0f) * 1.2f; }
    if (player.sleeping) cam.pos.y -= 0.9f;
    cam.update(w, h);
    FrameEnv env = makeEnv();
    if (player.hurtTime > 0) {
        // small camera roll when hurt
        float roll = std::sin((player.hurtTime - alpha) / 10.0f * PI) * 0.06f;
        cam.view = mat4::rotateZ(roll) * cam.view;
        cam.viewProj = cam.proj * cam.view;
        cam.frustum.build(cam.viewProj);
    }
    glClearColor(env.fogColor.x, env.fogColor.y, env.fogColor.z, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!player.headInWater && !player.inLava) renderer.drawSky(cam, env);
    renderer.drawWorld(*world, cam, env, LAYER_OPAQUE);
    renderer.drawWorld(*world, cam, env, LAYER_CUTOUT);
    // entities and dropped items
    GeoBatch batch;
    entities.render(batch, cam, alpha);
    renderer.drawBatch(batch, cam.viewProj, env, Renderer::DRAW_CUTOUT);
    // selection outline and crack overlay
    if (screen == Screen::PLAYING && player.target.hit && !player.targetEntity && !hideHud) {
        ivec3 b = player.target.block;
        AABB box;
        if (World::selectionBox(world->getBlock(b.x, b.y, b.z), world->getMeta(b.x, b.y, b.z), box)) {
            vec3 o = vec3((float)b.x, (float)b.y, (float)b.z) - cam.pos;
            vec3 mn = box.mn + o - vec3(0.002f), mx = box.mx + o + vec3(0.002f);
            GeoBatch lines;
            uint32_t c = rgba(0, 0, 0, 0.5f);
            vec3 p[8] = {{mn.x, mn.y, mn.z}, {mx.x, mn.y, mn.z}, {mx.x, mn.y, mx.z}, {mn.x, mn.y, mx.z},
                         {mn.x, mx.y, mn.z}, {mx.x, mx.y, mn.z}, {mx.x, mx.y, mx.z}, {mn.x, mx.y, mx.z}};
            const int E[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
            for (auto& e : E) { lines.vert(p[e[0]], 0.5f, 0.5f, TX_WHITE, c); lines.vert(p[e[1]], 0.5f, 0.5f, TX_WHITE, c); }
            renderer.drawBatch(lines, cam.viewProj, env, Renderer::DRAW_LINES, true, false);
            if (player.mining && player.mineProgress > 0) {
                int stage = std::min(9, (int)(player.mineProgress * 10));
                GeoBatch crack;
                uint16_t t[6]; for (auto& x : t) x = (uint16_t)(TX_CRACK0 + stage);
                crack.box(mat4::translate(o), box.mn - vec3(0.003f), box.mx + vec3(0.003f), t, 1.0f, 0xFFFFFFFF, false);
                glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(-1, -1);
                renderer.drawBatch(crack, cam.viewProj, env, Renderer::DRAW_BLEND, true, false);
                glDisable(GL_POLYGON_OFFSET_FILL);
            }
        }
    }
    GeoBatch pb;
    particles.render(pb, cam, *world, env.daylight, alpha);
    renderer.drawBatch(pb, cam.viewProj, env, Renderer::DRAW_BLEND, true, false);
    renderer.drawWorld(*world, cam, env, LAYER_WATER);
    if (!player.dead && !player.sleeping) renderHand(env);
}

void Game::renderHand(const FrameEnv& env) {
    glClear(GL_DEPTH_BUFFER_BIT);
    mat4 proj = mat4::perspective(70 * PI / 180.f, cam.aspect, 0.02f, 10.0f);
    vec3 eye = player.eyePos(alpha);
    int sx = ifloor(eye.x), sy = ifloor(eye.y), sz = ifloor(eye.z);
    float light = std::max(std::pow(0.8f, 15.0f - world->getSkyLight(sx, sy, sz)) * env.daylight, std::pow(0.8f, 15.0f - world->getBlockLight(sx, sy, sz)));
    light = std::max(light, 0.08f);
    float sw = lerpf(player.prevSwing, player.swing, alpha);
    if (player.swing == 0 && player.prevSwing > 0) sw = 0;
    float swingS = std::sin(sw * PI), swingSq = std::sin(std::sqrt(sw) * PI);
    float wd = lerpf(player.prevWalkDist, player.walkDist, alpha) * 2.2f;
    float bob = settings.viewBobbing ? lerpf(player.prevBobAmt, player.bobAmt, alpha) : 0;
    vec3 bobOff(std::sin(wd) * 0.02f * bob, -std::fabs(std::cos(wd)) * 0.03f * bob, 0);
    const ItemStack& held = player.inv.held();
    GeoBatch b;
    FrameEnv e2 = env; e2.fogStart = 100; e2.fogEnd = 200;
    if (held.empty()) {
        mat4 m = mat4::translate(vec3(0.42f, -0.42f, -0.55f) + bobOff + vec3(-swingSq * 0.2f, swingS * 0.12f, -swingS * 0.1f));
        m = m * mat4::rotateY(-0.35f + swingSq * 0.3f) * mat4::rotateX(1.25f - swingS * 0.6f) * mat4::rotateZ(0.1f);
        m = m * mat4::scale(vec3(0.9f, 0.9f, 0.9f));
        drawPlayerArm(b, m, light);
    } else {
        const ItemDef& d = itemDef(held.id);
        vec3 base = vec3(0.36f, -0.36f, -0.6f) + bobOff;
        float eat = 0;
        if (player.usingItem && d.foodHunger > 0) {
            eat = std::min(1.0f, (player.useTicks + alpha) / 8.0f);
            base = lerp(base, vec3(0.05f, -0.22f, -0.38f), eat);
            base.y += std::fabs(std::sin((player.useTicks + alpha) * 0.9f)) * 0.03f * eat;
        }
        mat4 m = mat4::translate(base + vec3(-swingSq * 0.25f, swingS * 0.1f, -swingS * 0.15f));
        bool cube = d.isBlock && (g_blocks[d.placesBlock].shape == SHAPE_CUBE || g_blocks[d.placesBlock].shape == SHAPE_CHEST ||
                                  g_blocks[d.placesBlock].shape == SHAPE_FARMLAND || g_blocks[d.placesBlock].shape == SHAPE_CACTUS);
        if (cube) {
            m = m * mat4::rotateY(0.75f + swingSq * 0.3f) * mat4::rotateX(-swingS * 0.6f);
            const BlockDef& bd = g_blocks[d.placesBlock];
            uint16_t tex[6]; for (int i = 0; i < 6; i++) tex[i] = bd.tex[i];
            if (d.placesBlock == FURNACE) tex[FACE_PZ] = TX_FURNACE_FRONT;
            if (d.placesBlock == CHEST) tex[FACE_PZ] = TX_CHEST_FRONT;
            b.box(m, vec3(-0.17f, -0.17f, -0.17f), vec3(0.17f, 0.17f, 0.17f), tex, light);
        } else {
            int layer = d.icon;
            if (held.id == BOW && player.usingItem) layer = TX_BOW + std::min(3, 1 + player.useTicks / 7);
            bool tool = d.toolType != TOOL_NONE || held.id == STICK || held.id == BONE;
            float rotY = held.id == BOW ? -0.2f : (tool ? -1.4f : -0.6f);
            m = m * mat4::rotateY(rotY + swingSq * 0.4f) * mat4::rotateZ(tool ? 0.2f : 0.0f) * mat4::rotateX(-swingS * 1.1f);
            if (held.id == BOW) m = m * mat4::rotateZ(-0.6f);
            float s = 0.4f;
            vec3 r = m.transformDir(vec3(1, 0, 0)) * s, u = m.transformDir(vec3(0, 1, 0)) * s, c0 = m.transformPoint(vec3(0, 0.08f, 0));
            uint32_t col = rgba(light, light, light);
            // two slightly offset layers give the flat sprite some thickness
            vec3 n = normalize(cross(r, u)) * 0.012f;
            for (int k = 0; k < 2; k++) {
                vec3 c = c0 + n * (float)k;
                uint32_t cc = k == 0 ? col : rgba(light * 0.7f, light * 0.7f, light * 0.7f);
                b.quad(c - r * 0.5f - u * 0.5f, c + r * 0.5f - u * 0.5f, c + r * 0.5f + u * 0.5f, c - r * 0.5f + u * 0.5f, 0, 0, 1, 1, layer, cc);
            }
        }
    }
    renderer.drawBatch(b, proj, e2, Renderer::DRAW_CUTOUT, true, true);
}

static std::string fmtTime(int64_t t) {
    int tod = (int)((t + 6000) % 24000);
    int hh = tod / 1000, mm = (tod % 1000) * 60 / 1000;
    char b[16]; snprintf(b, sizeof(b), "%02d:%02d", hh, mm);
    return b;
}

void Game::renderHUD() {
    float W = ui.W, H = ui.H;
    // screen overlays
    if (player.headInWater) ui.rect(0, 0, W, H, rgba(0.05f, 0.15f, 0.45f, 0.35f));
    if (player.inLava) ui.rect(0, 0, W, H, rgba(0.9f, 0.3f, 0.0f, 0.5f));
    if (player.fireTicks > 0 && !player.inWater) {
        for (int i = 0; i < 6; i++) ui.image(W * 0.15f + i * W * 0.12f, H - 60 - std::fabs(std::sin(titleTime * 7 + i)) * 10, 40, 60, TX_FLAME, rgba(1, 1, 1, 0.7f));
    }
    if (hurtFlash > 0) ui.rect(0, 0, W, H, rgba(0.8f, 0, 0, hurtFlash * 0.5f));
    if (player.sleeping) ui.rect(0, 0, W, H, rgba(0.02f, 0.02f, 0.06f, std::min(0.9f, player.sleepTicks / 100.0f)));
    if (screen == Screen::DEATH) return;
    // crosshair
    float cx = std::floor(W / 2), cy = std::floor(H / 2);
    ui.rect(cx - 4, cy, 9, 1, 0xC8FFFFFF);
    ui.rect(cx, cy - 4, 1, 4, 0xC8FFFFFF); ui.rect(cx, cy + 1, 1, 4, 0xC8FFFFFF);
    float strength = player.attackStrength(alpha);
    if (strength < 1) { ui.rect(cx - 8, cy + 9, 16, 2, 0x80000000); ui.rect(cx - 8, cy + 9, 16 * strength, 2, 0xE0FFFFFF); }
    // hotbar
    float hx = std::floor(W / 2 - 91), hy = H - 22;
    ui.rect(hx, hy, 182, 22, 0x90000000);
    ui.frame(hx, hy, 182, 22, 0xFF6A6A6A);
    for (int i = 0; i < 9; i++) {
        ui.frame(hx + 1 + i * 20, hy + 1, 20, 20, 0x60FFFFFF);
        ui.itemIcon(hx + 3 + i * 20, hy + 3, 16, player.inv.slots[i]);
    }
    float sx = hx + player.inv.selected * 20;
    ui.frame(sx - 1, hy - 1, 24, 24, 0xFF000000, 1);
    ui.frame(sx, hy, 22, 22, 0xFFFFFFFF, 2);
    // hearts
    float by = hy - 10;
    bool shake = player.health <= 4;
    for (int i = 0; i < 10; i++) {
        float x = hx + i * 8, y = by + (shake ? (float)((int)(titleTime * 30 + i * 7) % 3 - 1) : 0);
        float hp = player.health - i * 2;
        int layer = hp >= 2 ? TX_HEART_FULL : (hp >= 1 ? TX_HEART_HALF : TX_HEART_EMPTY);
        uint32_t tint = player.hurtTime > 5 ? 0xFFC0C0FF : 0xFFFFFFFF;
        ui.image(x, y, 9, 9, TX_HEART_EMPTY, tint);
        if (layer != TX_HEART_EMPTY) ui.image(x, y, 9, 9, layer, tint);
    }
    // hunger (right to left)
    for (int i = 0; i < 10; i++) {
        float x = hx + 182 - 9 - i * 8, y = by + (player.saturation <= 0 && player.hunger < 6 ? (float)((int)(titleTime * 20 + i * 5) % 3 - 1) : 0);
        float f = player.hunger - i * 2;
        int layer = f >= 2 ? TX_FOOD_FULL : (f >= 1 ? TX_FOOD_HALF : TX_FOOD_EMPTY);
        ui.image(x, y, 9, 9, TX_FOOD_EMPTY);
        if (layer != TX_FOOD_EMPTY) ui.image(x, y, 9, 9, layer);
    }
    // air bubbles
    if (player.headInWater || player.air < 300) {
        int bubbles = (int)std::ceil(std::max(0, player.air) * 10.0f / 300.0f);
        for (int i = 0; i < bubbles; i++) ui.image(hx + 182 - 9 - i * 8, by - 10, 9, 9, TX_BUBBLE);
    }
    // held item name and action messages
    if (heldNameTimer > 0 && !player.inv.held().empty()) {
        float a = std::min(1.0f, heldNameTimer);
        ui.textCentered(W / 2, hy - 34, itemName(player.inv.held().id), rgba(1, 1, 1, a));
    }
    if (player.actionMessageTicks > 0) ui.textCentered(W / 2, hy - 46, player.actionMessage, rgba(1, 1, 0.6f, std::min(1.0f, player.actionMessageTicks / 20.0f)));
    if (player.flying) ui.text(W - ui.textWidth("FLY") - 4, 4, "FLY", COL_YELLOW);
    if (showDebug) renderDebug();
}

void Game::renderDebug() {
    char b[256];
    int y = 2;
    auto line = [&](const char* s) { ui.rect(1, (float)y - 1, ui.textWidth(s) + 3, 10, 0x90000000); ui.text(2, (float)y, s); y += 10; };
    snprintf(b, sizeof(b), "Blockcraft  %d fps (%.1f ms)", fps, frameTime * 1000); line(b);
    snprintf(b, sizeof(b), "XYZ: %.2f / %.2f / %.2f", player.pos.x, player.pos.y, player.pos.z); line(b);
    int bx = ifloor(player.pos.x), byy = ifloor(player.pos.y), bz = ifloor(player.pos.z);
    snprintf(b, sizeof(b), "Block: %d %d %d   Chunk: %d %d", bx, byy, bz, bx >> 4, bz >> 4); line(b);
    static const char* dirs[4] = {"south (+Z)", "west (-X)", "north (-Z)", "east (+X)"};
    snprintf(b, sizeof(b), "Facing: %s", dirs[(player.facingFromYaw() + 2) & 3]); line(b);
    TerrainGen g(info.seed);
    ColumnInfo ci = g.column(bx, bz);
    snprintf(b, sizeof(b), "Biome: %s   Light: sky %d block %d", biomeName(ci.biome), world->getSkyLight(bx, byy, bz), world->getBlockLight(bx, byy, bz)); line(b);
    snprintf(b, sizeof(b), "Chunks: %d loaded, %d sections drawn, %d jobs", world->loadedChunkCount(), renderer.drawnSections, world->pendingJobs()); line(b);
    snprintf(b, sizeof(b), "Entities: %d (%d monsters)  Particles: %d", (int)entities.list.size(), entities.countType(true), (int)particles.list.size()); line(b);
    snprintf(b, sizeof(b), "Day %lld  Time %s  Seed %llu", (long long)(worldTime / 24000 + 1), fmtTime(worldTime).c_str(), (unsigned long long)info.seed); line(b);
    if (player.target.hit) {
        ivec3 t = player.target.block;
        snprintf(b, sizeof(b), "Target: %s (%d %d %d) meta %d", blockDef(world->getBlock(t.x, t.y, t.z)).name, t.x, t.y, t.z, world->getMeta(t.x, t.y, t.z)); line(b);
    }
}

void Game::takeScreenshot() {
    int w = plat::width(), h = plat::height();
    std::vector<uint8_t> px((size_t)w * h * 4), flip((size_t)w * h * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    for (int y = 0; y < h; y++) memcpy(&flip[(size_t)y * w * 4], &px[(size_t)(h - 1 - y) * w * 4], (size_t)w * 4);
    for (size_t i = 3; i < flip.size(); i += 4) flip[i] = 255;
    std::string dir = baseDir + "screenshots\\";
    plat::makeDir(dir);
    char name[64];
    time_t t = time(nullptr);
    strftime(name, sizeof(name), "%Y-%m-%d_%H.%M.%S.png", localtime(&t));
    if (writePNG(dir + name, w, h, flip.data())) statusMessage = std::string("Saved screenshot ") + name;
}
