// Block registry: every block type is defined once in a table (blocks.cpp).
#pragma once
#include <cstdint>

enum BlockId : uint16_t {
    AIR = 0, STONE, DIRT, GRASS, SAND, GRAVEL, CLAY, COBBLESTONE, BEDROCK,
    OAK_LOG, OAK_LEAVES, BIRCH_LOG, BIRCH_LEAVES, SPRUCE_LOG, SPRUCE_LEAVES,
    OAK_PLANKS, BIRCH_PLANKS, SPRUCE_PLANKS, GLASS,
    COAL_ORE, IRON_ORE, GOLD_ORE, DIAMOND_ORE,
    WATER, LAVA, TORCH, CRAFTING_TABLE, FURNACE, FURNACE_LIT, CHEST, BED, DOOR, WHEAT, FARMLAND,
    WOOL_WHITE, WOOL_GRAY, WOOL_BLACK, WOOL_BROWN, SNOW, ICE, CACTUS, TALL_GRASS, FLOWER_RED, FLOWER_YELLOW,
    DEAD_BUSH, SANDSTONE, STONE_BRICKS, IRON_BLOCK, GOLD_BLOCK, DIAMOND_BLOCK, COAL_BLOCK, BRICKS,
    MOSSY_COBBLE, SNOWY_GRASS, LADDER, OAK_SAPLING, BIRCH_SAPLING, SPRUCE_SAPLING, OBSIDIAN,
    BLOCK_COUNT
};

// Face order used everywhere: +X, -X, +Y, -Y, +Z, -Z
enum Face { FACE_PX = 0, FACE_NX, FACE_PY, FACE_NY, FACE_PZ, FACE_NZ };

enum Shape : uint8_t {
    SHAPE_NONE, SHAPE_CUBE, SHAPE_CROSS, SHAPE_LIQUID, SHAPE_TORCH, SHAPE_DOOR, SHAPE_BED,
    SHAPE_CACTUS, SHAPE_LADDER, SHAPE_CROP, SHAPE_FARMLAND, SHAPE_CHEST
};
enum RenderLayer : uint8_t { LAYER_OPAQUE = 0, LAYER_CUTOUT = 1, LAYER_WATER = 2 };
enum ToolType : uint8_t { TOOL_NONE = 0, TOOL_PICKAXE, TOOL_AXE, TOOL_SHOVEL, TOOL_SWORD, TOOL_HOE };
enum SoundType : uint8_t { SND_STONE, SND_WOOD, SND_GRASS, SND_SAND, SND_GRAVEL, SND_GLASS, SND_WOOL, SND_SNOW, SND_METAL };

struct BlockDef {
    const char* name = "?";
    uint16_t tex[6] = {0, 0, 0, 0, 0, 0};  // texture layer per face
    float hardness = 0;        // seconds-ish; <0 = unbreakable
    float blastResist = 0;
    uint8_t tool = TOOL_NONE;  // best tool
    int8_t harvestLevel = -1;  // -1: drops with anything; >=0 needs a pickaxe of that tier
    bool solid = false;        // has collision
    bool opaque = false;       // full opaque cube: hides faces and blocks light
    bool liquid = false;
    bool gravity = false;      // falls like sand
    bool replaceable = false;  // placing a block overwrites it (air, water, grass)
    bool needsSupport = false; // breaks when the block under it is removed
    bool climbable = false;
    uint8_t shape = SHAPE_NONE;
    uint8_t layer = LAYER_OPAQUE;
    uint8_t lightEmit = 0;     // 0..15
    uint8_t lightOpacity = 0;  // extra light loss when passing through (opaque = 15)
    uint8_t sound = SND_STONE;
    uint16_t drop = 0xFFFF;    // item dropped (0xFFFF = itself, 0 = nothing)
    uint8_t dropMin = 1, dropMax = 1;
    uint32_t mapColor = 0;     // unused except debug
};

extern BlockDef g_blocks[BLOCK_COUNT];
void initBlocks();
inline const BlockDef& blockDef(uint16_t id) { return g_blocks[id < BLOCK_COUNT ? id : 0]; }
inline bool isOpaque(uint16_t id) { return g_blocks[id].opaque; }
inline bool isSolid(uint16_t id) { return g_blocks[id].solid; }
inline bool isLiquid(uint16_t id) { return g_blocks[id].liquid; }

// Block metadata helpers
// Liquids: meta = level (0 = source, 1..7 flowing), bit 3 = falling
// Torch/ladder/furnace/chest/door/bed: low 2-3 bits = facing (0=+Z south,1=-X west,2=-Z north,3=+X east)
// Door: bit2 = open, bit3 = top half. Bed: bit2 = head part. Torch: 0=floor,1..4 = wall facing+1.
// Crops & saplings: meta = growth stage.
inline int facingDX(int f) { static const int d[4] = {0, -1, 0, 1}; return d[f & 3]; }
inline int facingDZ(int f) { static const int d[4] = {1, 0, -1, 0}; return d[f & 3]; }
