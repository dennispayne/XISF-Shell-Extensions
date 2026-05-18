// ShellExtensionHost.cpp - Settings UI + hardened catalog installer front-end.
//
// All security-sensitive operations (URL handling, TLS, hashing, atomic rename,
// URL allow-list) live in CatalogInstaller.cpp. This file is UI wiring only.

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <propsys.h>
#include <searchapi.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <memory>
#include <cstdint>
#include <commctrl.h>
#include <filesystem>
#include <unordered_map>
#include <tlhelp32.h>
#include <algorithm>
#include <fstream>
#include <cwctype>

#include "HostResources.h"
#include "HostSettings.h"
#include "Paths.h"
#include "CatalogSpec.h"
#include "CatalogInstaller.h"
#include "Sha256.h"
#include "HandlerDllPath.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "propsys.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "version.lib")

#ifndef XISF_VERSION_TEXT
#define XISF_VERSION_TEXT 0.1.0.0
#endif
#define _XS(x)  L ## #x
#define _XS_(x) _XS(x)
#define XISF_VERSION_WSTR _XS_(XISF_VERSION_TEXT)

using namespace xisf;

namespace {

HWND              g_hDlg = nullptr;
std::atomic<bool> g_cancelRequested{false};
std::atomic<bool> g_opInProgress{false};

static constexpr UINT WM_XISF_PROGRESS = WM_APP + 1;
static constexpr UINT WM_XISF_DONE     = WM_APP + 2;
static constexpr UINT WM_XISF_TRACE_EXPORT_DONE = WM_APP + 3;
static constexpr UINT WM_XISF_AUTO_INSTALL_DONE = WM_APP + 4;

COLORREF g_iconOkColor = RGB(18, 130, 44);
COLORREF g_iconWarnColor = RGB(196, 130, 0);
COLORREF g_iconBadColor = RGB(180, 32, 32);

COLORREF g_propertyIconColor = RGB(180, 32, 32);
COLORREF g_previewIconColor = RGB(180, 32, 32);
COLORREF g_filterIconColor = RGB(180, 32, 32);

// Tooltip text storage — keeps strings alive for the tooltip control
std::unordered_map<int, std::wstring> g_tooltipStrings;

std::wstring g_tracePath;
bool g_traceRunning = false;
std::atomic<bool> g_traceExportInProgress{false};
std::wstring g_activeTracePath;
std::wstring g_lastStoppedTracePath;

struct CatalogRowModel {
    bool builtIn = false;
    int builtInIndex = -1;
    installer::PresenceState presence = installer::PresenceState::Missing;
    std::wstring fileName;
    std::wstring sourceLink;
    std::wstring sourceHashDisplay;
    std::wstring localHashDisplay;
    std::wstring statusGlyph;
};

std::vector<CatalogRowModel> g_catalogRows;
std::unordered_map<std::wstring, std::wstring> g_importedSourceByFile;

enum class InstallMode {
    PinnedBuiltIn,
    FileImportUnverified,
};

InstallMode g_activeInstallMode = InstallMode::PinnedBuiltIn;
std::wstring g_activeTargetFileName;
std::wstring g_activeImportSourcePath;
std::wstring g_activeExpectedHash;

struct TraceExportResult {
    bool success = false;
    DWORD exitCode = 1;
    std::wstring xmlPath;
};

bool RunProcessHiddenAndWait(const std::wstring& exe, const std::wstring& args, DWORD* exitCode = nullptr);
bool RunProcessHiddenAndWaitTimeout(const std::wstring& exe, const std::wstring& args, DWORD timeoutMs, DWORD* exitCode = nullptr);

bool IsTraceSessionRunning()
{
    std::wstring exe = L"C:\\Windows\\System32\\logman.exe";
    DWORD ec = 1;
    return RunProcessHiddenAndWait(exe, L"query XISFTrace -ets", &ec) && ec == 0;
}

// Helper function to build documentation URLs
std::wstring GetDocumentationUrl(const wchar_t* docPath)
{
    // Construct local file:// URL or GitHub URL fallback
    // Try local path first (for MSI installs), fall back to GitHub
    wchar_t appDir[MAX_PATH]{};
    DWORD len = GetModuleFileNameW(nullptr, appDir, ARRAYSIZE(appDir));
    if (len > 0) {
        // Extract directory from exe path
        wchar_t* lastBackslash = wcsrchr(appDir, L'\\');
        if (lastBackslash) {
            *lastBackslash = L'\0';
        }
    }

    std::wstring localPath = appDir;
    localPath += L"\\docs\\";
    localPath += docPath;

    // Check if file exists at local path
    if (GetFileAttributesW(localPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        // Convert to file:// URL
        std::wstring fileUrl = L"file:///";
        for (wchar_t c : localPath) {
            if (c == L'\\') fileUrl += L'/';
            else fileUrl += c;
        }
        return fileUrl;
    }

    // Fall back to GitHub
    return L"https://github.com/dennispayne/XISF-Shell-Extensions/tree/main/docs/" + std::wstring(docPath);
}

void OpenDocumentation(HWND hDlg, const wchar_t* docPath)
{
    std::wstring url = GetDocumentationUrl(docPath);
    ShellExecuteW(hDlg, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

std::wstring BuildTimestampedTracePath()
{
    wchar_t tmp[MAX_PATH]{};
    GetTempPathW(ARRAYSIZE(tmp), tmp);

    std::filesystem::path dir(tmp);
    dir /= L"xisf";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t name[128]{};
    swprintf_s(name, L"xisf-%04u%02u%02u-%02u%02u%02u.etl",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    auto p = dir / name;
    return p.wstring();
}

void UpdateTraceActionButtons(HWND hDlg)
{
    const bool hasStoppedTrace = !g_lastStoppedTracePath.empty() &&
        GetFileAttributesW(g_lastStoppedTracePath.c_str()) != INVALID_FILE_ATTRIBUTES;
    const bool busy = g_traceExportInProgress.load(std::memory_order_relaxed);

    EnableWindow(GetDlgItem(hDlg, IDC_BTN_TRACE_START), busy ? FALSE : TRUE);
    EnableWindow(GetDlgItem(hDlg, IDC_BTN_TRACE_STOP), busy ? FALSE : TRUE);
    EnableWindow(GetDlgItem(hDlg, IDC_BTN_TRACE_OPEN_ETL), (!busy && !g_traceRunning && hasStoppedTrace) ? TRUE : FALSE);
    EnableWindow(GetDlgItem(hDlg, IDC_BTN_TRACE_EXPORT_XML), (!busy && !g_traceRunning && hasStoppedTrace) ? TRUE : FALSE);
}

void SetProgressText(const std::wstring& s);
void RefreshAllPresence();
void AddTooltips();
void SetTooltip(int id, const wchar_t* text);
void OnRegisterHandlers();
void OnFetchOnline(int idx);
void OnLocalImport(HWND hDlg, std::wstring targetFileName, std::wstring path);
void UpdateCatalogActionButtons();
void RemoveImportedSourcePath(std::wstring_view fileName);
bool TryReadRegisteredDllPath(const wchar_t* clsid, std::wstring& out);
INT_PTR CALLBACK AdvancedDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);

// Handler CLSIDs
static constexpr const wchar_t* kPropertyClsid = L"{7C54FA8B-9D63-4C10-8FBE-1A5A0F9A3B2E}";
static constexpr const wchar_t* kPreviewClsid  = L"{9C76E8AD-4E85-5F30-B00D-3C7D1AB5F6E0}";
static constexpr const wchar_t* kFilterClsid   = L"{B4E7F2A1-3D8C-4F5E-9A1B-6C2D8E4F7A3B}";

// --- Deferred-settings model ---
// Changes are tracked here and only written to registry on Apply.
struct PendingSettings {
    bool origPropertyEnabled = true;
    bool origPreviewEnabled = true;
    bool origFilterEnabled = true;
    hostsettings::FeatureTier origTier = hostsettings::FeatureTier::Full;
    bool origProjection = true;

    bool curPropertyEnabled = true;
    bool curPreviewEnabled = true;
    bool curFilterEnabled = true;
    hostsettings::FeatureTier curTier = hostsettings::FeatureTier::Full;
    bool curProjection = true;

    // Pending registration requests (for unregistered handlers)
    bool registerProperty = false;
    bool registerPreview = false;
    bool registerFilter = false;

    void Snapshot() {
        origPropertyEnabled = curPropertyEnabled = hostsettings::IsPropertyEnabled();
        origPreviewEnabled  = curPreviewEnabled  = hostsettings::IsPreviewEnabled();
        origFilterEnabled   = curFilterEnabled   = hostsettings::IsFilterEnabled();
        origTier            = curTier            = hostsettings::GetFeatureTier();
        origProjection      = curProjection      = hostsettings::IsProjectionEnabled();
        registerProperty = registerPreview = registerFilter = false;
    }

    int DirtyCount() const {
        int n = 0;
        if (curPropertyEnabled != origPropertyEnabled) n++;
        if (curPreviewEnabled  != origPreviewEnabled)  n++;
        if (curFilterEnabled   != origFilterEnabled)   n++;
        if (curTier            != origTier)            n++;
        if (curProjection      != origProjection)      n++;
        if (registerProperty) n++;
        if (registerPreview)  n++;
        if (registerFilter)   n++;
        return n;
    }

    bool HasPendingRegistration() const {
        return registerProperty || registerPreview || registerFilter;
    }

    bool NeedsElevation() const {
        return g_hDlg && (IsDlgButtonChecked(g_hDlg, IDC_CHK_RESTART_EXPLORER) == BST_CHECKED);
    }

    void Apply() {
        if (curPropertyEnabled != origPropertyEnabled) {
            hostsettings::SetPropertyEnabled(curPropertyEnabled);
            origPropertyEnabled = curPropertyEnabled;
        }
        if (curPreviewEnabled != origPreviewEnabled) {
            hostsettings::SetPreviewEnabled(curPreviewEnabled);
            origPreviewEnabled = curPreviewEnabled;
        }
        if (curFilterEnabled != origFilterEnabled) {
            hostsettings::SetFilterEnabled(curFilterEnabled);
            origFilterEnabled = curFilterEnabled;
        }
        if (curTier != origTier) {
            hostsettings::SetFeatureTier(curTier);
            origTier = curTier;
        }
        if (curProjection != origProjection) {
            hostsettings::SetProjectionEnabled(curProjection);
            origProjection = curProjection;
        }
    }
};

PendingSettings g_pending;

void UpdatePendingDisplay()
{
    int n = g_pending.DirtyCount();
    bool restart = g_hDlg && (IsDlgButtonChecked(g_hDlg, IDC_CHK_RESTART_EXPLORER) == BST_CHECKED);
    if (n > 0) {
        wchar_t buf[64];
        swprintf_s(buf, L"%d change%s pending", n, n == 1 ? L"" : L"s");
        SetDlgItemTextW(g_hDlg, IDC_STATIC_PENDING_TEXT, buf);
    } else {
        SetDlgItemTextW(g_hDlg, IDC_STATIC_PENDING_TEXT, L"");
    }
    EnableWindow(GetDlgItem(g_hDlg, IDC_BTN_APPLY), (n > 0 || restart) ? TRUE : FALSE);
    SendDlgItemMessageW(g_hDlg, IDC_BTN_APPLY, BCM_SETSHIELD, 0,
                        restart ? TRUE : FALSE);
}

void UpdateToggleButton(int btnId, bool enabled)
{
    SetDlgItemTextW(g_hDlg, btnId, enabled ? L"Disable" : L"Enable");
}

// Check if a handler CLSID is registered and its DLL exists
bool IsHandlerRegistered(const wchar_t* clsid)
{
    std::wstring dll;
    return TryReadRegisteredDllPath(clsid, dll) &&
           GetFileAttributesW(dll.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// Update toggle button text based on registration + enabled state + pending registration
void UpdateToggleButtonState(int btnId, const wchar_t* clsid, bool enabled, bool pendingRegister)
{
    if (pendingRegister) {
        SetDlgItemTextW(g_hDlg, btnId, L"Cancel");
    } else if (IsHandlerRegistered(clsid)) {
        SetDlgItemTextW(g_hDlg, btnId, enabled ? L"Disable" : L"Enable");
    } else {
        SetDlgItemTextW(g_hDlg, btnId, L"Register");
    }
}

std::wstring GetExePath()
{
    wchar_t p[MAX_PATH]{};
    GetModuleFileNameW(nullptr, p, ARRAYSIZE(p));
    return p;
}

std::wstring GetExeDir()
{
    auto p = std::filesystem::path(GetExePath());
    return p.parent_path().wstring();
}

std::wstring FindSolutionRoot()
{
    namespace fs = std::filesystem;
    fs::path cur = fs::path(GetExeDir());
    for (int i = 0; i < 8; ++i) {
        if (fs::exists(cur / L"Win11-XISF-Shell-Extensions.sln")) {
            return cur.wstring();
        }
        if (!cur.has_parent_path()) break;
        cur = cur.parent_path();
    }
    return L"";
}

std::wstring BuildHandlerDllPath(hostpaths::HandlerType handler)
{
    auto root = FindSolutionRoot();
    if (root.empty()) return L"";
#ifdef _DEBUG
    constexpr const wchar_t* cfg = L"Debug";
#else
    constexpr const wchar_t* cfg = L"Release";
#endif
    return hostpaths::ResolveHandlerDllPath(root, handler, cfg);
}

bool RunProcessHiddenAndWait(const std::wstring& exe, const std::wstring& args, DWORD* exitCode)
{
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};

    std::wstring cmd = L"\"" + exe + L"\" " + args;
    BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi);
    if (!ok) return false;

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD ec = 1;
    GetExitCodeProcess(pi.hProcess, &ec);
    if (exitCode) *exitCode = ec;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

bool RunProcessHiddenAndWaitTimeout(const std::wstring& exe, const std::wstring& args, DWORD timeoutMs, DWORD* exitCode)
{
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};

    std::wstring cmd = L"\"" + exe + L"\" " + args;
    BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi);
    if (!ok) return false;

    DWORD wr = WaitForSingleObject(pi.hProcess, timeoutMs);
    if (wr == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return false;
    }

    DWORD ec = 1;
    GetExitCodeProcess(pi.hProcess, &ec);
    if (exitCode) *exitCode = ec;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

void DeleteClsidTreeBothRoots(const wchar_t* clsid)
{
    std::wstring k1 = L"SOFTWARE\\Classes\\CLSID\\";
    k1 += clsid;
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, k1.c_str());

    std::wstring k2 = L"CLSID\\";
    k2 += clsid;
    RegDeleteTreeW(HKEY_CLASSES_ROOT, k2.c_str());
}

void RestartExplorerDirect()
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe{};
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(snap, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, L"explorer.exe") == 0) {
                    HANDLE ph = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                    if (ph) {
                        TerminateProcess(ph, 0);
                        CloseHandle(ph);
                    }
                }
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
    }
    Sleep(2000);
    ShellExecuteW(nullptr, L"open", L"explorer.exe", nullptr, nullptr, SW_SHOWNORMAL);
}

bool RegisterOneHandlerDirect(hostpaths::HandlerType handler, std::wstring& err)
{
    auto dll = BuildHandlerDllPath(handler);
    if (dll.empty() || GetFileAttributesW(dll.c_str()) == INVALID_FILE_ATTRIBUTES) {
        err = L"Handler DLL not found.";
        return false;
    }

    const std::wstring regsvr = L"C:\\Windows\\System32\\regsvr32.exe";
    if (!RunProcessHiddenAndWait(regsvr, L"/u /s \"" + dll + L"\"")) {
        err = L"regsvr32 unregister failed.";
        return false;
    }

    switch (handler) {
    case hostpaths::HandlerType::Property:
        DeleteClsidTreeBothRoots(L"{7C54FA8B-9D63-4C10-8FBE-1A5A0F9A3B2E}");
        break;
    case hostpaths::HandlerType::Preview:
        DeleteClsidTreeBothRoots(L"{9C76E8AD-4E85-5F30-B00D-3C7D1AB5F6E0}");
        DeleteClsidTreeBothRoots(L"{AD87F6CE-5B03-6E41-C11E-4DB2AC06F5F1}");
        break;
    case hostpaths::HandlerType::Filter:
        DeleteClsidTreeBothRoots(L"{B4E7F2A1-3D8C-4F5E-9A1B-6C2D8E4F7A3B}");
        break;
    }

    RestartExplorerDirect();

    DWORD ec = 1;
    if (!RunProcessHiddenAndWait(regsvr, L"/s \"" + dll + L"\"", &ec) || ec != 0) {
        err = L"regsvr32 register failed.";
        return false;
    }

    return true;
}

int RunElevatedRegistrationMode(bool doProperty, bool doPreview, bool doFilter)
{
    std::wstring err;
    if (doProperty) {
        if (!RegisterOneHandlerDirect(hostpaths::HandlerType::Property, err)) return 2;
    }
    if (doPreview) {
        if (!RegisterOneHandlerDirect(hostpaths::HandlerType::Preview, err)) return 3;
    }
    if (doFilter) {
        if (!RegisterOneHandlerDirect(hostpaths::HandlerType::Filter, err)) return 4;
    }
    return 0;
}

bool StartsWith(const std::wstring& s, const std::wstring& prefix)
{
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

// ────────────────────────────────────────────────────────────────────
// Windows Search index scope management (optional MSI feature)
//
// Invoked by the MSI's --add-search-path / --remove-search-path CAs
// when the user provides an XISF data root in the installer UI. The
// XISFFilter handler we register is what makes .xisf indexable; this
// merely tells the System Index *where* to look.
//
// All paths are best-effort: if Windows Search is unavailable (service
// disabled, perms denied, COM not registered) we return 0 so the MSI
// never rolls back over an indexing detail.
// ────────────────────────────────────────────────────────────────────

std::wstring PathToCrawlUrl(const std::wstring& raw)
{
    std::wstring p = raw;
    auto isSpace = [](wchar_t c) { return std::iswspace(static_cast<wint_t>(c)) != 0; };
    while (!p.empty() && isSpace(p.front())) p.erase(p.begin());
    while (!p.empty() && isSpace(p.back()))  p.pop_back();
    while (!p.empty() && (p.back() == L'\\' || p.back() == L'/'))
        p.pop_back();
    if (p.empty()) return {};
    for (auto& c : p) if (c == L'\\') c = L'/';
    return L"file:///" + p + L"/";
}

enum class SearchPathOp { Add, Remove };

int RunSearchPathOp(SearchPathOp op, const std::wstring& pathIn)
{
    const std::wstring url = PathToCrawlUrl(pathIn);
    if (url.empty()) return 0;

    // Defined inline to avoid pulling in searchsdk.lib for a single GUID.
    static const CLSID kClsidCSearchManager =
        { 0x7D096C5F, 0xAC08, 0x4F1F, { 0xBE, 0xB7, 0x5C, 0x22, 0xC5, 0x17, 0xCE, 0x39 } };

    const HRESULT hrCom    = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool    needUninit = SUCCEEDED(hrCom);

    ISearchManager* mgr = nullptr;
    HRESULT hr = CoCreateInstance(kClsidCSearchManager, nullptr,
                                  CLSCTX_LOCAL_SERVER,
                                  IID_PPV_ARGS(&mgr));
    if (SUCCEEDED(hr) && mgr) {
        ISearchCatalogManager* cat = nullptr;
        hr = mgr->GetCatalog(L"SystemIndex", &cat);
        if (SUCCEEDED(hr) && cat) {
            ISearchCrawlScopeManager* csm = nullptr;
            hr = cat->GetCrawlScopeManager(&csm);
            if (SUCCEEDED(hr) && csm) {
                if (op == SearchPathOp::Add)
                    csm->AddDefaultScopeRule(url.c_str(), TRUE, FF_INDEXCOMPLEXURLS);
                else
                    csm->RemoveScopeRule(url.c_str());
                csm->SaveAll();
                csm->Release();
            }
            cat->Release();
        }
        mgr->Release();
    }

    if (needUninit) CoUninitialize();
    return 0;
}

// Headless mode: install all missing/mismatched catalogs silently.
// Returns 0 if all catalogs are verified, 1 if any install failed.
int RunSilentCatalogInstall()
{
    int failures = 0;
    for (size_t i = 0; i < catalogspec::kAllCatalogs.size(); ++i) {
        const auto* src = catalogspec::kAllCatalogs[i];
        auto p = installer::Probe(*src);
        if (p.state == installer::PresenceState::PresentVerified)
            continue;

        installer::Report rep{};
        if (i == 2 || i == 3) {
            wchar_t modPath[MAX_PATH] = {};
            GetModuleFileNameW(nullptr, modPath, MAX_PATH);
            PathRemoveFileSpecW(modPath);
            std::wstring seedPath = std::wstring(modPath) + L"\\" + std::wstring(src->fileName);
            rep = installer::InstallFromLocalFileVerified(
                *src, seedPath.c_str(),
                [](std::uint64_t, std::uint64_t, void*) -> bool { return true; }, nullptr);
        } else {
            rep = installer::InstallFromPinnedUrl(
                *src, [](std::uint64_t, std::uint64_t, void*) -> bool { return true; }, nullptr);
        }
        if (rep.result != installer::Result::Ok)
            failures++;
    }
    return failures > 0 ? 1 : 0;
}

// ---------------------------------------------------------------------------
// MSIX registration: write per-user shell metadata to HKCU.
//
// MSIX manifests register COM classes and file type associations but do NOT
// call DllRegisterServer.  Explorer also needs FullDetails / PreviewDetails /
// InfoTip to know which columns to display, plus KindMap, PerceivedType,
// Content Type, and the propdesc schema for custom property definitions.
//
// All writes target HKCU (no elevation required).  Idempotent — safe to call
// on every login via the windows.startupTask manifest extension.
// ---------------------------------------------------------------------------
int RunMsixRegistration()
{
    auto SetHKCU = [](const wchar_t* subKey, const wchar_t* valueName,
                      const wchar_t* data) -> bool {
        HKEY hKey = nullptr;
        DWORD dwDisp = 0;
        LONG lr = RegCreateKeyExW(HKEY_CURRENT_USER, subKey, 0, nullptr,
                                  REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr,
                                  &hKey, &dwDisp);
        if (lr != ERROR_SUCCESS) return false;
        DWORD cbData = static_cast<DWORD>((wcslen(data) + 1) * sizeof(wchar_t));
        lr = RegSetValueExW(hKey, valueName, 0, REG_SZ,
                            reinterpret_cast<const BYTE*>(data), cbData);
        RegCloseKey(hKey);
        return lr == ERROR_SUCCESS;
    };

    // FullDetails — determines columns in Explorer Details view.
    // Must match the authoritative copy in dllmain.cpp.
    static const wchar_t kFullDetails[] =
        L"prop:System.PropGroup.Description;"
        L"XISF.Constellation;XISF.Dec;XISF.DecBand;"
        L"XISF.MatchedObjects;XISF.ObjectDec;XISF.ObjectName;XISF.ObjectRA;"
        L"XISF.RA;XISF.RAHour;"
        L"System.PropGroup.Origin;"
        L"XISF.DateLocal;XISF.DateObserved;XISF.Software;"
        L"System.PropGroup.Image;"
        L"XISF.Airmass;XISF.Altitude;XISF.Azimuth;"
        L"XISF.ChannelCount;XISF.ColorSpace;XISF.DataState;"
        L"XISF.ExposureTime;XISF.FilterName;"
        L"XISF.ImageCount;XISF.ImageHeight;XISF.ImageType;XISF.ImageWidth;"
        L"XISF.PierSide;XISF.Rotation;XISF.SampleFormat;"
        L"System.PropGroup.Camera;"
        L"XISF.BayerPattern;XISF.Binning;XISF.CameraModel;"
        L"XISF.FilterWheel;XISF.FNumber;XISF.FocalLength;"
        L"XISF.FocuserName;XISF.FocuserPosition;XISF.FocuserTemp;"
        L"XISF.Gain;XISF.Offset;XISF.PixelSize;XISF.ReadoutMode;"
        L"XISF.RotatorAngle;XISF.RotatorName;"
        L"XISF.SensorTemperature;XISF.SetTemp;XISF.Telescope;"
        L"System.PropGroup.PhotoAdvanced;"
        L"XISF.GuideDec;XISF.GuideRA;"
        L"XISF.Median;XISF.Mean;XISF.ClippingLow;XISF.ClippingHigh;"
        L"XISF.StarFWHM;XISF.SkyBrightness;XISF.SkyQuality;"
        L"System.PropGroup.GPS;"
        L"XISF.SiteElevation;XISF.SiteLatitude;XISF.SiteLongitude;"
        L"XISF.AmbientTemp;XISF.CloudCover;XISF.DewPoint;XISF.Humidity;"
        L"XISF.Pressure;XISF.SkyTemp;XISF.WindSpeed;"
        L"System.PropGroup.FileSystem;"
        L"System.ItemNameDisplay;System.ItemType;System.ItemFolderPathDisplay;"
        L"System.DateCreated;System.DateModified;System.Size;System.FileAttributes";

    static const wchar_t kPreviewDetails[] =
        L"prop:XISF.ObjectName;XISF.ExposureTime;XISF.FilterName;XISF.CameraModel;"
        L"XISF.Gain;XISF.SensorTemperature;XISF.Telescope;XISF.FocalLength;XISF.FNumber;"
        L"XISF.Constellation;XISF.MatchedObjects;XISF.DataState;XISF.ColorSpace;XISF.SampleFormat";

    static const wchar_t kInfoTip[] =
        L"prop:System.ItemTypeText;System.Size;XISF.ObjectName;XISF.ExposureTime;XISF.FilterName;"
        L"XISF.CameraModel;XISF.Constellation;XISF.MatchedObjects";

    int failures = 0;

    // Property handler CLSID mapping — tells the Property System which COM
    // object to instantiate for .xisf files.  DllRegisterServer writes this
    // to HKLM; for per-user MSIX we try HKCU (may or may not be honored).
    const wchar_t* phKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\"
                           L"PropertySystem\\PropertyHandlers\\.xisf";
    if (!SetHKCU(phKey, nullptr, L"{7C54FA8B-9D63-4C10-8FBE-1A5A0F9A3B2E}"))
        failures++;

    // shellex handler CLSID mappings — Explorer uses these GUIDs to look up
    // which COM class provides thumbnails, preview, and property sheets.
    const wchar_t* thumbShellEx =
        L"Software\\Classes\\.xisf\\shellex\\"
        L"{E357FCCD-A995-4576-B01F-234630154E96}";
    SetHKCU(thumbShellEx, nullptr, L"{9C76E8AD-4E85-5F30-B00D-3C7D1AB5F6E0}");

    const wchar_t* previewShellEx =
        L"Software\\Classes\\.xisf\\shellex\\"
        L"{8895b1c6-b41f-4c1c-a562-0d564250836f}";
    SetHKCU(previewShellEx, nullptr, L"{AD87F6CE-5B03-6E41-C11E-4DB2AC06F5F1}");

    // Property sheet handler (Astro details tab)
    const wchar_t* propSheetKey =
        L"Software\\Classes\\SystemFileAssociations\\.xisf\\shellex\\"
        L"PropertySheetHandlers\\XISFHistogram";
    SetHKCU(propSheetKey, nullptr, L"{A3B7C8D9-E1F2-4A5B-8C6D-7E8F9A0B1C2D}");

    // SystemFileAssociations\.xisf — reliable regardless of ProgID resolution
    const wchar_t* sfaKey = L"Software\\Classes\\SystemFileAssociations\\.xisf";
    if (!SetHKCU(sfaKey, L"FullDetails",    kFullDetails))    failures++;
    if (!SetHKCU(sfaKey, L"PreviewDetails", kPreviewDetails)) failures++;
    if (!SetHKCU(sfaKey, L"InfoTip",        kInfoTip))        failures++;

    // XISFFile ProgID
    const wchar_t* progIdKey = L"Software\\Classes\\XISFFile";
    SetHKCU(progIdKey, nullptr,          L"XISF Image File");
    SetHKCU(progIdKey, L"FullDetails",    kFullDetails);
    SetHKCU(progIdKey, L"PreviewDetails", kPreviewDetails);
    SetHKCU(progIdKey, L"InfoTip",        kInfoTip);

    // .xisf extension metadata
    const wchar_t* extKey = L"Software\\Classes\\.xisf";
    SetHKCU(extKey, L"Content Type",  L"application/xisf");
    SetHKCU(extKey, L"PerceivedType", L"image");

    // KindMap — tells Explorer .xisf is a "picture"
    SetHKCU(L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\KindMap",
            L".xisf", L"picture");

    // Register property description schema.
    // The propdesc file inside WindowsApps may not be readable by Explorer
    // (ACL restrictions), so copy it to a user-accessible location first.
    wchar_t szExePath[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, szExePath, MAX_PATH)) {
        PathRemoveFileSpecW(szExePath);
        wchar_t szSrcPropdesc[MAX_PATH];
        wcscpy_s(szSrcPropdesc, szExePath);
        PathAppendW(szSrcPropdesc, L"xisf.propdesc");
        if (GetFileAttributesW(szSrcPropdesc) != INVALID_FILE_ATTRIBUTES) {
            // Copy to %LOCALAPPDATA%\DennisPayne\XISF Shell Extension
            wchar_t szLocalAppData[MAX_PATH] = {};
            if (SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0,
                                 szLocalAppData) == S_OK) {
                wchar_t szDestDir[MAX_PATH];
                wcscpy_s(szDestDir, szLocalAppData);
                PathAppendW(szDestDir, L"DennisPayne\\XISF Shell Extension");
                CreateDirectoryW(szDestDir, nullptr);
                // Ensure parent exists
                wchar_t szParent[MAX_PATH];
                wcscpy_s(szParent, szLocalAppData);
                PathAppendW(szParent, L"DennisPayne");
                CreateDirectoryW(szParent, nullptr);
                CreateDirectoryW(szDestDir, nullptr);

                wchar_t szDestPropdesc[MAX_PATH];
                wcscpy_s(szDestPropdesc, szDestDir);
                PathAppendW(szDestPropdesc, L"xisf.propdesc");
                CopyFileW(szSrcPropdesc, szDestPropdesc, FALSE);

                HRESULT hr = PSRegisterPropertySchema(szDestPropdesc);
                if (FAILED(hr)) failures++;
            } else {
                // Fallback: register from package location
                HRESULT hr = PSRegisterPropertySchema(szSrcPropdesc);
                if (FAILED(hr)) failures++;
            }
        }
    }

    // Notify Explorer of association changes
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);

    return failures > 0 ? 1 : 0;
}

bool TryHandleCommandMode(LPWSTR rawCmdLine, int& exitCode)
{
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv || argc <= 1) {
        if (argv) LocalFree(argv);
        return false;
    }

    bool mode = false;
    bool adv = false;
    bool prop = false;
    bool prev = false;
    bool filt = false;
    bool silentInstall = false;
    bool registerMsix = false;
    bool searchAdd = false;
    bool searchRemove = false;
    std::wstring searchPath;
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--register-direct") mode = true;
        else if (a == L"--advanced-direct") adv = true;
        else if (a == L"--property") prop = true;
        else if (a == L"--preview") prev = true;
        else if (a == L"--filter") filt = true;
        else if (a == L"--silent-install") silentInstall = true;
        else if (a == L"--register-msix") registerMsix = true;
        else if (a == L"--add-search-path" && i + 1 < argc) {
            searchAdd = true; searchPath = argv[++i];
        }
        else if (a == L"--remove-search-path" && i + 1 < argc) {
            searchRemove = true; searchPath = argv[++i];
        }
    }
    LocalFree(argv);

    if (searchAdd) {
        exitCode = RunSearchPathOp(SearchPathOp::Add, searchPath);
        return true;
    }
    if (searchRemove) {
        exitCode = RunSearchPathOp(SearchPathOp::Remove, searchPath);
        return true;
    }

    if (registerMsix) {
        exitCode = RunMsixRegistration();
        return true;
    }

    if (silentInstall) {
        exitCode = RunSilentCatalogInstall();
        return true;
    }

    if (adv) {
        exitCode = 100; // sentinel consumed by wWinMain to open Advanced directly
        return true;
    }

    if (!mode) return false;
    exitCode = RunElevatedRegistrationMode(prop, prev, filt);
    return true;
}

std::wstring FormatBytes(std::uint64_t n)
{
    wchar_t buf[64];
    if (n < 1024) swprintf_s(buf, L"%llu B", n);
    else if (n < 1024ull * 1024ull) swprintf_s(buf, L"%.1f KB", n / 1024.0);
    else swprintf_s(buf, L"%.2f MB", n / (1024.0 * 1024.0));
    return buf;
}

const wchar_t* ResultName(installer::Result r)
{
    using R = installer::Result;
    switch (r) {
        case R::Ok:                    return L"OK";
        case R::AllocFailed:           return L"out of memory";
        case R::CatalogDirUnavailable: return L"catalog directory unavailable";
        case R::HttpOpenFailed:        return L"WinHTTP open failed";
        case R::HttpConnectFailed:     return L"WinHTTP connect failed";
        case R::HttpRequestFailed:     return L"HTTP request failed";
        case R::HttpBadStatus:         return L"HTTP status not 200";
        case R::UrlNotAllowed:         return L"URL not in allow-list";
        case R::SizeExceeded:          return L"size exceeded cap";
        case R::WriteFailed:           return L"disk write failed";
        case R::HashInitFailed:        return L"SHA-256 init failed";
        case R::HashFailed:            return L"SHA-256 streaming failed";
        case R::HashMismatch:          return L"SHA-256 mismatch (rejected)";
        case R::InvalidContent:        return L"content validation failed";
        case R::MoveFailed:            return L"atomic rename failed";
        case R::SourceOpenFailed:      return L"cannot open source file";
        case R::OperationCancelled:    return L"cancelled";
    }
    return L"unknown";
}

void SetStatusLabelColorById(int id, COLORREF color)
{
    if (id == IDC_STATIC_PROPERTY_STATUS) g_propertyIconColor = color;
    else if (id == IDC_STATIC_PREVIEW_STATUS) g_previewIconColor = color;
    else if (id == IDC_STATIC_FILTER_STATUS) g_filterIconColor = color;
}

void SetStatusIconTextAndColor(int id, const wchar_t* icon, COLORREF color)
{
    SetDlgItemTextW(g_hDlg, id, icon);
    SetStatusLabelColorById(id, color);
    InvalidateRect(GetDlgItem(g_hDlg, id), nullptr, TRUE);
}

std::wstring GetFileVersionString(const std::wstring& path)
{
    DWORD handle = 0;
    DWORD cb = GetFileVersionInfoSizeW(path.c_str(), &handle);
    if (!cb) return L"";

    std::vector<BYTE> blob(cb);
    if (!GetFileVersionInfoW(path.c_str(), 0, cb, blob.data())) return L"";

    VS_FIXEDFILEINFO* ffi = nullptr;
    UINT ffiLen = 0;
    if (!VerQueryValueW(blob.data(), L"\\", reinterpret_cast<LPVOID*>(&ffi), &ffiLen) || !ffi) return L"";

    wchar_t buf[32]{};
    swprintf_s(buf, L"%u.%u.%u.%u",
        HIWORD(ffi->dwFileVersionMS), LOWORD(ffi->dwFileVersionMS),
        HIWORD(ffi->dwFileVersionLS), LOWORD(ffi->dwFileVersionLS));
    return buf;
}

bool TryReadRegisteredDllPath(const wchar_t* clsid, std::wstring& out)
{
    wchar_t key[160]{};
    swprintf_s(key, L"CLSID\\%s\\InProcServer32", clsid);

    HKEY hk{};
    if (RegOpenKeyExW(HKEY_CLASSES_ROOT, key, 0, KEY_READ, &hk) != ERROR_SUCCESS)
        return false;

    wchar_t buf[MAX_PATH]{};
    DWORD type = 0;
    DWORD cb = sizeof(buf);
    LONG rc = RegQueryValueExW(hk, nullptr, nullptr, &type, reinterpret_cast<LPBYTE>(buf), &cb);
    RegCloseKey(hk);
    if (rc != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || buf[0] == 0)
        return false;

    std::wstring s = buf;
    if (!s.empty() && s.front() == L'"' && s.back() == L'"') {
        s = s.substr(1, s.size() - 2);
    }

    wchar_t expanded[MAX_PATH]{};
    DWORD n = ExpandEnvironmentStringsW(s.c_str(), expanded, ARRAYSIZE(expanded));
    if (n > 0 && n < ARRAYSIZE(expanded)) {
        out = expanded;
    } else {
        out = s;
    }
    return true;
}

void SetHandlerStatus(int iconId, int verId, int pathId, int btnId,
                      const wchar_t* clsid, bool handlerEnabled, bool pendingRegister)
{
    std::wstring dll;
    bool registered = TryReadRegisteredDllPath(clsid, dll) &&
                      GetFileAttributesW(dll.c_str()) != INVALID_FILE_ATTRIBUTES;

    if (pendingRegister) {
        SetStatusIconTextAndColor(iconId, L"\u2026", g_iconWarnColor);
        SetDlgItemTextW(g_hDlg, verId, L"Will register on Apply");
        SetDlgItemTextW(g_hDlg, pathId, L"");
    } else if (registered) {
        auto v = GetFileVersionString(dll);
        if (handlerEnabled) {
            SetStatusIconTextAndColor(iconId, L"\u2713", g_iconOkColor);
            std::wstring text = v.empty() ? L"Registered" : (L"v" + v);
            SetDlgItemTextW(g_hDlg, verId, text.c_str());
        } else {
            SetStatusIconTextAndColor(iconId, L"\u26A0", g_iconWarnColor);
            std::wstring text = v.empty() ? L"Registered, disabled" : (L"v" + v + L" \u2014 disabled");
            SetDlgItemTextW(g_hDlg, verId, text.c_str());
        }
        SetDlgItemTextW(g_hDlg, pathId, dll.c_str());
        SetTooltip(verId, dll.c_str());
        SetTooltip(pathId, dll.c_str());
    } else {
        SetStatusIconTextAndColor(iconId, L"\u2717", g_iconBadColor);
        SetDlgItemTextW(g_hDlg, verId, L"Not registered");
        SetDlgItemTextW(g_hDlg, pathId, L"");
    }

    UpdateToggleButtonState(btnId, clsid, handlerEnabled, pendingRegister);
}

void RefreshHandlerStatuses()
{
    SetHandlerStatus(IDC_STATIC_PROPERTY_STATUS, IDC_STATIC_PROPERTY_VER, IDC_STATIC_PROPERTY_PATH,
        IDC_BTN_TOGGLE_PROPERTY,
        kPropertyClsid, g_pending.curPropertyEnabled, g_pending.registerProperty);
    SetHandlerStatus(IDC_STATIC_PREVIEW_STATUS, IDC_STATIC_PREVIEW_VER, IDC_STATIC_PREVIEW_PATH,
        IDC_BTN_TOGGLE_PREVIEW,
        kPreviewClsid, g_pending.curPreviewEnabled, g_pending.registerPreview);
    SetHandlerStatus(IDC_STATIC_FILTER_STATUS, IDC_STATIC_FILTER_VER, IDC_STATIC_FILTER_PATH,
        IDC_BTN_TOGGLE_FILTER,
        kFilterClsid, g_pending.curFilterEnabled, g_pending.registerFilter);

    // Show UAC shield on Apply when registration is pending
    SendDlgItemMessageW(g_hDlg, IDC_BTN_APPLY, BCM_SETSHIELD, 0,
        g_pending.HasPendingRegistration() ? TRUE : FALSE);
}

bool IsCatalogInstalled(const catalogspec::CatalogSource& src)
{
    return installer::Probe(src).state != installer::PresenceState::Missing;
}

bool IsCatalogUpToDate(const catalogspec::CatalogSource& src)
{
    const auto state = installer::Probe(src).state;
    if (_wcsicmp(src.sourceHashDisplay.data(), catalogspec::kSourceHashNA.data()) == 0) {
        return state != installer::PresenceState::Missing;
    }
    return state == installer::PresenceState::PresentVerified;
}

bool RemoveCatalogFile(const catalogspec::CatalogSource& src)
{
    auto p = installer::Probe(src);
    if (p.state == installer::PresenceState::Missing) return true;

    std::wstring path = paths::CatalogFile(src.fileName.data());
    return DeleteFileW(path.c_str()) != 0;
}

void OnRemoveCatalog(int idx)
{
    const auto& src = *catalogspec::kAllCatalogs[idx];
    std::wstring prompt = L"Remove local ";
    prompt += src.fileName;
    prompt += L"?";
    if (MessageBoxW(g_hDlg, prompt.c_str(), L"Remove Catalog", MB_YESNO | MB_ICONQUESTION) != IDYES)
        return;

    if (RemoveCatalogFile(src)) {
        RemoveImportedSourcePath(src.fileName);
        SetProgressText(std::wstring(src.fileName) + L" removed.");
    } else {
        SetProgressText(std::wstring(L"Failed to remove ") + std::wstring(src.fileName) + L".");
    }

    RefreshAllPresence();
    UpdateCatalogActionButtons();
    InvalidateRect(g_hDlg, nullptr, TRUE);
}

int GetSelectedCatalogRowIndex()
{
    HWND hList = GetDlgItem(g_hDlg, IDC_LIST_CATALOGS);
    if (!hList) return -1;
    return ListView_GetNextItem(hList, -1, LVNI_SELECTED);
}

void OnInstallSelectedCatalog()
{
    const int rowIndex = GetSelectedCatalogRowIndex();
    if (rowIndex < 0 || rowIndex >= static_cast<int>(g_catalogRows.size())) return;
    const auto& row = g_catalogRows[rowIndex];
    if (!row.builtIn || row.builtInIndex < 0) {
        SetProgressText(L"Install/Update is available only for pinned built-in catalogs.");
        return;
    }
    OnFetchOnline(row.builtInIndex);
}

void OnRemoveSelectedCatalog()
{
    const int rowIndex = GetSelectedCatalogRowIndex();
    if (rowIndex < 0 || rowIndex >= static_cast<int>(g_catalogRows.size())) return;
    const auto& row = g_catalogRows[rowIndex];
    if (row.builtIn && row.builtInIndex >= 0) {
        OnRemoveCatalog(row.builtInIndex);
        return;
    }

    std::wstring filePath = paths::CatalogFile(row.fileName.c_str());
    std::wstring prompt = L"Remove local ";
    prompt += row.fileName;
    prompt += L"?";
    if (MessageBoxW(g_hDlg, prompt.c_str(), L"Remove Catalog", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    if (DeleteFileW(filePath.c_str())) {
        RemoveImportedSourcePath(row.fileName);
        SetProgressText(row.fileName + L" removed.");
    } else {
        SetProgressText(L"Failed to remove " + row.fileName + L".");
    }
    RefreshAllPresence();
}

std::wstring CatalogPinnedVersion(const catalogspec::CatalogSource& src)
{
    if (_wcsicmp(src.sourceHashDisplay.data(), catalogspec::kSourceHashNA.data()) == 0)
        return std::wstring(catalogspec::kSourceHashNA);
    return std::wstring(src.sourceHashDisplay);
}

std::wstring CatalogLocalVersion(const installer::Presence& p)
{
    if (p.state == installer::PresenceState::Missing) return L"-";
    if (!p.computedHash.empty()) return p.computedHash;
    return L"?";
}

std::wstring StatusGlyphForPresence(installer::PresenceState state)
{
    if (state == installer::PresenceState::PresentVerified) return L"✓";
    if (state == installer::PresenceState::PresentMismatch) return L"⚠";
    if (state == installer::PresenceState::PresentUnknown) return L"⚠";
    return L"✗";
}

bool IsBuiltInCatalogFileName(std::wstring_view fileName)
{
    return std::any_of(catalogspec::kAllCatalogs.begin(), catalogspec::kAllCatalogs.end(),
        [fileName](const catalogspec::CatalogSource* src) {
            return _wcsicmp(src->fileName.data(), std::wstring(fileName).c_str()) == 0;
        });
}

bool TryComputeFileHash(const std::wstring& fullPath, std::wstring& outHash)
{
    std::array<std::uint8_t, 32> digest{};
    std::uint64_t size = 0;
    if (FAILED(HashFile(fullPath.c_str(), digest, size))) return false;
    outHash = ToHexLower(digest);
    return true;
}

std::wstring NormalizeCatalogFileKey(std::wstring_view fileName)
{
    std::wstring key(fileName);
    std::transform(key.begin(), key.end(), key.begin(),
        [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return key;
}

void LoadImportedCatalogMetadata()
{
    g_importedSourceByFile.clear();
    std::wstring metaPath = paths::CatalogMetadataFile();
    if (metaPath.empty()) return;
    std::wifstream in{ std::filesystem::path(metaPath) };
    if (!in.is_open()) return;

    std::wstring line;
    while (std::getline(in, line)) {
        const auto tab = line.find(L'\t');
        if (tab == std::wstring::npos) continue;
        std::wstring fileName = line.substr(0, tab);
        std::wstring source = line.substr(tab + 1);
        if (fileName.empty() || source.empty()) continue;
        g_importedSourceByFile[NormalizeCatalogFileKey(fileName)] = source;
    }
}

void SaveImportedCatalogMetadata()
{
    std::wstring metaPath = paths::CatalogMetadataFile();
    if (metaPath.empty()) return;
    std::wofstream out{ std::filesystem::path(metaPath), std::ios::trunc };
    if (!out.is_open()) return;
    for (const auto& [fileKey, sourcePath] : g_importedSourceByFile) {
        out << fileKey << L'\t' << sourcePath << L"\n";
    }
}

std::wstring GetImportedSourcePath(std::wstring_view fileName)
{
    const auto it = g_importedSourceByFile.find(NormalizeCatalogFileKey(fileName));
    return (it == g_importedSourceByFile.end()) ? L"" : it->second;
}

void SetImportedSourcePath(std::wstring_view fileName, const std::wstring& sourcePath)
{
    g_importedSourceByFile[NormalizeCatalogFileKey(fileName)] = sourcePath;
    SaveImportedCatalogMetadata();
}

void RemoveImportedSourcePath(std::wstring_view fileName)
{
    g_importedSourceByFile.erase(NormalizeCatalogFileKey(fileName));
    SaveImportedCatalogMetadata();
}

void PopulateCatalogRows()
{
    LoadImportedCatalogMetadata();
    g_catalogRows.clear();
    for (size_t i = 0; i < catalogspec::kAllCatalogs.size(); ++i) {
        const auto& src = *catalogspec::kAllCatalogs[i];
        const auto p = installer::Probe(src);
        CatalogRowModel row{};
        row.builtIn = true;
        row.builtInIndex = static_cast<int>(i);
        row.presence = p.state;
        row.fileName = std::wstring(src.fileName);
        const auto importedSource = GetImportedSourcePath(row.fileName);
        if (!importedSource.empty()) {
            row.sourceLink = importedSource;
            row.sourceHashDisplay = std::wstring(catalogspec::kSourceHashNA);
        } else {
            row.sourceLink = std::wstring(src.sourceUrl);
            row.sourceHashDisplay = CatalogPinnedVersion(src);
        }
        row.localHashDisplay = CatalogLocalVersion(p);
        row.statusGlyph = StatusGlyphForPresence(p.state);
        g_catalogRows.push_back(std::move(row));
    }

    const std::wstring catalogDir = paths::CatalogDir();
    if (catalogDir.empty()) return;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(catalogDir, ec)) {
        if (ec || !entry.is_regular_file()) continue;
        const auto fileName = entry.path().filename().wstring();
        if (_wcsicmp(entry.path().extension().c_str(), L".csv") != 0) continue;
        if (IsBuiltInCatalogFileName(fileName)) continue;

        CatalogRowModel row{};
        row.builtIn = false;
        row.fileName = fileName;
        const auto importedSource = GetImportedSourcePath(fileName);
        row.sourceLink = importedSource.empty() ? entry.path().wstring() : importedSource;
        row.sourceHashDisplay = std::wstring(catalogspec::kSourceHashNA);
        std::wstring localHash;
        row.localHashDisplay = TryComputeFileHash(entry.path().wstring(), localHash) ? localHash : L"?";
        row.presence = installer::PresenceState::PresentVerified;
        row.statusGlyph = StatusGlyphForPresence(row.presence);
        g_catalogRows.push_back(std::move(row));
    }
}

void UpdateCatalogActionButtons()
{
    HWND hList = GetDlgItem(g_hDlg, IDC_LIST_CATALOGS);
    int selected = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
    if (selected < 0 || selected >= static_cast<int>(g_catalogRows.size())) {
        SetDlgItemTextW(g_hDlg, IDC_BTN_CATALOG_INSTALL, L"Install / Update");
        EnableWindow(GetDlgItem(g_hDlg, IDC_BTN_CATALOG_INSTALL), FALSE);
        EnableWindow(GetDlgItem(g_hDlg, IDC_BTN_CATALOG_REMOVE), FALSE);
        return;
    }

    const auto& row = g_catalogRows[selected];
    if (row.builtIn) {
        const auto& src = *catalogspec::kAllCatalogs[row.builtInIndex];
        const bool installed = IsCatalogInstalled(src);
        const bool upToDate = IsCatalogUpToDate(src);
        SetDlgItemTextW(g_hDlg, IDC_BTN_CATALOG_INSTALL, installed ? L"Update" : L"Install");
        EnableWindow(GetDlgItem(g_hDlg, IDC_BTN_CATALOG_INSTALL), upToDate ? FALSE : TRUE);
        EnableWindow(GetDlgItem(g_hDlg, IDC_BTN_CATALOG_REMOVE), installed ? TRUE : FALSE);
    } else {
        SetDlgItemTextW(g_hDlg, IDC_BTN_CATALOG_INSTALL, L"N/A");
        EnableWindow(GetDlgItem(g_hDlg, IDC_BTN_CATALOG_INSTALL), FALSE);
        EnableWindow(GetDlgItem(g_hDlg, IDC_BTN_CATALOG_REMOVE), TRUE);
    }
}

void RefreshCatalogListControl()
{
    HWND hList = GetDlgItem(g_hDlg, IDC_LIST_CATALOGS);
    if (!hList) return;
    PopulateCatalogRows();
    ListView_DeleteAllItems(hList);

    int rowIndex = 0;
    for (const auto& row : g_catalogRows) {
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = rowIndex;
        item.pszText = const_cast<LPWSTR>(row.fileName.c_str());
        ListView_InsertItem(hList, &item);

        ListView_SetItemText(hList, rowIndex, 1, const_cast<LPWSTR>(row.sourceLink.c_str()));
        ListView_SetItemText(hList, rowIndex, 2, const_cast<LPWSTR>(row.sourceHashDisplay.c_str()));
        ListView_SetItemText(hList, rowIndex, 3, const_cast<LPWSTR>(row.localHashDisplay.c_str()));
        ListView_SetItemText(hList, rowIndex, 4, const_cast<LPWSTR>(row.statusGlyph.c_str()));
        rowIndex++;
    }

    if (rowIndex > 0) {
        ListView_SetItemState(hList, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
    UpdateCatalogActionButtons();
}

void RefreshAllPresence()
{
    RefreshCatalogListControl();
}

void SetProgressText(const std::wstring& s)
{
    SetDlgItemTextW(g_hDlg, IDC_STATIC_PROGRESS_TEXT, s.c_str());
}

void SetBusy(bool busy)
{
    EnableWindow(GetDlgItem(g_hDlg, IDC_LIST_CATALOGS), !busy);
    EnableWindow(GetDlgItem(g_hDlg, IDC_BTN_CATALOG_INSTALL), !busy);
    EnableWindow(GetDlgItem(g_hDlg, IDC_BTN_CATALOG_REMOVE), !busy);
    EnableWindow(GetDlgItem(g_hDlg, IDC_BTN_IMPORT_FILE),  !busy);
    g_opInProgress = busy;
}

struct WorkerContext { HWND hDlg; };

bool CALLBACK ProgressTrampoline(std::uint64_t bytes, std::uint64_t max, void* user)
{
    auto* ctx = static_cast<WorkerContext*>(user);
    PostMessageW(ctx->hDlg, WM_XISF_PROGRESS,
                 static_cast<WPARAM>(bytes), static_cast<LPARAM>(max));
    return !g_cancelRequested.load(std::memory_order_relaxed);
}

void RunOnlineInstall(HWND hDlg, int idx)
{
    WorkerContext ctx{ hDlg };
    const auto& src = *catalogspec::kAllCatalogs[idx];
    installer::Report rep = installer::InstallFromPinnedUrl(src, ProgressTrampoline, &ctx);

    auto* heap = new installer::Report(std::move(rep));
    PostMessageW(hDlg, WM_XISF_DONE,
                 static_cast<WPARAM>(idx),
                 reinterpret_cast<LPARAM>(heap));
}

void RunLocalImport(HWND hDlg, std::wstring targetFileName, std::wstring path)
{
    WorkerContext ctx{ hDlg };
    constexpr std::uint64_t kImportMaxBytes = 64ull * 1024ull * 1024ull;
    auto rep = installer::InstallFromLocalFileUnverified(targetFileName.c_str(),
                                                          path.c_str(),
                                                          kImportMaxBytes,
                                                          ProgressTrampoline, &ctx);
    auto* heap = new installer::Report(std::move(rep));
    PostMessageW(hDlg, WM_XISF_DONE,
                 static_cast<WPARAM>(-1),
                 reinterpret_cast<LPARAM>(heap));
}

void OnFetchOnline(int idx)
{
    if (g_opInProgress.exchange(true)) return;
    g_cancelRequested = false;
    SetBusy(true);
    const auto& src = *catalogspec::kAllCatalogs[idx];
    g_activeInstallMode = InstallMode::PinnedBuiltIn;
    g_activeTargetFileName = std::wstring(src.fileName);
    g_activeImportSourcePath.clear();
    g_activeExpectedHash = std::wstring(src.expectedSha256);
    SetProgressText(std::wstring(L"Installing ") + std::wstring(src.fileName) + L" from pinned source…");
    SendDlgItemMessageW(g_hDlg, IDC_PROGRESS, PBM_SETMARQUEE, TRUE, 30);
    SendDlgItemMessageW(g_hDlg, IDC_PROGRESS, PBM_SETPOS, 0, 0);
    std::thread([hDlg = g_hDlg, idx]() { RunOnlineInstall(hDlg, idx); }).detach();
}

void OnLocalImport(HWND hDlg, std::wstring targetFileName, std::wstring path)
{
    if (g_opInProgress.exchange(true)) return;
    g_cancelRequested = false;
    SetBusy(true);
    SetProgressText(L"Importing local file…");
    SendDlgItemMessageW(g_hDlg, IDC_PROGRESS, PBM_SETMARQUEE, TRUE, 30);

    g_activeInstallMode = InstallMode::FileImportUnverified;
    g_activeTargetFileName = targetFileName;
    g_activeImportSourcePath = path;
    g_activeExpectedHash.clear();

    std::thread([hDlg = g_hDlg,
                 targetFileName = std::move(targetFileName),
                 path = std::move(path)]() mutable {
        RunLocalImport(hDlg, std::move(targetFileName), std::move(path));
    }).detach();
}

void OnFetchOnline()
{
    OnFetchOnline(0);
}

void OnImportFile()
{
    if (g_opInProgress.load()) return;

    wchar_t filePath[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = g_hDlg;
    ofn.lpstrFile   = filePath;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrFilter = L"CSV files (*.csv)\0*.csv\0All files (*.*)\0*.*\0";
    ofn.lpstrTitle  = L"Import catalog file";
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;

    if (!GetOpenFileNameW(&ofn)) return;

    std::filesystem::path src(filePath);
    const std::wstring fileName = src.filename().wstring();
    if (fileName.empty()) {
        SetProgressText(L"Import failed: invalid file name.");
        return;
    }

    OnLocalImport(g_hDlg, fileName, filePath);
}

void OnOpenCatalogDir()
{
    std::wstring dir = paths::CatalogDir();
    if (!dir.empty())
        ShellExecuteW(g_hDlg, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void OnRestartExplorer()
{
    if (MessageBoxW(g_hDlg,
            L"Refresh handlers now?\r\n\r\n"
            L"This runs unregister \u2192 restart Explorer \u2192 register for Property, Preview, and Filter handlers.",
            L"Refresh Handlers", MB_YESNO | MB_ICONQUESTION) != IDYES)
        return;

    OnRegisterHandlers();
}

void OnFlushThumbnailCache()
{
    if (MessageBoxW(g_hDlg,
            L"Flush the Windows thumbnail cache now?\r\n\r\n"
            L"This deletes thumbcache_*.db under your local Explorer cache.",
            L"Flush Thumbnail Cache", MB_YESNO | MB_ICONQUESTION) != IDYES)
        return;

    static constexpr wchar_t kFlushArgs[] =
        L"-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -Command "
        L"\"Remove-Item \"\"$env:LOCALAPPDATA\\Microsoft\\Windows\\Explorer\\thumbcache_*.db\"\" -Force -ErrorAction SilentlyContinue\"";

    auto rc = reinterpret_cast<INT_PTR>(
        ShellExecuteW(g_hDlg, L"runas", L"powershell.exe", kFlushArgs, nullptr, SW_HIDE));
    if (rc <= 32) {
        if (rc == SE_ERR_ACCESSDENIED)
            SetProgressText(L"Flush thumbnail cache cancelled.");
        else
            SetProgressText(L"Failed to flush thumbnail cache.");
        return;
    }

    SetProgressText(L"Thumbnail cache flush requested.");
}

void OnCopyExpectedHashes()
{
    std::wstring text;
    text += L"OpenNGC pinned commit: ";
    text += catalogspec::kOpenNGCCommit;
    text += L"\r\nDate: ";
    text += catalogspec::kOpenNGCCommitDate;
    text += L"\r\n\r\n";
    for (auto* src : catalogspec::kAllCatalogs) {
        text += src->fileName; text += L"  source-hash = ";
        text += src->sourceHashDisplay; text += L"\r\n";
        text += L"  pin-sha256 = "; text += src->expectedSha256; text += L"\r\n";
        text += L"  url = "; text += src->url; text += L"\r\n\r\n";
    }

    if (!OpenClipboard(g_hDlg)) return;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hMem) {
        if (auto* dst = static_cast<wchar_t*>(GlobalLock(hMem))) {
            memcpy(dst, text.c_str(), bytes);
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        } else {
            GlobalFree(hMem);
        }
    }
    CloseClipboard();
    SetProgressText(L"Expected hashes copied to clipboard.");
}

void SetTooltip(int id, const wchar_t* text)
{
    HWND hCtrl = GetDlgItem(g_hDlg, id);
    if (!hCtrl) return;

    // Store the string so it outlives this call
    g_tooltipStrings[id] = text;

    HWND hTip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
        WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        g_hDlg, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hTip) return;

    TOOLINFOW ti{};
    ti.cbSize = sizeof(ti);
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd = g_hDlg;
    ti.uId = reinterpret_cast<UINT_PTR>(hCtrl);
    ti.lpszText = const_cast<LPWSTR>(g_tooltipStrings[id].c_str());
    SendMessageW(hTip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
    SendMessageW(hTip, TTM_SETMAXTIPWIDTH, 0, 600);
}

void AddTooltips()
{
    auto makeTip = [](int id, const wchar_t* text) {
        SetTooltip(id, text);
    };

    // Handler toggles with documentation references
    makeTip(IDC_BTN_TOGGLE_PROPERTY, L"Toggle the Property Handler (details pane, file info)\nClick for more info about handlers: Open Settings > Help");
    makeTip(IDC_BTN_TOGGLE_PREVIEW, L"Toggle the Preview/Thumbnail Handler (preview pane, thumbnails)\nClick for more info about handlers: Open Settings > Help");
    makeTip(IDC_BTN_TOGGLE_FILTER, L"Toggle the Search Filter (Windows Search content indexing)\nClick for more info about handlers: Open Settings > Help");

    // Catalog management buttons
    makeTip(IDC_BTN_CATALOG_INSTALL, L"Install or update the selected pinned catalog with hash verification");
    makeTip(IDC_BTN_CATALOG_REMOVE, L"Remove the selected catalog row from local storage");
    makeTip(IDC_BTN_IMPORT_FILE, L"Import a catalog from a local file\nClick for more info: See Help");
    makeTip(IDC_BTN_COPY_EXPECTED_HASHES, L"Copy expected SHA-256 hashes for verification\nClick for more info: See Help");

    // Feature tier
    makeTip(IDC_BTN_SHOW_TIERS, L"Show detailed information about each feature tier\nClick for documentation: Open Settings > Help");

    // Projection
    makeTip(IDC_BTN_SHOW_MAPPING, L"Show System.Photo property mapping\nClick for documentation: Open Settings > Help");

    // Advanced features
    makeTip(IDC_BTN_ADVANCED, L"ETW tracing and other advanced handler management\nClick for documentation: Open Settings > Help");
}

void OnRegisterHandlers()
{
    std::wstring params = L"--register-direct --property --preview --filter";

    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.hwnd = g_hDlg;
    sei.lpVerb = L"runas";
    auto exe = GetExePath();
    sei.lpFile = exe.c_str();
    sei.lpParameters = params.c_str();
    sei.nShow = SW_HIDE;

    if (!ShellExecuteExW(&sei)) {
        SetProgressText(GetLastError() == ERROR_CANCELLED ? L"Register handlers cancelled." : L"Failed to register handlers.");
        return;
    }

    WaitForSingleObject(sei.hProcess, INFINITE);
    DWORD ec = 1;
    GetExitCodeProcess(sei.hProcess, &ec);
    CloseHandle(sei.hProcess);

    if (ec == 0) {
        SetProgressText(L"Handlers registered.");
    } else {
        SetProgressText(L"Handler registration failed.");
    }
    RefreshHandlerStatuses();
}

void OnDone(int idx, installer::Report* repPtr)
{
    std::unique_ptr<installer::Report> rep(repPtr);
    SendDlgItemMessageW(g_hDlg, IDC_PROGRESS, PBM_SETMARQUEE, FALSE, 0);
    SendDlgItemMessageW(g_hDlg, IDC_PROGRESS, PBM_SETPOS, 0, 0);

    std::wstring fileLabel = g_activeTargetFileName;
    std::wstring expectedHash = g_activeExpectedHash;
    if (idx >= 0 && idx < static_cast<int>(catalogspec::kAllCatalogs.size())) {
        const auto& src = *catalogspec::kAllCatalogs[idx];
        fileLabel = std::wstring(src.fileName);
        expectedHash = std::wstring(src.expectedSha256);
    }
    if (fileLabel.empty()) fileLabel = L"catalog";

    std::wstring msg;

    if (rep->result == installer::Result::Ok) {
        if (g_activeInstallMode == InstallMode::FileImportUnverified) {
            SetImportedSourcePath(fileLabel, g_activeImportSourcePath);
            msg = fileLabel + L": imported (" + FormatBytes(rep->bytesTransferred) + L", source hash N/A).";
        } else {
            RemoveImportedSourcePath(fileLabel);
            msg = fileLabel + L": installed (" + FormatBytes(rep->bytesTransferred) +
                  L", sha256 matches pin).";
        }
        RefreshAllPresence();
    } else if (rep->result == installer::Result::HashMismatch) {
        msg = fileLabel + L": REJECTED \u2014 SHA-256 mismatch.\r\n"
              L"  expected " + expectedHash + L"\r\n"
              L"  got      " + rep->computedHash + L"\r\n"
              L"The candidate file was deleted.";
    } else if (rep->result == installer::Result::HttpBadStatus) {
        wchar_t buf[64];
        swprintf_s(buf, L" (HTTP %u)", rep->httpStatus);
        msg = fileLabel + L": failed \u2014 " + ResultName(rep->result) + buf;
    } else {
        msg = fileLabel + L": failed \u2014 " + ResultName(rep->result);
        if (!rep->errorDetail.empty()) { msg += L"  ["; msg += rep->errorDetail; msg += L"]"; }
    }
    SetProgressText(msg);
    SetBusy(false);
    RefreshAllPresence();
    UpdateCatalogActionButtons();
    InvalidateRect(g_hDlg, nullptr, TRUE);
}

void OnProgressTick(std::uint64_t bytes, std::uint64_t maxB)
{
    SendDlgItemMessageW(g_hDlg, IDC_PROGRESS, PBM_SETMARQUEE, FALSE, 0);
    SendDlgItemMessageW(g_hDlg, IDC_PROGRESS, PBM_SETRANGE32, 0,
                        static_cast<LPARAM>(maxB > 0xFFFFFFFFull ? 0xFFFFFFFFull : maxB));
    SendDlgItemMessageW(g_hDlg, IDC_PROGRESS, PBM_SETPOS,
                        static_cast<WPARAM>(bytes > 0xFFFFFFFFull ? 0xFFFFFFFFull : bytes), 0);
    std::wstring s = L"Transferring " + FormatBytes(bytes) + L" / cap " + FormatBytes(maxB) +
                     L"  (hashing in stream)";
    SetProgressText(s);
}

INT_PTR CALLBACK DlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_INITDIALOG: {
        g_hDlg = hDlg;
        if (HICON hAppIcon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),
                MAKEINTRESOURCEW(IDI_SETTINGS_APP), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR))) {
            SendMessageW(hDlg, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hAppIcon));
        }
        if (HICON hAppIconSmall = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),
                MAKEINTRESOURCEW(IDI_SETTINGS_APP), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR))) {
            SendMessageW(hDlg, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hAppIconSmall));
        }

        g_pending.Snapshot();

        SendDlgItemMessageW(hDlg, IDC_BTN_ADVANCED, BCM_SETSHIELD, 0, TRUE);
        SendDlgItemMessageW(hDlg, IDC_BTN_RESTART_EXPLORER, BCM_SETSHIELD, 0, TRUE);
        SendDlgItemMessageW(hDlg, IDC_BTN_FLUSH_THUMBCACHE, BCM_SETSHIELD, 0, TRUE);
        SendDlgItemMessageW(hDlg, IDC_BTN_REGISTER_HANDLERS, BCM_SETSHIELD, 0, TRUE);

        UpdateToggleButtonState(IDC_BTN_TOGGLE_PROPERTY, kPropertyClsid,
            g_pending.curPropertyEnabled, g_pending.registerProperty);
        UpdateToggleButtonState(IDC_BTN_TOGGLE_PREVIEW, kPreviewClsid,
            g_pending.curPreviewEnabled, g_pending.registerPreview);
        UpdateToggleButtonState(IDC_BTN_TOGGLE_FILTER, kFilterClsid,
            g_pending.curFilterEnabled, g_pending.registerFilter);

        // Feature tier combo box
        {
            HWND hCombo = GetDlgItem(hDlg, IDC_COMBO_FEATURE_TIER);
            SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"Basic");
            SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"Standard");
            SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"Full");
            SendMessageW(hCombo, CB_SETCURSEL, static_cast<WPARAM>(g_pending.curTier), 0);
        }

        // Projection checkbox
        CheckDlgButton(hDlg, IDC_CHK_PROJECTION,
                        g_pending.curProjection ? BST_CHECKED : BST_UNCHECKED);

        // Version link
        std::wstring ver = L"Version " XISF_VERSION_WSTR
                           L"  \u2014  <a href=\"https://github.com/dennispayne/XISF-Shell-Extensions\">"
                           L"github.com/dennispayne/XISF-Shell-Extensions</a>";
        SetDlgItemTextW(hDlg, IDC_LINK_VERSION, ver.c_str());

        // Inline documentation links on each handler row (replace the legacy
        // Shift/Ctrl+click "open docs? Y/N" prompts). Each anchor opens the
        // local handlers-overview.md at the relevant section via the WM_NOTIFY
        // handler below.
        SetDlgItemTextW(hDlg, IDC_LINK_PROPERTY_DOC, L"<a href=\"property\">Docs</a>");
        SetDlgItemTextW(hDlg, IDC_LINK_PREVIEW_DOC,  L"<a href=\"preview\">Docs</a>");
        SetDlgItemTextW(hDlg, IDC_LINK_FILTER_DOC,   L"<a href=\"filter\">Docs</a>");

        // Documentation link next to the Advanced button (replaces the YN
        // prompt that used to ask "open ETW docs first?").
        SetDlgItemTextW(hDlg, IDC_LINK_ADVANCED_DOC,
            L"<a href=\"etw\">ETW tracing documentation</a>");

        // Catalog hash verification help link (previously had a YN prompt
        // attached on click; now opens the relevant local doc section).
        SetDlgItemTextW(hDlg, IDC_LINK_HASH_HELP,
            L"<a href=\"verify\">Hash verification help</a>");

        // Apply button starts disabled (no pending changes)
        EnableWindow(GetDlgItem(hDlg, IDC_BTN_APPLY), FALSE);

        {
            HWND hList = GetDlgItem(hDlg, IDC_LIST_CATALOGS);
            ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

            LVCOLUMNW col{};
            col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
            col.pszText = const_cast<LPWSTR>(L"Catalog");
            col.cx = 110;
            ListView_InsertColumn(hList, 0, &col);

            col.pszText = const_cast<LPWSTR>(L"Source");
            col.cx = 150;
            col.iSubItem = 1;
            ListView_InsertColumn(hList, 1, &col);

            col.pszText = const_cast<LPWSTR>(L"Source Hash");
            col.cx = 120;
            col.iSubItem = 2;
            ListView_InsertColumn(hList, 2, &col);

            col.pszText = const_cast<LPWSTR>(L"Local Hash");
            col.cx = 120;
            col.iSubItem = 3;
            ListView_InsertColumn(hList, 3, &col);

            col.pszText = const_cast<LPWSTR>(L"Status");
            col.cx = 45;
            col.iSubItem = 4;
            ListView_InsertColumn(hList, 4, &col);
        }

        RefreshAllPresence();
        RefreshHandlerStatuses();
        UpdateCatalogActionButtons();
        AddTooltips();

        // Auto-install missing catalogs in background
        {
            bool anyMissing = false;
            for (const auto* src : catalogspec::kAllCatalogs) {
                auto p = installer::Probe(*src);
                if (p.state != installer::PresenceState::PresentVerified) {
                    anyMissing = true;
                    break;
                }
            }
            if (anyMissing && !g_opInProgress.exchange(true)) {
                SetBusy(true);
                SetProgressText(L"Auto-installing missing catalogs\u2026");
                std::thread([hDlg]() {
                    int installed = 0;
                    int failed = 0;
                    for (size_t i = 0; i < catalogspec::kAllCatalogs.size(); ++i) {
                        const auto* src = catalogspec::kAllCatalogs[i];
                        auto p = installer::Probe(*src);
                        if (p.state == installer::PresenceState::PresentVerified)
                            continue;

                        installer::Report rep = installer::InstallFromPinnedUrl(
                            *src,
                            [](std::uint64_t bytes, std::uint64_t max, void* u) -> bool {
                                PostMessageW(static_cast<HWND>(u), WM_XISF_PROGRESS,
                                    static_cast<WPARAM>(bytes), static_cast<LPARAM>(max));
                                return !g_cancelRequested.load(std::memory_order_relaxed);
                            },
                            static_cast<void*>(hDlg));
                        if (rep.result == installer::Result::Ok) installed++;
                        else failed++;
                    }
                    // Pack installed|failed into wParam/lParam
                    PostMessageW(hDlg, WM_XISF_AUTO_INSTALL_DONE,
                        static_cast<WPARAM>(installed), static_cast<LPARAM>(failed));
                }).detach();
            }
        }

        return TRUE;
    }

    case WM_NOTIFY: {
        auto* hdr = reinterpret_cast<NMHDR*>(lParam);
        if (hdr && hdr->idFrom == IDC_LIST_CATALOGS) {
            if (hdr->code == NM_CUSTOMDRAW) {
                auto* cd = reinterpret_cast<NMLVCUSTOMDRAW*>(lParam);
                if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) {
                    return CDRF_NOTIFYITEMDRAW;
                }
                if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                    return CDRF_NOTIFYSUBITEMDRAW;
                }
                if (cd->nmcd.dwDrawStage == (CDDS_ITEMPREPAINT | CDDS_SUBITEM)) {
                    const int row = static_cast<int>(cd->nmcd.dwItemSpec);
                    const int sub = cd->iSubItem;
                    if (sub == 1) {
                        // Make source values look and scan like hyperlinks.
                        cd->clrText = RGB(0, 102, 204);
                        return CDRF_NEWFONT;
                    }
                    if (sub == 4 && row >= 0 && row < static_cast<int>(g_catalogRows.size())) {
                        const auto& glyph = g_catalogRows[row].statusGlyph;
                        if (glyph == L"✓") {
                            cd->clrText = RGB(18, 130, 44);      // success
                        } else if (glyph == L"⚠") {
                            cd->clrText = RGB(196, 130, 0);      // warning / non-fatal
                        } else {
                            cd->clrText = RGB(180, 32, 32);      // failure
                        }
                        return CDRF_NEWFONT;
                    }
                    return CDRF_DODEFAULT;
                }
            }
            if (hdr->code == LVN_ITEMCHANGED) {
                auto* nmlv = reinterpret_cast<NMLISTVIEW*>(lParam);
                if ((nmlv->uChanged & LVIF_STATE) != 0 &&
                    ((nmlv->uNewState ^ nmlv->uOldState) & LVIS_SELECTED) != 0) {
                    UpdateCatalogActionButtons();
                }
                return TRUE;
            }
            if (hdr->code == NM_CLICK || hdr->code == NM_DBLCLK) {
                auto* act = reinterpret_cast<NMITEMACTIVATE*>(lParam);
                if (act->iItem >= 0 &&
                    act->iItem < static_cast<int>(g_catalogRows.size()) &&
                    (act->iSubItem == 1 || hdr->code == NM_DBLCLK)) {
                    const auto& link = g_catalogRows[act->iItem].sourceLink;
                    auto rc = reinterpret_cast<INT_PTR>(
                        ShellExecuteW(hDlg, L"open", link.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
                    SetProgressText(rc > 32 ? L"Opened catalog source link." : L"Failed to open catalog source link.");
                    return TRUE;
                }
            }
        }
        if (hdr && hdr->idFrom == IDC_LINK_HASH_HELP && (hdr->code == NM_CLICK || hdr->code == NM_RETURN)) {
            OpenDocumentation(hDlg, L"user-guide/catalog-management.md#verification");
            SetProgressText(L"Opened catalog verification documentation.");
            return TRUE;
        }
        if (hdr && hdr->idFrom == IDC_LINK_VERSION && (hdr->code == NM_CLICK || hdr->code == NM_RETURN)) {
            ShellExecuteW(hDlg, L"open",
                L"https://github.com/dennispayne/XISF-Shell-Extensions",
                nullptr, nullptr, SW_SHOWNORMAL);
            return TRUE;
        }
        if (hdr && (hdr->code == NM_CLICK || hdr->code == NM_RETURN)) {
            switch (hdr->idFrom) {
            case IDC_LINK_PROPERTY_DOC:
                OpenDocumentation(hDlg, L"user-guide/handlers-overview.md#property-handler");
                SetProgressText(L"Opened Property Handler documentation.");
                return TRUE;
            case IDC_LINK_PREVIEW_DOC:
                OpenDocumentation(hDlg, L"user-guide/handlers-overview.md#preview-handler");
                SetProgressText(L"Opened Preview Handler documentation.");
                return TRUE;
            case IDC_LINK_FILTER_DOC:
                OpenDocumentation(hDlg, L"user-guide/handlers-overview.md#search-filter");
                SetProgressText(L"Opened Search Filter documentation.");
                return TRUE;
            case IDC_LINK_ADVANCED_DOC:
                OpenDocumentation(hDlg, L"user-guide/settings-reference.md#etw-tracing");
                SetProgressText(L"Opened ETW tracing documentation.");
                return TRUE;
            }
        }
        break;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        HWND hCtl = reinterpret_cast<HWND>(lParam);
        int id = GetDlgCtrlID(hCtl);
        if (id == IDC_STATIC_PROPERTY_STATUS || id == IDC_STATIC_PREVIEW_STATUS ||
            id == IDC_STATIC_FILTER_STATUS) {
            COLORREF color = g_iconBadColor;
            if (id == IDC_STATIC_PROPERTY_STATUS) color = g_propertyIconColor;
            else if (id == IDC_STATIC_PREVIEW_STATUS) color = g_previewIconColor;
            else if (id == IDC_STATIC_FILTER_STATUS) color = g_filterIconColor;
            SetTextColor(hdc, color);
            SetBkColor(hdc, GetSysColor(COLOR_BTNFACE));
            SetBkMode(hdc, OPAQUE);
            return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_BTNFACE));
        }
        break;
    }

    case WM_COMMAND: {
        WORD id   = LOWORD(wParam);
        WORD code = HIWORD(wParam);
        switch (id) {
        case IDC_BTN_TOGGLE_PROPERTY: {
            if (g_pending.registerProperty) {
                g_pending.registerProperty = false;
                SetProgressText(L"Property Handler registration cancelled.");
            } else if (IsHandlerRegistered(kPropertyClsid)) {
                g_pending.curPropertyEnabled = !g_pending.curPropertyEnabled;
                SetProgressText(g_pending.curPropertyEnabled
                    ? L"Property Handler will be enabled after Apply."
                    : L"Property Handler will be disabled after Apply.");
            } else {
                g_pending.registerProperty = true;
                SetProgressText(L"Property Handler will be registered on Apply (requires elevation).");
            }
            RefreshHandlerStatuses();
            UpdatePendingDisplay();
            return TRUE;
        }
        case IDC_BTN_TOGGLE_PREVIEW: {
            if (g_pending.registerPreview) {
                g_pending.registerPreview = false;
                SetProgressText(L"Preview Handler registration cancelled.");
            } else if (IsHandlerRegistered(kPreviewClsid)) {
                g_pending.curPreviewEnabled = !g_pending.curPreviewEnabled;
                SetProgressText(g_pending.curPreviewEnabled
                    ? L"Preview Handler will be enabled after Apply."
                    : L"Preview Handler will be disabled after Apply.");
            } else {
                g_pending.registerPreview = true;
                SetProgressText(L"Preview Handler will be registered on Apply (requires elevation).");
            }
            RefreshHandlerStatuses();
            UpdatePendingDisplay();
            return TRUE;
        }
        case IDC_BTN_TOGGLE_FILTER: {
            if (g_pending.registerFilter) {
                g_pending.registerFilter = false;
                SetProgressText(L"Search Filter registration cancelled.");
            } else if (IsHandlerRegistered(kFilterClsid)) {
                g_pending.curFilterEnabled = !g_pending.curFilterEnabled;
                SetProgressText(g_pending.curFilterEnabled
                    ? L"Search Filter will be enabled after Apply."
                    : L"Search Filter will be disabled after Apply.");
            } else {
                g_pending.registerFilter = true;
                SetProgressText(L"Search Filter will be registered on Apply (requires elevation).");
            }
            RefreshHandlerStatuses();
            UpdatePendingDisplay();
            return TRUE;
        }
        case IDC_BTN_SHOW_TIERS: {
            DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_TIERS), hDlg,
                [](HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) -> INT_PTR {
                    if (msg == WM_INITDIALOG) {
                        SetDlgItemTextW(hDlg, IDC_LINK_TIERS_DOC,
                            L"<a href=\"https://github.com/dennispayne/XISF-Shell-Extensions/blob/main/docs/features/feature-tiers.md\">Open full feature tiers documentation</a>");
                        return TRUE;
                    }
                    if (msg == WM_NOTIFY) {
                        auto* hdr = reinterpret_cast<NMHDR*>(lParam);
                        if (hdr && hdr->idFrom == IDC_LINK_TIERS_DOC &&
                            (hdr->code == NM_CLICK || hdr->code == NM_RETURN)) {
                            auto rc = reinterpret_cast<INT_PTR>(ShellExecuteW(hDlg, L"open",
                                L"https://github.com/dennispayne/XISF-Shell-Extensions/blob/main/docs/features/feature-tiers.md",
                                nullptr, nullptr, SW_SHOWNORMAL));
                            return rc > 32 ? TRUE : FALSE;
                        }
                    }
                    if (msg == WM_COMMAND && LOWORD(wParam) == IDOK) { EndDialog(hDlg, IDOK); return TRUE; }
                    if (msg == WM_CLOSE) { EndDialog(hDlg, IDCANCEL); return TRUE; }
                    return FALSE;
                }, 0);
            return TRUE;
        }
        case IDC_COMBO_FEATURE_TIER: {
            if (code == CBN_SELCHANGE) {
                int sel = (int)SendDlgItemMessageW(hDlg, IDC_COMBO_FEATURE_TIER, CB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel <= 2) {
                    g_pending.curTier = static_cast<hostsettings::FeatureTier>(sel);
                    static const wchar_t* tierNames[] = { L"Basic", L"Standard", L"Full" };
                    wchar_t buf[128]; swprintf_s(buf, L"Feature tier will be %s after Apply.", tierNames[sel]);
                    SetProgressText(buf);
                    UpdatePendingDisplay();
                }
            }
            return TRUE;
        }
        case IDC_CHK_PROJECTION: {
            g_pending.curProjection = (IsDlgButtonChecked(hDlg, IDC_CHK_PROJECTION) == BST_CHECKED);
            SetProgressText(g_pending.curProjection
                ? L"System.Photo projection will be enabled after Apply."
                : L"System.Photo projection will be disabled after Apply.");
            UpdatePendingDisplay();
            return TRUE;
        }
        case IDC_CHK_RESTART_EXPLORER: {
            UpdatePendingDisplay();
            return TRUE;
        }
        case IDC_BTN_SHOW_MAPPING: {
            DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_MAPPING), hDlg,
                [](HWND hDlg, UINT msg, WPARAM wParam, LPARAM) -> INT_PTR {
                    if (msg == WM_COMMAND && LOWORD(wParam) == IDOK) { EndDialog(hDlg, IDOK); return TRUE; }
                    if (msg == WM_CLOSE) { EndDialog(hDlg, IDCANCEL); return TRUE; }
                    return FALSE;
                }, 0);
            return TRUE;
        }
        case IDC_BTN_REGISTER_HANDLERS:    OnRegisterHandlers();      return TRUE;
        case IDC_BTN_CATALOG_INSTALL:      OnInstallSelectedCatalog(); return TRUE;
        case IDC_BTN_CATALOG_REMOVE:       OnRemoveSelectedCatalog();  return TRUE;
        case IDC_BTN_IMPORT_FILE:          OnImportFile();           return TRUE;
        case IDC_BTN_OPEN_CATALOG_DIR:     OnOpenCatalogDir();       return TRUE;
        case IDC_BTN_COPY_EXPECTED_HASHES: OnCopyExpectedHashes();   return TRUE;
        case IDC_BTN_ADVANCED: {
            SHELLEXECUTEINFOW sei{};
            sei.cbSize = sizeof(sei);
            sei.fMask = SEE_MASK_NOCLOSEPROCESS;
            sei.hwnd = hDlg;
            sei.lpVerb = L"runas";
            auto exe = GetExePath();
            sei.lpFile = exe.c_str();
            sei.lpParameters = L"--advanced-direct";
            sei.nShow = SW_SHOWNORMAL;
            if (!ShellExecuteExW(&sei)) {
                SetProgressText(GetLastError() == ERROR_CANCELLED ? L"Advanced cancelled." : L"Failed to open Advanced.");
                return TRUE;
            }
            CloseHandle(sei.hProcess);
            return TRUE;
        }
        case IDC_BTN_APPLY: {
            int n = g_pending.DirtyCount();

            // Handle pending registrations (requires elevation)
            if (g_pending.HasPendingRegistration()) {
                std::wstring params = L"--register-direct";
                if (g_pending.registerProperty) params += L" --property";
                if (g_pending.registerPreview)  params += L" --preview";
                if (g_pending.registerFilter)   params += L" --filter";

                SHELLEXECUTEINFOW sei{};
                sei.cbSize = sizeof(sei);
                sei.fMask = SEE_MASK_NOCLOSEPROCESS;
                sei.hwnd = hDlg;
                sei.lpVerb = L"runas";
                auto exe = GetExePath();
                sei.lpFile = exe.c_str();
                sei.lpParameters = params.c_str();
                sei.nShow = SW_HIDE;

                if (!ShellExecuteExW(&sei)) {
                    SetProgressText(GetLastError() == ERROR_CANCELLED
                        ? L"Registration cancelled by user."
                        : L"Failed to start registration.");
                    return TRUE;
                }

                WaitForSingleObject(sei.hProcess, INFINITE);
                DWORD ec = 1;
                GetExitCodeProcess(sei.hProcess, &ec);
                CloseHandle(sei.hProcess);

                if (ec == 0) {
                    g_pending.registerProperty = false;
                    g_pending.registerPreview = false;
                    g_pending.registerFilter = false;
                } else {
                    SetProgressText(L"Handler registration failed.");
                    RefreshHandlerStatuses();
                    UpdatePendingDisplay();
                    return TRUE;
                }
            }

            // Apply enable/disable and other settings
            g_pending.Apply();

            bool restart = (IsDlgButtonChecked(hDlg, IDC_CHK_RESTART_EXPLORER) == BST_CHECKED);
            if (restart) {
                CheckDlgButton(hDlg, IDC_CHK_RESTART_EXPLORER, BST_UNCHECKED);
            }

            RefreshHandlerStatuses();
            UpdatePendingDisplay();
            wchar_t buf[128];
            swprintf_s(buf, L"%d setting%s applied.%s", n, n == 1 ? L"" : L"s",
                       restart ? L" Restarting Explorer\u2026" : L" Restart Explorer to take effect.");
            SetProgressText(buf);
            if (restart) {
                static constexpr wchar_t kRestartArgs[] =
                    L"-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -Command "
                    L"\"Stop-Process -Name explorer -Force -ErrorAction SilentlyContinue; "
                    L"Start-Sleep -Milliseconds 500; Start-Process explorer.exe\"";
                ShellExecuteW(hDlg, L"runas", L"powershell.exe", kRestartArgs, nullptr, SW_HIDE);
            }
            return TRUE;
        }
        case IDOK:
        case IDCANCEL:
            if (g_opInProgress.load()) {
                if (MessageBoxW(hDlg,
                        L"An install is in progress. Cancel it and close?",
                        L"XISF", MB_YESNO | MB_ICONQUESTION) != IDYES)
                    return TRUE;
                g_cancelRequested = true;
                Sleep(100);
            }
            if (g_pending.DirtyCount() > 0) {
                int r = MessageBoxW(hDlg,
                    L"You have unsaved changes. Apply them before closing?",
                    L"XISF Shell Extensions",
                    MB_YESNOCANCEL | MB_ICONQUESTION);
                if (r == IDCANCEL) return TRUE;
                if (r == IDYES) g_pending.Apply();
            }
            EndDialog(hDlg, 0);
            return TRUE;
        }
        break;
    }

    case WM_XISF_PROGRESS:
        OnProgressTick(static_cast<std::uint64_t>(wParam),
                       static_cast<std::uint64_t>(lParam));
        return TRUE;

    case WM_XISF_DONE:
        OnDone(static_cast<int>(wParam),
               reinterpret_cast<installer::Report*>(lParam));
        return TRUE;

    case WM_XISF_AUTO_INSTALL_DONE: {
        int installed = static_cast<int>(wParam);
        int failed = static_cast<int>(lParam);
        SetBusy(false);
        g_opInProgress = false;
        RefreshAllPresence();
        UpdateCatalogActionButtons();
        if (failed > 0) {
            wchar_t buf[128];
            swprintf_s(buf, L"Auto-install: %d installed, %d failed.", installed, failed);
            SetProgressText(buf);
        } else if (installed > 0) {
            wchar_t buf[128];
            swprintf_s(buf, L"Auto-installed %d catalog%s.", installed, installed == 1 ? L"" : L"s");
            SetProgressText(buf);
        }
        SendDlgItemMessageW(g_hDlg, IDC_PROGRESS, PBM_SETPOS, 0, 0);
        return TRUE;
    }

    case WM_CLOSE:
        PostMessageW(hDlg, WM_COMMAND, IDCANCEL, 0);
        return TRUE;

    case WM_ACTIVATE:
        if (LOWORD(wParam) != WA_INACTIVE) {
            RefreshHandlerStatuses();
        }
        return TRUE;
    }
    return FALSE;
}

std::wstring GetTracePath()
{
    wchar_t tmp[MAX_PATH]{};
    GetTempPathW(ARRAYSIZE(tmp), tmp);

    std::filesystem::path dir(tmp);
    dir /= L"xisf";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    auto p = dir / L"xisf.etl";
    return p.wstring();
}

bool RunLogman(const std::wstring& args)
{
    std::wstring exe = L"C:\\Windows\\System32\\logman.exe";
    DWORD ec = 1;
    return RunProcessHiddenAndWait(exe, args, &ec) && ec == 0;
}

void SetAdvStatus(HWND hDlg, const std::wstring& s)
{
    SetDlgItemTextW(hDlg, IDC_STATIC_TRACE_STATUS, s.c_str());
}

bool CopyTextToClipboard(HWND hWnd, const std::wstring& text)
{
    if (!OpenClipboard(hWnd)) return false;
    EmptyClipboard();

    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!hMem) {
        CloseClipboard();
        return false;
    }

    auto* dst = static_cast<wchar_t*>(GlobalLock(hMem));
    if (!dst) {
        GlobalFree(hMem);
        CloseClipboard();
        return false;
    }

    memcpy(dst, text.c_str(), bytes);
    GlobalUnlock(hMem);
    SetClipboardData(CF_UNICODETEXT, hMem);
    CloseClipboard();
    return true;
}

void OnTracePathDoubleClick(HWND hDlg)
{
    wchar_t buf[MAX_PATH * 2]{};
    GetDlgItemTextW(hDlg, IDC_STATIC_TRACE_PATH, buf, ARRAYSIZE(buf));
    std::wstring text = buf;
    if (text.empty()) {
        SetAdvStatus(hDlg, L"No trace path to copy yet.");
        return;
    }
    if (CopyTextToClipboard(hDlg, text)) {
        SetAdvStatus(hDlg, L"Trace path copied to clipboard.");
    } else {
        SetAdvStatus(hDlg, L"Failed to copy trace path.");
    }
}

void OnTraceStart(HWND hDlg)
{
    if (g_traceExportInProgress.load(std::memory_order_relaxed)) {
        SetAdvStatus(hDlg, L"XML export is in progress.");
        UpdateTraceActionButtons(hDlg);
        return;
    }

    const bool propError = IsDlgButtonChecked(hDlg, IDC_CHK_TRACE_PROP_ERROR) == BST_CHECKED;
    const bool propWarn = IsDlgButtonChecked(hDlg, IDC_CHK_TRACE_PROP_WARN) == BST_CHECKED;
    const bool propInfo = IsDlgButtonChecked(hDlg, IDC_CHK_TRACE_PROP_INFO) == BST_CHECKED;
    const bool propVerbose = IsDlgButtonChecked(hDlg, IDC_CHK_TRACE_PROP_VERBOSE) == BST_CHECKED;

    const bool prevError = IsDlgButtonChecked(hDlg, IDC_CHK_TRACE_PREV_ERROR) == BST_CHECKED;
    const bool prevWarn = IsDlgButtonChecked(hDlg, IDC_CHK_TRACE_PREV_WARN) == BST_CHECKED;
    const bool prevInfo = IsDlgButtonChecked(hDlg, IDC_CHK_TRACE_PREV_INFO) == BST_CHECKED;
    const bool prevVerbose = IsDlgButtonChecked(hDlg, IDC_CHK_TRACE_PREV_VERBOSE) == BST_CHECKED;

    const bool filtError = IsDlgButtonChecked(hDlg, IDC_CHK_TRACE_FILT_ERROR) == BST_CHECKED;
    const bool filtWarn = IsDlgButtonChecked(hDlg, IDC_CHK_TRACE_FILT_WARN) == BST_CHECKED;
    const bool filtInfo = IsDlgButtonChecked(hDlg, IDC_CHK_TRACE_FILT_INFO) == BST_CHECKED;
    const bool filtVerbose = IsDlgButtonChecked(hDlg, IDC_CHK_TRACE_FILT_VERBOSE) == BST_CHECKED;

    const bool includeProp = propError || propWarn || propInfo || propVerbose;
    const bool includePrev = prevError || prevWarn || prevInfo || prevVerbose;
    const bool includeFilt = filtError || filtWarn || filtInfo || filtVerbose;

    if (!includeProp && !includePrev && !includeFilt) {
        SetAdvStatus(hDlg, L"Select at least one item/level in the trace matrix.");
        return;
    }

    auto levelFromRow = [](bool e, bool w, bool i, bool v) -> int {
        if (v) return 0x05;
        if (i) return 0x04;
        if (w) return 0x03;
        if (e) return 0x02;
        return -1;
    };

    const int propLevel = levelFromRow(propError, propWarn, propInfo, propVerbose);
    const int prevLevel = levelFromRow(prevError, prevWarn, prevInfo, prevVerbose);
    const int filtLevel = levelFromRow(filtError, filtWarn, filtInfo, filtVerbose);

    g_traceRunning = IsTraceSessionRunning();
    if (g_traceRunning) {
        RunLogman(L"stop XISFTrace -ets");
        g_traceRunning = IsTraceSessionRunning();
    }

    g_activeTracePath = BuildTimestampedTracePath();
    g_tracePath = g_activeTracePath;
    g_lastStoppedTracePath.clear();

    if (!RunLogman(L"create trace XISFTrace -o \"" + g_activeTracePath + L"\" -ets")) {
        SetAdvStatus(hDlg, L"Failed to create trace session.");
        return;
    }

    if (includeProp) {
        std::wstring cmd = L"update trace XISFTrace -p \"{6F6B0C9D-6B76-5A24-BC3D-708314E96F2B}\" 0xFFFF ";
        cmd += std::to_wstring(propLevel);
        cmd += L" -ets";
        if (!RunLogman(cmd)) {
            SetAdvStatus(hDlg, L"Failed to add Property provider.");
            return;
        }
    }

    if (includePrev) {
        std::wstring cmd = L"update trace XISFTrace -p \"{4FD34FD0-08B3-5D9A-8D77-B9D6705D6B75}\" 0xFFFF ";
        cmd += std::to_wstring(prevLevel);
        cmd += L" -ets";
        if (!RunLogman(cmd)) {
            SetAdvStatus(hDlg, L"Failed to add Preview provider.");
            return;
        }
    }

    if (includeFilt) {
        std::wstring cmd = L"update trace XISFTrace -p \"{3A4B5C6D-7E8F-9012-AB34-CD56EF789012}\" 0xFFFF ";
        cmd += std::to_wstring(filtLevel);
        cmd += L" -ets";
        if (!RunLogman(cmd)) {
            SetAdvStatus(hDlg, L"Failed to add Filter provider.");
            return;
        }
    }

    SetDlgItemTextW(hDlg, IDC_STATIC_TRACE_PATH, g_activeTracePath.c_str());
    g_traceRunning = IsTraceSessionRunning();
    UpdateTraceActionButtons(hDlg);
    SetAdvStatus(hDlg, g_traceRunning
        ? L"Trace session is RUNNING. Stop trace to enable Open ETL / Export XML."
        : L"Trace start command completed, but session is not running.");
}

void OnTraceStop(HWND hDlg)
{
    if (g_traceExportInProgress.load(std::memory_order_relaxed)) {
        SetAdvStatus(hDlg, L"XML export is in progress.");
        UpdateTraceActionButtons(hDlg);
        return;
    }

    if (RunLogman(L"stop XISFTrace -ets")) {
        g_traceRunning = IsTraceSessionRunning();
        if (!g_traceRunning && !g_activeTracePath.empty()) {
            g_lastStoppedTracePath = g_activeTracePath;
        }
        if (!g_lastStoppedTracePath.empty()) {
            SetDlgItemTextW(hDlg, IDC_STATIC_TRACE_PATH, g_lastStoppedTracePath.c_str());
        }
        UpdateTraceActionButtons(hDlg);
        SetAdvStatus(hDlg, g_traceRunning ? L"Trace session still running." : L"Trace stopped.");
    } else {
        g_traceRunning = IsTraceSessionRunning();
        UpdateTraceActionButtons(hDlg);
        SetAdvStatus(hDlg, L"Trace stop failed or not running.");
    }
}

void OnTraceView(HWND hDlg)
{
    if (g_traceExportInProgress.load(std::memory_order_relaxed)) {
        SetAdvStatus(hDlg, L"XML export is in progress.");
        UpdateTraceActionButtons(hDlg);
        return;
    }

    g_traceRunning = IsTraceSessionRunning();
    if (g_traceRunning) {
        SetAdvStatus(hDlg, L"Stop trace before opening the ETL.");
        UpdateTraceActionButtons(hDlg);
        return;
    }

    if (g_lastStoppedTracePath.empty()) {
        SetAdvStatus(hDlg, L"No stopped trace available yet.");
        return;
    }

    auto p = g_lastStoppedTracePath;
    if (GetFileAttributesW(p.c_str()) == INVALID_FILE_ATTRIBUTES) {
        SetAdvStatus(hDlg, L"Stopped trace file not found.");
        return;
    }
    auto rc = reinterpret_cast<INT_PTR>(ShellExecuteW(hDlg, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (rc <= 32) SetAdvStatus(hDlg, L"Failed to open trace file.");
}

void OnTraceExportXml(HWND hDlg)
{
    if (g_traceExportInProgress.exchange(true)) {
        SetAdvStatus(hDlg, L"XML export is already in progress.");
        return;
    }

    g_traceRunning = IsTraceSessionRunning();
    if (g_traceRunning) {
        g_traceExportInProgress = false;
        SetAdvStatus(hDlg, L"Stop trace before exporting XML.");
        UpdateTraceActionButtons(hDlg);
        return;
    }

    if (g_lastStoppedTracePath.empty()) {
        g_traceExportInProgress = false;
        SetAdvStatus(hDlg, L"No stopped trace available yet.");
        UpdateTraceActionButtons(hDlg);
        return;
    }

    auto etl = g_lastStoppedTracePath;
    if (GetFileAttributesW(etl.c_str()) == INVALID_FILE_ATTRIBUTES) {
        g_traceExportInProgress = false;
        SetAdvStatus(hDlg, L"Trace file not found yet.");
        UpdateTraceActionButtons(hDlg);
        return;
    }

    std::wstring xml = etl;
    auto dot = xml.find_last_of(L'.');
    if (dot != std::wstring::npos) xml = xml.substr(0, dot);
    xml += L".xml";

    SetAdvStatus(hDlg, L"Exporting XML report...");
    UpdateTraceActionButtons(hDlg);

    std::thread([hDlg, etl = std::move(etl), xml = std::move(xml)]() mutable {
        auto* result = new TraceExportResult();
        result->xmlPath = xml;

        DeleteFileW(xml.c_str());

        std::wstring args = L"\"" + etl + L"\" -o \"" + xml + L"\" -of XML";
        DWORD ec = 1;
        result->success = RunProcessHiddenAndWaitTimeout(L"C:\\Windows\\System32\\tracerpt.exe", args, 60000, &ec) && ec == 0;
        result->exitCode = ec;

        if (!PostMessageW(hDlg, WM_XISF_TRACE_EXPORT_DONE, 0, reinterpret_cast<LPARAM>(result))) {
            delete result;
        }
    }).detach();
}

void DeleteTraceFiles()
{
    if (!g_activeTracePath.empty()) {
        DeleteFileW(g_activeTracePath.c_str());
        g_activeTracePath.clear();
    }
    if (!g_lastStoppedTracePath.empty()) {
        DeleteFileW(g_lastStoppedTracePath.c_str());
        g_lastStoppedTracePath.clear();
    }
}

INT_PTR CALLBACK AdvancedDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_INITDIALOG:
        g_tracePath = GetTracePath();
        SetDlgItemTextW(hDlg, IDC_STATIC_TRACE_PATH, g_tracePath.c_str());
        SetDlgItemTextW(hDlg, IDC_LINK_ETW_DOC,
            L"<a href=\"etw\">ETW tracing documentation</a>");
        CheckDlgButton(hDlg, IDC_CHK_TRACE_PROP_ERROR, BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_CHK_TRACE_PROP_WARN, BST_CHECKED);
        CheckDlgButton(hDlg, IDC_CHK_TRACE_PROP_INFO, BST_CHECKED);
        CheckDlgButton(hDlg, IDC_CHK_TRACE_PROP_VERBOSE, BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_CHK_TRACE_PREV_ERROR, BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_CHK_TRACE_PREV_WARN, BST_CHECKED);
        CheckDlgButton(hDlg, IDC_CHK_TRACE_PREV_INFO, BST_CHECKED);
        CheckDlgButton(hDlg, IDC_CHK_TRACE_PREV_VERBOSE, BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_CHK_TRACE_FILT_ERROR, BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_CHK_TRACE_FILT_WARN, BST_CHECKED);
        CheckDlgButton(hDlg, IDC_CHK_TRACE_FILT_INFO, BST_CHECKED);
        CheckDlgButton(hDlg, IDC_CHK_TRACE_FILT_VERBOSE, BST_UNCHECKED);
        g_traceRunning = IsTraceSessionRunning();
        g_traceExportInProgress = false;
        g_activeTracePath.clear();
        g_lastStoppedTracePath.clear();
        UpdateTraceActionButtons(hDlg);
        SetAdvStatus(hDlg, g_traceRunning ? L"Trace session is RUNNING." : L"Trace session is STOPPED.");
        return TRUE;
    case WM_NOTIFY: {
        auto* hdr = reinterpret_cast<NMHDR*>(lParam);
        if (hdr && hdr->idFrom == IDC_LINK_ETW_DOC &&
            (hdr->code == NM_CLICK || hdr->code == NM_RETURN)) {
            OpenDocumentation(hDlg, L"features/telemetry-etw.md");
            SetAdvStatus(hDlg, L"Opened ETW tracing documentation.");
            return TRUE;
        }
        break;
    }
    case WM_XISF_TRACE_EXPORT_DONE: {
        std::unique_ptr<TraceExportResult> result(reinterpret_cast<TraceExportResult*>(lParam));
        g_traceExportInProgress = false;
        g_traceRunning = IsTraceSessionRunning();
        UpdateTraceActionButtons(hDlg);

        if (!result || !result->success) {
            SetAdvStatus(hDlg, L"Export XML failed or timed out. Ensure trace is stopped and try again.");
            return TRUE;
        }

        SetAdvStatus(hDlg, L"XML report exported.");
        ShellExecuteW(hDlg, L"open", result->xmlPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_STATIC_TRACE_PATH:
            if (HIWORD(wParam) == STN_DBLCLK) { OnTracePathDoubleClick(hDlg); return TRUE; }
            break;
        case IDC_BTN_REGISTER_HANDLERS: OnRegisterHandlers(); return TRUE;
        case IDC_BTN_RESTART_EXPLORER:  OnRestartExplorer();  return TRUE;
        case IDC_BTN_FLUSH_THUMBCACHE:  OnFlushThumbnailCache(); return TRUE;
        case IDC_BTN_TRACE_START:    OnTraceStart(hDlg);   return TRUE;
        case IDC_BTN_TRACE_STOP:     OnTraceStop(hDlg);    return TRUE;
        case IDC_BTN_TRACE_OPEN_ETL: OnTraceView(hDlg);    return TRUE;
        case IDC_BTN_TRACE_EXPORT_XML: OnTraceExportXml(hDlg); return TRUE;
        case IDOK:
        case IDCANCEL: EndDialog(hDlg, 0); return TRUE;
        }
        break;
    }
    return FALSE;
}

} // namespace

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR lpCmdLine, int)
{
    int cmdExit = 0;
    if (TryHandleCommandMode(lpCmdLine, cmdExit)) {
        if (cmdExit == 100) {
            INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS | ICC_LINK_CLASS };
            InitCommonControlsEx(&icc);
            DialogBoxParamW(hInstance, MAKEINTRESOURCEW(IDD_ADVANCED), nullptr, AdvancedDlgProc, 0);
            return 0;
        }
        return cmdExit;
    }

    // Self-healing: ensure HKCU shell metadata is registered every launch.
    // Idempotent and fast — just overwrites existing values.
    RunMsixRegistration();

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS | ICC_LINK_CLASS };
    InitCommonControlsEx(&icc);

    DialogBoxParamW(hInstance, MAKEINTRESOURCEW(IDD_SETTINGS),
                    nullptr, DlgProc, 0);
    return 0;
}
