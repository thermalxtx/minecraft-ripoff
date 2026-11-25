// Immediate-mode pixel-art UI drawn with the generated bitmap font and texture array.
#pragma once
#include <string>
#include <vector>
#include "../core/gl.h"
#include "../core/math.h"
#include "../game/items.h"

class Renderer;

struct UIVert { float x, y, u, v, layer, mode; uint8_t r, g, b, a; };

class UI {
public:
    float scale = 2;        // screen pixels per UI pixel
    float W = 0, H = 0;     // UI size in UI pixels
    float mx = 0, my = 0;   // mouse in UI pixels
    bool clicked = false, rclicked = false, mouseHeld = false, released = false;
    int hotId = -1;
    std::string tooltip;

    bool init(Renderer* r);
    void begin(int screenW, int screenH, int guiScaleSetting);
    void flush();

    // primitives (UI pixels)
    void rect(float x, float y, float w, float h, uint32_t col);
    void frame(float x, float y, float w, float h, uint32_t col, float t = 1);
    void image(float x, float y, float w, float h, int layer, uint32_t tint = 0xFFFFFFFF, float u0 = 0, float v0 = 0, float u1 = 1, float v1 = 1);
    void quad4(const vec2 p[4], const vec2 uv[4], int layer, uint32_t col, int mode = 1);
    void text(float x, float y, const std::string& s, uint32_t col = 0xFFFFFFFF, bool shadow = true, float size = 1);
    void textCentered(float cx, float y, const std::string& s, uint32_t col = 0xFFFFFFFF, bool shadow = true, float size = 1);
    float textWidth(const std::string& s, float size = 1) const;
    void itemIcon(float x, float y, float size, const ItemStack& st, bool showCount = true);
    void panel(float x, float y, float w, float h);          // light grey bevelled window
    void slot(float x, float y, float size = 18);            // inventory slot background

    // widgets
    bool button(float x, float y, float w, float h, const std::string& label, bool enabled = true);
    bool slider(float x, float y, float w, float h, const std::string& label, float& value, float mn, float mx);
    bool textField(int id, float x, float y, float w, float h, std::string& value, int maxLen, bool digitsOnly = false);
    int focusedField = -1;
    bool hovered(float x, float y, float w, float h) const { return mx >= x && my >= y && mx < x + w && my < y + h; }

private:
    Renderer* r = nullptr;
    std::vector<UIVert> verts;
    GLuint vao = 0, vbo = 0;
    size_t cap = 0;
    int screenW = 0, screenH = 0;
    int dragSlider = -1, sliderCounter = 0;
    void push(float x, float y, float u, float v, float layer, float mode, uint32_t c);
};

// Colours as 0xAABBGGRR (matches rgba())
constexpr uint32_t COL_WHITE = 0xFFFFFFFF;
constexpr uint32_t COL_GRAY = 0xFFA0A0A0;
constexpr uint32_t COL_DARK = 0xFF404040;
constexpr uint32_t COL_YELLOW = 0xFF55FFFF;
constexpr uint32_t COL_RED = 0xFF5555FF;
constexpr uint32_t COL_GREEN = 0xFF55FF55;
constexpr uint32_t COL_BLACK = 0xFF000000;
