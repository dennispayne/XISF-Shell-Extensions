// ClassFactory.h — IClassFactory for CXISFFilter (Search Filter)
#pragma once
#include <windows.h>
#include <unknwn.h>

class CXISFFilter;

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
