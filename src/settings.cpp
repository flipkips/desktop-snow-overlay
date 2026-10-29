#include "settings.h"
#include <shlobj.h>

static Settings g_cfg;
static const wchar_t kSection[] = L"snow";
static const wchar_t kRunKey[]  = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

Settings& Cfg() { return g_cfg; }

std::wstring SettingsDir() {
    PWSTR p = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &p))) {
        dir = std::wstring(p) + L"\\" APP_NAME;
        CoTaskMemFree(p);
    }
    return dir;
}

static std::wstring IniPath() { return SettingsDir() + L"\\settings.ini"; }

// Field table: ini key -> int/bool in g_cfg
struct Field { const wchar_t* key; int* i; bool* b; int lo, hi; };

static std::vector<Field> Fields(Settings& s) {
    return {
        {L"enabled", nullptr, &s.enabled, 0, 1},
        {L"showFalling", nullptr, &s.showFalling, 0, 1},
        {L"mode", &s.mode, nullptr, 0, 1},
        {L"intensity", &s.intensity, nullptr, 0, 100},
        {L"fallSpeed", &s.fallSpeed, nullptr, 0, 100},
        {L"angle", &s.angle, nullptr, -60, 60},
        {L"gusts", &s.gusts, nullptr, 0, 100},
        {L"flakeSize", &s.flakeSize, nullptr, 0, 100},
        {L"crystalShare", &s.crystalShare, nullptr, 0, 100},
        {L"cursorWind", &s.cursorWind, nullptr, 0, 100},
        {L"accumulation", &s.accumulation, nullptr, 0, 100},
        {L"pileGround", &s.pileGround, nullptr, 0, 200},
        {L"pileWindows", &s.pileWindows, nullptr, 0, 100},
        {L"melt", &s.melt, nullptr, 0, 100},
        {L"drifts", &s.drifts, nullptr, 0, 100},
        {L"onWindows", nullptr, &s.onWindows, 0, 1},
        {L"onTaskbar", nullptr, &s.onTaskbar, 0, 1},
        {L"cursorKnock", nullptr, &s.cursorKnock, 0, 1},
        {L"cursorRadius", &s.cursorRadius, nullptr, 8, 80},
        {L"pauseFullscreen", nullptr, &s.pauseFullscreen, 0, 1},
        {L"fps", &s.fps, nullptr, 15, 144},
        {L"lang", &s.lang, nullptr, -1, 3},
    };
}

void LoadSettings() {
    std::wstring ini = IniPath();
    for (auto& f : Fields(g_cfg)) {
        int def = f.i ? *f.i : (*f.b ? 1 : 0);
        int v = (int)GetPrivateProfileIntW(kSection, f.key, def, ini.c_str());
        v = std::clamp(v, f.lo, f.hi);
        if (f.i) *f.i = v; else *f.b = v != 0;
    }
}

void SaveSettings() {
    CreateDirectoryW(SettingsDir().c_str(), nullptr);
    std::wstring ini = IniPath();
    for (auto& f : Fields(g_cfg)) {
        int v = f.i ? *f.i : (*f.b ? 1 : 0);
        WritePrivateProfileStringW(kSection, f.key, std::to_wstring(v).c_str(), ini.c_str());
    }
}

void ResetSettings() {
    bool enabled = g_cfg.enabled;
    int lang = g_cfg.lang;
    g_cfg = Settings();
    g_cfg.enabled = enabled;
    g_cfg.lang = lang;
}

struct PresetValues { int intensity, fallSpeed, gusts, angle, accumulation; };
static const PresetValues kPresets[4] = {
    {18, 28, 15, 5, 30},    // light
    {45, 40, 30, 10, 50},   // normal
    {75, 55, 45, 18, 70},   // heavy
    {100, 80, 85, 38, 90},  // blizzard
};

void ApplyPreset(Preset p) {
    const PresetValues& v = kPresets[p];
    g_cfg.intensity = v.intensity;
    g_cfg.fallSpeed = v.fallSpeed;
    g_cfg.gusts = v.gusts;
    g_cfg.angle = v.angle;
    g_cfg.accumulation = v.accumulation;
}

int CurrentPreset() {
    for (int i = 0; i < 4; ++i) {
        const PresetValues& v = kPresets[i];
        if (g_cfg.intensity == v.intensity && g_cfg.fallSpeed == v.fallSpeed && g_cfg.gusts == v.gusts &&
            g_cfg.angle == v.angle && g_cfg.accumulation == v.accumulation)
            return i;
    }
    return -1;
}

bool IsAutostartEnabled() {
    wchar_t buf[MAX_PATH * 2];
    DWORD size = sizeof(buf);
    return RegGetValueW(HKEY_CURRENT_USER, kRunKey, APP_NAME, RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS;
}

void SetAutostart(bool on, const std::wstring& exe) {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return;
    if (on) {
        std::wstring cmd = L"\"" + exe + L"\" /autostart";
        RegSetValueExW(key, APP_NAME, 0, REG_SZ, (const BYTE*)cmd.c_str(), (DWORD)((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(key, APP_NAME);
    }
    RegCloseKey(key);
}
