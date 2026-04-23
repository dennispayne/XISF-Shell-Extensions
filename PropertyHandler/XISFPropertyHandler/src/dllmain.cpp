// dllmain.cpp - COM DLL entry points for XISF Property Handler (Property Handler)
#include <windows.h>
#include <initguid.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <propsys.h>
#include <olectl.h>
#include "PropertyHandlerTraceLogging.h"
#include <new>
#include <wchar.h>
#include "ClassFactory.h"

// {7C54FA8B-9D63-4C10-8FBE-1A5A0F9A3B2E}
DEFINE_GUID(CLSID_XISFPropertyHandler,
    0x7C54FA8B, 0x9D63, 0x4C10, 0x8F, 0xBE, 0x1A, 0x5A, 0x0F, 0x9A, 0x3B, 0x2E);

TRACELOGGING_DEFINE_PROVIDER(g_hPropertyProvider, "XISF-PropertyHandler",
    (0x6f6b0c9d, 0x6b76, 0x5a24, 0xbc, 0x3d, 0x70, 0x83, 0x14, 0xe9, 0x6f, 0x2b));

HINSTANCE g_hInst = nullptr;
long g_cDllRef = 0;

static const wchar_t kClsidStr[] = L"{7C54FA8B-9D63-4C10-8FBE-1A5A0F9A3B2E}";
static const wchar_t kPropertyHandlersKey[] =
    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\PropertySystem\\PropertyHandlers\\.xisf";
static const wchar_t kExtKey[] = L".xisf";
static const wchar_t kProgID[] = L"XISFFile";
static const wchar_t kSearchExtKey[] =
    L"SOFTWARE\\Microsoft\\Windows Search\\CrawlScopeManager\\Windows\\SystemIndex\\Extensions\\.xisf";

BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, LPVOID) {
    if (dwReason == DLL_PROCESS_ATTACH) {
        g_hInst = hModule;
        DisableThreadLibraryCalls(hModule);
        TraceLoggingRegister(g_hPropertyProvider);
    }
    else if (dwReason == DLL_PROCESS_DETACH) {
        TraceLoggingUnregister(g_hPropertyProvider);
    }
    return TRUE;
}

STDAPI DllCanUnloadNow(void) { return (g_cDllRef == 0) ? S_OK : S_FALSE; }

STDAPI DllGetClassObject(_In_ REFCLSID rclsid, _In_ REFIID riid, _Outptr_ void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (!IsEqualCLSID(rclsid, CLSID_XISFPropertyHandler)) return CLASS_E_CLASSNOTAVAILABLE;
    CClassFactory* pf = new (std::nothrow) CClassFactory();
    if (!pf) return E_OUTOFMEMORY;
    HRESULT hr = pf->QueryInterface(riid, ppv);
    pf->Release();
    return hr;
}

static HRESULT SetRegSZValue(HKEY hRoot, const wchar_t* subKey, const wchar_t* valueName, const wchar_t* data) {
    HKEY hKey = nullptr; DWORD dwDisp = 0;
    LONG lr = RegCreateKeyExW(hRoot, subKey, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, &dwDisp);
    if (lr != ERROR_SUCCESS) return HRESULT_FROM_WIN32(lr);
    DWORD cbData = static_cast<DWORD>((wcslen(data) + 1) * sizeof(wchar_t));
    lr = RegSetValueExW(hKey, valueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(data), cbData);
    RegCloseKey(hKey);
    return HRESULT_FROM_WIN32(lr);
}

static HRESULT SetRegDWORDValue(HKEY hRoot, const wchar_t* subKey, const wchar_t* valueName, DWORD data) {
    HKEY hKey = nullptr; DWORD dwDisp = 0;
    LONG lr = RegCreateKeyExW(hRoot, subKey, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, &dwDisp);
    if (lr != ERROR_SUCCESS) return HRESULT_FROM_WIN32(lr);
    lr = RegSetValueExW(hKey, valueName, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&data), sizeof(data));
    RegCloseKey(hKey);
    return HRESULT_FROM_WIN32(lr);
}

STDAPI DllRegisterServer(void) {
    wchar_t szDllPath[MAX_PATH] = {};
    if (!GetModuleFileNameW(g_hInst, szDllPath, ARRAYSIZE(szDllPath))) return SELFREG_E_CLASS;
    HRESULT hr = S_OK;
    wchar_t szClsidRoot[128]; swprintf_s(szClsidRoot, L"CLSID\\%s", kClsidStr);
    hr = SetRegSZValue(HKEY_CLASSES_ROOT, szClsidRoot, nullptr, L"XISF Property Handler");
    if (FAILED(hr)) return SELFREG_E_CLASS;
    wchar_t szInProc[256]; swprintf_s(szInProc, L"CLSID\\%s\\InProcServer32", kClsidStr);
    hr = SetRegSZValue(HKEY_CLASSES_ROOT, szInProc, nullptr, szDllPath);
    if (FAILED(hr)) return SELFREG_E_CLASS;
    hr = SetRegSZValue(HKEY_CLASSES_ROOT, szInProc, L"ThreadingModel", L"Apartment");
    if (FAILED(hr)) return SELFREG_E_CLASS;
    hr = SetRegDWORDValue(HKEY_CLASSES_ROOT, szClsidRoot, L"DisableProcessIsolation", 1);
    if (FAILED(hr)) return SELFREG_E_CLASS;
    hr = SetRegSZValue(HKEY_LOCAL_MACHINE, kPropertyHandlersKey, nullptr, kClsidStr);
    if (FAILED(hr)) return SELFREG_E_CLASS;
    hr = SetRegSZValue(HKEY_CLASSES_ROOT, kExtKey, nullptr, kProgID);
    if (FAILED(hr)) return SELFREG_E_CLASS;
    hr = SetRegSZValue(HKEY_CLASSES_ROOT, kExtKey, L"Content Type", L"application/xisf");
    if (FAILED(hr)) return SELFREG_E_CLASS;
    hr = SetRegSZValue(HKEY_CLASSES_ROOT, kExtKey, L"PerceivedType", L"image");
    if (FAILED(hr)) return SELFREG_E_CLASS;
    hr = SetRegSZValue(HKEY_CLASSES_ROOT, kProgID, nullptr, L"XISF Image File");
    if (FAILED(hr)) return SELFREG_E_CLASS;
    hr = SetRegSZValue(HKEY_CLASSES_ROOT, kProgID, L"FullDetails",
        L"prop:System.PropGroup.FileSystem;System.ItemNameDisplay;System.ItemTypeText;"
        L"System.ItemFolderPathDisplay;System.Size;System.DateCreated;System.DateModified;"
        L"System.FileAttributes;"
        L"System.PropGroup.Image;XISF.ExposureTime;XISF.CameraModel;XISF.FocalLength;XISF.FNumber;"
        L"XISF.ObjectName;XISF.FilterName;XISF.ImageType;XISF.Gain;XISF.Offset;"
        L"XISF.SensorTemperature;XISF.Telescope;XISF.Binning;XISF.DateObserved;XISF.Software;"
        L"XISF.RA;XISF.Dec;XISF.RAHour;XISF.DecBand;XISF.Constellation;XISF.MatchedObjects;"
        L"XISF.Airmass;XISF.PierSide;XISF.Rotation;"
        L"XISF.SetTemp;XISF.PixelSize;XISF.ReadoutMode;XISF.BayerPattern;"
        L"XISF.SiteLatitude;XISF.SiteLongitude;XISF.SiteElevation;XISF.Altitude;XISF.Azimuth;"
        L"XISF.FocuserName;XISF.FocuserPosition;XISF.FocuserTemp;"
        L"XISF.RotatorName;XISF.RotatorAngle;XISF.FilterWheel;"
        L"XISF.DewPoint;XISF.Humidity;XISF.AmbientTemp;XISF.DateLocal;"
        L"XISF.StarFWHM;XISF.SkyQuality;XISF.SkyBrightness;XISF.CloudCover;"
        L"XISF.Pressure;XISF.SkyTemp;XISF.WindSpeed;"
        L"XISF.GuideRA;XISF.GuideDec;"
        L"XISF.ObjectRA;XISF.ObjectDec");
    if (FAILED(hr)) return SELFREG_E_CLASS;
    hr = SetRegSZValue(HKEY_CLASSES_ROOT, kProgID, L"PreviewDetails",
        L"prop:XISF.ObjectName;XISF.ExposureTime;XISF.FilterName;XISF.CameraModel;"
        L"XISF.Gain;XISF.SensorTemperature;XISF.Telescope;XISF.FocalLength;XISF.FNumber;"
        L"XISF.Constellation;XISF.MatchedObjects");
    if (FAILED(hr)) return SELFREG_E_CLASS;
    hr = SetRegSZValue(HKEY_CLASSES_ROOT, kProgID, L"InfoTip",
        L"prop:System.ItemTypeText;System.Size;XISF.ObjectName;XISF.ExposureTime;XISF.FilterName;XISF.CameraModel;XISF.Constellation;XISF.MatchedObjects");
    if (FAILED(hr)) return SELFREG_E_CLASS;
    // Register property description schema (propdesc next to DLL)
    {
        wchar_t szPropdesc[MAX_PATH] = {};
        wcscpy_s(szPropdesc, szDllPath);
        PathRemoveFileSpecW(szPropdesc);
        PathAppendW(szPropdesc, L"xisf.propdesc");
        HRESULT hrSchema = PSRegisterPropertySchema(szPropdesc);
        if (FAILED(hrSchema)) return SELFREG_E_CLASS;
    }
    // Register with Windows Search indexer so .xisf is indexed automatically
    SetRegDWORDValue(HKEY_LOCAL_MACHINE, kSearchExtKey, L"Enabled", 1);
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}

STDAPI DllUnregisterServer(void) {
    // Unregister property description schema
    {
        wchar_t szDllPath[MAX_PATH] = {};
        GetModuleFileNameW(g_hInst, szDllPath, ARRAYSIZE(szDllPath));
        wchar_t szPropdesc[MAX_PATH] = {};
        wcscpy_s(szPropdesc, szDllPath);
        PathRemoveFileSpecW(szPropdesc);
        PathAppendW(szPropdesc, L"xisf.propdesc");
        PSUnregisterPropertySchema(szPropdesc);
    }
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, kSearchExtKey);
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, kPropertyHandlersKey);
    wchar_t szClsidRoot[128]; swprintf_s(szClsidRoot, L"CLSID\\%s", kClsidStr);
    RegDeleteTreeW(HKEY_CLASSES_ROOT, szClsidRoot);
    // Only remove our values from .xisf — don't nuke the entire key (PixInsight may own it)
    {
        HKEY hExtKey = nullptr;
        if (RegOpenKeyExW(HKEY_CLASSES_ROOT, kExtKey, 0, KEY_SET_VALUE | KEY_READ, &hExtKey) == ERROR_SUCCESS) {
            // Only delete default value if it points to our ProgID
            wchar_t buf[128] = {};
            DWORD cb = sizeof(buf);
            if (RegGetValueW(hExtKey, nullptr, nullptr, RRF_RT_REG_SZ, nullptr, buf, &cb) == ERROR_SUCCESS
                && wcscmp(buf, kProgID) == 0) {
                RegDeleteValueW(hExtKey, nullptr);
            }
            RegDeleteValueW(hExtKey, L"Content Type");
            RegDeleteValueW(hExtKey, L"PerceivedType");
            RegCloseKey(hExtKey);
        }
    }
    RegDeleteTreeW(HKEY_CLASSES_ROOT, kProgID);
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}
