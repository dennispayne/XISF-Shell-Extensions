// ClassFactory.h — IClassFactory for CXISFPropertyHandler and CXISFPropertySheetHandler
#pragma once
#include <windows.h>
#include <unknwn.h>

class CClassFactory : public IClassFactory
{
public:
    enum class HandlerType { PropertyStore, PropertySheet };

    explicit CClassFactory(HandlerType type = HandlerType::PropertyStore);

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    IFACEMETHODIMP_(ULONG) AddRef() override;
    IFACEMETHODIMP_(ULONG) Release() override;

    // IClassFactory
    IFACEMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv) override;
    IFACEMETHODIMP LockServer(BOOL fLock) override;

private:
    long m_cRef;
    HandlerType m_type;
};
