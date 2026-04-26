// ShellExtensionHost.cpp - Settings UI + hardened catalog installer front-end.
//
// All security-sensitive operations (URL handling, TLS, hashing, atomic rename,
// URL allow-list) live in CatalogInstaller.cpp. This file is UI wiring only.

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <memory>
#include <cstdint>
#include <commctrl.h>
#include <filesystem>
#include <vector>
#include <tlhelp32.h>

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

COLORREF g_iconOkColor = RGB(18, 130, 44);
COLORREF g_iconWarnColor = RGB(196, 130, 0);
COLORREF g_iconBadColor = RGB(180, 32, 32);

COLORREF g_propertyIconColor = RGB(180, 32, 32);
COLORREF g_previewIconColor = RGB(180, 32, 32);
COLORREF g_ngcIconColor = RGB(180, 32, 32);
COLORREF g_addIconColor = RGB(180, 32, 32);

HWND g_tipHashes = nullptr;
std::wstring g_tipNgcGitHub;
std::wstring g_tipNgcLocal;
std::wstring g_tipAddGitHub;
std::wstring g_tipAddLocal;
std::wstring g_tracePath;
bool g_traceRunning = false;
std::atomic<bool> g_traceExportInProgress{false};
std::wstring g_activeTracePath;
std::wstring g_lastStoppedTracePath;

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
void OnRegisterHandlers();
INT_PTR CALLBACK AdvancedDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);

// --- Deferred-settings model ---
// Changes are tracked here and only written to registry on Apply.
struct PendingSettings {
    bool origPropertyEnabled = true;
    bool origPreviewEnabled = true;
    hostsettings::FeatureTier origTier = hostsettings::FeatureTier::Full;
    bool origProjection = true;

    bool curPropertyEnabled = true;
    bool curPreviewEnabled = true;
    hostsettings::FeatureTier curTier = hostsettings::FeatureTier::Full;
    bool curProjection = true;

    void Snapshot() {
        origPropertyEnabled = curPropertyEnabled = hostsettings::IsPropertyEnabled();
        origPreviewEnabled  = curPreviewEnabled  = hostsettings::IsPreviewEnabled();
        origTier            = curTier            = hostsettings::GetFeatureTier();
        origProjection      = curProjection      = hostsettings::IsProjectionEnabled();
    }

    int DirtyCount() const {
        int n = 0;
        if (curPropertyEnabled != origPropertyEnabled) n++;
        if (curPreviewEnabled  != origPreviewEnabled)  n++;
        if (curTier            != origTier)            n++;
        if (curProjection      != origProjection)      n++;
        return n;
    }

    bool NeedsElevation() const { return false; }

    void Apply() {
        if (curPropertyEnabled != origPropertyEnabled) {
            hostsettings::SetPropertyEnabled(curPropertyEnabled);
            origPropertyEnabled = curPropertyEnabled;
        }
        if (curPreviewEnabled != origPreviewEnabled) {
            hostsettings::SetPreviewEnabled(curPreviewEnabled);
            origPreviewEnabled = curPreviewEnabled;
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
    if (n > 0) {
        wchar_t buf[64];
        swprintf_s(buf, L"%d change%s pending", n, n == 1 ? L"" : L"s");
        SetDlgItemTextW(g_hDlg, IDC_STATIC_PENDING_TEXT, buf);
    } else {
        SetDlgItemTextW(g_hDlg, IDC_STATIC_PENDING_TEXT, L"");
    }
    EnableWindow(GetDlgItem(g_hDlg, IDC_BTN_APPLY), n > 0 ? TRUE : FALSE);
    SendDlgItemMessageW(g_hDlg, IDC_BTN_APPLY, BCM_SETSHIELD, 0,
                        g_pending.NeedsElevation() ? TRUE : FALSE);
}

void UpdateToggleButton(int btnId, bool enabled)
{
    SetDlgItemTextW(g_hDlg, btnId, enabled ? L"Disable" : L"Enable");
}

void SetHashCellTooltip(int controlId, std::wstring& storage, const std::wstring& text)
{
    storage = text;
    HWND hCtrl = GetDlgItem(g_hDlg, controlId);
    if (!g_tipHashes || !hCtrl) return;

    TOOLINFOW ti{};
    ti.cbSize = sizeof(ti);
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd = g_hDlg;
    ti.uId = reinterpret_cast<UINT_PTR>(hCtrl);
    ti.lpszText = const_cast<LPWSTR>(storage.c_str());

    SendMessageW(g_tipHashes, TTM_DELTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
    SendMessageW(g_tipHashes, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
}

void EnsureHashTooltipHost()
{
    if (g_tipHashes) return;
    g_tipHashes = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
        WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        g_hDlg, nullptr, GetModuleHandleW(nullptr), nullptr);
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

std::wstring BuildHandlerDllPath(bool propertyHandler)
{
    auto root = FindSolutionRoot();
    if (root.empty()) return L"";
#ifdef _DEBUG
    constexpr const wchar_t* cfg = L"Debug";
#else
    constexpr const wchar_t* cfg = L"Release";
#endif
    return hostpaths::ResolveHandlerDllPath(root, propertyHandler, cfg);
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

bool RegisterOneHandlerDirect(bool propertyHandler, std::wstring& err)
{
    auto dll = BuildHandlerDllPath(propertyHandler);
    if (dll.empty() || GetFileAttributesW(dll.c_str()) == INVALID_FILE_ATTRIBUTES) {
        err = L"Handler DLL not found.";
        return false;
    }

    const std::wstring regsvr = L"C:\\Windows\\System32\\regsvr32.exe";
    if (!RunProcessHiddenAndWait(regsvr, L"/u /s \"" + dll + L"\"")) {
        err = L"regsvr32 unregister failed.";
        return false;
    }

    if (propertyHandler) {
        DeleteClsidTreeBothRoots(L"{7C54FA8B-9D63-4C10-8FBE-1A5A0F9A3B2E}");
    } else {
        DeleteClsidTreeBothRoots(L"{9C76E8AD-4E85-5F30-B00D-3C7D1AB5F6E0}");
        DeleteClsidTreeBothRoots(L"{AD87F6CE-5B03-6E41-C11E-4DB2AC06F5F1}");
    }

    RestartExplorerDirect();

    DWORD ec = 1;
    if (!RunProcessHiddenAndWait(regsvr, L"/s \"" + dll + L"\"", &ec) || ec != 0) {
        err = L"regsvr32 register failed.";
        return false;
    }

    return true;
}

int RunElevatedRegistrationMode(bool doProperty, bool doPreview)
{
    std::wstring err;
    if (doProperty) {
        if (!RegisterOneHandlerDirect(true, err)) return 2;
    }
    if (doPreview) {
        if (!RegisterOneHandlerDirect(false, err)) return 3;
    }
    return 0;
}

bool StartsWith(const std::wstring& s, const std::wstring& prefix)
{
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
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
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--register-direct") mode = true;
        else if (a == L"--advanced-direct") adv = true;
        else if (a == L"--property") prop = true;
        else if (a == L"--preview") prev = true;
    }
    LocalFree(argv);

    if (adv) {
        exitCode = 100; // sentinel consumed by wWinMain to open Advanced directly
        return true;
    }

    if (!mode) return false;
    exitCode = RunElevatedRegistrationMode(prop, prev);
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

std::wstring ShortHash(const std::wstring& hex)
{
    if (hex.size() < 12) return hex;
    return hex.substr(0, 8) + L"\u2026" + hex.substr(hex.size() - 4);
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
    else if (id == IDC_STATIC_NGC_MATCH) g_ngcIconColor = color;
    else if (id == IDC_STATIC_ADD_MATCH) g_addIconColor = color;
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

void SetHandlerStatus(int iconId, int verId, const wchar_t* clsid, bool handlerEnabled)
{
    std::wstring dll;
    if (TryReadRegisteredDllPath(clsid, dll) &&
        GetFileAttributesW(dll.c_str()) != INVALID_FILE_ATTRIBUTES) {
        auto v = GetFileVersionString(dll);
        if (handlerEnabled) {
            SetStatusIconTextAndColor(iconId, L"✓", g_iconOkColor);
            std::wstring text = v.empty() ? L"Registered" : (L"v" + v);
            SetDlgItemTextW(g_hDlg, verId, text.c_str());
        } else {
            SetStatusIconTextAndColor(iconId, L"⚠", g_iconWarnColor);
            std::wstring text = v.empty() ? L"Registered, disabled" : (L"v" + v + L" \u2014 disabled");
            SetDlgItemTextW(g_hDlg, verId, text.c_str());
        }
    } else {
        SetStatusIconTextAndColor(iconId, L"✗", g_iconBadColor);
        SetDlgItemTextW(g_hDlg, verId, L"Not registered");
    }
}

void RefreshHandlerStatuses()
{
    SetHandlerStatus(IDC_STATIC_PROPERTY_STATUS, IDC_STATIC_PROPERTY_VER,
        L"{7C54FA8B-9D63-4C10-8FBE-1A5A0F9A3B2E}", g_pending.curPropertyEnabled);
    SetHandlerStatus(IDC_STATIC_PREVIEW_STATUS, IDC_STATIC_PREVIEW_VER,
        L"{9C76E8AD-4E85-5F30-B00D-3C7D1AB5F6E0}", g_pending.curPreviewEnabled);
}

bool IsCatalogInstalled(const catalogspec::CatalogSource& src)
{
    return installer::Probe(src).state != installer::PresenceState::Missing;
}

bool IsCatalogUpToDate(const catalogspec::CatalogSource& src)
{
    return installer::Probe(src).state == installer::PresenceState::PresentVerified;
}

void UpdateCatalogActionButtons()
{
    auto setRow = [](int fetchId, int removeId, const catalogspec::CatalogSource& src) {
        auto p = installer::Probe(src);
        bool installed = p.state != installer::PresenceState::Missing;
        bool upToDate = p.state == installer::PresenceState::PresentVerified;
        SetDlgItemTextW(g_hDlg, fetchId, installed ? L"Update" : L"Install");
        EnableWindow(GetDlgItem(g_hDlg, fetchId), upToDate ? FALSE : TRUE);
        EnableWindow(GetDlgItem(g_hDlg, removeId), installed ? TRUE : FALSE);
    };
    setRow(IDC_BTN_FETCH_NGC, IDC_BTN_REMOVE_NGC, catalogspec::kNGC);
    setRow(IDC_BTN_FETCH_ADD, IDC_BTN_REMOVE_ADD, catalogspec::kAddendum);
}

std::wstring BuildCommitLinkText()
{
    std::wstring s = L"<a href=\"https://github.com/mattiaverga/OpenNGC/tree/";
    s += catalogspec::kOpenNGCCommit;
    s += L"/database_files\">OpenNGC commit ";
    s += catalogspec::kOpenNGCCommit;
    s += L"</a>";
    return s;
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
        SetProgressText(std::wstring(src.fileName) + L" removed.");
    } else {
        SetProgressText(std::wstring(L"Failed to remove ") + std::wstring(src.fileName) + L".");
    }

    RefreshAllPresence();
    UpdateCatalogActionButtons();
    InvalidateRect(g_hDlg, nullptr, TRUE);
}

std::wstring CatalogPinnedVersion(const catalogspec::CatalogSource& src)
{
    return ShortHash(std::wstring(src.expectedSha256));
}

std::wstring CatalogLocalVersion(const installer::Presence& p)
{
    if (p.state == installer::PresenceState::Missing) return L"-";
    if (!p.computedHash.empty()) return ShortHash(p.computedHash);
    return L"?";
}

void RefreshPresenceRow(int githubLabelId, int localLabelId, int matchLabelId, const catalogspec::CatalogSource& src)
{
    const auto ghFull = std::wstring(src.expectedSha256);
    SetDlgItemTextW(g_hDlg, githubLabelId, CatalogPinnedVersion(src).c_str());

    auto p = installer::Probe(src);
    auto local = CatalogLocalVersion(p);
    SetDlgItemTextW(g_hDlg, localLabelId, local.c_str());

    std::wstring localFull = p.computedHash.empty() ? L"" : p.computedHash;
    if (githubLabelId == IDC_STATIC_NGC_GITHUB) {
        SetHashCellTooltip(IDC_STATIC_NGC_GITHUB, g_tipNgcGitHub, ghFull);
        SetHashCellTooltip(IDC_STATIC_NGC_LOCAL, g_tipNgcLocal, localFull.empty() ? L"(missing)" : localFull);
    } else {
        SetHashCellTooltip(IDC_STATIC_ADD_GITHUB, g_tipAddGitHub, ghFull);
        SetHashCellTooltip(IDC_STATIC_ADD_LOCAL, g_tipAddLocal, localFull.empty() ? L"(missing)" : localFull);
    }

    if (p.state == installer::PresenceState::PresentVerified) {
        SetStatusIconTextAndColor(matchLabelId, L"✓", g_iconOkColor);
    } else if (p.state == installer::PresenceState::PresentMismatch) {
        SetStatusIconTextAndColor(matchLabelId, L"⚠", g_iconWarnColor);
    } else {
        SetStatusIconTextAndColor(matchLabelId, L"✗", g_iconBadColor);
    }

    RedrawWindow(GetDlgItem(g_hDlg, matchLabelId), nullptr, nullptr,
        RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE);
}

void RefreshAllPresence()
{
    EnsureHashTooltipHost();
    RefreshPresenceRow(IDC_STATIC_NGC_GITHUB, IDC_STATIC_NGC_LOCAL, IDC_STATIC_NGC_MATCH, catalogspec::kNGC);
    RefreshPresenceRow(IDC_STATIC_ADD_GITHUB, IDC_STATIC_ADD_LOCAL, IDC_STATIC_ADD_MATCH, catalogspec::kAddendum);
}

void SetProgressText(const std::wstring& s)
{
    SetDlgItemTextW(g_hDlg, IDC_STATIC_PROGRESS_TEXT, s.c_str());
}

void SetBusy(bool busy)
{
    EnableWindow(GetDlgItem(g_hDlg, IDC_BTN_FETCH_NGC), !busy);
    EnableWindow(GetDlgItem(g_hDlg, IDC_BTN_FETCH_ADD), !busy);
    EnableWindow(GetDlgItem(g_hDlg, IDC_BTN_REMOVE_NGC), !busy);
    EnableWindow(GetDlgItem(g_hDlg, IDC_BTN_REMOVE_ADD), !busy);
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
    auto rep = installer::InstallFromPinnedUrl(src, ProgressTrampoline, &ctx);
    auto* heap = new installer::Report(std::move(rep));
    PostMessageW(hDlg, WM_XISF_DONE,
                 static_cast<WPARAM>(idx),
                 reinterpret_cast<LPARAM>(heap));
}

void RunLocalImport(HWND hDlg, int idx, std::wstring path)
{
    WorkerContext ctx{ hDlg };
    const auto& src = *catalogspec::kAllCatalogs[idx];
    auto rep = installer::InstallFromLocalFileVerified(src, path.c_str(),
                                                       ProgressTrampoline, &ctx);
    auto* heap = new installer::Report(std::move(rep));
    PostMessageW(hDlg, WM_XISF_DONE,
                 static_cast<WPARAM>(idx),
                 reinterpret_cast<LPARAM>(heap));
}

void OnFetchOnline(int idx)
{
    if (g_opInProgress.exchange(true)) return;
    g_cancelRequested = false;
    SetBusy(true);
    const auto& src = *catalogspec::kAllCatalogs[idx];
    SetProgressText(std::wstring(L"Downloading ") + std::wstring(src.fileName) + L" from pinned GitHub URL…");
    SendDlgItemMessageW(g_hDlg, IDC_PROGRESS, PBM_SETMARQUEE, TRUE, 30);
    SendDlgItemMessageW(g_hDlg, IDC_PROGRESS, PBM_SETPOS, 0, 0);
    std::thread([hDlg = g_hDlg, idx]() { RunOnlineInstall(hDlg, idx); }).detach();
}

void OnLocalImport(HWND hDlg, int idx, std::wstring path)
{
    if (g_opInProgress.exchange(true)) return;
    g_cancelRequested = false;
    SetBusy(true);
    SetProgressText(L"Verifying local file\u2026");
    SendDlgItemMessageW(g_hDlg, IDC_PROGRESS, PBM_SETMARQUEE, TRUE, 30);

    std::thread([hDlg = g_hDlg, idx, path = std::move(path)]() mutable {
        RunLocalImport(hDlg, idx, std::move(path));
    }).detach();
}

void OnFetchOnline()
{
    OnFetchOnline(0);
}

int ChooseCatalogIndexForImport()
{
    int r = MessageBoxW(g_hDlg,
        L"Which catalog file are you importing?\r\n\r\n"
        L"Yes = NGC.csv\r\nNo = addendum.csv\r\nCancel = abort\r\n\r\n"
        L"The file's SHA-256 will be verified against the pinned value. "
        L"Files that don't match are rejected.",
        L"Select Catalog", MB_YESNOCANCEL | MB_ICONQUESTION);
    if (r == IDYES) return 0;
    if (r == IDNO)  return 1;
    return -1;
}

void OnImportFile()
{
    if (g_opInProgress.exchange(true)) return;
    int idx = ChooseCatalogIndexForImport();
    if (idx < 0) { g_opInProgress = false; return; }

    wchar_t filePath[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = g_hDlg;
    ofn.lpstrFile   = filePath;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrFilter = L"CSV files (*.csv)\0*.csv\0All files (*.*)\0*.*\0";
    ofn.lpstrTitle  = L"Select local catalog file (must match pinned SHA-256)";
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;

    if (!GetOpenFileNameW(&ofn)) { g_opInProgress = false; return; }

    g_cancelRequested = false;
    SetBusy(true);
    SetProgressText(L"Verifying local file\u2026");
    SendDlgItemMessageW(g_hDlg, IDC_PROGRESS, PBM_SETMARQUEE, TRUE, 30);

    std::wstring path = filePath;
    std::thread([hDlg = g_hDlg, idx, path]() { RunLocalImport(hDlg, idx, path); }).detach();
}

void OnOpenCatalogDir()
{
    std::wstring dir = paths::CatalogDir();
    if (!dir.empty())
        ShellExecuteW(g_hDlg, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

std::wstring OpenNGCSourceUrl()
{
    std::wstring url = L"https://github.com/mattiaverga/OpenNGC/tree/";
    url += catalogspec::kOpenNGCCommit;
    url += L"/database_files";
    return url;
}

void OnBrowseOpenNGC()
{
    std::wstring url = OpenNGCSourceUrl();
    auto rc = reinterpret_cast<INT_PTR>(
        ShellExecuteW(g_hDlg, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (rc <= 32) {
        SetProgressText(L"Failed to open the pinned OpenNGC source page.");
        return;
    }
    SetProgressText(L"Opened the pinned OpenNGC source page on github.com.");
}

void OnRestartExplorer()
{
    if (MessageBoxW(g_hDlg,
            L"Restart File Explorer now?\r\n\r\n"
            L"This closes and relaunches explorer.exe so handler changes apply immediately.",
            L"Restart Explorer", MB_YESNO | MB_ICONQUESTION) != IDYES)
        return;

    static constexpr wchar_t kRestartExplorerArgs[] =
        L"-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -Command "
        L"\"Stop-Process -Name explorer -Force -ErrorAction SilentlyContinue; "
        L"Start-Sleep -Milliseconds 500; Start-Process explorer.exe\"";

    auto rc = reinterpret_cast<INT_PTR>(
        ShellExecuteW(g_hDlg, L"runas", L"powershell.exe", kRestartExplorerArgs, nullptr, SW_HIDE));
    if (rc <= 32) {
        if (rc == SE_ERR_ACCESSDENIED)
            SetProgressText(L"Restart Explorer cancelled.");
        else
            SetProgressText(L"Failed to restart Explorer.");
        return;
    }

    SetProgressText(L"Restart Explorer requested.");
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
        text += src->fileName; text += L"  sha256 = ";
        text += src->expectedSha256; text += L"\r\n";
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
    SetProgressText(L"Expected hashes copied to clipboard. Verify independently on github.com.");
}

void AddTooltips()
{
    auto makeTip = [](int id, const wchar_t* text) {
        HWND hCtrl = GetDlgItem(g_hDlg, id);
        if (!hCtrl) return;
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
        ti.lpszText = const_cast<LPWSTR>(text);
        SendMessageW(hTip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
    };

    makeTip(IDC_BTN_TOGGLE_PROPERTY, L"Toggle the Property Handler (details pane, file info)");
    makeTip(IDC_BTN_TOGGLE_PREVIEW, L"Toggle the Preview/Thumbnail Handler (preview pane, thumbnails)");
}

void OnRegisterHandlers()
{
    std::wstring params = L"--register-direct --property --preview";

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

    const auto& src = *catalogspec::kAllCatalogs[idx];
    std::wstring fileLabel(src.fileName);
    std::wstring msg;

    if (rep->result == installer::Result::Ok) {
        msg = fileLabel + L": installed (" + FormatBytes(rep->bytesTransferred) +
              L", sha256 matches pin).";
        RefreshAllPresence();
    } else if (rep->result == installer::Result::HashMismatch) {
        msg = fileLabel + L": REJECTED \u2014 SHA-256 mismatch.\r\n"
              L"  expected " + std::wstring(src.expectedSha256) + L"\r\n"
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

        g_pending.Snapshot();

        std::wstring linkText = BuildCommitLinkText();
        SetDlgItemTextW(hDlg, IDC_LINK_OPENNGC_COMMIT, linkText.c_str());
        SendDlgItemMessageW(hDlg, IDC_BTN_ADVANCED, BCM_SETSHIELD, 0, TRUE);
        SendDlgItemMessageW(hDlg, IDC_BTN_RESTART_EXPLORER, BCM_SETSHIELD, 0, TRUE);
        SendDlgItemMessageW(hDlg, IDC_BTN_FLUSH_THUMBCACHE, BCM_SETSHIELD, 0, TRUE);
        SendDlgItemMessageW(hDlg, IDC_BTN_REGISTER_HANDLERS, BCM_SETSHIELD, 0, TRUE);

        UpdateToggleButton(IDC_BTN_TOGGLE_PROPERTY, g_pending.curPropertyEnabled);
        UpdateToggleButton(IDC_BTN_TOGGLE_PREVIEW, g_pending.curPreviewEnabled);

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

        // Apply button starts disabled (no pending changes)
        EnableWindow(GetDlgItem(hDlg, IDC_BTN_APPLY), FALSE);

        RefreshAllPresence();
        RefreshHandlerStatuses();
        UpdateCatalogActionButtons();
        AddTooltips();
        return TRUE;
    }

    case WM_NOTIFY: {
        auto* hdr = reinterpret_cast<NMHDR*>(lParam);
        if (hdr && hdr->idFrom == IDC_LINK_OPENNGC_COMMIT && (hdr->code == NM_CLICK || hdr->code == NM_RETURN)) {
            std::wstring url = OpenNGCSourceUrl();
            ShellExecuteW(hDlg, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return TRUE;
        }
        if (hdr && hdr->idFrom == IDC_LINK_HASH_HELP && (hdr->code == NM_CLICK || hdr->code == NM_RETURN)) {
            ShellExecuteW(hDlg, L"open", L"https://docs.github.com/en/authentication/keeping-your-account-and-data-secure/about-git-commit-signature-verification", nullptr, nullptr, SW_SHOWNORMAL);
            SetProgressText(L"Open GitHub docs: independently verify file content and commit provenance.");
            return TRUE;
        }
        if (hdr && hdr->idFrom == IDC_LINK_VERSION && (hdr->code == NM_CLICK || hdr->code == NM_RETURN)) {
            ShellExecuteW(hDlg, L"open",
                L"https://github.com/dennispayne/XISF-Shell-Extensions",
                nullptr, nullptr, SW_SHOWNORMAL);
            return TRUE;
        }
        break;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        HWND hCtl = reinterpret_cast<HWND>(lParam);
        int id = GetDlgCtrlID(hCtl);
        if (id == IDC_STATIC_PROPERTY_STATUS || id == IDC_STATIC_PREVIEW_STATUS ||
            id == IDC_STATIC_NGC_MATCH || id == IDC_STATIC_ADD_MATCH) {
            COLORREF color = g_iconBadColor;
            if (id == IDC_STATIC_PROPERTY_STATUS) color = g_propertyIconColor;
            else if (id == IDC_STATIC_PREVIEW_STATUS) color = g_previewIconColor;
            else if (id == IDC_STATIC_NGC_MATCH) color = g_ngcIconColor;
            else if (id == IDC_STATIC_ADD_MATCH) color = g_addIconColor;
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
            g_pending.curPropertyEnabled = !g_pending.curPropertyEnabled;
            UpdateToggleButton(IDC_BTN_TOGGLE_PROPERTY, g_pending.curPropertyEnabled);
            RefreshHandlerStatuses();
            SetProgressText(g_pending.curPropertyEnabled
                ? L"Property Handler will be enabled after Apply."
                : L"Property Handler will be disabled after Apply.");
            UpdatePendingDisplay();
            return TRUE;
        }
        case IDC_BTN_TOGGLE_PREVIEW: {
            g_pending.curPreviewEnabled = !g_pending.curPreviewEnabled;
            UpdateToggleButton(IDC_BTN_TOGGLE_PREVIEW, g_pending.curPreviewEnabled);
            RefreshHandlerStatuses();
            SetProgressText(g_pending.curPreviewEnabled
                ? L"Preview Handler will be enabled after Apply."
                : L"Preview Handler will be disabled after Apply.");
            UpdatePendingDisplay();
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
        case IDC_BTN_SHOW_MAPPING: {
            DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_MAPPING), hDlg,
                [](HWND hDlg, UINT msg, WPARAM wParam, LPARAM) -> INT_PTR {
                    if (msg == WM_COMMAND && LOWORD(wParam) == IDOK) { EndDialog(hDlg, IDOK); return TRUE; }
                    if (msg == WM_CLOSE) { EndDialog(hDlg, IDCANCEL); return TRUE; }
                    return FALSE;
                }, 0);
            return TRUE;
        }
        case IDC_BTN_SHOW_TIERS: {
            DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_TIERS), hDlg,
                [](HWND hDlg, UINT msg, WPARAM wParam, LPARAM) -> INT_PTR {
                    if (msg == WM_COMMAND && LOWORD(wParam) == IDOK) { EndDialog(hDlg, IDOK); return TRUE; }
                    if (msg == WM_CLOSE) { EndDialog(hDlg, IDCANCEL); return TRUE; }
                    return FALSE;
                }, 0);
            return TRUE;
        }
        case IDC_BTN_REGISTER_HANDLERS:    OnRegisterHandlers();      return TRUE;
        case IDC_BTN_FETCH_NGC:            OnFetchOnline(0);         return TRUE;
        case IDC_BTN_FETCH_ADD:            OnFetchOnline(1);         return TRUE;
        case IDC_BTN_REMOVE_NGC:           OnRemoveCatalog(0);       return TRUE;
        case IDC_BTN_REMOVE_ADD:           OnRemoveCatalog(1);       return TRUE;
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
            g_pending.Apply();
            UpdatePendingDisplay();
            wchar_t buf[128];
            swprintf_s(buf, L"%d setting%s applied. Restart Explorer to take effect.", n, n == 1 ? L"" : L"s");
            SetProgressText(buf);
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

    const bool includeProp = propError || propWarn || propInfo || propVerbose;
    const bool includePrev = prevError || prevWarn || prevInfo || prevVerbose;

    if (!includeProp && !includePrev) {
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
        CheckDlgButton(hDlg, IDC_CHK_TRACE_PROP_ERROR, BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_CHK_TRACE_PROP_WARN, BST_CHECKED);
        CheckDlgButton(hDlg, IDC_CHK_TRACE_PROP_INFO, BST_CHECKED);
        CheckDlgButton(hDlg, IDC_CHK_TRACE_PROP_VERBOSE, BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_CHK_TRACE_PREV_ERROR, BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_CHK_TRACE_PREV_WARN, BST_CHECKED);
        CheckDlgButton(hDlg, IDC_CHK_TRACE_PREV_INFO, BST_CHECKED);
        CheckDlgButton(hDlg, IDC_CHK_TRACE_PREV_VERBOSE, BST_UNCHECKED);
        g_traceRunning = IsTraceSessionRunning();
        g_traceExportInProgress = false;
        g_activeTracePath.clear();
        g_lastStoppedTracePath.clear();
        UpdateTraceActionButtons(hDlg);
        SetAdvStatus(hDlg, g_traceRunning ? L"Trace session is RUNNING." : L"Trace session is STOPPED.");
        return TRUE;
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
        case IDC_BTN_TRACE_START: OnTraceStart(hDlg); return TRUE;
        case IDC_BTN_TRACE_STOP:  OnTraceStop(hDlg);  return TRUE;
        case IDC_BTN_TRACE_OPEN_ETL:  OnTraceView(hDlg);  return TRUE;
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

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS | ICC_LINK_CLASS };
    InitCommonControlsEx(&icc);

    DialogBoxParamW(hInstance, MAKEINTRESOURCEW(IDD_SETTINGS),
                    nullptr, DlgProc, 0);
    return 0;
}
