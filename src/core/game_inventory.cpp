// Inventory, crafting table, furnace and chest screens with Minecraft-style slot handling:
// click to pick up/place/swap, right-click to split or place one, shift-click to move, drag to spread.
#include "game.h"
#include "platform.h"
#include "../game/crafting.h"
#include "../render/textures.h"
#include "../audio/audio.h"

static const float PW = 176, PH = 166;

void Game::buildSlots(std::vector<SlotRef>& s, float ox, float oy) {
    s.clear();
    // player inventory (main rows then hotbar)
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 9; c++) s.push_back({&player.inv.slots[9 + r * 9 + c], ox + 8 + c * 18, oy + 84 + r * 18, 0, 1});
    for (int c = 0; c < 9; c++) s.push_back({&player.inv.slots[c], ox + 8 + c * 18, oy + 142, 0, 0});
    switch (screen) {
    case Screen::INVENTORY:
        for (int r = 0; r < 2; r++) for (int c = 0; c < 2; c++) s.push_back({&craftGrid[r * 2 + c], ox + 98 + c * 18, oy + 18 + r * 18, 0, 2});
        s.push_back({&craftResult, ox + 154, oy + 28, 1, 2});
        break;
    case Screen::CRAFTING:
        for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) s.push_back({&craftGrid[r * 3 + c], ox + 30 + c * 18, oy + 17 + r * 18, 0, 2});
        s.push_back({&craftResult, ox + 124, oy + 35, 1, 2});
        break;
    case Screen::FURNACE: {
        TileEntity* t = world->getTile(containerPos.x, containerPos.y, containerPos.z);
        if (!t) break;
        s.push_back({&t->slots[0], ox + 56, oy + 17, 0, 3});
        s.push_back({&t->slots[1], ox + 56, oy + 53, 0, 4});
        s.push_back({&t->slots[2], ox + 116, oy + 35, 2, 2});
        break;
    }
    case Screen::CHEST: {
        TileEntity* t = world->getTile(containerPos.x, containerPos.y, containerPos.z);
        if (!t) break;
        for (int r = 0; r < 3; r++) for (int c = 0; c < 9; c++) s.push_back({&t->slots[r * 9 + c], ox + 8 + c * 18, oy + 18 + r * 18, 0, 2});
        break;
    }
    default: break;
    }
}

void Game::updateCraftResult() {
    if (screen == Screen::INVENTORY) {
        ItemStack g3[4] = {craftGrid[0], craftGrid[1], craftGrid[2], craftGrid[3]};
        craftResult = matchRecipe(g3, 2);
    } else if (screen == Screen::CRAFTING) craftResult = matchRecipe(craftGrid, 3);
}

static void consumeCraft(ItemStack* grid, bool small) {
    if (small) { ItemStack g[4] = {grid[0], grid[1], grid[2], grid[3]}; consumeIngredients(g, 2); for (int i = 0; i < 4; i++) grid[i] = g[i]; }
    else consumeIngredients(grid, 3);
}

// Move a stack into the given slot group (merge first, then empty slots)
bool Game::moveInto(ItemStack& st, std::vector<SlotRef>& slots, int group) {
    bool moved = false;
    for (int pass = 0; pass < 2 && !st.empty(); pass++)
        for (auto& sr : slots) {
            if (sr.group != group || sr.kind != 0 || st.empty()) continue;
            if (pass == 0 && !sr.st->empty() && sr.st->sameItem(st)) moved |= mergeInto(*sr.st, st);
            else if (pass == 1 && sr.st->empty()) moved |= mergeInto(*sr.st, st);
        }
    return moved;
}

void Game::shiftMove(std::vector<SlotRef>& slots, int idx) {
    SlotRef& sr = slots[idx];
    ItemStack& st = *sr.st;
    if (st.empty()) return;
    bool container = screen == Screen::CHEST || screen == Screen::FURNACE;
    if (sr.group >= 2) {  // from container/grid to the player (hotbar first like pickups)
        moveInto(st, slots, 0);
        moveInto(st, slots, 1);
        return;
    }
    if (screen == Screen::CHEST) { moveInto(st, slots, 2); if (!st.empty()) moveInto(st, slots, sr.group == 0 ? 1 : 0); return; }
    if (screen == Screen::FURNACE) {
        if (!smeltResult(st.id).empty()) moveInto(st, slots, 3);
        else if (fuelTicks(st.id) > 0) moveInto(st, slots, 4);
        if (!st.empty()) moveInto(st, slots, sr.group == 0 ? 1 : 0);
        return;
    }
    (void)container;
    moveInto(st, slots, sr.group == 0 ? 1 : 0);
}

void Game::slotClick(std::vector<SlotRef>& slots, int idx, int button, bool shift) {
    SlotRef& sr = slots[idx];
    ItemStack& st = *sr.st;
    if (sr.kind == 1) {  // crafting result
        if (craftResult.empty()) return;
        bool small = screen == Screen::INVENTORY;
        if (shift) {
            for (int guard = 0; guard < 64 && !craftResult.empty(); guard++) {
                ItemStack r = craftResult;
                // only craft if it all fits
                Inventory test = player.inv;
                if (test.add(r) > 0) break;
                player.inv.add(r);
                consumeCraft(craftGrid, small);
                updateCraftResult();
            }
        } else {
            if (cursor.empty()) { cursor = craftResult; }
            else if (cursor.sameItem(craftResult) && cursor.count + craftResult.count <= cursor.maxStack()) cursor.count = (uint8_t)(cursor.count + craftResult.count);
            else return;
            consumeCraft(craftGrid, small);
        }
        audio::play2D(SFX_POP, 0.3f, 1.4f);
        updateCraftResult();
        return;
    }
    if (shift) { shiftMove(slots, idx); return; }
    if (sr.kind == 2) {  // output only: take
        if (st.empty()) return;
        if (cursor.empty()) { cursor = st; st.clear(); }
        else if (cursor.sameItem(st)) mergeInto(cursor, st);
        return;
    }
    if (button == 0) {
        if (cursor.empty()) { cursor = st; st.clear(); }
        else if (st.empty()) { st = cursor; cursor.clear(); }
        else if (st.sameItem(cursor)) { mergeInto(st, cursor); }
        else std::swap(st, cursor);
    } else {
        if (cursor.empty()) {
            if (st.empty()) return;
            int half = (st.count + 1) / 2;
            cursor = st; cursor.count = (uint8_t)half;
            st.count = (uint8_t)(st.count - half);
            if (st.count == 0) st.clear();
        } else if (st.empty() || st.sameItem(cursor)) {
            mergeInto(st, cursor, 1);
        } else std::swap(st, cursor);
    }
}

void Game::screenContainer() {
    if (screenAge > 1 && (plat::keyPressed(KEY_ESCAPE) || plat::keyPressed(KEY_E))) { closeContainer(); return; }
    if ((screen == Screen::FURNACE || screen == Screen::CHEST) && !world->getTile(containerPos.x, containerPos.y, containerPos.z)) { closeContainer(); return; }
    if (screen != Screen::INVENTORY && length(vec3(containerPos.x + 0.5f, containerPos.y + 0.5f, containerPos.z + 0.5f) - player.pos) > 8) { closeContainer(); return; }
    ui.rect(0, 0, ui.W, ui.H, 0x90101010);
    float ox = std::floor(ui.W / 2 - PW / 2), oy = std::floor(ui.H / 2 - PH / 2);
    ui.panel(ox, oy, PW, PH);
    std::vector<SlotRef> slots;
    buildSlots(slots, ox, oy);
    // titles & decorations
    const char* title = screen == Screen::INVENTORY ? "Crafting" : screen == Screen::CRAFTING ? "Crafting" : screen == Screen::FURNACE ? "Furnace" : "Chest";
    float tx = screen == Screen::INVENTORY ? ox + 97 : ox + 8;
    ui.text(tx, oy + 6, title, 0xFF404040, false);
    ui.text(ox + 8, oy + 73, "Inventory", 0xFF404040, false);
    if (screen == Screen::INVENTORY) {
        // little portrait of the player
        ui.rect(ox + 25, oy + 7, 52, 63, 0xFF000000);
        ui.rect(ox + 26, oy + 8, 50, 61, 0xFF6E6E6E);
        float px = ox + 41, py = oy + 10;
        ui.image(px, py, 20, 20, TX_PLAYER_FACE);
        ui.image(px, py + 20, 20, 22, TX_PLAYER_SHIRT);
        ui.image(px - 7, py + 20, 7, 20, TX_PLAYER_SKIN);
        ui.image(px + 20, py + 20, 7, 20, TX_PLAYER_SKIN);
        ui.image(px, py + 42, 10, 15, TX_PLAYER_PANTS);
        ui.image(px + 10, py + 42, 10, 15, TX_PLAYER_PANTS);
        ui.text(ox + 134, oy + 32, "->", 0xFF404040, false);
    }
    if (screen == Screen::CRAFTING) ui.text(ox + 92, oy + 39, "-->", 0xFF404040, false, 1.5f);
    if (screen == Screen::FURNACE) {
        TileEntity* t = world->getTile(containerPos.x, containerPos.y, containerPos.z);
        if (t) {
            float burn = t->burnTotal > 0 ? (float)t->burnTime / t->burnTotal : 0;
            ui.image(ox + 57, oy + 36, 14, 14, TX_FLAME, rgba(0.3f, 0.3f, 0.3f));
            if (burn > 0) ui.image(ox + 57, oy + 36 + 14 * (1 - burn), 14, 14 * burn, TX_FLAME, 0xFFFFFFFF, 0, 1 - burn, 1, 1);
            float cook = t->cookTime / 200.0f;
            ui.rect(ox + 79, oy + 39, 24, 6, 0xFF8B8B8B);
            ui.rect(ox + 79, oy + 39, 24 * cook, 6, 0xFFFFFFFF);
            ui.rect(ox + 103, oy + 37, 2, 10, 0xFF8B8B8B);
        }
    }
    // slots
    int hover = -1;
    for (size_t i = 0; i < slots.size(); i++) {
        auto& sr = slots[i];
        bool big = sr.kind != 0 && (screen == Screen::CRAFTING || screen == Screen::FURNACE);
        float sz = big ? 26 : 18, off = big ? -4 : -1;
        ui.slot(sr.x + off, sr.y + off, sz);
        ui.itemIcon(sr.x, sr.y, 16, *sr.st);
        if (ui.hovered(sr.x - 1, sr.y - 1, 18, 18)) { hover = (int)i; ui.rect(sr.x, sr.y, 16, 16, 0x60FFFFFF); }
    }
    for (int i : dragSlots) if (i < (int)slots.size()) ui.rect(slots[i].x, slots[i].y, 16, 16, 0x50FFFFFF);
    bool shift = plat::keyDown(KEY_SHIFT);
    // mouse handling
    if ((ui.clicked || ui.rclicked) && hover >= 0) {
        int btn = ui.clicked ? 0 : 1;
        SlotRef& sr = slots[hover];
        bool canDrag = !shift && !cursor.empty() && sr.kind == 0 && (sr.st->empty() || sr.st->sameItem(cursor));
        if (canDrag) { dragging = true; dragButton = btn; dragSlots.clear(); dragSlots.push_back(hover); }
        else slotClick(slots, hover, btn, shift);
    } else if ((ui.clicked || ui.rclicked) && hover < 0 && !cursor.empty() && !ui.hovered(ox, oy, PW, PH)) {
        // click outside the window drops the held stack
        ItemStack d = cursor;
        if (ui.rclicked) { d.count = 1; cursor.count--; if (cursor.count == 0) cursor.clear(); } else cursor.clear();
        entities.spawnItem(player.eyePos(1), d, true, player.lookDir());
    }
    if (dragging) {
        bool held = dragButton == 0 ? plat::mouseDown(MOUSE_LEFT) : plat::mouseDown(MOUSE_RIGHT);
        if (held && hover >= 0 && std::find(dragSlots.begin(), dragSlots.end(), hover) == dragSlots.end()) {
            SlotRef& sr = slots[hover];
            if (sr.kind == 0 && (sr.st->empty() || sr.st->sameItem(cursor)) && (int)dragSlots.size() < cursor.count) dragSlots.push_back(hover);
        }
        if (!held) {
            if (dragSlots.size() <= 1) {
                if (!dragSlots.empty()) slotClick(slots, dragSlots[0], dragButton, false);
            } else {
                int per = dragButton == 0 ? std::max(1, cursor.count / (int)dragSlots.size()) : 1;
                for (int i : dragSlots) { if (cursor.empty()) break; mergeInto(*slots[i].st, cursor, per); }
            }
            dragging = false; dragSlots.clear();
        }
    }
    updateCraftResult();
    // number keys swap the hovered slot with the hotbar
    if (hover >= 0 && slots[hover].kind == 0)
        for (int k = 0; k < 9; k++)
            if (plat::keyPressed(KEY_1 + k)) std::swap(*slots[hover].st, player.inv.slots[k]);
    // tooltip and cursor stack
    if (hover >= 0 && cursor.empty() && !slots[hover].st->empty()) {
        const ItemStack& s = *slots[hover].st;
        std::string name = itemName(s.id);
        const ItemDef& d = itemDef(s.id);
        if (d.maxDamage > 0 && s.damage > 0) name += "  (" + std::to_string(d.maxDamage - s.damage) + "/" + std::to_string(d.maxDamage) + ")";
        float w = ui.textWidth(name) + 6;
        float tx2 = ui.mx + 10, ty2 = ui.my - 12;
        ui.rect(tx2 - 3, ty2 - 3, w, 13, 0xF0100010);
        ui.frame(tx2 - 3, ty2 - 3, w, 13, 0xFF5000A0);
        ui.text(tx2, ty2, name, COL_WHITE);
    }
    if (!cursor.empty()) {
        ItemStack shown = cursor;
        if (dragging && dragSlots.size() > 1) {
            int per = dragButton == 0 ? std::max(1, cursor.count / (int)dragSlots.size()) : 1;
            int left = cursor.count - per * (int)dragSlots.size();
            shown.count = (uint8_t)std::max(0, left);
        }
        if (shown.count > 0) ui.itemIcon(ui.mx - 8, ui.my - 8, 16, shown);
    }
}
