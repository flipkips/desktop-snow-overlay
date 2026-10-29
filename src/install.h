#pragma once
#include "common.h"

std::wstring InstallDir();   // %LOCALAPPDATA%\Programs\LayerSnow
std::wstring InstalledExe();
bool IsInstalledCopy();      // running from the install folder
int  RunInstaller();
int  RunUninstaller(bool quiet);
void CloseRunningInstance(); // asks the running copy to exit and waits
