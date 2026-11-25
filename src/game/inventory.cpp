#include "inventory.h"
#include <algorithm>

bool mergeInto(ItemStack& dst, ItemStack& src, int maxMove) {
    if (src.empty()) return false;
    if (dst.empty()) {
        int n = std::min({(int)src.count, src.maxStack(), maxMove});
        dst = src; dst.count = (uint8_t)n;
        src.count = (uint8_t)(src.count - n);
        if (src.count == 0) src.clear();
        return n > 0;
    }
    if (!dst.sameItem(src) || itemDef(dst.id).maxStack <= 1) return false;
    int space = dst.maxStack() - dst.count;
    int n = std::min({space, (int)src.count, maxMove});
    if (n <= 0) return false;
    dst.count = (uint8_t)(dst.count + n);
    src.count = (uint8_t)(src.count - n);
    if (src.count == 0) src.clear();
    return true;
}

int Inventory::add(ItemStack st) {
    if (st.empty()) return 0;
    // existing stacks first (hotbar then main), then empty slots
    for (int pass = 0; pass < 2 && !st.empty(); pass++)
        for (int i = 0; i < SIZE && !st.empty(); i++) {
            ItemStack& s = slots[i];
            if (pass == 0 && !s.empty() && s.sameItem(st)) mergeInto(s, st);
            else if (pass == 1 && s.empty()) mergeInto(s, st);
        }
    return st.empty() ? 0 : st.count;
}

int Inventory::count(uint16_t id) const {
    int n = 0;
    for (auto& s : slots) if (s.id == id) n += s.count;
    return n;
}

bool Inventory::removeItem(uint16_t id, int n) {
    if (count(id) < n) return false;
    for (int i = SIZE - 1; i >= 0 && n > 0; i--) {
        ItemStack& s = slots[i];
        if (s.id != id) continue;
        int k = std::min(n, (int)s.count);
        s.count = (uint8_t)(s.count - k); n -= k;
        if (s.count == 0) s.clear();
    }
    return true;
}
