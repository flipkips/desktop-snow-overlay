#include "ui.h"
#include "i18n.h"
#include "settings.h"
#include "snowui.h"
#include <commctrl.h>
#include <shellapi.h>

HICON AppIcon(bool smallIcon, UINT dpi) {
    HICON icon = nullptr;
    int cx = smallIcon ? GetSystemMetricsForDpi(SM_CXSMICON, dpi) : GetSystemMetricsForDpi(SM_CXICON, dpi);
    LoadIconWithScaleDown(AppInstance(), MAKEINTRESOURCEW(IDI_APP), cx, cx, &icon);
    return icon;
}

// ---------- Tray icon ----------

static NOTIFYICONDATAW g_nid = {};

bool TrayAdd(HWND owner) {
    g_nid = {};
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = owner;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    g_nid.uCallbackMessage = WM_APP_TRAY;
    g_nid.hIcon = AppIcon(true, GetDpiForSystem());
    wcscpy_s(g_nid.szTip, Tr(S_TRAY_TIP));
    if (!Shell_NotifyIconW(NIM_ADD, &g_nid)) return false;
    g_nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &g_nid);
    return true;
}

void TrayUpdateTip() {
    if (!g_nid.hWnd) return;
    NOTIFYICONDATAW n = g_nid;
    n.uFlags = NIF_TIP | NIF_SHOWTIP;
    wcscpy_s(n.szTip, Tr(S_TRAY_TIP));
    Shell_NotifyIconW(NIM_MODIFY, &n);
}

void TrayBalloon(const wchar_t* title, const wchar_t* text) {
    if (!g_nid.hWnd) return;
    NOTIFYICONDATAW n = g_nid;
    n.uFlags = NIF_INFO;
    n.dwInfoFlags = NIIF_USER | NIIF_LARGE_ICON;
    n.hBalloonIcon = AppIcon(false, GetDpiForSystem());
    wcscpy_s(n.szInfoTitle, title);
    wcscpy_s(n.szInfo, text);
    Shell_NotifyIconW(NIM_MODIFY, &n);
}

void TrayRemove() {
    if (g_nid.hWnd) Shell_NotifyIconW(NIM_DELETE, &g_nid);
    g_nid.hWnd = nullptr;
}

// ---------- Settings window ----------

enum { BTN_PRESET0 = 10, BTN_SHAKE = 20, BTN_RESET, BTN_DONE };

static std::unique_ptr<SnowPanel> g_panel, g_flyout;
static HWND g_notify = nullptr;
static UINT_PTR g_saveTimer = 0;
static int g_uiLang = -1;  // language the open windows were built with

HWND SettingsWindow() { return g_panel ? g_panel->Hwnd() : nullptr; }
void RefreshSettingsWindow() { if (g_panel) g_panel->Redraw(); }

static void CALLBACK SaveTimerProc(HWND, UINT, UINT_PTR id, DWORD) {
    KillTimer(nullptr, id);
    g_saveTimer = 0;
    SaveSettings();
}

static void Changed() {
    if (g_notify) PostMessageW(g_notify, WM_APP_CFGCHANGED, 0, 0);
    if (g_saveTimer) KillTimer(nullptr, g_saveTimer);
    g_saveTimer = SetTimer(nullptr, 0, 700, SaveTimerProc);  // save once the slider is released
    if (CurrentLang() != g_uiLang && g_notify) PostMessageW(g_notify, WM_APP_RELOADUI, 0, 0);
}

static void OnPanelEvent(int id) {
    if (id == 0) {
        Changed();
    } else if (id >= BTN_PRESET0 && id < BTN_PRESET0 + 4) {
        ApplyPreset((Preset)(id - BTN_PRESET0));
        Changed();
    } else if (id == BTN_RESET) {
        ResetSettings();
        Changed();
    } else if (id == BTN_SHAKE) {
        if (g_notify) PostMessageW(g_notify, WM_APP_SHAKE, 0, 0);
    } else if (id == BTN_DONE) {
        PostMessageW(SettingsWindow(), WM_CLOSE, 0, 0);
    }
    RefreshSettingsWindow();
}

void ShowSettingsWindow(HWND notify) {
    g_notify = notify;
    if (HWND w = SettingsWindow()) {
        ShowWindow(w, SW_RESTORE);
        SetForegroundWindow(w);
        return;
    }
    using F = std::function<int()>;
    using S = std::function<void(int)>;
    auto gi = [](int Settings::*f) -> F { return [f] { return Cfg().*f; }; };
    auto si = [](int Settings::*f) -> S { return [f](int v) { Cfg().*f = v; }; };
    auto gb = [](bool Settings::*f) -> F { return [f] { return Cfg().*f ? 1 : 0; }; };
    auto sb = [](bool Settings::*f) -> S { return [f](int v) { Cfg().*f = v != 0; }; };

    g_uiLang = CurrentLang();
    g_panel = std::make_unique<SnowPanel>();
    SnowPanel& p = *g_panel;
    p.SetHeader(L"LayerSnow", Tr(S_SUBTITLE));
    p.AddCaption({560, 36, 860, 56}, Tr(S_AUTHOR), true);
    p.AddLink({560, 60, 860, 80}, Tr(S_STAR_GITHUB), APP_REPO, true);

    const float y0 = 112, cardH = 398;
    const D2D1_RECT_F L = {20, y0, 436, y0 + cardH};
    p.AddCard(L, Tr(S_CARD_SNOWFALL));
    p.AddSegment({L.left + 18, y0 + 46, L.right - 18, y0 + 82}, {Tr(S_MODE_TOP), Tr(S_MODE_BEHIND)},
                 gi(&Settings::mode), si(&Settings::mode));
    const float pw = (L.right - L.left - 36 - 3 * 8) / 4;
    for (int i = 0; i < 4; ++i) {
        float x = L.left + 18 + i * (pw + 8);
        p.AddButton({x, y0 + 94, x + pw, y0 + 126}, Tr((Str)(S_PRESET_LIGHT + i)), BTN_PRESET0 + i);
    }
    struct Row { Str label; int Settings::*f; int lo, hi; const wchar_t* unit; };
    const Row fall[] = {
        {S_INTENSITY, &Settings::intensity, 0, 100, L"%"},
        {S_FALL_SPEED, &Settings::fallSpeed, 0, 100, L"%"},
        {S_WIND_ANGLE, &Settings::angle, -60, 60, L"°"},
        {S_CURSOR_WIND, &Settings::cursorWind, 0, 100, L"%"},
        {S_GUSTS, &Settings::gusts, 0, 100, L"%"},
        {S_FLAKE_SIZE, &Settings::flakeSize, 0, 100, L"%"},
        {S_CRYSTALS, &Settings::crystalShare, 0, 100, L"%"},
    };
    for (int i = 0; i < 7; ++i) {
        float y = y0 + 138 + i * 36;
        p.AddSlider({L.left + 18, y, L.right - 18, y + 34}, Tr(fall[i].label), fall[i].lo, fall[i].hi, fall[i].unit,
                    gi(fall[i].f), si(fall[i].f));
    }

    const D2D1_RECT_F R = {444, y0, 860, y0 + cardH};
    p.AddCard(R, Tr(S_CARD_PILES));
    const Row piles[] = {
        {S_ACCUMULATION, &Settings::accumulation, 0, 100, L"%"},
        {S_PILE_TASKBAR, &Settings::pileGround, 0, 200, L" px"},
        {S_PILE_WINDOWS, &Settings::pileWindows, 0, 100, L" px"},
        {S_DRIFTS, &Settings::drifts, 0, 100, L"%"},
        {S_MELT, &Settings::melt, 0, 100, L"%"},
        {S_CURSOR_RADIUS, &Settings::cursorRadius, 8, 80, L" px"},
        {S_SMOOTHNESS, &Settings::fps, 15, 144, L" FPS"},
    };
    for (int i = 0; i < 7; ++i) {
        float y = y0 + 48 + i * 48;
        p.AddSlider({R.left + 18, y, R.right - 18, y + 34}, Tr(piles[i].label), piles[i].lo, piles[i].hi,
                    piles[i].unit, gi(piles[i].f), si(piles[i].f));
    }

    const float y1 = y0 + cardH + 12;
    p.AddCard({20, y1, 860, y1 + 184}, Tr(S_CARD_BEHAVIOR));
    struct Tog { Str label; bool Settings::*f; };
    const Tog toggles[] = {
        {S_SNOW_ON, &Settings::enabled},           {S_SHOW_FALLING, &Settings::showFalling},
        {S_ON_WINDOWS, &Settings::onWindows},      {S_ON_TASKBAR, &Settings::onTaskbar},
        {S_CURSOR_KNOCK, &Settings::cursorKnock},  {S_PAUSE_GAMES, &Settings::pauseFullscreen},
    };
    for (int i = 0; i < 6; ++i) {
        float x = i < 4 ? 40.0f : 460.0f, y = y1 + 48 + (i % 4) * 32;
        p.AddToggle({x, y, x + 390, y + 28}, Tr(toggles[i].label), gb(toggles[i].f), sb(toggles[i].f));
    }
    const float ya = y1 + 48 + 2 * 32, yl = y1 + 48 + 3 * 32;
    p.AddToggle({460, ya, 850, ya + 28}, Tr(S_AUTOSTART), [] { return IsAutostartEnabled() ? 1 : 0; },
                [](int v) { SetAutostart(v != 0); });
    p.AddText({462, yl + 4, 580, yl + 28}, Tr(S_LANGUAGE));
    p.AddSegment({580, yl, 846, yl + 28}, {kLangCodes[0], kLangCodes[1], kLangCodes[2], kLangCodes[3]},
                 [] { return CurrentLang(); }, [](int v) { Cfg().lang = v; });

    const float fy = y1 + 184 + 14;
    p.AddButton({20, fy, 196, fy + 38}, Tr(S_SHAKE), BTN_SHAKE);
    p.AddButton({516, fy, 692, fy + 38}, Tr(S_DEFAULTS), BTN_RESET);
    p.AddButton({704, fy, 860, fy + 38}, Tr(S_DONE), BTN_DONE, true);

    p.onDestroy = [] { SaveSettings(); };  // the object itself is freed when the window is opened next time
    if (!p.Create(Tr(S_SETTINGS_TITLE), 880, fy + 38 + 40, OnPanelEvent)) g_panel.reset();
}

// After a language change rebuild the open windows in place.
void ReloadUi() {
    TrayUpdateTip();
    if (g_flyout && g_flyout->Hwnd()) DestroyWindow(g_flyout->Hwnd());
    HWND w = SettingsWindow();
    if (!w) return;
    RECT r;
    GetWindowRect(w, &r);
    DestroyWindow(w);
    ShowSettingsWindow(g_notify);
    if (HWND n = SettingsWindow()) SetWindowPos(n, nullptr, r.left, r.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

// ---------- Tray menu (right click) ----------

void ShowTrayFlyout(HWND notify) {
    g_notify = notify;
    if (HWND w = g_flyout ? g_flyout->Hwnd() : nullptr) {  // a second click closes it
        DestroyWindow(w);
        return;
    }
    g_uiLang = CurrentLang();
    g_flyout = std::make_unique<SnowPanel>();
    SnowPanel& p = *g_flyout;
    p.SetHeader(L"LayerSnow", L"");

    p.AddToggle({18, 62, 298, 90}, Tr(S_SNOW_ON),
                [] { return Cfg().enabled ? 1 : 0; }, [](int v) { Cfg().enabled = v != 0; });
    p.AddToggle({18, 92, 298, 120}, Tr(S_SHOW_FALLING),
                [] { return Cfg().showFalling ? 1 : 0; }, [](int v) { Cfg().showFalling = v != 0; });
    p.AddToggle({18, 122, 298, 150}, Tr(S_AUTOSTART),
                [] { return IsAutostartEnabled() ? 1 : 0; }, [](int v) { SetAutostart(v != 0); });

    p.AddCaption({20, 162, 298, 180}, Tr(S_MODE));
    p.AddSegment({16, 182, 300, 214}, {Tr(S_MODE_TOP), Tr(S_MODE_BEHIND)},
                 [] { return Cfg().mode; }, [](int v) { Cfg().mode = v; });
    p.AddCaption({20, 222, 298, 240}, Tr(S_SNOWFALL_STRENGTH));
    p.AddSegment({16, 242, 300, 274},
                 {Tr(S_PRESET_LIGHT), Tr(S_PRESET_NORMAL), Tr(S_PRESET_STRONG), Tr(S_PRESET_BLIZZARD)},
                 [] { return CurrentPreset(); }, [](int v) { ApplyPreset((Preset)v); });
    p.AddCaption({20, 282, 298, 300}, Tr(S_SMOOTHNESS_FPS));
    std::vector<std::wstring> fps;
    for (int f : kFpsChoices) fps.push_back(std::to_wstring(f));
    p.AddSegment({16, 302, 300, 334}, fps,
                 [] {
                     for (int i = 0; i < kFpsChoiceCount; ++i)
                         if (Cfg().fps == kFpsChoices[i]) return i;
                     return -1;
                 },
                 [](int v) { Cfg().fps = kFpsChoices[v]; });

    p.AddItem({8, 346, 308, 376}, Tr(S_SHAKE_ALL), CMD_SHAKE);
    p.AddItem({8, 378, 308, 408}, Tr(S_ALL_SETTINGS), CMD_SETTINGS);
    p.AddItem({8, 410, 308, 440}, Tr(S_EXIT), CMD_EXIT);
    p.AddLink({20, 452, 200, 472}, L"★ GitHub", APP_REPO);
    p.AddCaption({180, 452, 296, 472}, APP_AUTHOR, true);

    auto onEvent = [](int id) {
        if (id == 0) {
            Changed();
            RefreshSettingsWindow();
            if (g_flyout) g_flyout->Redraw();
            return;
        }
        PostMessageW(g_notify, WM_APP_TRAYCMD, id, 0);
        PostMessageW(g_flyout->Hwnd(), WM_CLOSE, 0, 0);
    };
    POINT pt;
    GetCursorPos(&pt);
    if (!p.Create(L"LayerSnow", 316, 500, onEvent, &pt)) g_flyout.reset();
}
