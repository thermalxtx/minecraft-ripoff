// The block table. Every block is defined here once.
#include "block.h"
#include "../render/textures.h"
#include "../game/items.h"

BlockDef g_blocks[BLOCK_COUNT];

namespace {
struct B {
    BlockDef& d;
    explicit B(uint16_t id, const char* name) : d(g_blocks[id]) {
        d = BlockDef(); d.name = name; d.solid = true; d.opaque = true; d.shape = SHAPE_CUBE; d.lightOpacity = 15;
    }
    B& tex(uint16_t all) { for (auto& t : d.tex) t = all; return *this; }
    B& tex(uint16_t side, uint16_t top, uint16_t bottom) { d.tex[0] = d.tex[1] = d.tex[4] = d.tex[5] = side; d.tex[2] = top; d.tex[3] = bottom; return *this; }
    B& hard(float h, float blast = -1) { d.hardness = h; d.blastResist = blast < 0 ? h * 3 : blast; return *this; }
    B& tool(uint8_t t, int lvl = -1) { d.tool = t; d.harvestLevel = (int8_t)lvl; return *this; }
    B& sound(uint8_t s) { d.sound = s; return *this; }
    B& drop(uint16_t item, int mn = 1, int mx = 1) { d.drop = item; d.dropMin = (uint8_t)mn; d.dropMax = (uint8_t)mx; return *this; }
    B& transparent(uint8_t layer, uint8_t opacity = 0) { d.opaque = false; d.layer = layer; d.lightOpacity = opacity; return *this; }
    B& shape(uint8_t s) { d.shape = s; return *this; }
    B& nonSolid() { d.solid = false; return *this; }
    B& light(uint8_t l) { d.lightEmit = l; return *this; }
    B& gravity() { d.gravity = true; return *this; }
    B& replaceable() { d.replaceable = true; return *this; }
    B& support() { d.needsSupport = true; return *this; }
};
}

void initBlocks() {
    BlockDef& air = g_blocks[AIR];
    air = BlockDef(); air.name = "Air"; air.replaceable = true; air.drop = 0; air.hardness = -1;

    B(STONE, "Stone").tex(TX_STONE).hard(1.5f, 6).tool(TOOL_PICKAXE, 0).drop(COBBLESTONE);
    B(DIRT, "Dirt").tex(TX_DIRT).hard(0.5f).tool(TOOL_SHOVEL).sound(SND_GRAVEL);
    B(GRASS, "Grass Block").tex(TX_GRASS_SIDE, TX_GRASS_TOP, TX_DIRT).hard(0.6f).tool(TOOL_SHOVEL).sound(SND_GRASS).drop(DIRT);
    B(SAND, "Sand").tex(TX_SAND).hard(0.5f).tool(TOOL_SHOVEL).sound(SND_SAND).gravity();
    B(GRAVEL, "Gravel").tex(TX_GRAVEL).hard(0.6f).tool(TOOL_SHOVEL).sound(SND_GRAVEL).gravity();
    B(CLAY, "Clay").tex(TX_CLAY).hard(0.6f).tool(TOOL_SHOVEL).sound(SND_GRAVEL).drop(CLAY_BALL, 4, 4);
    B(COBBLESTONE, "Cobblestone").tex(TX_COBBLE).hard(2, 6).tool(TOOL_PICKAXE, 0);
    B(BEDROCK, "Bedrock").tex(TX_BEDROCK).hard(-1, 1e9f).drop(0);
    B(OAK_LOG, "Oak Log").tex(TX_OAK_LOG, TX_OAK_LOG_TOP, TX_OAK_LOG_TOP).hard(2).tool(TOOL_AXE).sound(SND_WOOD);
    B(OAK_LEAVES, "Oak Leaves").tex(TX_OAK_LEAVES).hard(0.2f).transparent(LAYER_CUTOUT, 1).sound(SND_GRASS).drop(0).tool(TOOL_HOE);
    B(BIRCH_LOG, "Birch Log").tex(TX_BIRCH_LOG, TX_BIRCH_LOG_TOP, TX_BIRCH_LOG_TOP).hard(2).tool(TOOL_AXE).sound(SND_WOOD);
    B(BIRCH_LEAVES, "Birch Leaves").tex(TX_BIRCH_LEAVES).hard(0.2f).transparent(LAYER_CUTOUT, 1).sound(SND_GRASS).drop(0).tool(TOOL_HOE);
    B(SPRUCE_LOG, "Spruce Log").tex(TX_SPRUCE_LOG, TX_SPRUCE_LOG_TOP, TX_SPRUCE_LOG_TOP).hard(2).tool(TOOL_AXE).sound(SND_WOOD);
    B(SPRUCE_LEAVES, "Spruce Leaves").tex(TX_SPRUCE_LEAVES).hard(0.2f).transparent(LAYER_CUTOUT, 1).sound(SND_GRASS).drop(0).tool(TOOL_HOE);
    B(OAK_PLANKS, "Oak Planks").tex(TX_OAK_PLANKS).hard(2).tool(TOOL_AXE).sound(SND_WOOD);
    B(BIRCH_PLANKS, "Birch Planks").tex(TX_BIRCH_PLANKS).hard(2).tool(TOOL_AXE).sound(SND_WOOD);
    B(SPRUCE_PLANKS, "Spruce Planks").tex(TX_SPRUCE_PLANKS).hard(2).tool(TOOL_AXE).sound(SND_WOOD);
    B(GLASS, "Glass").tex(TX_GLASS).hard(0.3f).transparent(LAYER_CUTOUT).sound(SND_GLASS).drop(0);
    B(COAL_ORE, "Coal Ore").tex(TX_COAL_ORE).hard(3, 9).tool(TOOL_PICKAXE, 0).drop(COAL);
    B(IRON_ORE, "Iron Ore").tex(TX_IRON_ORE).hard(3, 9).tool(TOOL_PICKAXE, 1);
    B(GOLD_ORE, "Gold Ore").tex(TX_GOLD_ORE).hard(3, 9).tool(TOOL_PICKAXE, 2);
    B(DIAMOND_ORE, "Diamond Ore").tex(TX_DIAMOND_ORE).hard(3, 9).tool(TOOL_PICKAXE, 2).drop(DIAMOND);
    B(WATER, "Water").tex(TX_WATER).hard(-1, 500).transparent(LAYER_WATER, 2).nonSolid().shape(SHAPE_LIQUID).replaceable().drop(0);
    g_blocks[WATER].liquid = true;
    B(LAVA, "Lava").tex(TX_LAVA).hard(-1, 500).transparent(LAYER_OPAQUE, 15).nonSolid().shape(SHAPE_LIQUID).replaceable().light(15).drop(0);
    g_blocks[LAVA].liquid = true; g_blocks[LAVA].lightOpacity = 15;
    B(TORCH, "Torch").tex(TX_TORCH).hard(0).transparent(LAYER_CUTOUT).nonSolid().shape(SHAPE_TORCH).light(14).sound(SND_WOOD).support();
    B(CRAFTING_TABLE, "Crafting Table").tex(TX_CRAFT_SIDE, TX_CRAFT_TOP, TX_OAK_PLANKS).hard(2.5f).tool(TOOL_AXE).sound(SND_WOOD);
    g_blocks[CRAFTING_TABLE].tex[FACE_PZ] = TX_CRAFT_FRONT; g_blocks[CRAFTING_TABLE].tex[FACE_NZ] = TX_CRAFT_FRONT;
    B(FURNACE, "Furnace").tex(TX_FURNACE_SIDE, TX_FURNACE_TOP, TX_FURNACE_TOP).hard(3.5f).tool(TOOL_PICKAXE, 0);
    B(FURNACE_LIT, "Lit Furnace").tex(TX_FURNACE_SIDE, TX_FURNACE_TOP, TX_FURNACE_TOP).hard(3.5f).tool(TOOL_PICKAXE, 0).light(13).drop(FURNACE);
    B(CHEST, "Chest").tex(TX_CHEST_SIDE, TX_CHEST_TOP, TX_CHEST_TOP).hard(2.5f).tool(TOOL_AXE).sound(SND_WOOD).transparent(LAYER_OPAQUE).shape(SHAPE_CHEST);
    B(BED, "Bed").tex(TX_BED_SIDE_FOOT).hard(0.2f).sound(SND_WOOL).transparent(LAYER_CUTOUT).shape(SHAPE_BED).drop(BED_ITEM);
    B(DOOR, "Door").tex(TX_DOOR_BOTTOM).hard(3).tool(TOOL_AXE).sound(SND_WOOD).transparent(LAYER_CUTOUT).shape(SHAPE_DOOR).drop(DOOR_ITEM).support();
    B(WHEAT, "Wheat Crops").tex(TX_WHEAT0).hard(0).sound(SND_GRASS).transparent(LAYER_CUTOUT).nonSolid().shape(SHAPE_CROP).support().drop(0);
    B(FARMLAND, "Farmland").tex(TX_DIRT, TX_FARMLAND, TX_DIRT).hard(0.6f).tool(TOOL_SHOVEL).sound(SND_GRAVEL).transparent(LAYER_OPAQUE).shape(SHAPE_FARMLAND).drop(DIRT);
    B(WOOL_WHITE, "White Wool").tex(TX_WOOL_WHITE).hard(0.8f).sound(SND_WOOL);
    B(WOOL_GRAY, "Gray Wool").tex(TX_WOOL_GRAY).hard(0.8f).sound(SND_WOOL);
    B(WOOL_BLACK, "Black Wool").tex(TX_WOOL_BLACK).hard(0.8f).sound(SND_WOOL);
    B(WOOL_BROWN, "Brown Wool").tex(TX_WOOL_BROWN).hard(0.8f).sound(SND_WOOL);
    B(SNOW, "Snow").tex(TX_SNOW).hard(0.2f).tool(TOOL_SHOVEL).sound(SND_SNOW);
    B(ICE, "Ice").tex(TX_ICE).hard(0.5f).tool(TOOL_PICKAXE).sound(SND_GLASS).drop(0);
    B(CACTUS, "Cactus").tex(TX_CACTUS_SIDE, TX_CACTUS_TOP, TX_CACTUS_TOP).hard(0.4f).sound(SND_WOOL).transparent(LAYER_CUTOUT).shape(SHAPE_CACTUS).support();
    B(TALL_GRASS, "Tall Grass").tex(TX_TALL_GRASS).hard(0).sound(SND_GRASS).transparent(LAYER_CUTOUT).nonSolid().shape(SHAPE_CROSS).replaceable().support().drop(0);
    B(FLOWER_RED, "Red Flower").tex(TX_FLOWER_RED).hard(0).sound(SND_GRASS).transparent(LAYER_CUTOUT).nonSolid().shape(SHAPE_CROSS).support();
    B(FLOWER_YELLOW, "Yellow Flower").tex(TX_FLOWER_YELLOW).hard(0).sound(SND_GRASS).transparent(LAYER_CUTOUT).nonSolid().shape(SHAPE_CROSS).support();
    B(DEAD_BUSH, "Dead Bush").tex(TX_DEAD_BUSH).hard(0).sound(SND_GRASS).transparent(LAYER_CUTOUT).nonSolid().shape(SHAPE_CROSS).replaceable().support().drop(STICK, 0, 2);
    B(SANDSTONE, "Sandstone").tex(TX_SANDSTONE_SIDE, TX_SANDSTONE_TOP, TX_SANDSTONE_TOP).hard(0.8f).tool(TOOL_PICKAXE, 0);
    B(STONE_BRICKS, "Stone Bricks").tex(TX_STONE_BRICKS).hard(1.5f, 6).tool(TOOL_PICKAXE, 0);
    B(IRON_BLOCK, "Block of Iron").tex(TX_IRON_BLOCK).hard(5, 18).tool(TOOL_PICKAXE, 1).sound(SND_METAL);
    B(GOLD_BLOCK, "Block of Gold").tex(TX_GOLD_BLOCK).hard(3, 18).tool(TOOL_PICKAXE, 2).sound(SND_METAL);
    B(DIAMOND_BLOCK, "Block of Diamond").tex(TX_DIAMOND_BLOCK).hard(5, 18).tool(TOOL_PICKAXE, 2).sound(SND_METAL);
    B(COAL_BLOCK, "Block of Coal").tex(TX_COAL_BLOCK).hard(5, 18).tool(TOOL_PICKAXE, 0);
    B(BRICKS, "Bricks").tex(TX_BRICKS).hard(2, 18).tool(TOOL_PICKAXE, 0);
    B(MOSSY_COBBLE, "Mossy Cobblestone").tex(TX_MOSSY_COBBLE).hard(2, 6).tool(TOOL_PICKAXE, 0);
    B(SNOWY_GRASS, "Snowy Grass").tex(TX_SNOWY_GRASS_SIDE, TX_SNOW, TX_DIRT).hard(0.6f).tool(TOOL_SHOVEL).sound(SND_SNOW).drop(DIRT);
    B(LADDER, "Ladder").tex(TX_LADDER).hard(0.4f).tool(TOOL_AXE).sound(SND_WOOD).transparent(LAYER_CUTOUT).nonSolid().shape(SHAPE_LADDER);
    g_blocks[LADDER].climbable = true;
    B(OAK_SAPLING, "Oak Sapling").tex(TX_OAK_SAPLING).hard(0).sound(SND_GRASS).transparent(LAYER_CUTOUT).nonSolid().shape(SHAPE_CROSS).support();
    B(BIRCH_SAPLING, "Birch Sapling").tex(TX_BIRCH_SAPLING).hard(0).sound(SND_GRASS).transparent(LAYER_CUTOUT).nonSolid().shape(SHAPE_CROSS).support();
    B(SPRUCE_SAPLING, "Spruce Sapling").tex(TX_SPRUCE_SAPLING).hard(0).sound(SND_GRASS).transparent(LAYER_CUTOUT).nonSolid().shape(SHAPE_CROSS).support();
    B(OBSIDIAN, "Obsidian").tex(TX_OBSIDIAN).hard(50, 6000).tool(TOOL_PICKAXE, 3);
}
