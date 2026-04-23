// ClassFactory.cpp — IClassFactory implementation for CXISFPropertyHandler (Property Handler)
#include "ClassFactory.h"
#include "PropertyStore.h"
#include "HandlerSettings.h"
#include <new>

extern long g_cDllRef;

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

CClassFactory::CClassFactory() : m_cRef(1)
{
    InterlockedIncrement(&g_cDllRef);
}

// ---------------------------------------------------------------------------
// IUnknown
// ---------------------------------------------------------------------------

IFACEMETHODIMP CClassFactory::QueryInterface(REFIID riid, void** ppv)
{
    if (ppv == nullptr)
        return E_POINTER;

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

IFACEMETHODIMP_(ULONG) CClassFactory::AddRef()
{
    return InterlockedIncrement(&m_cRef);
}

IFACEMETHODIMP_(ULONG) CClassFactory::Release()
{
    ULONG cRef = InterlockedDecrement(&m_cRef);
    if (cRef == 0)
    {
        InterlockedDecrement(&g_cDllRef);
        delete this;
    }
    return cRef;
}

// ---------------------------------------------------------------------------
// IClassFactory
// ---------------------------------------------------------------------------

IFACEMETHODIMP CClassFactory::CreateInstance(IUnknown* pUnkOuter,
                                              REFIID riid, void** ppv)
{
    if (ppv == nullptr)
        return E_POINTER;

    *ppv = nullptr;

    if (pUnkOuter != nullptr)
        return CLASS_E_NOAGGREGATION;

    // Runtime toggle: when disabled via HKCU, Explorer falls back to default behavior.
    if (!xisf::IsPropertyHandlerEnabled())
        return CLASS_E_CLASSNOTAVAILABLE;

    CXISFPropertyHandler* pHandler = new (std::nothrow) CXISFPropertyHandler();
    if (pHandler == nullptr)
        return E_OUTOFMEMORY;

    HRESULT hr = pHandler->QueryInterface(riid, ppv);
    pHandler->Release();
    return hr;
}

IFACEMETHODIMP CClassFactory::LockServer(BOOL fLock)
{
    if (fLock)
        InterlockedIncrement(&g_cDllRef);
    else
        InterlockedDecrement(&g_cDllRef);

    return S_OK;
}
