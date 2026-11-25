#include "crafting.h"
#include "../world/world.h"
#include "../core/events.h"
#include "../audio/audio.h"
#include <map>
#include <cstring>

static std::vector<Recipe> g_recipes;
const std::vector<Recipe>& allRecipes() { return g_recipes; }

// Pattern rows use single characters; the key maps characters to items
static void shaped(std::initializer_list<const char*> rows, std::map<char, uint16_t> key, uint16_t outId, int outCount) {
    Recipe r;
    r.h = (int)rows.size();
    int y = 0;
    for (const char* row : rows) {
        int len = (int)strlen(row);
        r.w = std::max(r.w, len);
        for (int x = 0; x < len; x++) r.in[y * 3 + x] = row[x] == ' ' ? 0 : key[row[x]];
        y++;
    }
    // compact to w-wide rows
    uint16_t tmp[9] = {};
    for (int yy = 0; yy < r.h; yy++) for (int xx = 0; xx < r.w; xx++) tmp[yy * r.w + xx] = r.in[yy * 3 + xx];
    memcpy(r.in, tmp, sizeof(tmp));
    r.out = ItemStack(outId, outCount);
    g_recipes.push_back(r);
}

void initRecipes() {
    g_recipes.clear();
    shaped({"L"}, {{'L', OAK_LOG}}, OAK_PLANKS, 4);
    shaped({"L"}, {{'L', BIRCH_LOG}}, BIRCH_PLANKS, 4);
    shaped({"L"}, {{'L', SPRUCE_LOG}}, SPRUCE_PLANKS, 4);
    shaped({"P", "P"}, {{'P', ANY_PLANKS}}, STICK, 4);
    shaped({"PP", "PP"}, {{'P', ANY_PLANKS}}, CRAFTING_TABLE, 1);
    shaped({"CCC", "C C", "CCC"}, {{'C', COBBLESTONE}}, FURNACE, 1);
    shaped({"PPP", "P P", "PPP"}, {{'P', ANY_PLANKS}}, CHEST, 1);
    shaped({"C", "S"}, {{'C', COAL}, {'S', STICK}}, TORCH, 4);
    // tools
    const uint16_t mats[5] = {ANY_PLANKS, COBBLESTONE, IRON_INGOT, GOLD_INGOT, DIAMOND};
    for (int m = 0; m < 5; m++) {
        std::map<char, uint16_t> k = {{'M', mats[m]}, {'S', STICK}};
        shaped({"MMM", " S ", " S "}, k, toolItem(m, KIND_PICKAXE), 1);
        shaped({"MM", "MS", " S"}, k, toolItem(m, KIND_AXE), 1);
        shaped({"M", "S", "S"}, k, toolItem(m, KIND_SHOVEL), 1);
        shaped({"M", "M", "S"}, k, toolItem(m, KIND_SWORD), 1);
        shaped({"MM", " S", " S"}, k, toolItem(m, KIND_HOE), 1);
    }
    shaped({" SX", "S X", " SX"}, {{'S', STICK}, {'X', STRING}}, BOW, 1);
    shaped({"F", "S", "E"}, {{'F', FLINT}, {'S', STICK}, {'E', FEATHER}}, ARROW, 4);
    shaped({"WWW", "PPP"}, {{'W', ANY_WOOL}, {'P', ANY_PLANKS}}, BED_ITEM, 1);
    shaped({"PP", "PP", "PP"}, {{'P', ANY_PLANKS}}, DOOR_ITEM, 3);
    shaped({"WWW"}, {{'W', WHEAT_ITEM}}, BREAD, 1);
    shaped({"S S", "SSS", "S S"}, {{'S', STICK}}, LADDER, 3);
    shaped({"SS", "SS"}, {{'S', STRING}}, WOOL_WHITE, 1);
    shaped({"SS", "SS"}, {{'S', SAND}}, SANDSTONE, 1);
    shaped({"SS", "SS"}, {{'S', STONE}}, STONE_BRICKS, 4);
    shaped({"BB", "BB"}, {{'B', BRICK}}, BRICKS, 1);
    shaped({"CC", "CC"}, {{'C', CLAY_BALL}}, CLAY, 1);
    const uint16_t blk[4][2] = {{IRON_INGOT, IRON_BLOCK}, {GOLD_INGOT, GOLD_BLOCK}, {DIAMOND, DIAMOND_BLOCK}, {COAL, COAL_BLOCK}};
    for (auto& b : blk) {
        shaped({"XXX", "XXX", "XXX"}, {{'X', b[0]}}, b[1], 1);
        shaped({"X"}, {{'X', b[1]}}, b[0], 9);
    }
    shaped({"WS"}, {{'W', WOOL_WHITE}, {'S', COAL}}, WOOL_BLACK, 1);
    shaped({"W", "S"}, {{'W', WOOL_WHITE}, {'S', COAL}}, WOOL_GRAY, 1);
    shaped({"WD"}, {{'W', WOOL_WHITE}, {'D', DIRT}}, WOOL_BROWN, 1);
    shaped({"C", "V"}, {{'C', COBBLESTONE}, {'V', TALL_GRASS}}, MOSSY_COBBLE, 1);
}

static bool ingredientMatches(uint16_t want, const ItemStack& have) {
    if (want == 0) return have.empty();
    if (have.empty()) return false;
    if (want == ANY_PLANKS) return have.id == OAK_PLANKS || have.id == BIRCH_PLANKS || have.id == SPRUCE_PLANKS;
    if (want == ANY_LOG) return have.id == OAK_LOG || have.id == BIRCH_LOG || have.id == SPRUCE_LOG;
    if (want == ANY_WOOL) return have.id == WOOL_WHITE || have.id == WOOL_GRAY || have.id == WOOL_BLACK || have.id == WOOL_BROWN;
    return have.id == want;
}

static bool matchAt(const Recipe& r, const ItemStack* grid, int n, int ox, int oy, bool mirror) {
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++) {
            int rx = x - ox, ry = y - oy;
            uint16_t want = 0;
            if (rx >= 0 && ry >= 0 && rx < r.w && ry < r.h) want = r.in[ry * r.w + (mirror ? r.w - 1 - rx : rx)];
            if (!ingredientMatches(want, grid[y * n + x])) return false;
        }
    return true;
}

ItemStack matchRecipe(const ItemStack* grid, int n) {
    for (auto& r : g_recipes) {
        if (r.w > n || r.h > n) continue;
        for (int oy = 0; oy + r.h <= n; oy++)
            for (int ox = 0; ox + r.w <= n; ox++)
                if (matchAt(r, grid, n, ox, oy, false) || matchAt(r, grid, n, ox, oy, true)) return r.out;
    }
    return ItemStack();
}

void consumeIngredients(ItemStack* grid, int n) {
    for (int i = 0; i < n * n; i++) {
        if (grid[i].empty()) continue;
        grid[i].count--;
        if (grid[i].count == 0) grid[i].clear();
    }
}

ItemStack smeltResult(uint16_t in) {
    switch (in) {
        case IRON_ORE: return ItemStack(IRON_INGOT, 1);
        case GOLD_ORE: return ItemStack(GOLD_INGOT, 1);
        case SAND: return ItemStack(GLASS, 1);
        case COBBLESTONE: return ItemStack(STONE, 1);
        case CLAY_BALL: return ItemStack(BRICK, 1);
        case OAK_LOG: case BIRCH_LOG: case SPRUCE_LOG: return ItemStack(COAL, 1);
        case RAW_PORK: return ItemStack(COOKED_PORK, 1);
        case RAW_BEEF: return ItemStack(STEAK, 1);
        case RAW_CHICKEN: return ItemStack(COOKED_CHICKEN, 1);
        case RAW_MUTTON: return ItemStack(COOKED_MUTTON, 1);
        default: return ItemStack();
    }
}

int fuelTicks(uint16_t id) { return id < ITEM_END ? itemDef(id).fuelTicks : 0; }

// Furnace: slot 0 input, 1 fuel, 2 output. 200 ticks (10 s) per item.
void tickFurnaceTile(World& w, int x, int y, int z, TileEntity& t) {
    ItemStack& in = t.slots[0];
    ItemStack& fuel = t.slots[1];
    ItemStack& out = t.slots[2];
    bool wasBurning = t.burnTime > 0;
    if (t.burnTime > 0) t.burnTime--;
    ItemStack res = in.empty() ? ItemStack() : smeltResult(in.id);
    bool canSmelt = !res.empty() && (out.empty() || (out.sameItem(res) && out.count + res.count <= out.maxStack()));
    if (canSmelt && t.burnTime == 0 && !fuel.empty() && fuelTicks(fuel.id) > 0) {
        t.burnTime = t.burnTotal = (int16_t)std::min(32000, fuelTicks(fuel.id));
        fuel.count--;
        if (fuel.count == 0) fuel.clear();
    }
    if (t.burnTime > 0 && canSmelt) {
        t.cookTime++;
        if (t.cookTime >= 200) {
            t.cookTime = 0;
            if (out.empty()) out = res; else out.count = (uint8_t)(out.count + res.count);
            in.count--;
            if (in.count == 0) in.clear();
        }
    } else if (t.cookTime > 0) t.cookTime = (int16_t)std::max(0, t.cookTime - 2);
    bool burning = t.burnTime > 0;
    if (burning != wasBurning) {
        uint8_t meta = w.getMeta(x, y, z);
        w.setBlock(x, y, z, burning ? FURNACE_LIT : FURNACE, meta);
        if (burning) g_events.sound(vec3(x + 0.5f, y + 0.5f, z + 0.5f), SFX_FURNACE, 0.6f);
    }
}
