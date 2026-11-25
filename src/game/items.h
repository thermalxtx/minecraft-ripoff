// Item registry. Item ids 1..255 are block items (same id as the block); 256+ are pure items.
#pragma once
#include <cstdint>
#include "../world/block.h"

enum ItemId : uint16_t {
    ITEM_FIRST = 256,
    STICK = 256, COAL, IRON_INGOT, GOLD_INGOT, DIAMOND,
    // tools: material-major (wood, stone, iron, gold, diamond) x (pickaxe, axe, shovel, sword, hoe)
    TOOL_FIRST, TOOL_LAST = TOOL_FIRST + 24,
    APPLE, RAW_PORK, COOKED_PORK, RAW_BEEF, STEAK, RAW_CHICKEN, COOKED_CHICKEN, RAW_MUTTON, COOKED_MUTTON,
    BREAD, WHEAT_ITEM, SEEDS, ROTTEN_FLESH, BONE, ARROW, BOW, STRING, GUNPOWDER, FEATHER, LEATHER,
    CLAY_BALL, BRICK, FLINT, BED_ITEM, DOOR_ITEM,
    ITEM_END
};
enum ToolMaterial { MAT_WOOD = 0, MAT_STONE, MAT_IRON, MAT_GOLD, MAT_DIAMOND };
enum ToolKind { KIND_PICKAXE = 0, KIND_AXE, KIND_SHOVEL, KIND_SWORD, KIND_HOE };
inline uint16_t toolItem(int mat, int kind) { return (uint16_t)(TOOL_FIRST + mat * 5 + kind); }

struct ItemDef {
    const char* name = "";
    uint16_t icon = 0;         // texture layer
    uint8_t maxStack = 64;
    uint16_t maxDamage = 0;    // durability (0 = none)
    uint8_t toolType = TOOL_NONE;
    int8_t toolTier = -1;      // harvest level
    float toolSpeed = 1.0f;    // mining speed multiplier on matching blocks
    float attackDamage = 1.0f; // in half-hearts
    float attackCooldown = 0.25f; // seconds for a full-strength swing
    uint8_t foodHunger = 0;
    float foodSaturation = 0;
    uint16_t fuelTicks = 0;    // furnace burn time
    uint16_t placesBlock = 0;  // block placed on use (0 = none)
    bool isBlock = false;
};

extern ItemDef g_items[ITEM_END];
void initItems();
inline const ItemDef& itemDef(uint16_t id) { return g_items[id < ITEM_END ? id : 0]; }
const char* itemName(uint16_t id);

struct ItemStack {
    uint16_t id = 0;
    uint8_t count = 0;
    uint16_t damage = 0;
    bool empty() const { return id == 0 || count == 0; }
    void clear() { id = 0; count = 0; damage = 0; }
    ItemStack() = default;
    ItemStack(uint16_t i, int c, int d = 0) : id(i), count((uint8_t)c), damage((uint16_t)d) {}
    bool sameItem(const ItemStack& o) const { return id == o.id && damage == o.damage; }
    int maxStack() const { return itemDef(id).maxStack; }
};
