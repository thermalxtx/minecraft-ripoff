#include "ui.h"
#include "../render/renderer.h"
#include "../render/textures.h"
#include "../core/platform.h"
#include <cstdio>

bool UI::init(Renderer* rr) {
    r = rr;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UIVert), (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(UIVert), (void*)8);
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(UIVert), (void*)24);
    glBindVertexArray(0);
    return true;
}

void UI::begin(int sw, int sh, int guiScaleSetting) {
    screenW = sw; screenH = sh;
    int autoScale = std::max(1, std::min(sw / 320, sh / 240));
    scale = (float)(guiScaleSetting > 0 ? std::min(guiScaleSetting, autoScale) : autoScale);
    W = sw / scale; H = sh / scale;
    mx = plat::mouseX() / scale; my = plat::mouseY() / scale;
    clicked = plat::mousePressed(MOUSE_LEFT);
    rclicked = plat::mousePressed(MOUSE_RIGHT);
    mouseHeld = plat::mouseDown(MOUSE_LEFT);
    released = plat::mouseReleased(MOUSE_LEFT);
    if (!mouseHeld) dragSlider = -1;
    sliderCounter = 0;
    tooltip.clear();
    verts.clear();
}

void UI::push(float x, float y, float u, float v, float layer, float mode, uint32_t c) {
    UIVert vv{x * scale, y * scale, u, v, layer, mode, (uint8_t)(c & 255), (uint8_t)((c >> 8) & 255), (uint8_t)((c >> 16) & 255), (uint8_t)(c >> 24)};
    verts.push_back(vv);
}

void UI::flush() {
    if (verts.empty()) return;
    glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    r->uiSh.use();
    glUniform2f(r->uiSh.loc("uScreen"), (float)screenW, (float)screenH);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D_ARRAY, r->texArray);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, r->fontTex);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    size_t bytes = verts.size() * sizeof(UIVert);
    if (bytes > cap) { cap = bytes * 2; glBufferData(GL_ARRAY_BUFFER, cap, nullptr, GL_STREAM_DRAW); }
    glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, verts.data());
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)verts.size());
    glBindVertexArray(0);
    verts.clear();
}

void UI::rect(float x, float y, float w, float h, uint32_t c) {
    push(x, y, 0, 0, 0, 0, c); push(x + w, y, 0, 0, 0, 0, c); push(x + w, y + h, 0, 0, 0, 0, c);
    push(x, y, 0, 0, 0, 0, c); push(x + w, y + h, 0, 0, 0, 0, c); push(x, y + h, 0, 0, 0, 0, c);
}
void UI::frame(float x, float y, float w, float h, uint32_t c, float t) {
    rect(x, y, w, t, c); rect(x, y + h - t, w, t, c); rect(x, y + t, t, h - 2 * t, c); rect(x + w - t, y + t, t, h - 2 * t, c);
}
void UI::image(float x, float y, float w, float h, int layer, uint32_t tint, float u0, float v0, float u1, float v1) {
    float L = (float)layer;
    push(x, y, u0, v0, L, 1, tint); push(x + w, y, u1, v0, L, 1, tint); push(x + w, y + h, u1, v1, L, 1, tint);
    push(x, y, u0, v0, L, 1, tint); push(x + w, y + h, u1, v1, L, 1, tint); push(x, y + h, u0, v1, L, 1, tint);
}
void UI::quad4(const vec2 p[4], const vec2 uv[4], int layer, uint32_t c, int mode) {
    int order[6] = {0, 1, 2, 0, 2, 3};
    for (int i : order) push(p[i].x, p[i].y, uv[i].x, uv[i].y, (float)layer, (float)mode, c);
}

float UI::textWidth(const std::string& s, float size) const {
    float w = 0;
    for (unsigned char ch : s) w += (ch < 128 ? r->glyphW[ch] : 6) * size;
    return w;
}
void UI::text(float x, float y, const std::string& s, uint32_t col, bool shadow, float size) {
    if (shadow) {
        uint32_t sc = (col & 0xFF000000) | (((col & 0xFCFCFC) >> 2));
        text(x + size, y + size, s, sc, false, size);
    }
    float cx = x;
    for (unsigned char ch : s) {
        if (ch >= 128) ch = '?';
        if (ch > 32) {
            float u0 = (ch % 16) * 8 / 128.f, v0 = (ch / 16) * 8 / 64.f, u1 = u0 + 8 / 128.f, v1 = v0 + 8 / 64.f;
            float w = 8 * size, h = 8 * size;
            push(cx, y, u0, v0, 0, 2, col); push(cx + w, y, u1, v0, 0, 2, col); push(cx + w, y + h, u1, v1, 0, 2, col);
            push(cx, y, u0, v0, 0, 2, col); push(cx + w, y + h, u1, v1, 0, 2, col); push(cx, y + h, u0, v1, 0, 2, col);
        }
        cx += r->glyphW[ch] * size;
    }
}
void UI::textCentered(float cx, float y, const std::string& s, uint32_t col, bool shadow, float size) {
    text(cx - textWidth(s, size) / 2, y, s, col, shadow, size);
}

void UI::itemIcon(float x, float y, float size, const ItemStack& st, bool showCount) {
    if (st.empty()) return;
    const ItemDef& d = itemDef(st.id);
    bool cube = false;
    if (d.isBlock) {
        uint8_t sh = g_blocks[d.placesBlock].shape;
        cube = sh == SHAPE_CUBE || sh == SHAPE_CHEST || sh == SHAPE_FARMLAND || sh == SHAPE_CACTUS;
        if (d.placesBlock == WATER || d.placesBlock == LAVA) cube = true;
    }
    if (cube) {
        const BlockDef& b = g_blocks[d.placesBlock];
        float s = size * 0.92f, cx = x + size / 2, cy = y + size / 2 + size * 0.02f;
        vec2 top[4] = {{cx - s / 2, cy - s / 4}, {cx, cy}, {cx + s / 2, cy - s / 4}, {cx, cy - s / 2}};
        vec2 left[4] = {{cx - s / 2, cy - s / 4}, {cx - s / 2, cy + s / 4}, {cx, cy + s / 2}, {cx, cy}};
        vec2 right[4] = {{cx, cy}, {cx, cy + s / 2}, {cx + s / 2, cy + s / 4}, {cx + s / 2, cy - s / 4}};
        vec2 uvTop[4] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
        vec2 uvSide[4] = {{0, 0}, {0, 1}, {1, 1}, {1, 0}};
        int front = b.tex[FACE_PZ], side = b.tex[FACE_PX];
        if (d.placesBlock == FURNACE) front = TX_FURNACE_FRONT;
        if (d.placesBlock == CHEST) front = TX_CHEST_FRONT;
        quad4(top, uvTop, b.tex[FACE_PY], 0xFFFFFFFF);
        quad4(left, uvSide, front, 0xFFCCCCCC);
        quad4(right, uvSide, side, 0xFF999999);
    } else {
        image(x, y, size, size, d.icon);
    }
    if (showCount && st.count > 1) {
        std::string n = std::to_string(st.count);
        text(x + size - textWidth(n) + 1, y + size - 7, n, COL_WHITE);
    }
    if (d.maxDamage > 0 && st.damage > 0) {
        float f = 1.0f - (float)st.damage / d.maxDamage;
        rect(x + 2, y + size - 2, size - 4, 2, 0xFF000000);
        uint32_t c = rgba(1 - f, f, 0.1f);
        rect(x + 2, y + size - 2, (size - 4) * f, 1, c);
    }
}

void UI::panel(float x, float y, float w, float h) {
    rect(x, y, w, h, 0xFFC6C6C6);
    rect(x, y, w - 1, 1, 0xFFFFFFFF); rect(x, y, 1, h - 1, 0xFFFFFFFF);
    rect(x + 1, y + h - 1, w - 1, 1, 0xFF555555); rect(x + w - 1, y + 1, 1, h - 1, 0xFF555555);
    frame(x - 1, y - 1, w + 2, h + 2, 0xFF000000);
}
void UI::slot(float x, float y, float size) {
    rect(x, y, size, size, 0xFF8B8B8B);
    rect(x, y, size - 1, 1, 0xFF373737); rect(x, y, 1, size - 1, 0xFF373737);
    rect(x + 1, y + size - 1, size - 1, 1, 0xFFFFFFFF); rect(x + size - 1, y + 1, 1, size - 1, 0xFFFFFFFF);
}

bool UI::button(float x, float y, float w, float h, const std::string& label, bool enabled) {
    bool hov = enabled && hovered(x, y, w, h);
    uint32_t bg = !enabled ? 0xFF2C2C2C : (hov ? 0xFF8A6E5A : 0xFF6B6B6B);
    rect(x, y, w, h, 0xFF000000);
    rect(x + 1, y + 1, w - 2, h - 2, bg);
    rect(x + 1, y + 1, w - 2, 1, enabled ? (hov ? 0xFFC8A890 : 0xFFAAAAAA) : 0xFF404040);
    rect(x + 1, y + h - 2, w - 2, 1, enabled ? 0xFF3A3A3A : 0xFF202020);
    textCentered(x + w / 2, y + (h - 7) / 2, label, !enabled ? 0xFF707070 : (hov ? COL_YELLOW : COL_WHITE));
    return hov && clicked;
}

bool UI::slider(float x, float y, float w, float h, const std::string& label, float& value, float mn, float mxv) {
    int id = sliderCounter++;
    bool hov = hovered(x, y, w, h);
    if (hov && clicked) dragSlider = id;
    bool changed = false;
    if (dragSlider == id && mouseHeld) {
        float t = clampf((mx - x - 4) / (w - 8), 0, 1);
        float nv = mn + t * (mxv - mn);
        if (nv != value) { value = nv; changed = true; }
    }
    rect(x, y, w, h, 0xFF000000);
    rect(x + 1, y + 1, w - 2, h - 2, 0xFF3A3A3A);
    float t = (value - mn) / (mxv - mn);
    float kx = x + 1 + t * (w - 10);
    rect(kx, y + 1, 8, h - 2, hov ? 0xFFC8A890 : 0xFFAAAAAA);
    textCentered(x + w / 2, y + (h - 7) / 2, label, hov ? COL_YELLOW : COL_WHITE);
    return changed;
}

bool UI::textField(int id, float x, float y, float w, float h, std::string& value, int maxLen, bool digitsOnly) {
    bool hov = hovered(x, y, w, h);
    if (clicked) { if (hov) focusedField = id; else if (focusedField == id) focusedField = -1; }
    bool focused = focusedField == id;
    bool changed = false;
    if (focused) {
        for (unsigned ch : plat::typedChars()) {
            if ((int)value.size() >= maxLen || ch >= 127) continue;
            if (digitsOnly && !((ch >= '0' && ch <= '9') || (ch == '-' && value.empty()))) continue;
            value.push_back((char)ch); changed = true;
        }
        if (plat::keyPressed(KEY_BACKSPACE) && !value.empty()) { value.pop_back(); changed = true; }
    }
    rect(x, y, w, h, focused ? 0xFFFFFFFF : 0xFFA0A0A0);
    rect(x + 1, y + 1, w - 2, h - 2, 0xFF000000);
    std::string shown = value;
    while (textWidth(shown) > w - 10 && !shown.empty()) shown.erase(shown.begin());
    bool blink = focused && ((int)(plat::timeSeconds() * 2) % 2 == 0);
    text(x + 4, y + (h - 7) / 2, shown + (blink ? "_" : ""), COL_WHITE);
    return changed;
}
