#include "world.h"
#include <dwmapi.h>
#include <shellapi.h>
#include <shellscalingapi.h>

static bool IsTaskbarClass(const wchar_t* cls) {
    return !wcscmp(cls, L"Shell_TrayWnd") || !wcscmp(cls, L"Shell_SecondaryTrayWnd");
}

static bool IsDesktopClass(const wchar_t* cls) {
    return !wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW");
}

struct EnumCtx { World* w; HWND overlay; };

static BOOL CALLBACK EnumWinProc(HWND hwnd, LPARAM lp) {
    auto* ctx = reinterpret_cast<EnumCtx*>(lp);
    if (hwnd == ctx->overlay || !IsWindowVisible(hwnd) || IsIconic(hwnd)) return TRUE;

    int cloaked = 0;  // background UWP windows and windows on other virtual desktops
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked)
        return TRUE;

    wchar_t cls[96] = {};
    GetClassNameW(hwnd, cls, 96);
    bool taskbar = IsTaskbarClass(cls);
    LONG ex = GetWindowLongW(hwnd, GWL_EXSTYLE);
    if (!taskbar) {
        if (IsDesktopClass(cls)) return TRUE;
        if (!wcscmp(cls, L"tooltips_class32") || !wcscmp(cls, L"SysShadow")) return TRUE;  // tooltips and window shadows
        if ((ex & WS_EX_TRANSPARENT) && (ex & WS_EX_LAYERED)) return TRUE;               // other apps' click-through overlays
        BYTE alpha = 255;
        DWORD flags = 0;
        if ((ex & WS_EX_LAYERED) && GetLayeredWindowAttributes(hwnd, nullptr, &alpha, &flags) &&
            (flags & LWA_ALPHA) && alpha == 0)
            return TRUE;  // fully transparent window
    }

    RECT r;
    if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r))))
        GetWindowRect(hwnd, &r);
    if (!taskbar && (r.right - r.left < 100 || r.bottom - r.top < 40)) return TRUE;

    const RectI& v = ctx->w->virt;
    WinInfo wi;
    wi.hwnd = hwnd;
    wi.rc = {r.left - v.l, r.top - v.t, r.right - v.l, r.bottom - v.t};
    wi.taskbar = taskbar;
    wi.transient = !taskbar && ((ex & WS_EX_TOOLWINDOW) || !wcscmp(cls, L"Windows.UI.Core.CoreWindow") ||
                                !wcscmp(cls, L"#32768"));
    if (taskbar && ctx->w->firstTaskbarZ < 0) ctx->w->firstTaskbarZ = (int)ctx->w->wins.size();
    ctx->w->wins.push_back(wi);
    return TRUE;
}

static BOOL CALLBACK EnumMonProc(HMONITOR mon, HDC, LPRECT, LPARAM lp) {
    auto* w = reinterpret_cast<World*>(lp);
    MONITORINFO mi = {sizeof(mi)};
    if (!GetMonitorInfoW(mon, &mi)) return TRUE;
    const RectI& v = w->virt;
    MonitorInfo m;
    m.rc   = {mi.rcMonitor.left - v.l, mi.rcMonitor.top - v.t, mi.rcMonitor.right - v.l, mi.rcMonitor.bottom - v.t};
    m.work = {mi.rcWork.left - v.l, mi.rcWork.top - v.t, mi.rcWork.right - v.l, mi.rcWork.bottom - v.t};
    m.groundY = m.work.b < m.rc.b ? m.work.b : m.rc.b;
    UINT dx = 96, dy = 96;
    if (SUCCEEDED(GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dx, &dy))) m.scale = dx / 96.0f;
    w->monitors.push_back(m);
    return TRUE;
}

// Launcher games live in recognizable folders
static bool PathLooksLikeGame(std::wstring p) {
    for (auto& ch : p) ch = (wchar_t)towlower(ch);
    static const wchar_t* const skip[] = {L"\\epic games\\launcher\\", L"\\riot client\\"};  // the launchers themselves
    for (auto* s : skip)
        if (p.find(s) != std::wstring::npos) return false;
    static const wchar_t* const marks[] = {
        L"\\steamapps\\",    L"\\epic games\\",   L"\\riot games\\",    L"\\gog games\\",
        L"\\gog galaxy\\games\\", L"\\ubisoft game launcher\\games\\", L"\\ea games\\", L"\\origin games\\",
        L"\\xboxgames\\",    L"\\rockstar games\\", L"\\wargaming.net\\", L"\\games\\",
    };
    for (auto* m : marks)
        if (p.find(m) != std::wstring::npos) return true;
    return false;
}

static bool IsGameProcess(HWND fg) {
    static DWORD cachedPid = 0;
    static bool cached = false;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    if (pid == cachedPid) return cached;
    cachedPid = pid;
    cached = false;
    if (HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
        wchar_t path[MAX_PATH * 2];
        DWORD len = MAX_PATH * 2;
        if (QueryFullProcessImageNameW(h, 0, path, &len)) cached = PathLooksLikeGame(std::wstring(path, len));
        CloseHandle(h);
    }
    return cached;
}

static bool DetectGameOrFullscreen(HWND overlay) {
    QUERY_USER_NOTIFICATION_STATE st;
    if (SUCCEEDED(SHQueryUserNotificationState(&st)) &&
        (st == QUNS_RUNNING_D3D_FULL_SCREEN || st == QUNS_PRESENTATION_MODE || st == QUNS_BUSY))
        return true;

    HWND fg = GetForegroundWindow();
    if (!fg || fg == overlay) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    if (pid == GetCurrentProcessId()) return false;
    wchar_t cls[96] = {};
    GetClassNameW(fg, cls, 96);
    if (IsDesktopClass(cls) || IsTaskbarClass(cls)) return false;

    // Window covers the whole monitor (fullscreen or borderless).
    // A normal maximized window with a caption does not count: it fills the screen when the taskbar auto-hides
    LONG style = GetWindowLongW(fg, GWL_STYLE);
    bool normalMaximized = IsZoomed(fg) && (style & WS_CAPTION) == WS_CAPTION;
    RECT r;
    GetWindowRect(fg, &r);
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfoW(MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST), &mi);
    const RECT& m = mi.rcMonitor;
    if (!normalMaximized && r.left <= m.left + 1 && r.top <= m.top + 1 && r.right >= m.right - 1 &&
        r.bottom >= m.bottom - 1)
        return true;

    // The game captured the mouse: the cursor is clipped to its window
    RECT clip;
    if (GetClipCursor(&clip)) {
        int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN), vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        if (clip.right - clip.left < vw - 2 || clip.bottom - clip.top < vh - 2) return true;
    }

    // Windowed game from a Steam, Epic, GOG or similar folder
    return IsGameProcess(fg);
}

void World::Refresh(HWND overlay) {
    virt.l = GetSystemMetrics(SM_XVIRTUALSCREEN);
    virt.t = GetSystemMetrics(SM_YVIRTUALSCREEN);
    virt.r = virt.l + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    virt.b = virt.t + GetSystemMetrics(SM_CYVIRTUALSCREEN);

    wins.clear();
    monitors.clear();
    firstTaskbarZ = -1;
    EnumCtx ctx{this, overlay};
    EnumWindows(EnumWinProc, reinterpret_cast<LPARAM>(&ctx));
    EnumDisplayMonitors(nullptr, nullptr, EnumMonProc, reinterpret_cast<LPARAM>(this));
    gameOrFullscreen = DetectGameOrFullscreen(overlay);
}

bool World::LayoutChanged(const World& prev) const {
    if (wins.size() != prev.wins.size() || monitors.size() != prev.monitors.size()) return true;
    for (size_t i = 0; i < wins.size(); ++i) {
        const RectI &a = wins[i].rc, &b = prev.wins[i].rc;
        if (wins[i].hwnd != prev.wins[i].hwnd || a.l != b.l || a.t != b.t || a.r != b.r || a.b != b.b) return true;
    }
    for (size_t i = 0; i < monitors.size(); ++i)
        if (monitors[i].groundY != prev.monitors[i].groundY || monitors[i].rc.l != prev.monitors[i].rc.l ||
            monitors[i].rc.r != prev.monitors[i].rc.r)
            return true;
    return false;
}
