// Headless tests of world, survival and entity logic (build with tests/run_tests.sh on Linux).
#include "../src/world/world.h"
#include "../src/world/worldgen.h"
#include "../src/core/jobs.h"
#include "../src/core/events.h"
#include "../src/game/player.h"
#include "../src/game/crafting.h"
#include "../src/game/physics.h"
#include "../src/entity/entity.h"
#include "../src/io/region.h"
#include "../src/io/save.h"
#include <cstdio>
#include <cstdlib>
#include <thread>

EventQueue g_events;
static int g_fail = 0, g_pass = 0;
#define CHECK(cond, ...) do { if (cond) { g_pass++; } else { g_fail++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static World* W; static EntityManager* EM; static Player* P; static Rng R(1);

static void drainEvents() {
    for (int round = 0; round < 6 && !g_events.events.empty(); round++) {
        std::vector<GameEvent> ev; ev.swap(g_events.events);
        for (auto& e : ev) {
            if (e.type == EV_DROP_ITEM) EM->spawnItem(e.pos, ItemStack((uint16_t)e.a, e.b, e.c));
            else if (e.type == EV_FALLING_BLOCK) { Entity* f = EM->spawn(ET_FALLING_BLOCK, e.pos + vec3(0.5f, 0, 0.5f)); f->blockId = (uint16_t)e.a; }
            else if (e.type == EV_EXPLOSION) EM->explode(e.pos, e.f);
            else if (e.type == EV_BLOCK_BROKEN && e.c == 0) { std::vector<ItemStack> d; blockDrops((uint16_t)e.a, (uint8_t)e.b, ItemStack(), R, d); for (auto& s : d) EM->spawnItem(e.pos, s); }
        }
    }
    g_events.events.clear();
}
static void ticks(int n) { for (int i = 0; i < n; i++) { W->tick(P->pos); EM->tick(); drainEvents(); } }
static void loadAround(vec3 p, int radius) {
    W->renderDistance = radius;
    for (int it = 0; it < 4000; it++) {
        W->updateStreaming(p);
        W->processJobResults(64, 0);
        for (auto& kc : W->newlyReadyChunks) EM->spawnFromChunk(kc.second);
        W->newlyReadyChunks.clear();
        if (W->spawnAreaReady(ifloor(p.x), ifloor(p.z), radius)) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}
// carve a sealed test room far below the surface so tests don't interfere with terrain
static void room(int x0, int y0, int z0, int sx, int sy, int sz, uint16_t wall = STONE) {
    for (int x = x0 - 1; x <= x0 + sx; x++) for (int y = y0 - 1; y <= y0 + sy; y++) for (int z = z0 - 1; z <= z0 + sz; z++) {
        bool inside = x >= x0 && x < x0 + sx && y >= y0 && y < y0 + sy && z >= z0 && z < z0 + sz;
        W->setBlock(x, y, z, inside ? AIR : wall);
    }
}

int main() {
    initBlocks(); initItems(); initRecipes();
    if (system("rm -rf /tmp/bc_test && mkdir -p /tmp/bc_test/")) {}
    JobSystem jobs; jobs.start(3);
    World world; W = &world;
    world.init(12345, "/tmp/bc_test/", &jobs);
    EntityManager em; EM = &em; em.world = &world;
    Player player; P = &player; em.player = &player;
    em.mobSpawning = false;
    vec3 base(8, 100, 8);
    player.pos = player.prevPos = base;
    loadAround(base, 3);
    CHECK(world.loadedChunkCount() >= 49, "chunks loaded: %d", world.loadedChunkCount());

    // ---- terrain sanity ----
    int stone = 0, ores = 0, air = 0, water = 0;
    for (auto& kv : world.chunks) for (int i = 0; i < CHUNK_VOL; i++) {
        uint16_t b = kv.second->blocks[i];
        if (b == STONE) stone++; else if (b >= COAL_ORE && b <= DIAMOND_ORE) ores++; else if (b == AIR) air++; else if (b == WATER) water++;
    }
    CHECK(stone > 100000, "stone %d", stone);
    CHECK(ores > 200, "ores %d", ores);
    CHECK(world.getBlock(5, 0, 5) == BEDROCK, "bedrock at y=0");

    // ---- lighting: torch in a sealed dark room ----
    room(0, 20, 0, 9, 4, 9);
    ticks(1);
    CHECK(world.getSkyLight(4, 21, 4) == 0 && world.getBlockLight(4, 21, 4) == 0, "room dark sky=%d blk=%d", world.getSkyLight(4, 21, 4), world.getBlockLight(4, 21, 4));
    world.setBlock(4, 20, 4, TORCH);
    CHECK(world.getBlockLight(4, 20, 4) == 14, "torch light %d", world.getBlockLight(4, 20, 4));
    CHECK(world.getBlockLight(7, 20, 4) == 11, "light 3 away %d", world.getBlockLight(7, 20, 4));
    world.setBlock(4, 20, 4, AIR);
    CHECK(world.getBlockLight(7, 20, 4) == 0, "light removed %d", world.getBlockLight(7, 20, 4));
    // opening the roof lets sky light in
    for (int y = 24; y < 256; y++) world.setBlock(2, y, 2, AIR);
    CHECK(world.getSkyLight(2, 23, 2) == 15, "sky shaft %d", world.getSkyLight(2, 23, 2));
    CHECK(world.getSkyLight(2, 20, 2) == 15, "sky reaches floor %d", world.getSkyLight(2, 20, 2));
    CHECK(world.getSkyLight(5, 20, 2) == 12, "sky spreads sideways %d", world.getSkyLight(5, 20, 2));
    world.setBlock(2, 30, 2, STONE);
    CHECK(world.getSkyLight(2, 20, 2) < 15, "shaft closed %d", world.getSkyLight(2, 20, 2));

    // ---- light across a chunk border ----
    room(12, 30, 2, 8, 3, 3);
    world.setBlock(15, 30, 3, TORCH);
    CHECK(world.getBlockLight(17, 30, 3) == 12, "light crosses chunk border %d", world.getBlockLight(17, 30, 3));

    // ---- water flow ----
    room(0, 40, 0, 12, 3, 12);
    world.setBlock(6, 40, 6, WATER, 0);
    ticks(200);
    int wcount = 0;
    for (int x = 0; x < 12; x++) for (int z = 0; z < 12; z++) if (world.getBlock(x, 40, z) == WATER) wcount++;
    CHECK(wcount > 60 && wcount < 144, "water spread to %d blocks", wcount);
    CHECK(world.getBlock(6, 40, 13) != WATER, "walls hold water");
    CHECK(world.getBlock(0, 40, 0) == AIR, "water stops after 7 (corner dist 12)");
    auto dumpW = [&](const char* t) { if (!getenv("DUMPW")) return; printf("%s\n", t); for (int z = 0; z < 12; z++) { for (int x = 0; x < 12; x++) { uint16_t b = world.getBlock(x, 40, z); printf(b == WATER ? "%2d" : " .", world.getMeta(x, 40, z)); } printf("\n"); } };
    dumpW("after spread");
    world.setBlock(6, 40, 6, AIR);
    ticks(10); dumpW("10 ticks after removal");
    ticks(290);
    dumpW("300 ticks after removal");
    wcount = 0;
    for (int x = 0; x < 12; x++) for (int z = 0; z < 12; z++) if (world.getBlock(x, 40, z) == WATER) wcount++;
    CHECK(wcount == 0, "water drains after removing source: %d left", wcount);
    // infinite source: two sources fill the gap between them
    room(0, 50, 0, 3, 2, 1);
    world.setBlock(0, 50, 0, WATER, 0); world.setBlock(2, 50, 0, WATER, 0);
    ticks(40);
    CHECK(world.getBlock(1, 50, 0) == WATER && (world.getMeta(1, 50, 0) & 15) == 0, "infinite water source");
    // lava + water
    room(0, 60, 0, 5, 2, 1);
    world.setBlock(0, 60, 0, LAVA, 0);
    world.setBlock(4, 60, 0, WATER, 0);
    ticks(200);
    bool cob = false; for (int x = 0; x < 5; x++) { uint16_t b = world.getBlock(x, 60, 0); if (b == COBBLESTONE || b == OBSIDIAN || b == STONE) cob = true; }
    CHECK(cob, "lava and water make stone");

    // ---- falling sand ----
    room(0, 70, 0, 3, 6, 3);
    world.setBlock(1, 75, 1, SAND);
    world.setBlock(1, 70, 1, STONE); world.setBlock(1, 70, 1, AIR);  // poke a neighbour update
    world.scheduleUpdate(1, 75, 1, 1);
    ticks(80);
    CHECK(world.getBlock(1, 75, 1) == AIR && world.getBlock(1, 70, 1) == SAND, "sand fell to %d", world.getBlock(1, 70, 1));

    // ---- crafting ----
    {
        ItemStack g[9]; g[0] = ItemStack(OAK_LOG, 1);
        CHECK(matchRecipe(g, 2).id == OAK_PLANKS && matchRecipe(g, 2).count == 4, "log -> planks");
        ItemStack t[9]; for (int i = 0; i < 3; i++) t[i] = ItemStack(COBBLESTONE, 1); t[4] = ItemStack(STICK, 1); t[7] = ItemStack(STICK, 1);
        CHECK(matchRecipe(t, 3).id == toolItem(MAT_STONE, KIND_PICKAXE), "stone pickaxe");
        ItemStack a[9]; a[1] = ItemStack(BIRCH_PLANKS, 1); a[2] = ItemStack(OAK_PLANKS, 1); a[4] = ItemStack(SPRUCE_PLANKS, 1); a[5] = ItemStack(STICK, 1); a[8] = ItemStack(STICK, 1);
        // mirrored axe with mixed planks:  .PP / .PS / ..S
        CHECK(matchRecipe(a, 3).id == toolItem(MAT_WOOD, KIND_AXE), "mirrored wooden axe got %d", matchRecipe(a, 3).id);
        ItemStack s2[4]; s2[0] = ItemStack(OAK_PLANKS, 1); s2[2] = ItemStack(OAK_PLANKS, 1);
        CHECK(matchRecipe(s2, 2).id == STICK, "sticks in 2x2");
        ItemStack bad[9]; bad[0] = ItemStack(DIRT, 1);
        CHECK(matchRecipe(bad, 3).empty(), "dirt makes nothing");
        CHECK(allRecipes().size() >= 40, "recipe count %d", (int)allRecipes().size());
        printf("recipes: %d, blocks: %d, items: %d\n", (int)allRecipes().size(), (int)BLOCK_COUNT - 1, (int)(ITEM_END - ITEM_FIRST));
        ItemStack tor[9]; tor[1] = ItemStack(COAL, 1); tor[4] = ItemStack(STICK, 1);
        CHECK(matchRecipe(tor, 3).id == TORCH && matchRecipe(tor, 3).count == 4, "torches");
    }

    // ---- furnace ----
    room(0, 80, 0, 3, 2, 3);
    world.setBlock(1, 80, 1, FURNACE, 0);
    TileEntity* f = world.createTile(1, 80, 1, TILE_FURNACE);
    f->slots[0] = ItemStack(IRON_ORE, 3);
    f->slots[1] = ItemStack(COAL, 1);
    for (int i = 0; i < 700; i++) tickFurnaceTile(world, 1, 80, 1, *world.getTile(1, 80, 1));
    f = world.getTile(1, 80, 1);
    CHECK(f && f->slots[2].id == IRON_INGOT && f->slots[2].count == 3, "smelted %d ingots", f ? f->slots[2].count : -1);
    CHECK(f && f->slots[1].empty(), "coal used");
    CHECK(world.getBlock(1, 80, 1) == FURNACE_LIT, "furnace lit while burning");
    CHECK(world.getBlockLight(1, 81, 1) >= 12, "lit furnace emits light %d", world.getBlockLight(1, 81, 1));

    // ---- player physics ----
    room(0, 90, 0, 8, 20, 8);
    player.reset();
    player.pos = player.prevPos = vec3(4.5f, 108, 4.5f); player.vel = vec3(0, 0, 0); player.fallDistance = 0;
    PlayerInput none;
    for (int i = 0; i < 80; i++) { player.updateTarget(world, em, 1); player.tick(world, em, none, false); }
    CHECK(std::fabs(player.pos.y - 90) < 0.01f && player.onGround, "landed y=%.3f", player.pos.y);
    CHECK(player.health < 20 && player.health > 0, "fall damage hp=%.1f", player.health);
    float hpAfterFall = player.health;
    CHECK(std::fabs(hpAfterFall - (20 - 15)) < 0.01f, "18-block fall = 15 damage, hp %.1f", hpAfterFall);
    // walk into a wall
    PlayerInput walk; walk.forward = 1;
    player.yaw = 0;  // facing -Z
    for (int i = 0; i < 60; i++) player.tick(world, em, walk, false);
    CHECK(player.pos.z > 0.29f && player.pos.z < 0.32f, "stopped at wall z=%.3f", player.pos.z);
    // jump height
    player.pos = player.prevPos = vec3(4.5f, 90, 4.5f); player.vel = vec3(0, 0, 0);
    PlayerInput jump; jump.jump = true;
    float maxY = 0;
    for (int i = 0; i < 14; i++) { player.tick(world, em, i == 0 ? jump : none, false); maxY = std::max(maxY, player.pos.y); }
    CHECK(maxY - 90 > 1.1f && maxY - 90 < 1.35f, "jump height %.3f", maxY - 90);
    // sneaking stops at an edge
    room(0, 120, 0, 8, 4, 8);
    for (int x = 0; x < 8; x++) for (int z = 0; z < 4; z++) world.setBlock(x, 120, z, STONE);  // ledge z<4 at y=120
    player.pos = player.prevPos = vec3(4.5f, 121, 1.5f); player.vel = vec3(0, 0, 0); player.yaw = PI;  // facing +Z
    PlayerInput sneakWalk; sneakWalk.forward = 1; sneakWalk.sneak = true;
    for (int i = 0; i < 100; i++) player.tick(world, em, sneakWalk, false);
    CHECK(player.pos.y > 120.9f && player.pos.z < 4.35f, "sneak edge: y=%.2f z=%.2f", player.pos.y, player.pos.z);
    // hunger regenerates health
    player.health = 10; player.hunger = 20; player.saturation = 5;
    player.pos = player.prevPos = vec3(4.5f, 91, 4.5f);
    room(0, 90, 0, 8, 6, 8);
    for (int i = 0; i < 400; i++) player.tick(world, em, none, false);
    CHECK(player.health > 13, "regenerated to %.1f", player.health);

    // ---- mining with tools ----
    player.inv.clear();
    CHECK(canHarvest(IRON_ORE, ItemStack(toolItem(MAT_STONE, KIND_PICKAXE), 1)), "stone pick harvests iron");
    CHECK(!canHarvest(IRON_ORE, ItemStack(toolItem(MAT_WOOD, KIND_PICKAXE), 1)), "wood pick cannot");
    CHECK(!canHarvest(STONE, ItemStack()), "hand cannot harvest stone");
    CHECK(canHarvest(DIRT, ItemStack()), "hand harvests dirt");
    CHECK(breakSpeed(OAK_LOG, ItemStack(toolItem(MAT_IRON, KIND_AXE), 1)) == 6, "iron axe speed");

    // ---- pathfinding ----
    room(0, 140, 0, 14, 3, 14);
    for (int z = 0; z < 13; z++) world.setBlock(7, 140, z, STONE), world.setBlock(7, 141, z, STONE), world.setBlock(7, 142, z, STONE);  // wall with gap at z=13
    std::vector<ivec3> path;
    bool ok = findPath(world, ivec3(2, 140, 2), ivec3(12, 140, 2), 2, false, 2000, path);
    CHECK(ok && path.size() > 20, "path around wall, len %d", (int)path.size());
    bool throughWall = false; for (auto& p : path) if (p.x == 7 && p.z < 13) throughWall = true;
    CHECK(!throughWall, "path avoids wall");
    // step up one block
    room(0, 150, 0, 8, 4, 3);
    for (int x = 4; x < 8; x++) for (int z = 0; z < 3; z++) world.setBlock(x, 150, z, STONE);
    ok = findPath(world, ivec3(1, 150, 1), ivec3(6, 151, 1), 2, false, 500, path);
    CHECK(ok, "path steps up");

    // ---- mobs: a shambler chases and hurts the player ----
    room(0, 160, 0, 16, 4, 6);
    player.reset(); player.flying = false;
    player.pos = player.prevPos = vec3(14.5f, 160, 3.5f);
    Entity* z = em.spawn(ET_SHAMBLER, vec3(2.5f, 160, 3.5f));
    em.isDay = false; em.dayFactor = 0.2f;
    for (int i = 0; i < 300 && player.health >= 20; i++) { ticks(1); player.tick(world, em, none, true); }
    CHECK(player.health < 20, "shambler attacked, hp %.1f (mob at %.1f)", player.health, z->pos.x);
    // kill it with a sword
    z = nullptr;
    for (auto& e : em.list) if (e->type == ET_SHAMBLER) z = e.get();
    if (z) { for (int i = 0; i < 10; i++) { z->invuln = 0; em.hurt(z, 7, vec3(1, 0, 0), true); } }
    ticks(30);
    int rotten = 0, shamblers = 0;
    for (auto& e : em.list) { if (e->type == ET_ITEM && e->item.id == ROTTEN_FLESH) rotten++; if (e->type == ET_SHAMBLER) shamblers++; }
    CHECK(shamblers == 0, "shambler removed after death");
    (void)rotten;

    // ---- fuseling explodes and destroys blocks ----
    room(0, 180, 0, 12, 5, 12);
    for (int x = 0; x < 12; x++) for (int z2 = 0; z2 < 12; z2++) world.setBlock(x, 180, z2, DIRT);
    player.reset(); player.pos = player.prevPos = vec3(6.5f, 181, 6.5f);
    em.spawn(ET_FUSELING, vec3(4.5f, 181, 6.5f));
    int dirtBefore = 0; for (int x = 0; x < 12; x++) for (int z2 = 0; z2 < 12; z2++) if (world.getBlock(x, 180, z2) == DIRT) dirtBefore++;
    for (int i = 0; i < 80; i++) { ticks(1); player.tick(world, em, none, true); }
    int dirtAfter = 0; for (int x = 0; x < 12; x++) for (int z2 = 0; z2 < 12; z2++) if (world.getBlock(x, 180, z2) == DIRT) dirtAfter++;
    CHECK(dirtAfter < dirtBefore - 5, "explosion destroyed blocks %d -> %d", dirtBefore, dirtAfter);
    CHECK(player.health < 20, "explosion hurt player hp %.1f", player.health);

    // ---- arrows ----
    room(0, 200, 0, 20, 4, 3);
    em.spawnArrow(vec3(1.5f, 201.5f, 1.5f), vec3(2.0f, 0.05f, 0), true, 6, 0);
    ticks(40);
    Entity* arrow = nullptr; for (auto& e : em.list) if (e->type == ET_ARROW) arrow = e.get();
    CHECK(arrow && arrow->stuck && arrow->pos.x > 19.5f, "arrow stuck in far wall at x=%.2f", arrow ? arrow->pos.x : -1);
    ticks(60);
    arrow = nullptr; for (auto& e : em.list) if (e->type == ET_ARROW) arrow = e.get();
    CHECK(arrow && arrow->stuck, "arrow stays stuck");

    // ---- dropped items are picked up ----
    player.reset(); player.inv.clear();
    room(0, 210, 0, 6, 3, 6);
    player.pos = player.prevPos = vec3(3.5f, 210, 3.5f);
    em.spawnItem(vec3(4.5f, 210.5f, 3.5f), ItemStack(DIAMOND, 3));
    for (int i = 0; i < 60; i++) { ticks(1); player.tick(world, em, none, false); }
    CHECK(player.inv.count(DIAMOND) == 3, "picked up %d diamonds", player.inv.count(DIAMOND));

    // ---- inventory ----
    {
        Inventory inv;
        CHECK(inv.add(ItemStack(COBBLESTONE, 64)) == 0 && inv.add(ItemStack(COBBLESTONE, 10)) == 0, "add");
        CHECK(inv.count(COBBLESTONE) == 74, "count %d", inv.count(COBBLESTONE));
        CHECK(inv.slots[0].count == 64 && inv.slots[1].count == 10, "stacking");
        CHECK(inv.add(ItemStack(toolItem(MAT_IRON, KIND_SWORD), 1)) == 0 && inv.slots[2].count == 1, "tools don't stack");
        for (int i = 0; i < 40; i++) inv.add(ItemStack(DIRT, 64));
        CHECK(inv.add(ItemStack(DIRT, 5)) == 5, "full inventory returns leftover");
    }

    // ---- saving: modified chunks come back from region files ----
    world.setBlock(3, 230, 3, GOLD_BLOCK);
    world.setBlock(9, 231, 9, CHEST);
    TileEntity* ch = world.createTile(9, 231, 9, TILE_CHEST);
    ch->slots[5] = ItemStack(DIAMOND, 7);
    world.saveAll();
    world.shutdown();
    World w2; W = &w2; em.world = &w2; em.clear();
    w2.init(12345, "/tmp/bc_test/", &jobs);
    loadAround(base, 2);
    CHECK(w2.getBlock(3, 230, 3) == GOLD_BLOCK, "saved block persisted (%d)", w2.getBlock(3, 230, 3));
    TileEntity* ch2 = w2.getTile(9, 231, 9);
    CHECK(ch2 && ch2->slots[5].id == DIAMOND && ch2->slots[5].count == 7, "chest contents persisted");
    // player.dat round trip
    Player pp; pp.pos = vec3(1, 2, 3); pp.health = 13; pp.inv.slots[4] = ItemStack(BOW, 1, 17);
    savePlayer("/tmp/bc_test/", pp);
    Player pl; loadPlayer("/tmp/bc_test/", pl);
    CHECK(pl.pos.y == 2 && pl.health == 13 && pl.inv.slots[4].id == BOW && pl.inv.slots[4].damage == 17, "player.dat round trip");
    WorldInfo wi; wi.name = "T"; wi.seed = 99; wi.time = 12345;
    saveWorldInfo("/tmp/bc_test/", wi);
    WorldInfo wo; loadWorldInfo("/tmp/bc_test/", wo);
    CHECK(wo.name == "T" && wo.seed == 99 && wo.time == 12345, "world.dat round trip");

    w2.shutdown();
    jobs.stop();
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
