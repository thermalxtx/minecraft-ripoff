// Item table: tools, food, materials, and block items.
#include "items.h"
#include "../render/textures.h"
#include <cstdio>
#include <algorithm>

ItemDef g_items[ITEM_END];

const char* itemName(uint16_t id) { return id < ITEM_END ? g_items[id].name : "?"; }

void initItems() {
    // Block items mirror the block table
    for (int b = 1; b < BLOCK_COUNT; b++) {
        ItemDef& d = g_items[b];
        const BlockDef& bd = g_blocks[b];
        d.name = bd.name; d.icon = bd.tex[FACE_PZ]; d.isBlock = true; d.placesBlock = (uint16_t)b;
        if (b == OAK_LOG || b == BIRCH_LOG || b == SPRUCE_LOG || b == OAK_PLANKS || b == BIRCH_PLANKS || b == SPRUCE_PLANKS ||
            b == CRAFTING_TABLE || b == CHEST || b == LADDER) d.fuelTicks = 300;
        if (b == OAK_SAPLING || b == BIRCH_SAPLING || b == SPRUCE_SAPLING) d.fuelTicks = 100;
        if (b == COAL_BLOCK) d.fuelTicks = 16000;
    }
    g_items[GRASS].icon = TX_GRASS_SIDE;
    auto item = [](uint16_t id, const char* name, uint16_t icon, int stack = 64) -> ItemDef& {
        ItemDef& d = g_items[id]; d = ItemDef(); d.name = name; d.icon = icon; d.maxStack = (uint8_t)stack; return d;
    };
    item(STICK, "Stick", TX_STICK).fuelTicks = 100;
    item(COAL, "Coal", TX_COAL).fuelTicks = 1600;
    item(IRON_INGOT, "Iron Ingot", TX_IRON_INGOT);
    item(GOLD_INGOT, "Gold Ingot", TX_GOLD_INGOT);
    item(DIAMOND, "Diamond", TX_DIAMOND);

    static const char* matNames[5] = {"Wooden", "Stone", "Iron", "Golden", "Diamond"};
    static const char* kindNames[5] = {"Pickaxe", "Axe", "Shovel", "Sword", "Hoe"};
    static char nameBuf[25][32];
    const int durability[5] = {59, 131, 250, 32, 1561};
    const float speed[5] = {2, 4, 6, 12, 8};
    const int tier[5] = {0, 1, 2, 0, 3};
    const float swordDmg[5] = {4, 5, 6, 4, 7};
    const uint8_t toolTypes[5] = {TOOL_PICKAXE, TOOL_AXE, TOOL_SHOVEL, TOOL_SWORD, TOOL_HOE};
    const float dmgOffset[5] = {-2, -1, -2.5f, 0, -3};
    const float cooldown[5] = {0.83f, 1.0f, 1.0f, 0.62f, 0.5f};
    for (int m = 0; m < 5; m++)
        for (int k = 0; k < 5; k++) {
            snprintf(nameBuf[m * 5 + k], 32, "%s %s", matNames[m], kindNames[k]);
            ItemDef& d = item(toolItem(m, k), nameBuf[m * 5 + k], TX_TOOLS_FIRST + m * 5 + k, 1);
            d.maxDamage = durability[m]; d.toolType = toolTypes[k]; d.toolTier = tier[m]; d.toolSpeed = speed[m];
            d.attackDamage = std::max(1.0f, swordDmg[m] + dmgOffset[k] + (k == KIND_AXE ? 2.0f : 0.0f));
            d.attackCooldown = cooldown[k];
            if (m == MAT_WOOD) d.fuelTicks = 200;
        }
    auto foodItem = [&](uint16_t id, const char* n, uint16_t icon, int hunger, float sat) { ItemDef& d = item(id, n, icon); d.foodHunger = (uint8_t)hunger; d.foodSaturation = sat; };
    foodItem(APPLE, "Apple", TX_APPLE, 4, 2.4f);
    foodItem(RAW_PORK, "Raw Porkchop", TX_RAW_PORK, 3, 1.8f);
    foodItem(COOKED_PORK, "Cooked Porkchop", TX_COOKED_PORK, 8, 12.8f);
    foodItem(RAW_BEEF, "Raw Beef", TX_RAW_BEEF, 3, 1.8f);
    foodItem(STEAK, "Steak", TX_STEAK, 8, 12.8f);
    foodItem(RAW_CHICKEN, "Raw Chicken", TX_RAW_CHICKEN, 2, 1.2f);
    foodItem(COOKED_CHICKEN, "Cooked Chicken", TX_COOKED_CHICKEN, 6, 7.2f);
    foodItem(RAW_MUTTON, "Raw Mutton", TX_RAW_MUTTON, 2, 1.2f);
    foodItem(COOKED_MUTTON, "Cooked Mutton", TX_COOKED_MUTTON, 6, 9.6f);
    foodItem(BREAD, "Bread", TX_BREAD, 5, 6.0f);
    foodItem(ROTTEN_FLESH, "Rotten Flesh", TX_ROTTEN_FLESH, 4, 0.8f);
    item(WHEAT_ITEM, "Wheat", TX_WHEAT_ITEM);
    item(SEEDS, "Seeds", TX_SEEDS).placesBlock = WHEAT;
    item(BONE, "Bone", TX_BONE);
    item(ARROW, "Arrow", TX_ARROW);
    { ItemDef& d = item(BOW, "Bow", TX_BOW, 1); d.maxDamage = 384; d.fuelTicks = 300; }
    item(STRING, "String", TX_STRING);
    item(GUNPOWDER, "Gunpowder", TX_GUNPOWDER);
    item(FEATHER, "Feather", TX_FEATHER);
    item(LEATHER, "Leather", TX_LEATHER);
    item(CLAY_BALL, "Clay Ball", TX_CLAY_BALL);
    item(BRICK, "Brick", TX_BRICK);
    item(FLINT, "Flint", TX_FLINT);
    item(BED_ITEM, "Bed", TX_BED_ITEM, 1).placesBlock = BED;
    item(DOOR_ITEM, "Door", TX_DOOR_ITEM, 16).placesBlock = DOOR;
    // Blocks that aren't placed as themselves
    g_items[BED].name = "Bed"; g_items[DOOR].name = "Door"; g_items[WHEAT].name = "Wheat Crops";
}
