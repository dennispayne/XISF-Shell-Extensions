// XISFFilter.h — IFilter + IPersistStream + IPersistFile for XISF Windows Search
#pragma once

#include <windows.h>
#include <objidl.h>
#include <filter.h>
#include <filterr.h>
#include <string>
#include <string_view>
#include <vector>

namespace xisf { struct ParseResult; }

// {B4E7F2A1-3D8C-4F5E-9A1B-6C2D8E4F7A3B}
DEFINE_GUID(CLSID_XISFFilter,
    0xB4E7F2A1, 0x3D8C, 0x4F5E, 0x9A, 0x1B, 0x6C, 0x2D, 0x8E, 0x4F, 0x7A, 0x3B);

class CXISFFilter : public IFilter, public IPersistStream, public IPersistFile
{
public:
    CXISFFilter();

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    IFACEMETHODIMP_(ULONG) AddRef() override;
    IFACEMETHODIMP_(ULONG) Release() override;

    IFACEMETHODIMP GetClassID(CLSID* pClassID) override;

    IFACEMETHODIMP IsDirty() override;
    IFACEMETHODIMP Load(IStream* pStm) override;
    IFACEMETHODIMP Save(IStream* pStm, BOOL fClearDirty) override;
    IFACEMETHODIMP GetSizeMax(ULARGE_INTEGER* pcbSize) override;

    IFACEMETHODIMP Load(LPCOLESTR pszFileName, DWORD dwMode) override;
    IFACEMETHODIMP Save(LPCOLESTR pszFileName, BOOL fRemember) override;
    IFACEMETHODIMP SaveCompleted(LPCOLESTR pszFileName) override;
    IFACEMETHODIMP GetCurFile(LPOLESTR* ppszFileName) override;

    IFACEMETHODIMP Init(ULONG grfFlags, ULONG cAttributes,
                        const FULLPROPSPEC* aAttributes, ULONG* pFlags) override;
    IFACEMETHODIMP GetChunk(STAT_CHUNK* pStat) override;
    IFACEMETHODIMP GetText(ULONG* pcwcBuffer, WCHAR* awcBuffer) override;
    IFACEMETHODIMP GetValue(PROPVARIANT** ppPropValue) override;
    IFACEMETHODIMP BindRegion(FILTERREGION origPos, REFIID riid, void** ppunk) override;

private:
    void ResetState();
    HRESULT ParseFromStream(IStream* pStm);
    void PopulateDerivedValues(const xisf::ParseResult& result);
    static bool DetermineIsLinearFromMetadata(std::string_view sampleFormat,
                                              std::string_view colorSpace);

    long m_cRef;
    std::vector<std::wstring> m_textChunks;
    ULONG m_currentChunk;    // Index into m_textChunks for GetChunk
    ULONG m_currentOffset;   // Character offset within current chunk for GetText
    bool  m_initialized;     // True after Init() succeeds
    bool  m_pendingDataStateValue;
    bool  m_hasDataStateValue;
    std::wstring m_dataStateValue;
};
