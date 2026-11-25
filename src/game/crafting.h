// Shaped crafting recipes (data table), furnace smelting and fuel.
#pragma once
#include "items.h"
#include <vector>

// Wildcards usable in recipe patterns
constexpr uint16_t ANY_PLANKS = 0xFFF0;
constexpr uint16_t ANY_LOG = 0xFFF1;
constexpr uint16_t ANY_WOOL = 0xFFF2;

struct Recipe {
    int w = 0, h = 0;
    uint16_t in[9] = {};
    ItemStack out;
};

void initRecipes();
const std::vector<Recipe>& allRecipes();
// grid is gridSize x gridSize (2 or 3), row-major. Returns the result or empty.
ItemStack matchRecipe(const ItemStack* grid, int gridSize);
// Remove one of each ingredient after crafting
void consumeIngredients(ItemStack* grid, int gridSize);

ItemStack smeltResult(uint16_t input);
int fuelTicks(uint16_t id);
