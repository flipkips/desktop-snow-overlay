#include "common.h"
#include "i18n.h"
#include "install.h"
#include "render.h"
#include "settings.h"
#include "snow.h"
#include "ui.h"
#include "world.h"
#include <dwmapi.h>
#include <shellapi.h>
#include <shellscalingapi.h>

static HINSTANCE g_inst;
HINSTANCE AppInstance() { return g_inst; }

std::wstring ExePath() {
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH * 2);
    return std::wstring(buf, n);
}

DWORD WindowsBuild() {
    static DWORD build = [] {
        RTL_OSVERSIONINFOW v = {sizeof(v)};
        using Fn = LONG(WINAPI*)(RTL_OSVERSIONINFOW*);
        if (auto fn = (Fn)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion")) fn(&v);
        return v.dwBuildNumber;
    }();
    return build;
}

static float PrimaryScale() {
    UINT dx = 96, dy = 96;
    GetDpiForMonitor(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), MDT_EFFECTIVE_DPI, &dx, &dy);
    return dx / 96.0f;
}

// A window moved, was minimized or reordered: refresh in the same frame instead of waiting for the poll
static bool g_worldDirty = true;

static void CALLBACK WinEventProc(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD) {
    if (event == EVENT_OBJECT_LOCATIONCHANGE &&
        (idObject != OBJID_WINDOW || idChild != CHILDID_SELF || !hwnd || GetAncestor(hwnd, GA_ROOT) != hwnd))
        return;  // ignore the cursor, caret and child objects
    g_worldDirty = true;
}

static std::vector<HWINEVENTHOOK> InstallWinEventHooks() {
    const DWORD ranges[][2] = {
        {EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND},
        {EVENT_SYSTEM_MOVESIZESTART, EVENT_SYSTEM_MINIMIZEEND},
        {EVENT_OBJECT_DESTROY, EVENT_OBJECT_REORDER},
        {EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE},
        {EVENT_OBJECT_CLOAKED, EVENT_OBJECT_UNCLOAKED},
    };
    std::vector<HWINEVENTHOOK> hooks;
    for (auto& r : ranges)
        if (HWINEVENTHOOK h = SetWinEventHook(r[0], r[1], nullptr, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT))
            hooks.push_back(h);
    return hooks;
}

class App {
public:
    int Run(bool welcome);

private:
    static LRESULT CALLBACK MsgProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMsg(HWND, UINT, WPARAM, LPARAM);
    void Tick();
    void HandleCmd(int cmd);
    void PlaceOverlay();
    bool EnsureRenderer();

    HWND msgWnd_ = nullptr, overlay_ = nullptr;
    World world_;
    Snow snow_;
    Renderer renderer_;
    RectI overlayRect_;
    UINT taskbarCreated_ = 0;
    bool running_ = true, shown_ = false, wasIdle_ = false, reinit_ = false;
    double interval_ = 1.0 / 30, worldTimer_ = 1, topmostTimer_ = 0;
    LARGE_INTEGER freq_{}, last_{};
};

static App* g_app;

LRESULT CALLBACK App::MsgProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    return g_app ? g_app->HandleMsg(h, m, w, l) : DefWindowProcW(h, m, w, l);
}

LRESULT App::HandleMsg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == taskbarCreated_ && taskbarCreated_) {  // Explorer restarted: add the tray icon again
        TrayAdd(msgWnd_);
        return 0;
    }
    switch (msg) {
    case WM_APP_TRAY:
        switch (LOWORD(lp)) {
        case WM_CONTEXTMENU: ShowTrayFlyout(msgWnd_); break;
        case NIN_SELECT:
        case NIN_KEYSELECT: ShowSettingsWindow(msgWnd_); break;
        }
        return 0;
    case WM_APP_SHOWSETTINGS: ShowSettingsWindow(msgWnd_); return 0;
    case WM_APP_CFGCHANGED:   snow_.Sync(world_); return 0;
    case WM_APP_SHAKE:        snow_.ShakeAll(); return 0;
    case WM_APP_TRAYCMD:      HandleCmd((int)wp); return 0;
    case WM_APP_RELOADUI:     ReloadUi(); return 0;
    case WM_DISPLAYCHANGE:    reinit_ = true; return 0;
    case WM_QUERYENDSESSION:  SaveSettings(); return TRUE;
    case WM_CLOSE:            running_ = false; return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void App::HandleCmd(int cmd) {
    switch (cmd) {
    case CMD_SHAKE:    snow_.ShakeAll(); break;
    case CMD_SETTINGS: ShowSettingsWindow(msgWnd_); break;
    case CMD_EXIT:     running_ = false; break;
    }
}

void App::PlaceOverlay() {
    overlayRect_ = world_.virt;
    SetWindowPos(overlay_, HWND_TOPMOST, overlayRect_.l, overlayRect_.t, overlayRect_.w(), overlayRect_.h(),
                 SWP_NOACTIVATE);
}

bool App::EnsureRenderer() {
    if (renderer_.Ready() && !reinit_) return true;
    reinit_ = false;
    float scale = PrimaryScale();
    snow_.SetScale(scale);
    PlaceOverlay();
    return renderer_.Init(overlay_, overlayRect_.w(), overlayRect_.h(), scale);
}

void App::Tick() {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    float dt = std::min(0.1f, (float)(now.QuadPart - last_.QuadPart) / freq_.QuadPart);
    last_ = now;

    worldTimer_ += dt;
    if (g_worldDirty || worldTimer_ >= 0.25) {  // on window events, plus a fallback poll 4 times a second
        g_worldDirty = false;
        worldTimer_ = 0;
        world_.Refresh(overlay_);
        const RectI& v = world_.virt;
        if (v.l != overlayRect_.l || v.t != overlayRect_.t || v.r != overlayRect_.r || v.b != overlayRect_.b)
            reinit_ = true;
        snow_.Sync(world_);
    }

    const Settings& c = Cfg();
    bool active = c.enabled && !(c.pauseFullscreen && world_.gameOrFullscreen);
    if (!active) {  // disabled or a game is running: hide the overlay and free GPU memory
        if (shown_) {
            ShowWindow(overlay_, SW_HIDE);
            renderer_.Shutdown();
            shown_ = false;
        }
        interval_ = 0.25;
        return;
    }
    if (!EnsureRenderer()) { interval_ = 1.0; return; }
    if (!shown_) {
        ShowWindow(overlay_, SW_SHOWNOACTIVATE);
        shown_ = true;
        wasIdle_ = false;
    }

    topmostTimer_ += dt;
    if (topmostTimer_ > 2) {  // other topmost windows may have covered us
        topmostTimer_ = 0;
        SetWindowPos(overlay_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }

    POINT cur;
    bool cv = GetCursorPos(&cur) != 0;
    cur.x -= world_.virt.l;
    cur.y -= world_.virt.t;
    snow_.Update(dt, world_, cur, cv);

    bool idle = snow_.Idle();
    if (!idle || !wasIdle_) {
        if (!renderer_.Draw(snow_, world_)) reinit_ = true;
    }
    wasIdle_ = idle;
    interval_ = idle && c.intensity == 0 ? 0.25 : 1.0 / std::max(15, c.fps);
    if (!idle && !snow_.VisibleMotion()) interval_ = std::max(interval_, 0.1);  // only drifts on screen: 10 fps is enough
}

int App::Run(bool welcome) {
    g_app = this;
    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = MsgProc;
    wc.hInstance = g_inst;
    wc.hIcon = AppIcon(false);
    wc.lpszClassName = kMsgWndClass;
    RegisterClassExW(&wc);
    msgWnd_ = CreateWindowExW(0, kMsgWndClass, APP_NAME, WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, g_inst, nullptr);

    WNDCLASSEXW oc = {sizeof(oc)};
    oc.lpfnWndProc = DefWindowProcW;
    oc.hInstance = g_inst;
    oc.lpszClassName = L"LayerSnow.Overlay";
    RegisterClassExW(&oc);
    // Transparent topmost window: clicks pass through, hidden from Alt+Tab and the taskbar
    overlay_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TRANSPARENT |
                                   WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP,
                               oc.lpszClassName, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, g_inst, nullptr);
    if (!msgWnd_ || !overlay_) return 1;
    SetLayeredWindowAttributes(overlay_, 0, 255, LWA_ALPHA);
    BOOL excl = TRUE;
    DwmSetWindowAttribute(overlay_, DWMWA_EXCLUDED_FROM_PEEK, &excl, sizeof(excl));

    taskbarCreated_ = RegisterWindowMessageW(L"TaskbarCreated");
    TrayAdd(msgWnd_);
    if (welcome)
        TrayBalloon(Tr(S_WELCOME_TITLE), Tr(S_WELCOME_TEXT));

    world_.Refresh(overlay_);
    snow_.SetScale(PrimaryScale());
    snow_.Sync(world_);
    reinit_ = true;
    std::vector<HWINEVENTHOOK> hooks = InstallWinEventHooks();

    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!timer) timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    QueryPerformanceFrequency(&freq_);
    QueryPerformanceCounter(&last_);
    auto arm = [&](double seconds) {
        LARGE_INTEGER due;
        due.QuadPart = -(LONGLONG)(std::max(0.001, seconds) * 1e7);
        SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE);
    };
    arm(0.01);

    while (running_) {
        DWORD r = MsgWaitForMultipleObjectsEx(1, &timer, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { running_ = false; break; }
            HWND sw = SettingsWindow();
            if (sw && IsDialogMessageW(sw, &msg)) continue;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (running_ && r == WAIT_OBJECT_0) {
            LARGE_INTEGER t0, t1;
            QueryPerformanceCounter(&t0);
            Tick();
            QueryPerformanceCounter(&t1);
            arm(interval_ - (double)(t1.QuadPart - t0.QuadPart) / freq_.QuadPart);
        }
    }

    for (HWINEVENTHOOK h : hooks) UnhookWinEvent(h);
    if (HWND sw = SettingsWindow()) DestroyWindow(sw);
    TrayRemove();
    renderer_.Shutdown();
    CloseHandle(timer);
    DestroyWindow(overlay_);
    DestroyWindow(msgWnd_);
    g_app = nullptr;
    return 0;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    g_inst = inst;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    bool uninstall = false, quiet = false, portable = false, welcome = false, autostart = false;
    int argc = 0;
    if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        for (int i = 1; i < argc; ++i) {
            if (!_wcsicmp(argv[i], L"/uninstall")) uninstall = true;
            else if (!_wcsicmp(argv[i], L"/quiet")) quiet = true;
            else if (!_wcsicmp(argv[i], L"/portable")) portable = true;
            else if (!_wcsicmp(argv[i], L"/welcome")) welcome = true;
            else if (!_wcsicmp(argv[i], L"/autostart")) autostart = true;
        }
        LocalFree(argv);
    }

    // A copy with "uninstall" in its file name (uninstall.exe, LayerSnow-Uninstall.exe) uninstalls right away
    std::wstring exe = ExePath();
    std::wstring name = exe.substr(exe.find_last_of(L"\\/") + 1);
    for (auto& ch : name) ch = (wchar_t)towlower(ch);
    if (name.find(L"uninstall") != std::wstring::npos) uninstall = true;

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    LoadSettings();  // the installer needs the language too
    int rc = 0;
    if (uninstall) {
        rc = RunUninstaller(quiet);
    } else if (!IsInstalledCopy() && !portable && !autostart) {
        rc = RunInstaller();  // started from a downloaded file: show the installer
    } else {
        HANDLE mutex = CreateMutexW(nullptr, TRUE, kMutexName);
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            // Already running: open settings in that copy
            if (HWND w = FindWindowW(kMsgWndClass, nullptr); w && !autostart) {
                DWORD pid = 0;
                GetWindowThreadProcessId(w, &pid);
                AllowSetForegroundWindow(pid);
                PostMessageW(w, WM_APP_SHOWSETTINGS, 0, 0);
            }
        } else {
            App app;
            rc = app.Run(welcome);
            SaveSettings();
        }
        if (mutex) CloseHandle(mutex);
    }
    CoUninitialize();
    return rc;
}
