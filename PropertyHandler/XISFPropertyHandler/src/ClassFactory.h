// ClassFactory.h — IClassFactory for CXISFPropertyHandler (Property Handler)
#pragma once
#include <windows.h>
#include <unknwn.h>

// Forward declaration of the handler created by this factory
class CXISFPropertyHandler;

class CClassFactory : public IClassFactory
{
public:
    CClassFactory();

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    IFACEMETHODIMP_(ULONG) AddRef() override;
    IFACEMETHODIMP_(ULONG) Release() override;

    // IClassFactory
    IFACEMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv) override;
    IFACEMETHODIMP LockServer(BOOL fLock) override;

private:
    long m_cRef;
};
