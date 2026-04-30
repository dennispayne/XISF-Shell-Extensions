// dllmain.cpp — COM DLL entry points for XISF Search Filter
#include <windows.h>
#include <initguid.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <olectl.h>
#include "FilterTelemetry.h"
#include <strsafe.h>
#include <new>
#include <wchar.h>
#include "ClassFactory.h"
#include "XISFFilter.h"

TRACELOGGING_DEFINE_PROVIDER(g_hFilterProvider, "XISF-Filter",
    (0x3a4b5c6d, 0x7e8f, 0x9012, 0xab, 0x34, 0xcd, 0x56, 0xef, 0x78, 0x90, 0x12));

// Single definition for the test hook pointer.
extern "C" XISFFilterTelemetryHook g_xisfFilterTelemetryHook = nullptr;

void WriteFilterTelemetry(UCHAR level, ULONGLONG keyword, PCWSTR format, ...) {
    const bool hookEnabled = (g_xisfFilterTelemetryHook != nullptr);
    wchar_t buffer[768] = {};
    va_list args;
    va_start(args, format);
    StringCchVPrintfW(buffer, ARRAYSIZE(buffer), format, args);
    va_end(args);

    EventWriteString(g_hFilterProvider->RegHandle, level, keyword, buffer);

    if (hookEnabled) {
        g_xisfFilterTelemetryHook(level, keyword, buffer);
    }
}

HINSTANCE g_hInst = nullptr;
long g_cDllRef = 0;

static const wchar_t kFilterClsidStr[] = L"{B4E7F2A1-3D8C-4F5E-9A1B-6C2D8E4F7A3B}";
static const wchar_t kPersistentHandlerClsidStr[] = L"{C5F8A3B2-4E9D-5067-AB2C-7D3E9F508B4C}";

// IID_IFilter {89BCB740-6119-101A-BCB7-00DD010655AF}
static const wchar_t kIIDFilterStr[] = L"{89BCB740-6119-101A-BCB7-00DD010655AF}";

BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, LPVOID) {
    if (dwReason == DLL_PROCESS_ATTACH) {
        g_hInst = hModule;
        DisableThreadLibraryCalls(hModule);
        TraceLoggingRegister(g_hFilterProvider);
    }
    else if (dwReason == DLL_PROCESS_DETACH) {
        TraceLoggingUnregister(g_hFilterProvider);
    }
    return TRUE;
}

STDAPI DllCanUnloadNow(void) { return (g_cDllRef == 0) ? S_OK : S_FALSE; }

STDAPI DllGetClassObject(_In_ REFCLSID rclsid, _In_ REFIID riid, _Outptr_ void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (!IsEqualCLSID(rclsid, CLSID_XISFFilter)) return CLASS_E_CLASSNOTAVAILABLE;
    CClassFactory* pf = new (std::nothrow) CClassFactory();
    if (!pf) return E_OUTOFMEMORY;
    HRESULT hr = pf->QueryInterface(riid, ppv);
    pf->Release();
    return hr;
}

// ---------------------------------------------------------------------------
// Registry helpers
// ---------------------------------------------------------------------------

static HRESULT SetRegSZValue(HKEY hRoot, const wchar_t* subKey,
                              const wchar_t* valueName, const wchar_t* data) {
    HKEY hKey = nullptr; DWORD dwDisp = 0;
    LONG lr = RegCreateKeyExW(hRoot, subKey, 0, nullptr, REG_OPTION_NON_VOLATILE,
                               KEY_WRITE, nullptr, &hKey, &dwDisp);
    if (lr != ERROR_SUCCESS) return HRESULT_FROM_WIN32(lr);
    DWORD cbData = static_cast<DWORD>((wcslen(data) + 1) * sizeof(wchar_t));
    lr = RegSetValueExW(hKey, valueName, 0, REG_SZ,
                         reinterpret_cast<const BYTE*>(data), cbData);
    RegCloseKey(hKey);
    return HRESULT_FROM_WIN32(lr);
}

// ---------------------------------------------------------------------------
// DllRegisterServer
// ---------------------------------------------------------------------------

STDAPI DllRegisterServer(void) {
    wchar_t szDllPath[MAX_PATH] = {};
    if (!GetModuleFileNameW(g_hInst, szDllPath, ARRAYSIZE(szDllPath)))
        return SELFREG_E_CLASS;

    HRESULT hr = S_OK;

    // HKCR\CLSID\{filter-clsid} = "XISF Search Filter"
    wchar_t szClsidRoot[128];
    swprintf_s(szClsidRoot, L"CLSID\\%s", kFilterClsidStr);
    hr = SetRegSZValue(HKEY_CLASSES_ROOT, szClsidRoot, nullptr, L"XISF Search Filter");
    if (FAILED(hr)) return SELFREG_E_CLASS;

    // HKCR\CLSID\{filter-clsid}\InProcServer32 = <dll-path>
    wchar_t szInProc[256];
    swprintf_s(szInProc, L"CLSID\\%s\\InProcServer32", kFilterClsidStr);
    hr = SetRegSZValue(HKEY_CLASSES_ROOT, szInProc, nullptr, szDllPath);
    if (FAILED(hr)) return SELFREG_E_CLASS;
    hr = SetRegSZValue(HKEY_CLASSES_ROOT, szInProc, L"ThreadingModel", L"Both");
    if (FAILED(hr)) return SELFREG_E_CLASS;

    // HKCR\CLSID\{persistent-handler-guid} = "XISF Persistent Handler"
    wchar_t szPHRoot[128];
    swprintf_s(szPHRoot, L"CLSID\\%s", kPersistentHandlerClsidStr);
    hr = SetRegSZValue(HKEY_CLASSES_ROOT, szPHRoot, nullptr, L"XISF Persistent Handler");
    if (FAILED(hr)) return SELFREG_E_CLASS;

    // HKCR\CLSID\{persistent-handler-guid}\PersistentAddinsRegistered\{IID_IFilter} = {filter-clsid}
    wchar_t szPAR[512];
    swprintf_s(szPAR, L"CLSID\\%s\\PersistentAddinsRegistered\\%s",
               kPersistentHandlerClsidStr, kIIDFilterStr);
    hr = SetRegSZValue(HKEY_CLASSES_ROOT, szPAR, nullptr, kFilterClsidStr);
    if (FAILED(hr)) return SELFREG_E_CLASS;

    // HKCR\.xisf\PersistentHandler = {persistent-handler-guid}
    hr = SetRegSZValue(HKEY_CLASSES_ROOT, L".xisf\\PersistentHandler",
                        nullptr, kPersistentHandlerClsidStr);
    if (FAILED(hr)) return SELFREG_E_CLASS;

    // HKLM\SOFTWARE\Microsoft\Windows Search\ContentIndexer\Extensions\.xisf = {filter-clsid}
    hr = SetRegSZValue(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows Search\\ContentIndexer\\Extensions\\.xisf",
        nullptr, kFilterClsidStr);
    if (FAILED(hr)) return SELFREG_E_CLASS;

    // Approved Shell Extensions list — required when EnforceShellExtensionSecurity GPO is set.
    hr = SetRegSZValue(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved",
        kFilterClsidStr, L"XISF Search Filter");
    if (FAILED(hr)) return SELFREG_E_CLASS;

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}

// ---------------------------------------------------------------------------
// DllUnregisterServer
// ---------------------------------------------------------------------------

STDAPI DllUnregisterServer(void) {
    // Only remove .xisf\PersistentHandler if its default value is OUR
    // persistent handler GUID. Other XISF tooling (e.g. PixInsight) may
    // install a different IFilter chain and we must not nuke theirs.
    {
        HKEY hPH = nullptr;
        if (RegOpenKeyExW(HKEY_CLASSES_ROOT, L".xisf\\PersistentHandler",
                          0, KEY_READ, &hPH) == ERROR_SUCCESS) {
            wchar_t buf[64] = {};
            DWORD cb = sizeof(buf);
            LONG g = RegGetValueW(hPH, nullptr, nullptr, RRF_RT_REG_SZ, nullptr, buf, &cb);
            RegCloseKey(hPH);
            if (g == ERROR_SUCCESS && _wcsicmp(buf, kPersistentHandlerClsidStr) == 0) {
                RegDeleteTreeW(HKEY_CLASSES_ROOT, L".xisf\\PersistentHandler");
            }
        }
    }

    // Remove persistent handler CLSID
    wchar_t szPHRoot[128];
    swprintf_s(szPHRoot, L"CLSID\\%s", kPersistentHandlerClsidStr);
    RegDeleteTreeW(HKEY_CLASSES_ROOT, szPHRoot);

    // Remove filter CLSID
    wchar_t szClsidRoot[128];
    swprintf_s(szClsidRoot, L"CLSID\\%s", kFilterClsidStr);
    RegDeleteTreeW(HKEY_CLASSES_ROOT, szClsidRoot);

    // Remove Windows Search indexer extension only if it currently points at our filter.
    {
        HKEY hExt = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows Search\\ContentIndexer\\Extensions\\.xisf",
            0, KEY_READ, &hExt) == ERROR_SUCCESS) {
            wchar_t buf[64] = {};
            DWORD cb = sizeof(buf);
            LONG g = RegGetValueW(hExt, nullptr, nullptr, RRF_RT_REG_SZ, nullptr, buf, &cb);
            RegCloseKey(hExt);
            if (g == ERROR_SUCCESS && _wcsicmp(buf, kFilterClsidStr) == 0) {
                RegDeleteTreeW(HKEY_LOCAL_MACHINE,
                    L"SOFTWARE\\Microsoft\\Windows Search\\ContentIndexer\\Extensions\\.xisf");
            }
        }
    }

    // Remove from Approved Shell Extensions list.
    {
        HKEY hApproved = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved",
            0, KEY_SET_VALUE, &hApproved) == ERROR_SUCCESS) {
            RegDeleteValueW(hApproved, kFilterClsidStr);
            RegCloseKey(hApproved);
        }
    }

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}
