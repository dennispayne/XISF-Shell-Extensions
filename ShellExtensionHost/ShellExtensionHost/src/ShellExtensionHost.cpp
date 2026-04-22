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

#include "HostResources.h"
#include "HostSettings.h"
#include "Paths.h"
#include "CatalogSpec.h"
#include "CatalogInstaller.h"
#include "Sha256.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")

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

void RefreshPresenceRow(int labelId, const catalogspec::CatalogSource& src)
{
    auto p = installer::Probe(src);
    std::wstring msg;
    switch (p.state) {
        case installer::PresenceState::Missing:
            msg = L"not installed"; break;
        case installer::PresenceState::PresentUnknown:
            msg = L"present (" + FormatBytes(p.sizeBytes) + L"), hash not computed"; break;
        case installer::PresenceState::PresentVerified:
            msg = L"OK  " + FormatBytes(p.sizeBytes) +
                  L"  sha256 " + ShortHash(p.computedHash) + L"  (matches pin)"; break;
        case installer::PresenceState::PresentMismatch:
            msg = L"WARNING: hash mismatch  " + FormatBytes(p.sizeBytes) +
                  L"  got " + ShortHash(p.computedHash) +
                  L"  expected " + ShortHash(std::wstring(src.expectedSha256)); break;
    }
    SetDlgItemTextW(g_hDlg, labelId, msg.c_str());
}

void RefreshAllPresence()
{
    RefreshPresenceRow(IDC_STATIC_NGC_STATUS, catalogspec::kNGC);
    RefreshPresenceRow(IDC_STATIC_ADD_STATUS, catalogspec::kAddendum);
}

void SetProgressText(const std::wstring& s)
{
    SetDlgItemTextW(g_hDlg, IDC_STATIC_PROGRESS_TEXT, s.c_str());
}

void SetBusy(bool busy)
{
    EnableWindow(GetDlgItem(g_hDlg, IDC_BTN_FETCH_ONLINE), !busy);
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

void OnFetchOnline()
{
    if (g_opInProgress.exchange(true)) return;
    g_cancelRequested = false;
    SetBusy(true);
    SetProgressText(L"Contacting raw.githubusercontent.com\u2026");
    SendDlgItemMessageW(g_hDlg, IDC_PROGRESS, PBM_SETMARQUEE, TRUE, 30);
    SendDlgItemMessageW(g_hDlg, IDC_PROGRESS, PBM_SETPOS, 0, 0);
    std::thread([hDlg = g_hDlg]() { RunOnlineInstall(hDlg, 0); }).detach();
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
        if (idx == 0) {
            SetProgressText(L"NGC.csv installed. Continuing with addendum.csv\u2026");
            SendDlgItemMessageW(g_hDlg, IDC_PROGRESS, PBM_SETMARQUEE, TRUE, 30);
            std::thread([hDlg = g_hDlg]() { RunOnlineInstall(hDlg, 1); }).detach();
            return;
        }
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

        std::wstring pin = L"Pinned OpenNGC commit ";
        pin += catalogspec::kOpenNGCCommit;
        pin += L"  (";
        pin += catalogspec::kOpenNGCCommitDate;
        pin += L").  Catalogs load from ";
        pin += paths::CatalogDir();
        SetDlgItemTextW(hDlg, IDC_STATIC_PIN, pin.c_str());

        CheckDlgButton(hDlg, IDC_CHK_PROPERTY,
                       hostsettings::IsPropertyEnabled() ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_CHK_PREVIEW,
                       hostsettings::IsPreviewEnabled()  ? BST_CHECKED : BST_UNCHECKED);

        std::wstring ver = L"Version " XISF_VERSION_WSTR
                           L"  \u2014  github.com/dennispayne/XISF-Shell-Extensions";
        SetDlgItemTextW(hDlg, IDC_STATIC_VERSION, ver.c_str());

        RefreshAllPresence();
        return TRUE;
    }

    case WM_COMMAND: {
        WORD id   = LOWORD(wParam);
        WORD code = HIWORD(wParam);
        switch (id) {
        case IDC_CHK_PROPERTY:
            if (code == BN_CLICKED)
                hostsettings::SetPropertyEnabled(
                    IsDlgButtonChecked(hDlg, IDC_CHK_PROPERTY) == BST_CHECKED);
            return TRUE;
        case IDC_CHK_PREVIEW:
            if (code == BN_CLICKED)
                hostsettings::SetPreviewEnabled(
                    IsDlgButtonChecked(hDlg, IDC_CHK_PREVIEW) == BST_CHECKED);
            return TRUE;
        case IDC_BTN_FETCH_ONLINE:         OnFetchOnline();         return TRUE;
        case IDC_BTN_IMPORT_FILE:          OnImportFile();          return TRUE;
        case IDC_BTN_OPEN_CATALOG_DIR:     OnOpenCatalogDir();      return TRUE;
        case IDC_BTN_COPY_EXPECTED_HASHES: OnCopyExpectedHashes();  return TRUE;
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
    }
    return FALSE;
}

} // namespace

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int)
{
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS };
    InitCommonControlsEx(&icc);

    DialogBoxParamW(hInstance, MAKEINTRESOURCEW(IDD_SETTINGS),
                    nullptr, DlgProc, 0);
    return 0;
}
