#pragma once
#include "common.h"

enum SnowMode { MODE_ON_TOP = 0, MODE_BEHIND = 1 };

struct Settings {
    bool enabled         = true;
    bool showFalling     = true; // off: drifts still build up, falling snow is hidden
    int  mode            = MODE_ON_TOP;
    int  intensity       = 45;   // 0..100
    int  fallSpeed       = 40;   // 0..100
    int  angle           = 10;   // -60..60 degrees
    int  gusts           = 30;   // 0..100
    int  flakeSize       = 50;   // 0..100
    int  crystalShare    = 20;   // 0..100, share of crystal flakes
    int  cursorWind      = 50;   // 0..100, wind leans toward the cursor
    int  accumulation    = 50;   // 0..100
    int  pileGround      = 60;   // px, max drift height above the taskbar
    int  pileWindows     = 24;   // px, max drift height on windows
    int  melt            = 0;    // 0..100
    int  drifts          = 60;   // 0..100, hills and bumps
    bool onWindows       = true;
    bool onTaskbar       = true;
    bool cursorKnock     = true;
    int  cursorRadius    = 26;   // px
    bool pauseFullscreen = true;
    int  fps             = 30;   // 15..144
    int  lang            = -1;   // -1: Windows UI language, otherwise Lang
};

enum Preset { PRESET_LIGHT, PRESET_NORMAL, PRESET_STRONG, PRESET_BLIZZARD };

Settings& Cfg();
void LoadSettings();
void SaveSettings();
void ResetSettings();
void ApplyPreset(Preset p);
int  CurrentPreset();  // preset matching the current values, or -1
std::wstring SettingsDir();

bool IsAutostartEnabled();
void SetAutostart(bool on, const std::wstring& exe = ExePath());
