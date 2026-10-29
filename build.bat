@echo off
rem Build LayerSnow -> dist\LayerSnow-Setup.exe
setlocal
cd /d "%~dp0"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
    echo Visual Studio Build Tools ^(C++^) not found
    exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1 || exit /b 1

if not exist build mkdir build
if not exist dist mkdir dist
if not exist res\snow.ico python tools\make_icon.py || exit /b 1

rc /nologo /fo build\app.res res\app.rc || exit /b 1
cl /nologo /std:c++17 /O2 /MT /W3 /utf-8 /EHsc /DNDEBUG /Fo"build\\" /Fe"dist\LayerSnow-Setup.exe" ^
   src\*.cpp build\app.res ^
   /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib shell32.lib ole32.lib advapi32.lib comctl32.lib ^
   d3d11.lib dxgi.lib d2d1.lib dwrite.lib dcomp.lib dwmapi.lib shcore.lib || exit /b 1

rem The same binary uninstalls when its file name contains "uninstall"
copy /y dist\LayerSnow-Setup.exe dist\LayerSnow-Uninstall.exe >nul || exit /b 1

echo.
echo OK: dist\LayerSnow-Setup.exe, dist\LayerSnow-Uninstall.exe
