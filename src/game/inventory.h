// Player inventory: 9 hotbar slots (0..8) + 27 main slots (9..35).
#pragma once
#include "items.h"

struct Inventory {
    static constexpr int SIZE = 36;
    ItemStack slots[SIZE];
    int selected = 0;
    ItemStack& held() { return slots[selected]; }
    // Adds as much as possible; returns how many items did not fit
    int add(ItemStack st);
    int count(uint16_t id) const;
    bool removeItem(uint16_t id, int n);
    void clear() { for (auto& s : slots) s.clear(); }
};

// Merge `src` into slot `dst` (same item); returns true if anything moved
bool mergeInto(ItemStack& dst, ItemStack& src, int maxMove = 255);
