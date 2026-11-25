// Platform layer: window, GL context, input, timing (Win32 + WGL implementation).
#pragma once
#include <string>
#include <vector>

// Key codes (match Windows virtual-key codes)
enum Key {
    KEY_BACKSPACE = 0x08, KEY_TAB = 0x09, KEY_ENTER = 0x0D, KEY_SHIFT = 0x10, KEY_CTRL = 0x11, KEY_ALT = 0x12,
    KEY_ESCAPE = 0x1B, KEY_SPACE = 0x20, KEY_PAGEUP = 0x21, KEY_PAGEDOWN = 0x22, KEY_END = 0x23, KEY_HOME = 0x24,
    KEY_LEFT = 0x25, KEY_UP = 0x26, KEY_RIGHT = 0x27, KEY_DOWN = 0x28, KEY_DELETE = 0x2E,
    KEY_0 = 0x30, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9,
    KEY_A = 0x41, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J, KEY_K, KEY_L, KEY_M,
    KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z,
    KEY_F1 = 0x70, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
    KEY_LSHIFT = 0xA0, KEY_RSHIFT = 0xA1, KEY_LCTRL = 0xA2, KEY_RCTRL = 0xA3,
};
enum MouseButton { MOUSE_LEFT = 0, MOUSE_RIGHT = 1, MOUSE_MIDDLE = 2 };

namespace plat {
bool init(const char* title, int w, int h);
void shutdown();
bool pollEvents();            // false when the window wants to close
void swapBuffers();
void setVsync(bool on);
int width();
int height();
bool focused();
bool keyDown(int key);
bool keyPressed(int key);     // went down this frame
bool mouseDown(int b);
bool mousePressed(int b);
bool mouseReleased(int b);
float mouseX();
float mouseY();
void mouseDelta(float& dx, float& dy);  // raw relative motion this frame
float scrollDelta();          // wheel notches this frame
const std::vector<unsigned>& typedChars();
void setMouseCaptured(bool c);
bool mouseCaptured();
void setFullscreen(bool fs);
bool isFullscreen();
double timeSeconds();
void setTitle(const std::string& t);
void showError(const std::string& msg);
void sleepMs(int ms);
std::string exeDir();         // folder containing the .exe (with trailing slash)
bool makeDir(const std::string& path);
bool removeDirRecursive(const std::string& path);
std::vector<std::string> listDirs(const std::string& path);
bool renameFile(const std::string& from, const std::string& to);  // atomic replace
}
