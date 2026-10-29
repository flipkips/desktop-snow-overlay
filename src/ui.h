#pragma once
#include "common.h"

// Commands from the tray menu, sent with WM_APP_TRAYCMD
enum TrayCmd { CMD_SHAKE = 1001, CMD_SETTINGS, CMD_EXIT };

constexpr int kFpsChoices[] = {20, 30, 45, 60, 90, 120, 144};
constexpr int kFpsChoiceCount = sizeof(kFpsChoices) / sizeof(kFpsChoices[0]);

HICON AppIcon(bool smallIcon, UINT dpi = 96);

bool TrayAdd(HWND owner);
void TrayRemove();
void TrayBalloon(const wchar_t* title, const wchar_t* text);
void TrayUpdateTip();
void ShowTrayFlyout(HWND notify);  // right-click menu of the tray icon
void ReloadUi();                   // after a language change

// Settings window. notify: hidden window that receives WM_APP_CFGCHANGED / WM_APP_SHAKE
void ShowSettingsWindow(HWND notify);
void RefreshSettingsWindow();
HWND SettingsWindow();
