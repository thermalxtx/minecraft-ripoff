// Win32 + WGL implementation of the platform layer.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <mmsystem.h>
#include <cstring>
#include <cstdio>
#include "platform.h"
#include "gl.h"

#ifndef HID_USAGE_PAGE_GENERIC
#define HID_USAGE_PAGE_GENERIC ((USHORT)0x01)
#define HID_USAGE_GENERIC_MOUSE ((USHORT)0x02)
#endif

// WGL extension constants
#define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB 0x2092
#define WGL_CONTEXT_FLAGS_ARB 0x2094
#define WGL_CONTEXT_PROFILE_MASK_ARB 0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001
typedef HGLRC(WINAPI* PFNWGLCREATECONTEXTATTRIBSARB)(HDC, HGLRC, const int*);
typedef BOOL(WINAPI* PFNWGLSWAPINTERVALEXT)(int);

namespace {
HWND g_hwnd = nullptr;
HDC g_hdc = nullptr;
HGLRC g_ctx = nullptr;
HMODULE g_glLib = nullptr;
int g_w = 1280, g_h = 720;
bool g_quit = false, g_focused = true;
bool g_keys[256], g_keysPrev[256], g_keyEdge[256];
bool g_mouse[3], g_mousePrev[3], g_mouseEdge[3], g_mouseUpEdge[3];
float g_mx = 0, g_my = 0, g_dx = 0, g_dy = 0, g_scroll = 0;
std::vector<unsigned> g_chars;
bool g_captured = false, g_fullscreen = false;
WINDOWPLACEMENT g_wpPrev = {sizeof(WINDOWPLACEMENT)};
LARGE_INTEGER g_freq, g_start;
PFNWGLSWAPINTERVALEXT g_swapInterval = nullptr;

void clipCursorToWindow() {
    RECT r; GetClientRect(g_hwnd, &r);
    POINT a = {r.left, r.top}, b = {r.right, r.bottom};
    ClientToScreen(g_hwnd, &a); ClientToScreen(g_hwnd, &b);
    RECT c = {a.x, a.y, b.x, b.y};
    ClipCursor(&c);
}

void applyCapture() {
    if (g_captured && g_focused) {
        clipCursorToWindow();
        while (ShowCursor(FALSE) >= 0) {}
    } else {
        ClipCursor(nullptr);
        while (ShowCursor(TRUE) < 0) {}
    }
}

LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CLOSE: g_quit = true; return 0;
    case WM_SIZE: g_w = LOWORD(lp); g_h = HIWORD(lp); if (g_captured) applyCapture(); return 0;
    case WM_SETFOCUS: g_focused = true; applyCapture(); return 0;
    case WM_KILLFOCUS:
        g_focused = false; memset(g_keys, 0, sizeof(g_keys)); memset(g_mouse, 0, sizeof(g_mouse));
        applyCapture(); return 0;
    case WM_KEYDOWN: case WM_SYSKEYDOWN:
        if (wp < 256) {
            if (!(lp & (1 << 30)) || wp == VK_BACK) g_keyEdge[wp] = true;  // ignore auto-repeat (except backspace)
            g_keys[wp] = true;
            if (wp == VK_SHIFT) { g_keys[(lp & (1 << 24)) ? VK_RSHIFT : VK_LSHIFT] = true; }
            if (wp == VK_CONTROL) { g_keys[(lp & (1 << 24)) ? VK_RCONTROL : VK_LCONTROL] = true; }
        }
        if (msg == WM_SYSKEYDOWN && wp == VK_F4) g_quit = true;
        return 0;
    case WM_KEYUP: case WM_SYSKEYUP:
        if (wp < 256) {
            g_keys[wp] = false;
            if (wp == VK_SHIFT) { g_keys[VK_LSHIFT] = g_keys[VK_RSHIFT] = false; }
            if (wp == VK_CONTROL) { g_keys[VK_LCONTROL] = g_keys[VK_RCONTROL] = false; }
        }
        return 0;
    case WM_SYSCHAR: return 0;  // stop Alt beeps
    case WM_CHAR: if (wp >= 32 && wp != 127) g_chars.push_back((unsigned)wp); return 0;
    case WM_MOUSEMOVE: g_mx = (float)GET_X_LPARAM(lp); g_my = (float)GET_Y_LPARAM(lp); return 0;
    case WM_LBUTTONDOWN: g_mouse[0] = true; g_mouseEdge[0] = true; SetCapture(h); return 0;
    case WM_LBUTTONUP: g_mouse[0] = false; g_mouseUpEdge[0] = true; ReleaseCapture(); return 0;
    case WM_RBUTTONDOWN: g_mouse[1] = true; g_mouseEdge[1] = true; return 0;
    case WM_RBUTTONUP: g_mouse[1] = false; g_mouseUpEdge[1] = true; return 0;
    case WM_MBUTTONDOWN: g_mouse[2] = true; g_mouseEdge[2] = true; return 0;
    case WM_MBUTTONUP: g_mouse[2] = false; g_mouseUpEdge[2] = true; return 0;
    case WM_MOUSEWHEEL: g_scroll += (float)GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA; return 0;
    case WM_INPUT: {
        RAWINPUT ri; UINT sz = sizeof(ri);
        if (GetRawInputData((HRAWINPUT)lp, RID_INPUT, &ri, &sz, sizeof(RAWINPUTHEADER)) != (UINT)-1) {
            if (ri.header.dwType == RIM_TYPEMOUSE && !(ri.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) {
                g_dx += (float)ri.data.mouse.lLastX; g_dy += (float)ri.data.mouse.lLastY;
            }
        }
        break;
    }
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) { SetCursor(g_captured && g_focused ? nullptr : LoadCursor(nullptr, IDC_ARROW)); return TRUE; }
        break;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void* getGLProc(const char* name) {
    void* p = (void*)wglGetProcAddress(name);
    if (!p || p == (void*)1 || p == (void*)2 || p == (void*)3 || p == (void*)-1) p = (void*)GetProcAddress(g_glLib, name);
    return p;
}
}

namespace plat {
bool init(const char* title, int w, int h) {
    SetProcessDPIAware();
    QueryPerformanceFrequency(&g_freq); QueryPerformanceCounter(&g_start);
    timeBeginPeriod(1);
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc = {sizeof(wc)};
    wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(inst, MAKEINTRESOURCE(1));
    wc.lpszClassName = L"BlockcraftWnd";
    RegisterClassExW(&wc);
    RECT r = {0, 0, w, h};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    wchar_t wtitle[256]; MultiByteToWideChar(CP_UTF8, 0, title, -1, wtitle, 256);
    g_hwnd = CreateWindowExW(0, wc.lpszClassName, wtitle, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                             r.right - r.left, r.bottom - r.top, nullptr, nullptr, inst, nullptr);
    if (!g_hwnd) return false;
    g_hdc = GetDC(g_hwnd);
    PIXELFORMATDESCRIPTOR pfd = {sizeof(pfd), 1};
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA; pfd.cColorBits = 32; pfd.cDepthBits = 24; pfd.cStencilBits = 8;
    int pf = ChoosePixelFormat(g_hdc, &pfd);
    if (!pf || !SetPixelFormat(g_hdc, pf, &pfd)) { showError("Could not set a pixel format."); return false; }
    HGLRC dummy = wglCreateContext(g_hdc);
    if (!dummy) { showError("Could not create an OpenGL context."); return false; }
    wglMakeCurrent(g_hdc, dummy);
    auto createAttribs = (PFNWGLCREATECONTEXTATTRIBSARB)wglGetProcAddress("wglCreateContextAttribsARB");
    if (!createAttribs) { showError("OpenGL 3.3 is not supported by this graphics driver."); return false; }
    int attribs[] = {WGL_CONTEXT_MAJOR_VERSION_ARB, 3, WGL_CONTEXT_MINOR_VERSION_ARB, 3,
                     WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB, 0};
    g_ctx = createAttribs(g_hdc, nullptr, attribs);
    if (!g_ctx) { showError("Could not create an OpenGL 3.3 core context. Please update your graphics driver."); return false; }
    wglMakeCurrent(nullptr, nullptr); wglDeleteContext(dummy);
    wglMakeCurrent(g_hdc, g_ctx);
    g_glLib = LoadLibraryA("opengl32.dll");
    if (!glf::loadGL(getGLProc)) { showError("Failed to load OpenGL functions."); return false; }
    g_swapInterval = (PFNWGLSWAPINTERVALEXT)wglGetProcAddress("wglSwapIntervalEXT");
    RAWINPUTDEVICE rid = {HID_USAGE_PAGE_GENERIC, HID_USAGE_GENERIC_MOUSE, 0, g_hwnd};
    RegisterRawInputDevices(&rid, 1, sizeof(rid));
    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);
    RECT cr; GetClientRect(g_hwnd, &cr); g_w = cr.right; g_h = cr.bottom;
    return true;
}

void shutdown() {
    ClipCursor(nullptr);
    if (g_ctx) { wglMakeCurrent(nullptr, nullptr); wglDeleteContext(g_ctx); }
    if (g_hwnd) DestroyWindow(g_hwnd);
    timeEndPeriod(1);
}

bool pollEvents() {
    memcpy(g_keysPrev, g_keys, sizeof(g_keys));
    memcpy(g_mousePrev, g_mouse, sizeof(g_mouse));
    memset(g_keyEdge, 0, sizeof(g_keyEdge));
    memset(g_mouseEdge, 0, sizeof(g_mouseEdge));
    memset(g_mouseUpEdge, 0, sizeof(g_mouseUpEdge));
    g_dx = g_dy = 0; g_scroll = 0; g_chars.clear();
    MSG m;
    while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
    // Shift/ctrl release can be missed if both were held; re-sync from async state
    if (g_focused) {
        g_keys[VK_SHIFT] = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        g_keys[VK_CONTROL] = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    }
    return !g_quit;
}
void swapBuffers() { SwapBuffers(g_hdc); }
void setVsync(bool on) { if (g_swapInterval) g_swapInterval(on ? 1 : 0); }
int width() { return g_w; }
int height() { return g_h; }
bool focused() { return g_focused; }
bool keyDown(int k) { return k >= 0 && k < 256 && g_keys[k]; }
bool keyPressed(int k) { return k >= 0 && k < 256 && g_keyEdge[k]; }
bool mouseDown(int b) { return g_mouse[b]; }
bool mousePressed(int b) { return g_mouseEdge[b]; }
bool mouseReleased(int b) { return g_mouseUpEdge[b] || (!g_mouse[b] && g_mousePrev[b]); }
float mouseX() { return g_mx; }
float mouseY() { return g_my; }
void mouseDelta(float& dx, float& dy) { dx = g_dx; dy = g_dy; }
float scrollDelta() { return g_scroll; }
const std::vector<unsigned>& typedChars() { return g_chars; }
void setMouseCaptured(bool c) {
    if (c == g_captured) return;
    g_captured = c;
    {  // centre the cursor whenever capture changes
        POINT p = {g_w / 2, g_h / 2}; ClientToScreen(g_hwnd, &p); SetCursorPos(p.x, p.y);
    }
    if (!c) {
        POINT p = {g_w / 2, g_h / 2}; ClientToScreen(g_hwnd, &p); SetCursorPos(p.x, p.y);
        g_mx = (float)(g_w / 2); g_my = (float)(g_h / 2);
    }
    applyCapture();
}
bool mouseCaptured() { return g_captured; }
void setFullscreen(bool fs) {
    if (fs == g_fullscreen) return;
    g_fullscreen = fs;
    DWORD style = GetWindowLong(g_hwnd, GWL_STYLE);
    if (fs) {
        MONITORINFO mi = {sizeof(mi)};
        if (GetWindowPlacement(g_hwnd, &g_wpPrev) && GetMonitorInfo(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTOPRIMARY), &mi)) {
            SetWindowLong(g_hwnd, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
            SetWindowPos(g_hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                         mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        }
    } else {
        SetWindowLong(g_hwnd, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(g_hwnd, &g_wpPrev);
        SetWindowPos(g_hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
    if (g_captured) applyCapture();
}
bool isFullscreen() { return g_fullscreen; }
double timeSeconds() {
    LARGE_INTEGER t; QueryPerformanceCounter(&t);
    return (double)(t.QuadPart - g_start.QuadPart) / (double)g_freq.QuadPart;
}
void setTitle(const std::string& t) { SetWindowTextA(g_hwnd, t.c_str()); }
void showError(const std::string& msg) { MessageBoxA(g_hwnd, msg.c_str(), "Blockcraft", MB_OK | MB_ICONERROR); }
void sleepMs(int ms) { Sleep(ms); }
std::string exeDir() {
    char buf[MAX_PATH]; DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string s(buf, n);
    size_t p = s.find_last_of("\\/");
    return p == std::string::npos ? std::string() : s.substr(0, p + 1);
}
bool makeDir(const std::string& path) { return CreateDirectoryA(path.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS; }
bool removeDirRecursive(const std::string& path) {
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((path + "\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::string n = fd.cFileName;
            if (n == "." || n == "..") continue;
            std::string full = path + "\\" + n;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) removeDirRecursive(full);
            else DeleteFileA(full.c_str());
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    return RemoveDirectoryA(path.c_str()) != 0;
}
std::vector<std::string> listDirs(const std::string& path) {
    std::vector<std::string> out;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((path + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        std::string n = fd.cFileName;
        if (n != "." && n != ".." && (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) out.push_back(n);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return out;
}
bool renameFile(const std::string& from, const std::string& to) {
    return MoveFileExA(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}
}
