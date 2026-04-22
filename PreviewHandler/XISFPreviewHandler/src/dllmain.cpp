// dllmain.cpp — COM DLL entry points for the XISF Preview & Thumbnail Handler (Preview Handler)
//
// Registers two COM objects:
//   CThumbnailProvider  CLSID {9C76E8AD-4E85-5F30-B00D-3C7D1AB5F6E0}
//   CPreviewHandler     CLSID {AD87F6CE-5B03-6E41-C11E-4DB2AC06F5F1}
//
#include <windows.h>
#include <shlwapi.h>
#include <olectl.h>
#include <initguid.h>
#include <evntprov.h>
#include <evntrace.h>
#include <new>

#include "ThumbnailProvider.h"
#include "PreviewHandler.h"
#include "PreviewHandlerTelemetry.h"
#include "HandlerSettings.h"

#pragma comment(lib, "advapi32.lib")

// {9C76E8AD-4E85-5F30-B00D-3C7D1AB5F6E0}
DEFINE_GUID(CLSID_XISFThumbnailProvider,
    0x9C76E8AD, 0x4E85, 0x5F30, 0xB0, 0x0D, 0x3C, 0x7D, 0x1A, 0xB5, 0xF6, 0xE0);

// {AD87F6CE-5B03-6E41-C11E-4DB2AC06F5F1}
DEFINE_GUID(CLSID_XISFPreviewHandler,
    0xAD87F6CE, 0x5B03, 0x6E41, 0xC1, 0x1E, 0x4D, 0xB2, 0xAC, 0x06, 0xF5, 0xF1);

// {4fd34fd0-08b3-5d9a-8d77-b9d6705d6b75}  Provider: "XISF-PreviewHandler"
extern "C" const GUID kPreviewHandlerTelemetryProvider =
    {0x4fd34fd0, 0x08b3, 0x5d9a, {0x8d, 0x77, 0xb9, 0xd6, 0x70, 0x5d, 0x6b, 0x75}};

HINSTANCE g_hInst   = nullptr;
long      g_cDllRef = 0;
extern "C" REGHANDLE g_hPreviewHandlerTelemetryHandle;

static const wchar_t kThumbClsidStr[]   = L"{9C76E8AD-4E85-5F30-B00D-3C7D1AB5F6E0}";
static const wchar_t kPreviewClsidStr[] = L"{AD87F6CE-5B03-6E41-C11E-4DB2AC06F5F1}";

// ---------------------------------------------------------------------------
// Minimal IClassFactory wrapper — templated to avoid duplication
// ---------------------------------------------------------------------------

template <typename T>
class CSimpleFactory : public IClassFactory
{
public:
    CSimpleFactory() : m_cRef(1) { InterlockedIncrement(&g_cDllRef); }

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv)
    {
        if (!ppv) return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) ||
            IsEqualIID(riid, IID_IClassFactory))
        {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    IFACEMETHODIMP_(ULONG) AddRef()  { return InterlockedIncrement(&m_cRef); }
    IFACEMETHODIMP_(ULONG) Release()
    {
        ULONG r = InterlockedDecrement(&m_cRef);
        if (r == 0) { InterlockedDecrement(&g_cDllRef); delete this; }
        return r;
    }

    IFACEMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv)
    {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (pUnkOuter) return CLASS_E_NOAGGREGATION;

        // Runtime toggle: when disabled via HKCU, Explorer falls back to default behavior.
        if (!xisf::IsPreviewHandlerEnabled())
            return CLASS_E_CLASSNOTAVAILABLE;

        T* p = new (std::nothrow) T();
        if (!p) return E_OUTOFMEMORY;

        HRESULT hr = p->QueryInterface(riid, ppv);
        p->Release();
        return hr;
    }

    IFACEMETHODIMP LockServer(BOOL fLock)
    {
        if (fLock) InterlockedIncrement(&g_cDllRef);
        else       InterlockedDecrement(&g_cDllRef);
        return S_OK;
    }

private:
    long m_cRef;
};

// ---------------------------------------------------------------------------
// DllMain
// ---------------------------------------------------------------------------

BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, LPVOID /*lpReserved*/)
{
    if (dwReason == DLL_PROCESS_ATTACH)
    {
        g_hInst = hModule;
        DisableThreadLibraryCalls(hModule);
        EventRegister(&kPreviewHandlerTelemetryProvider, nullptr, nullptr, &g_hPreviewHandlerTelemetryHandle);
        WritePreviewHandlerTelemetry(TRACE_LEVEL_INFORMATION, XISF_PREVIEW_KEYWORD_LIFECYCLE,
            L"PreviewHandlerDllAttach");
    }
    else if (dwReason == DLL_PROCESS_DETACH)
    {
        if (g_hPreviewHandlerTelemetryHandle != 0)
        {
            WritePreviewHandlerTelemetry(TRACE_LEVEL_INFORMATION, XISF_PREVIEW_KEYWORD_LIFECYCLE,
                L"PreviewHandlerDllDetach");
            EventUnregister(g_hPreviewHandlerTelemetryHandle);
            g_hPreviewHandlerTelemetryHandle = 0;
        }
    }
    return TRUE;
}

// ---------------------------------------------------------------------------
// DllCanUnloadNow
// ---------------------------------------------------------------------------

STDAPI DllCanUnloadNow()
{
    return (g_cDllRef == 0) ? S_OK : S_FALSE;
}

// ---------------------------------------------------------------------------
// DllGetClassObject
// ---------------------------------------------------------------------------

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv)
{
    if (!ppv) return E_POINTER;
    *ppv = nullptr;

    if (IsEqualCLSID(rclsid, CLSID_XISFThumbnailProvider))
    {
        auto* pf = new (std::nothrow) CSimpleFactory<CThumbnailProvider>();
        if (!pf) return E_OUTOFMEMORY;
        HRESULT hr = pf->QueryInterface(riid, ppv);
        pf->Release();
        return hr;
    }

    if (IsEqualCLSID(rclsid, CLSID_XISFPreviewHandler))
    {
        auto* pf = new (std::nothrow) CSimpleFactory<CPreviewHandler>();
        if (!pf) return E_OUTOFMEMORY;
        HRESULT hr = pf->QueryInterface(riid, ppv);
        pf->Release();
        return hr;
    }

    return CLASS_E_CLASSNOTAVAILABLE;
}

// ---------------------------------------------------------------------------
// Registry helpers
// ---------------------------------------------------------------------------

static HRESULT SetRegSZValue(HKEY hRoot, const wchar_t* subKey,
                              const wchar_t* valueName, const wchar_t* data)
{
    HKEY  hKey   = nullptr;
    DWORD dwDisp = 0;
    LONG  lr     = RegCreateKeyExW(hRoot, subKey, 0, nullptr,
                                    REG_OPTION_NON_VOLATILE, KEY_WRITE,
                                    nullptr, &hKey, &dwDisp);
    if (lr != ERROR_SUCCESS) return HRESULT_FROM_WIN32(lr);

    DWORD cbData = static_cast<DWORD>((wcslen(data) + 1) * sizeof(wchar_t));
    lr = RegSetValueExW(hKey, valueName, 0, REG_SZ,
                        reinterpret_cast<const BYTE*>(data), cbData);
    RegCloseKey(hKey);
    return HRESULT_FROM_WIN32(lr);
}

static HRESULT RegisterClsid(const wchar_t* clsidStr, const wchar_t* friendlyName,
                              const wchar_t* dllPath)
{
    wchar_t szClsidRoot[128], szInProc[256];
    swprintf_s(szClsidRoot, L"CLSID\\%s",              clsidStr);
    swprintf_s(szInProc,    L"CLSID\\%s\\InProcServer32", clsidStr);

    HRESULT hr = SetRegSZValue(HKEY_CLASSES_ROOT, szClsidRoot,
                               nullptr, friendlyName);
    if (FAILED(hr)) return hr;

    hr = SetRegSZValue(HKEY_CLASSES_ROOT, szInProc, nullptr, dllPath);
    if (FAILED(hr)) return hr;

    return SetRegSZValue(HKEY_CLASSES_ROOT, szInProc, L"ThreadingModel", L"Apartment");
}

// ---------------------------------------------------------------------------
// DllRegisterServer
// ---------------------------------------------------------------------------

STDAPI DllRegisterServer()
{
    wchar_t szDllPath[MAX_PATH] = {};
    if (!GetModuleFileNameW(g_hInst, szDllPath, ARRAYSIZE(szDllPath)))
        return SELFREG_E_CLASS;

    HRESULT hr = RegisterClsid(kThumbClsidStr,
                                L"XISF Thumbnail Provider", szDllPath);
    if (FAILED(hr)) return SELFREG_E_CLASS;

    hr = RegisterClsid(kPreviewClsidStr,
                       L"XISF Preview Handler", szDllPath);
    if (FAILED(hr)) return SELFREG_E_CLASS;

    // HKCR\.xisf\shellex\{E357FCCD-A995-4576-B01F-234630154E96} — thumbnail
    hr = SetRegSZValue(HKEY_CLASSES_ROOT,
                       L".xisf\\shellex\\{E357FCCD-A995-4576-B01F-234630154E96}",
                       nullptr, kThumbClsidStr);
    if (FAILED(hr)) return SELFREG_E_CLASS;

    // HKCR\.xisf\shellex\{8895b1c6-b41f-4c1c-a562-0d564250836f} — preview
    hr = SetRegSZValue(HKEY_CLASSES_ROOT,
                       L".xisf\\shellex\\{8895b1c6-b41f-4c1c-a562-0d564250836f}",
                       nullptr, kPreviewClsidStr);
    if (FAILED(hr)) return SELFREG_E_CLASS;

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}

// ---------------------------------------------------------------------------
// DllUnregisterServer
// ---------------------------------------------------------------------------

STDAPI DllUnregisterServer()
{
    RegDeleteTreeW(HKEY_CLASSES_ROOT,
                   L".xisf\\shellex\\{E357FCCD-A995-4576-B01F-234630154E96}");
    RegDeleteTreeW(HKEY_CLASSES_ROOT,
                   L".xisf\\shellex\\{8895b1c6-b41f-4c1c-a562-0d564250836f}");

    wchar_t szClsidRoot[128];

    swprintf_s(szClsidRoot, L"CLSID\\%s", kThumbClsidStr);
    RegDeleteTreeW(HKEY_CLASSES_ROOT, szClsidRoot);

    swprintf_s(szClsidRoot, L"CLSID\\%s", kPreviewClsidStr);
    RegDeleteTreeW(HKEY_CLASSES_ROOT, szClsidRoot);

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}
