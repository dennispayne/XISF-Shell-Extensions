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

static constexpr const wchar_t* kContentIndexerExtPath =
    L"SOFTWARE\\Microsoft\\Windows Search\\ContentIndexer\\Extensions\\.xisf";
static constexpr const wchar_t* kApprovedShellExtPath =
    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved";

static void FormatClsidPath(const wchar_t* guid, wchar_t* out, size_t outSize) {
    swprintf_s(out, outSize, L"CLSID\\%s", guid);
}

static bool IsOurRegistryValue(HKEY root, const wchar_t* subkey,
                                const wchar_t* valueName, const wchar_t* expected) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(root, subkey, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return false;
    wchar_t buf[64] = {};
    DWORD cb = sizeof(buf);
    LONG g = RegGetValueW(hKey, nullptr, valueName, RRF_RT_REG_SZ, nullptr, buf, &cb);
    RegCloseKey(hKey);
    return (g == ERROR_SUCCESS && _wcsicmp(buf, expected) == 0);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, LPVOID) {
    if (dwReason == DLL_PROCESS_ATTACH) {
        g_hInst = hModule;
        DisableThreadLibraryCalls(hModule);
        TraceLoggingRegister(g_hFilterProvider);
        TraceLoggingWrite(g_hFilterProvider, "FilterDllAttach",
            TraceLoggingLevel(TRACE_LEVEL_VERBOSE),
            TraceLoggingKeyword(XISF_FILTER_KEYWORD_LIFECYCLE));
    }
    else if (dwReason == DLL_PROCESS_DETACH) {
        TraceLoggingWrite(g_hFilterProvider, "FilterDllDetach",
            TraceLoggingLevel(TRACE_LEVEL_VERBOSE),
            TraceLoggingKeyword(XISF_FILTER_KEYWORD_LIFECYCLE));
        TraceLoggingUnregister(g_hFilterProvider);
    }
    return TRUE;
}

STDAPI DllCanUnloadNow(void) { return (g_cDllRef == 0) ? S_OK : S_FALSE; }

STDAPI DllGetClassObject(_In_ REFCLSID rclsid, _In_ REFIID riid, _Outptr_ void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;

    TraceLoggingWrite(g_hFilterProvider, "FilterDllGetClassObject",
        TraceLoggingLevel(TRACE_LEVEL_VERBOSE),
        TraceLoggingKeyword(XISF_FILTER_KEYWORD_LIFECYCLE),
        TraceLoggingGuid(rclsid, "CLSID"),
        TraceLoggingGuid(riid, "IID"));

    if (!IsEqualCLSID(rclsid, CLSID_XISFFilter)) return CLASS_E_CLASSNOTAVAILABLE;
    CClassFactory* pf = new (std::nothrow) CClassFactory();
    if (!pf) return E_OUTOFMEMORY;
    HRESULT hr = pf->QueryInterface(riid, ppv);
    pf->Release();
    return hr;
}

// Registry helpers

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

STDAPI DllRegisterServer(void) {
    wchar_t szDllPath[MAX_PATH] = {};
    if (!GetModuleFileNameW(g_hInst, szDllPath, ARRAYSIZE(szDllPath)))
        return SELFREG_E_CLASS;

    HRESULT hr = S_OK;

    wchar_t szClsidRoot[128];
    FormatClsidPath(kFilterClsidStr, szClsidRoot, ARRAYSIZE(szClsidRoot));
    hr = SetRegSZValue(HKEY_CLASSES_ROOT, szClsidRoot, nullptr, L"XISF Search Filter");
    if (FAILED(hr)) return SELFREG_E_CLASS;

    wchar_t szInProc[256];
    swprintf_s(szInProc, L"CLSID\\%s\\InProcServer32", kFilterClsidStr);
    hr = SetRegSZValue(HKEY_CLASSES_ROOT, szInProc, nullptr, szDllPath);
    if (FAILED(hr)) return SELFREG_E_CLASS;
    hr = SetRegSZValue(HKEY_CLASSES_ROOT, szInProc, L"ThreadingModel", L"Both");
    if (FAILED(hr)) return SELFREG_E_CLASS;

    wchar_t szPHRoot[128];
    FormatClsidPath(kPersistentHandlerClsidStr, szPHRoot, ARRAYSIZE(szPHRoot));
    hr = SetRegSZValue(HKEY_CLASSES_ROOT, szPHRoot, nullptr, L"XISF Persistent Handler");
    if (FAILED(hr)) return SELFREG_E_CLASS;

    wchar_t szPAR[512];
    swprintf_s(szPAR, L"CLSID\\%s\\PersistentAddinsRegistered\\%s",
               kPersistentHandlerClsidStr, kIIDFilterStr);
    hr = SetRegSZValue(HKEY_CLASSES_ROOT, szPAR, nullptr, kFilterClsidStr);
    if (FAILED(hr)) return SELFREG_E_CLASS;

    hr = SetRegSZValue(HKEY_CLASSES_ROOT, L".xisf\\PersistentHandler",
                        nullptr, kPersistentHandlerClsidStr);
    if (FAILED(hr)) return SELFREG_E_CLASS;

    hr = SetRegSZValue(HKEY_LOCAL_MACHINE, kContentIndexerExtPath,
                        nullptr, kFilterClsidStr);
    if (FAILED(hr)) return SELFREG_E_CLASS;

    // Approved Shell Extensions list — required when EnforceShellExtensionSecurity GPO is set.
    hr = SetRegSZValue(HKEY_LOCAL_MACHINE, kApprovedShellExtPath,
                        kFilterClsidStr, L"XISF Search Filter");
    if (FAILED(hr)) return SELFREG_E_CLASS;

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}

STDAPI DllUnregisterServer(void) {
    // Only remove .xisf\PersistentHandler if its default value is OUR
    // persistent handler GUID. Other XISF tooling (e.g. PixInsight) may
    // install a different IFilter chain and we must not nuke theirs.
    if (IsOurRegistryValue(HKEY_CLASSES_ROOT, L".xisf\\PersistentHandler",
                           nullptr, kPersistentHandlerClsidStr)) {
        RegDeleteTreeW(HKEY_CLASSES_ROOT, L".xisf\\PersistentHandler");
    }

    wchar_t szPHRoot[128];
    FormatClsidPath(kPersistentHandlerClsidStr, szPHRoot, ARRAYSIZE(szPHRoot));
    RegDeleteTreeW(HKEY_CLASSES_ROOT, szPHRoot);

    wchar_t szClsidRoot[128];
    FormatClsidPath(kFilterClsidStr, szClsidRoot, ARRAYSIZE(szClsidRoot));
    RegDeleteTreeW(HKEY_CLASSES_ROOT, szClsidRoot);

    // Remove Windows Search indexer extension only if it currently points at our filter.
    if (IsOurRegistryValue(HKEY_LOCAL_MACHINE, kContentIndexerExtPath,
                           nullptr, kFilterClsidStr)) {
        RegDeleteTreeW(HKEY_LOCAL_MACHINE, kContentIndexerExtPath);
    }

    // Remove from Approved Shell Extensions list.
    {
        HKEY hApproved = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kApprovedShellExtPath,
            0, KEY_SET_VALUE, &hApproved) == ERROR_SUCCESS) {
            RegDeleteValueW(hApproved, kFilterClsidStr);
            RegCloseKey(hApproved);
        }
    }

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}
