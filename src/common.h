#pragma once
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cwctype>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

#define APP_NAME    L"LayerSnow"
#define APP_VERSION L"1.0.0"
#define APP_AUTHOR  L"flipkips"
#define APP_REPO    L"https://github.com/flipkips/desktop-snow-overlay"
#define IDI_APP     101

// Messages for the hidden message window
#define WM_APP_TRAY          (WM_APP + 1)
#define WM_APP_SHOWSETTINGS  (WM_APP + 2)
#define WM_APP_CFGCHANGED    (WM_APP + 3)
#define WM_APP_SHAKE         (WM_APP + 4)
#define WM_APP_TRAYCMD       (WM_APP + 5)  // wParam: TrayCmd from the tray menu
#define WM_APP_RELOADUI      (WM_APP + 6)  // UI language changed

DWORD WindowsBuild();  // 22000+ is Windows 11

constexpr wchar_t kMsgWndClass[] = L"LayerSnow.MsgWnd";
constexpr wchar_t kMutexName[]   = L"Local\\LayerSnow.SingleInstance";

struct RectI {
    int l = 0, t = 0, r = 0, b = 0;
    int w() const { return r - l; }
    int h() const { return b - t; }
    bool contains(float x, float y) const { return x >= l && x < r && y >= t && y < b; }
};

// Fast PRNG (xorshift32)
inline uint32_t& RngState() { static uint32_t s = 0x9E3779B9u ^ GetTickCount(); return s; }
inline float Rand01() {
    uint32_t& s = RngState();
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return (s >> 8) * (1.0f / 16777216.0f);
}
inline float RandRange(float a, float b) { return a + (b - a) * Rand01(); }

inline float Clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

std::wstring ExePath();
HINSTANCE AppInstance();
