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

// {A3B7C8D9-E1F2-4A5B-8C6D-7E8F9A0B1C2D}
DEFINE_GUID(CLSID_XISFPropertySheet,
    0xA3B7C8D9, 0xE1F2, 0x4A5B, 0x8C, 0x6D, 0x7E, 0x8F, 0x9A, 0x0B, 0x1C, 0x2D);

TRACELOGGING_DEFINE_PROVIDER(g_hPropertyProvider, "XISF-PropertyHandler",
    (0x6f6b0c9d, 0x6b76, 0x5a24, 0xbc, 0x3d, 0x70, 0x83, 0x14, 0xe9, 0x6f, 0x2b));

HINSTANCE g_hInst = nullptr;
long g_cDllRef = 0;

static const wchar_t kClsidStr[] = L"{7C54FA8B-9D63-4C10-8FBE-1A5A0F9A3B2E}";
static const wchar_t kPropSheetClsidStr[] = L"{A3B7C8D9-E1F2-4A5B-8C6D-7E8F9A0B1C2D}";
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
        TraceLoggingWrite(g_hPropertyProvider, "PropertyHandlerDllAttach",
            TraceLoggingLevel(TRACE_LEVEL_INFORMATION),
            TraceLoggingKeyword(XISF_ETW_KEYWORD_LIFECYCLE));
        if (g_xisfPropertyHandlerTelemetryHook) {
            g_xisfPropertyHandlerTelemetryHook(TRACE_LEVEL_INFORMATION,
                XISF_ETW_KEYWORD_LIFECYCLE, L"PropertyHandlerDllAttach");
        }
    }
    else if (dwReason == DLL_PROCESS_DETACH) {
        TraceLoggingWrite(g_hPropertyProvider, "PropertyHandlerDllDetach",
            TraceLoggingLevel(TRACE_LEVEL_INFORMATION),
            TraceLoggingKeyword(XISF_ETW_KEYWORD_LIFECYCLE));
        if (g_xisfPropertyHandlerTelemetryHook) {
            g_xisfPropertyHandlerTelemetryHook(TRACE_LEVEL_INFORMATION,
                XISF_ETW_KEYWORD_LIFECYCLE, L"PropertyHandlerDllDetach");
        }
        TraceLoggingUnregister(g_hPropertyProvider);
    }
    return TRUE;
}

STDAPI DllCanUnloadNow(void) { return (g_cDllRef == 0) ? S_OK : S_FALSE; }

STDAPI DllGetClassObject(_In_ REFCLSID rclsid, _In_ REFIID riid, _Outptr_ void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;

    TraceLoggingWrite(g_hPropertyProvider, "PropertyHandlerDllGetClassObject",
        TraceLoggingLevel(TRACE_LEVEL_INFORMATION),
        TraceLoggingKeyword(XISF_ETW_KEYWORD_LIFECYCLE),
        TraceLoggingGuid(rclsid, "CLSID"),
        TraceLoggingGuid(riid, "IID"));
    if (g_xisfPropertyHandlerTelemetryHook) {
        g_xisfPropertyHandlerTelemetryHook(TRACE_LEVEL_INFORMATION,
            XISF_ETW_KEYWORD_LIFECYCLE, L"PropertyHandlerDllGetClassObject");
    }

    CClassFactory* pf = nullptr;
    if (IsEqualCLSID(rclsid, CLSID_XISFPropertyHandler)) {
        pf = new (std::nothrow) CClassFactory(CClassFactory::HandlerType::PropertyStore);
    } else if (IsEqualCLSID(rclsid, CLSID_XISFPropertySheet)) {
        pf = new (std::nothrow) CClassFactory(CClassFactory::HandlerType::PropertySheet);
    } else {
        return CLASS_E_CLASSNOTAVAILABLE;
    }

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
    // Approved Shell Extensions list — required when EnforceShellExtensionSecurity GPO is set.
    // Cheap insurance for managed/enterprise machines; harmless on default Windows.
    hr = SetRegSZValue(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved",
        kClsidStr, L"XISF Property Handler");
    if (FAILED(hr)) return SELFREG_E_CLASS;
    hr = SetRegSZValue(HKEY_CLASSES_ROOT, kExtKey, nullptr, kProgID);
    if (FAILED(hr)) return SELFREG_E_CLASS;
    hr = SetRegSZValue(HKEY_CLASSES_ROOT, kExtKey, L"Content Type", L"application/xisf");
    if (FAILED(hr)) return SELFREG_E_CLASS;
    hr = SetRegSZValue(HKEY_CLASSES_ROOT, kExtKey, L"PerceivedType", L"image");
    if (FAILED(hr)) return SELFREG_E_CLASS;
    hr = SetRegSZValue(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\KindMap",
        L".xisf", L"picture");
    if (FAILED(hr)) return SELFREG_E_CLASS;
    hr = SetRegSZValue(HKEY_CLASSES_ROOT, kProgID, nullptr, L"XISF Image File");
    if (FAILED(hr)) return SELFREG_E_CLASS;

    // FullDetails / PreviewDetails / InfoTip strings — shared across all ProgIDs
    // and SystemFileAssociations to ensure consistency regardless of resolution.
    // Section order matches standard Windows image formats (JPEG/TIFF/PNG):
    //   Description → Origin → Image → Camera → PhotoAdvanced → GPS → FileSystem
    // Properties alphabetized within each section.
    static const wchar_t kFullDetails[] =
        // — Description (target & celestial context) —
        L"prop:System.PropGroup.Description;"
        L"XISF.Constellation;XISF.Dec;XISF.DecBand;"
        L"XISF.MatchedObjects;XISF.ObjectDec;XISF.ObjectName;XISF.ObjectRA;"
        L"XISF.RA;XISF.RAHour;"
        // — Origin (dates, software) —
        L"System.PropGroup.Origin;"
        L"XISF.DateLocal;XISF.DateObserved;XISF.Software;"
        // — Image (image data & observation) —
        L"System.PropGroup.Image;"
        L"XISF.Airmass;XISF.Altitude;XISF.Azimuth;"
        L"XISF.ChannelCount;XISF.ColorSpace;XISF.DataState;"
        L"XISF.ExposureTime;XISF.FilterName;"
        L"XISF.ImageCount;XISF.ImageHeight;XISF.ImageType;XISF.ImageWidth;"
        L"XISF.PierSide;XISF.Rotation;XISF.SampleFormat;"
        // — Camera (equipment & sensor) —
        L"System.PropGroup.Camera;"
        L"XISF.BayerPattern;XISF.Binning;XISF.CameraModel;"
        L"XISF.FilterWheel;XISF.FNumber;XISF.FocalLength;"
        L"XISF.FocuserName;XISF.FocuserPosition;XISF.FocuserTemp;"
        L"XISF.Gain;XISF.Offset;XISF.PixelSize;XISF.ReadoutMode;"
        L"XISF.RotatorAngle;XISF.RotatorName;"
        L"XISF.SensorTemperature;XISF.SetTemp;XISF.Telescope;"
        // — PhotoAdvanced (guiding & quality) —
        L"System.PropGroup.PhotoAdvanced;"
        L"XISF.GuideDec;XISF.GuideRA;"
        L"XISF.Median;XISF.Mean;XISF.ClippingLow;XISF.ClippingHigh;"
        L"XISF.StarFWHM;XISF.SkyBrightness;XISF.SkyQuality;"
        // — GPS (site location & weather) —
        L"System.PropGroup.GPS;"
        L"XISF.SiteElevation;XISF.SiteLatitude;XISF.SiteLongitude;"
        L"XISF.AmbientTemp;XISF.CloudCover;XISF.DewPoint;XISF.Humidity;"
        L"XISF.Pressure;XISF.SkyTemp;XISF.WindSpeed;"
        // — File System —
        L"System.PropGroup.FileSystem;"
        L"System.ItemNameDisplay;System.ItemType;System.ItemFolderPathDisplay;"
        L"System.DateCreated;System.DateModified;System.Size;System.FileAttributes";
    static const wchar_t kPreviewDetails[] =
        L"prop:XISF.ObjectName;XISF.ExposureTime;XISF.FilterName;XISF.CameraModel;"
        L"XISF.Gain;XISF.SensorTemperature;XISF.Telescope;XISF.FocalLength;XISF.FNumber;"
        L"XISF.Constellation;XISF.MatchedObjects;XISF.DataState;XISF.ColorSpace;XISF.SampleFormat";
    static const wchar_t kInfoTipStr[] =
        L"prop:System.ItemTypeText;System.Size;XISF.ObjectName;XISF.ExposureTime;XISF.FilterName;"
        L"XISF.CameraModel;XISF.Constellation;XISF.MatchedObjects";

    hr = SetRegSZValue(HKEY_CLASSES_ROOT, kProgID, L"FullDetails", kFullDetails);
    if (FAILED(hr)) return SELFREG_E_CLASS;
    hr = SetRegSZValue(HKEY_CLASSES_ROOT, kProgID, L"PreviewDetails", kPreviewDetails);
    if (FAILED(hr)) return SELFREG_E_CLASS;
    hr = SetRegSZValue(HKEY_CLASSES_ROOT, kProgID, L"InfoTip", kInfoTipStr);
    if (FAILED(hr)) return SELFREG_E_CLASS;

    // SystemFileAssociations — reliable path that works regardless of which ProgID
    // Explorer resolves (e.g. XISFFile vs Pleiades Astrophoto.PixInsight.xisf)
    static const wchar_t kSFA[] = L"SystemFileAssociations\\.xisf";
    hr = SetRegSZValue(HKEY_CLASSES_ROOT, kSFA, L"FullDetails", kFullDetails);
    if (FAILED(hr)) return SELFREG_E_CLASS;
    hr = SetRegSZValue(HKEY_CLASSES_ROOT, kSFA, L"PreviewDetails", kPreviewDetails);
    if (FAILED(hr)) return SELFREG_E_CLASS;
    hr = SetRegSZValue(HKEY_CLASSES_ROOT, kSFA, L"InfoTip", kInfoTipStr);
    if (FAILED(hr)) return SELFREG_E_CLASS;

    // Also update PixInsight's ProgID if it exists
    {
        HKEY hTest = nullptr;
        if (RegOpenKeyExW(HKEY_CLASSES_ROOT, L"Pleiades Astrophoto.PixInsight.xisf",
                          0, KEY_READ, &hTest) == ERROR_SUCCESS) {
            RegCloseKey(hTest);
            SetRegSZValue(HKEY_CLASSES_ROOT, L"Pleiades Astrophoto.PixInsight.xisf",
                          L"FullDetails", kFullDetails);
            SetRegSZValue(HKEY_CLASSES_ROOT, L"Pleiades Astrophoto.PixInsight.xisf",
                          L"PreviewDetails", kPreviewDetails);
            SetRegSZValue(HKEY_CLASSES_ROOT, L"Pleiades Astrophoto.PixInsight.xisf",
                          L"InfoTip", kInfoTipStr);
        }
    }

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

    // ---- Property Sheet handler (Histogram tab) ----
    wchar_t szPSClsid[128]; swprintf_s(szPSClsid, L"CLSID\\%s", kPropSheetClsidStr);
    hr = SetRegSZValue(HKEY_CLASSES_ROOT, szPSClsid, nullptr, L"XISF Histogram Property Sheet");
    if (FAILED(hr)) return SELFREG_E_CLASS;
    wchar_t szPSInProc[256]; swprintf_s(szPSInProc, L"CLSID\\%s\\InProcServer32", kPropSheetClsidStr);
    hr = SetRegSZValue(HKEY_CLASSES_ROOT, szPSInProc, nullptr, szDllPath);
    if (FAILED(hr)) return SELFREG_E_CLASS;
    hr = SetRegSZValue(HKEY_CLASSES_ROOT, szPSInProc, L"ThreadingModel", L"Apartment");
    if (FAILED(hr)) return SELFREG_E_CLASS;
    // SystemFileAssociations is the reliable path — works regardless of which ProgID
    // Explorer resolves (e.g. XISFFile vs Pleiades Astrophoto.PixInsight.xisf)
    hr = SetRegSZValue(HKEY_CLASSES_ROOT,
        L"SystemFileAssociations\\.xisf\\shellex\\PropertySheetHandlers\\XISFHistogram",
        nullptr, kPropSheetClsidStr);
    if (FAILED(hr)) return SELFREG_E_CLASS;
    // Add to Approved shell extensions list
    hr = SetRegSZValue(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved",
        kPropSheetClsidStr, L"XISF Histogram Property Sheet");
    if (FAILED(hr)) return SELFREG_E_CLASS;

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
    // Remove KindMap entry
    {
        HKEY hKindMap = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\KindMap",
            0, KEY_SET_VALUE, &hKindMap) == ERROR_SUCCESS) {
            RegDeleteValueW(hKindMap, L".xisf");
            RegCloseKey(hKindMap);
        }
    }
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, kSearchExtKey);
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, kPropertyHandlersKey);
    // Remove from Approved Shell Extensions list (property handler CLSID).
    {
        HKEY hApproved = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved",
            0, KEY_SET_VALUE, &hApproved) == ERROR_SUCCESS) {
            RegDeleteValueW(hApproved, kClsidStr);
            RegCloseKey(hApproved);
        }
    }
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
    // ---- Property Sheet handler cleanup ----
    wchar_t szPSClsid[128]; swprintf_s(szPSClsid, L"CLSID\\%s", kPropSheetClsidStr);
    RegDeleteTreeW(HKEY_CLASSES_ROOT, szPSClsid);
    RegDeleteTreeW(HKEY_CLASSES_ROOT,
        L"SystemFileAssociations\\.xisf\\shellex\\PropertySheetHandlers\\XISFHistogram");
    // Clean up stale registrations from earlier builds
    RegDeleteTreeW(HKEY_CLASSES_ROOT, L".xisf\\shellex\\PropertySheetHandlers\\XISFHistogram");
    {
        wchar_t szPropSheetKey[256];
        swprintf_s(szPropSheetKey, L"%s\\shellex\\PropertySheetHandlers\\XISFHistogram", kProgID);
        RegDeleteTreeW(HKEY_CLASSES_ROOT, szPropSheetKey);
    }
    // Remove from Approved list
    {
        HKEY hApproved = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved",
            0, KEY_SET_VALUE, &hApproved) == ERROR_SUCCESS) {
            RegDeleteValueW(hApproved, kPropSheetClsidStr);
            RegCloseKey(hApproved);
        }
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}
