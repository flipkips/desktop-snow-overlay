// Per-user install (no admin rights) and uninstall
#include "install.h"
#include "i18n.h"
#include "settings.h"
#include "snowui.h"
#include "ui.h"
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

static const wchar_t kUninstallKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" APP_NAME;

static std::wstring KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    std::wstring s;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p))) { s = p; CoTaskMemFree(p); }
    return s;
}

std::wstring InstallDir() { return KnownFolder(FOLDERID_LocalAppData) + L"\\Programs\\" APP_NAME; }
std::wstring InstalledExe() { return InstallDir() + L"\\" APP_NAME L".exe"; }
static std::wstring UninstallerExe() { return InstallDir() + L"\\uninstall.exe"; }
static std::wstring StartMenuDir() { return KnownFolder(FOLDERID_Programs) + L"\\" APP_NAME; }
static std::wstring DesktopLink() { return KnownFolder(FOLDERID_Desktop) + L"\\" APP_NAME L".lnk"; }

bool IsInstalledCopy() { return _wcsicmp(ExePath().c_str(), InstalledExe().c_str()) == 0; }

void CloseRunningInstance() {
    HWND w = FindWindowW(kMsgWndClass, nullptr);
    if (!w) return;
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    if (pid == GetCurrentProcessId()) return;
    HANDLE proc = OpenProcess(SYNCHRONIZE, FALSE, pid);
    PostMessageW(w, WM_CLOSE, 0, 0);
    if (proc) { WaitForSingleObject(proc, 5000); CloseHandle(proc); }
}

static bool CreateLink(const std::wstring& link, const std::wstring& target, const wchar_t* args = L"") {
    ComPtr<IShellLinkW> sl;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&sl)))) return false;
    sl->SetPath(target.c_str());
    sl->SetArguments(args);
    sl->SetWorkingDirectory(InstallDir().c_str());
    sl->SetIconLocation(target.c_str(), 0);
    sl->SetDescription(Tr(S_INSTALL_SUB));
    ComPtr<IPersistFile> pf;
    if (FAILED(sl.As(&pf))) return false;
    return SUCCEEDED(pf->Save(link.c_str(), TRUE));
}

static void RemoveStartMenu() {
    std::wstring dir = StartMenuDir();
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*.lnk").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do DeleteFileW((dir + L"\\" + fd.cFileName).c_str());
        while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir.c_str());
    DeleteFileW((KnownFolder(FOLDERID_Programs) + L"\\" APP_NAME L".lnk").c_str());  // shortcut from older versions
}

static void RegStr(HKEY k, const wchar_t* name, const std::wstring& v) {
    RegSetValueExW(k, name, 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t)));
}
static void RegDword(HKEY k, const wchar_t* name, DWORD v) {
    RegSetValueExW(k, name, 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
}

static bool CopyWithRetry(const std::wstring& from, const std::wstring& to) {
    for (int i = 0; i < 20; ++i) {
        if (CopyFileW(from.c_str(), to.c_str(), FALSE)) return true;
        Sleep(250);
    }
    return false;
}

static std::wstring DoInstall(bool autostart, bool desktop) {
    CloseRunningInstance();
    std::wstring dir = InstallDir(), exe = InstalledExe(), unins = UninstallerExe();
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    if (!CopyWithRetry(ExePath(), exe) || !CopyWithRetry(ExePath(), unins)) return Tr(S_COPY_FAILED) + dir;

    RemoveStartMenu();
    SHCreateDirectoryExW(nullptr, StartMenuDir().c_str(), nullptr);
    CreateLink(StartMenuDir() + L"\\" APP_NAME L".lnk", exe);
    CreateLink(StartMenuDir() + L"\\" + Tr(S_UNINSTALL_LINK) + L".lnk", unins);
    if (desktop) CreateLink(DesktopLink(), exe);
    else DeleteFileW(DesktopLink().c_str());
    SetAutostart(autostart, exe);
    SaveSettings();  // keeps the language picked in the installer

    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kUninstallKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) ==
        ERROR_SUCCESS) {
        RegStr(k, L"DisplayName", Tr(S_TRAY_TIP));
        RegStr(k, L"DisplayVersion", APP_VERSION);
        RegStr(k, L"Publisher", APP_AUTHOR);
        RegStr(k, L"URLInfoAbout", APP_REPO);
        RegStr(k, L"DisplayIcon", exe + L",0");
        RegStr(k, L"InstallLocation", dir);
        RegStr(k, L"UninstallString", L"\"" + unins + L"\"");
        RegStr(k, L"QuietUninstallString", L"\"" + unins + L"\" /quiet");
        RegDword(k, L"NoModify", 1);
        RegDword(k, L"NoRepair", 1);
        WIN32_FILE_ATTRIBUTE_DATA fa;
        if (GetFileAttributesExW(exe.c_str(), GetFileExInfoStandard, &fa))
            RegDword(k, L"EstimatedSize", 2 * (fa.nFileSizeLow / 1024 + 1));
        RegCloseKey(k);
    }
    return L"";
}

static void DoUninstall(bool deleteSettings) {
    CloseRunningInstance();
    SetAutostart(false);
    RemoveStartMenu();
    DeleteFileW(DesktopLink().c_str());
    RegDeleteKeyW(HKEY_CURRENT_USER, kUninstallKey);
    if (deleteSettings) {
        std::wstring cfg = SettingsDir();
        DeleteFileW((cfg + L"\\settings.ini").c_str());
        RemoveDirectoryW(cfg.c_str());
    }
}

// Delete the folder a moment later from a separate process: our own .exe may live there and be locked
static void ScheduleFolderRemoval() {
    std::wstring cmd = L"cmd.exe /c ping 127.0.0.1 -n 3 >nul & rd /s /q \"" + InstallDir() + L"\"";
    STARTUPINFOW si = {sizeof(si)};
    PROCESS_INFORMATION pi = {};
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);  // the working directory must not be the folder being deleted
    if (CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, tmp, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

static void RunPanel(SnowPanel& p) {
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (p.Hwnd() && IsDialogMessageW(p.Hwnd(), &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

static std::pair<std::function<int()>, std::function<void(int)>> BindBool(bool& b) {
    return {[&b] { return b ? 1 : 0; }, [&b](int v) { b = v != 0; }};
}

// ---------- Uninstall window ----------

enum { BTN_OK = 1, BTN_CANCEL = 2 };

int RunUninstaller(bool quiet) {
    if (quiet) {
        DoUninstall(true);
        ScheduleFolderRemoval();
        return 0;
    }
    static bool deleteSettings = true;
    bool confirmed = false;
    {
        SnowPanel p;
        p.SetHeader(Tr(S_UNINSTALL_TITLE), Tr(S_UNINSTALL_SUB));
        p.AddCard({20, 112, 500, 250}, Tr(S_UNINSTALL_CARD));
        p.AddText({40, 150, 484, 206}, std::wstring(Tr(S_UNINSTALL_DESC)) + L"\n" + Tr(S_FOLDER) + InstallDir());
        auto ds = BindBool(deleteSettings);
        p.AddToggle({40, 212, 484, 240}, Tr(S_DELETE_SETTINGS), ds.first, ds.second);
        p.AddButton({250, 266, 360, 304}, Tr(S_CANCEL), BTN_CANCEL);
        p.AddButton({370, 266, 500, 304}, Tr(S_UNINSTALL), BTN_OK, true);
        p.onDestroy = [] { PostQuitMessage(0); };
        auto onEvent = [&](int id) {
            if (id == BTN_OK) confirmed = true;
            if (id == BTN_OK || id == BTN_CANCEL) PostMessageW(p.Hwnd(), WM_CLOSE, 0, 0);
        };
        if (!p.Create(Tr(S_UNINSTALL_TITLE), 520, 344, onEvent)) return 1;
        RunPanel(p);
    }
    if (!confirmed) return 0;

    DoUninstall(deleteSettings);
    {
        SnowPanel done;
        done.SetHeader(Tr(S_REMOVED_TITLE), Tr(S_REMOVED_TEXT));
        done.AddButton({370, 130, 500, 168}, Tr(S_CLOSE), BTN_OK, true);
        done.onDestroy = [] { PostQuitMessage(0); };
        if (done.Create(Tr(S_UNINSTALL_TITLE), 520, 208, [&](int) { PostMessageW(done.Hwnd(), WM_CLOSE, 0, 0); }))
            RunPanel(done);
    }
    ScheduleFolderRemoval();
    return 0;
}

// ---------- Installer window ----------

int RunInstaller() {
    static bool autostart = true, desktop = false, runAfter = true;
    bool done = false, reload = true;
    const bool update = GetFileAttributesW(InstalledExe().c_str()) != INVALID_FILE_ATTRIBUTES;

    while (reload) {  // rebuilt when the language changes
        reload = false;
        const int lang = CurrentLang();
        SnowPanel p;
        p.SetHeader(Tr(update ? S_UPDATE_TITLE : S_INSTALL_TITLE), Tr(S_INSTALL_SUB));
        p.AddCard({20, 112, 500, 270}, Tr(S_WHAT_INSTALLED));
        p.AddText({40, 150, 484, 262}, std::wstring(Tr(S_INSTALL_DESC)) + L"\n\n" + Tr(S_FOLDER) + InstallDir());
        p.AddCard({20, 282, 500, 452}, Tr(S_OPTIONS));
        auto a = BindBool(autostart), d = BindBool(desktop), r = BindBool(runAfter);
        p.AddToggle({40, 324, 484, 352}, Tr(S_AUTOSTART), a.first, a.second);
        p.AddToggle({40, 354, 484, 382}, Tr(S_DESKTOP_LINK), d.first, d.second);
        p.AddToggle({40, 384, 484, 412}, Tr(S_RUN_AFTER), r.first, r.second);
        p.AddText({42, 420, 200, 444}, Tr(S_LANGUAGE));
        p.AddSegment({220, 416, 484, 444}, {kLangCodes[0], kLangCodes[1], kLangCodes[2], kLangCodes[3]},
                     [] { return CurrentLang(); }, [](int v) { Cfg().lang = v; });
        p.AddCaption({24, 466, 240, 484}, Tr(S_AUTHOR));
        p.AddLink({24, 486, 240, 506}, Tr(S_STAR_GITHUB), APP_REPO);
        p.AddButton({250, 468, 360, 506}, Tr(S_CANCEL), BTN_CANCEL);
        p.AddButton({370, 468, 500, 506}, Tr(update ? S_UPDATE : S_INSTALL), BTN_OK, true);
        p.onDestroy = [] { PostQuitMessage(0); };

        auto onEvent = [&](int id) {
            if (id == 0 && CurrentLang() != lang) {
                reload = true;
                PostMessageW(p.Hwnd(), WM_CLOSE, 0, 0);
            } else if (id == BTN_CANCEL) {
                PostMessageW(p.Hwnd(), WM_CLOSE, 0, 0);
            } else if (id == BTN_OK) {
                SetCursor(LoadCursor(nullptr, IDC_WAIT));
                std::wstring err = DoInstall(autostart, desktop);
                if (!err.empty()) {
                    MessageBoxW(p.Hwnd(), err.c_str(), Tr(S_INSTALL_ERROR), MB_OK | MB_ICONERROR);
                    return;
                }
                done = true;
                if (runAfter)
                    ShellExecuteW(nullptr, L"open", InstalledExe().c_str(), L"/welcome", InstallDir().c_str(),
                                  SW_SHOWNORMAL);
                else
                    MessageBoxW(p.Hwnd(), Tr(S_INSTALL_DONE), L"LayerSnow", MB_OK | MB_ICONINFORMATION);
                PostMessageW(p.Hwnd(), WM_CLOSE, 0, 0);
            }
        };
        if (!p.Create(Tr(update ? S_UPDATE_TITLE : S_INSTALL_TITLE), 520, 546, onEvent)) return 1;
        RunPanel(p);
    }
    return done ? 0 : 1;
}
