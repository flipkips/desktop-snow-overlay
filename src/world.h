#pragma once
#include "common.h"

// Visible top-level window (overlay coordinates, i.e. relative to the virtual screen)
struct WinInfo {
    HWND  hwnd;
    RectI rc;
    bool  taskbar;
    bool  transient;  // popups: Start, search, menus, notification panels
};

struct MonitorInfo {
    RectI rc;      // whole monitor
    RectI work;    // work area (without the taskbar)
    int   groundY; // ground level: top of the taskbar or bottom of the screen
    float scale = 1; // Windows display scale on this monitor (1.5 = 150%)
    float LogicalArea() const { return rc.w() * rc.h() / (scale * scale); }
};

struct World {
    RectI virt;                       // virtual screen in screen coordinates
    std::vector<WinInfo> wins;        // top to bottom in z-order
    std::vector<MonitorInfo> monitors;
    int  firstTaskbarZ = -1;          // index of the first taskbar in wins
    bool gameOrFullscreen = false;    // a game or fullscreen app is active

    void Refresh(HWND overlay);
    bool LayoutChanged(const World& prev) const;
};
