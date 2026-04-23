// ClassFactory.cpp — IClassFactory implementation for CXISFFilter (Search Filter)
#include "ClassFactory.h"
#include "XISFFilter.h"
#include "HandlerSettings.h"
#include <new>

extern long g_cDllRef;

CClassFactory::CClassFactory() : m_cRef(1)
{
    InterlockedIncrement(&g_cDllRef);
}

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

IFACEMETHODIMP CClassFactory::CreateInstance(IUnknown* pUnkOuter,
                                              REFIID riid, void** ppv)
{
    if (ppv == nullptr)
        return E_POINTER;

    *ppv = nullptr;

    if (pUnkOuter != nullptr)
        return CLASS_E_NOAGGREGATION;

    if (!xisf::IsFilterEnabled())
        return CLASS_E_CLASSNOTAVAILABLE;

    CXISFFilter* pFilter = new (std::nothrow) CXISFFilter();
    if (pFilter == nullptr)
        return E_OUTOFMEMORY;

    HRESULT hr = pFilter->QueryInterface(riid, ppv);
    pFilter->Release();
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
