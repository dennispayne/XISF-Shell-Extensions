// XISFFilter.cpp — IFilter + IPersistStream + IPersistFile implementation
#include "XISFFilter.h"
#include "XISFParser.h"
#include "FilterTelemetry.h"
#include <shlwapi.h>
#include <propvarutil.h>
#include <new>
#include <cstring>
#include <fstream>
#include <sstream>

extern long g_cDllRef;

// PSGUID_STORAGE {B725F130-47EF-101A-A5F1-02608C9EEBAC}
static const GUID s_PSGUID_STORAGE =
    { 0xB725F130, 0x47EF, 0x101A, { 0xA5, 0xF1, 0x02, 0x60, 0x8C, 0x9E, 0xEB, 0xAC } };
static const ULONG PID_STG_CONTENTS = 19;
static const GUID s_PSGUID_XISF =
    { 0x7C54FA8B, 0x9D63, 0x4C10, { 0x8F, 0xBE, 0x1A, 0x5A, 0x0F, 0x9A, 0x3B, 0x2E } };
static const ULONG PID_XISF_DATASTATE = 55;

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

CXISFFilter::CXISFFilter()
    : m_cRef(1)
    , m_currentChunk(0)
    , m_currentOffset(0)
    , m_initialized(false)
    , m_pendingDataStateValue(false)
    , m_hasDataStateValue(false)
{
    InterlockedIncrement(&g_cDllRef);
}

// ---------------------------------------------------------------------------
// IUnknown
// ---------------------------------------------------------------------------

IFACEMETHODIMP CXISFFilter::QueryInterface(REFIID riid, void** ppv)
{
    if (!ppv) return E_POINTER;

    if (IsEqualIID(riid, IID_IUnknown))
    {
        *ppv = static_cast<IFilter*>(this);
    }
    else if (IsEqualIID(riid, IID_IFilter))
    {
        *ppv = static_cast<IFilter*>(this);
    }
    else if (IsEqualIID(riid, IID_IPersistStream))
    {
        *ppv = static_cast<IPersistStream*>(this);
    }
    else if (IsEqualIID(riid, IID_IPersistFile))
    {
        *ppv = static_cast<IPersistFile*>(this);
    }
    else if (IsEqualIID(riid, IID_IPersist))
    {
        *ppv = static_cast<IPersistStream*>(this);
    }
    else
    {
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    AddRef();
    return S_OK;
}

IFACEMETHODIMP_(ULONG) CXISFFilter::AddRef()
{
    return InterlockedIncrement(&m_cRef);
}

IFACEMETHODIMP_(ULONG) CXISFFilter::Release()
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
// IPersist
// ---------------------------------------------------------------------------

IFACEMETHODIMP CXISFFilter::GetClassID(CLSID* pClassID)
{
    if (!pClassID) return E_POINTER;
    *pClassID = CLSID_XISFFilter;
    return S_OK;
}

// ---------------------------------------------------------------------------
// IPersistStream
// ---------------------------------------------------------------------------

IFACEMETHODIMP CXISFFilter::IsDirty()
{
    return S_FALSE;
}

IFACEMETHODIMP CXISFFilter::Load(IStream* pStm)
{
    return ParseFromStream(pStm);
}

IFACEMETHODIMP CXISFFilter::Save(IStream*, BOOL)
{
    return E_NOTIMPL;
}

IFACEMETHODIMP CXISFFilter::GetSizeMax(ULARGE_INTEGER*)
{
    return E_NOTIMPL;
}

// ---------------------------------------------------------------------------
// IPersistFile
// ---------------------------------------------------------------------------

IFACEMETHODIMP CXISFFilter::Load(LPCOLESTR pszFileName, DWORD /*dwMode*/)
{
    if (!pszFileName) return E_INVALIDARG;

    // Convert wide path to narrow for XISFParser
    int needed = WideCharToMultiByte(CP_UTF8, 0, pszFileName, -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return E_FAIL;
    std::string path(static_cast<size_t>(needed - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, pszFileName, -1, path.data(), needed, nullptr, nullptr);

    xisf::ParseResult result = xisf::XISFParser::ParseFile(path);
    if (!result.ok()) {
        WriteFilterTelemetry(TRACE_LEVEL_WARNING, XISF_FILTER_KEYWORD_PARSE,
                              L"FilterError IPersistFile::Load parse failed");
        return E_FAIL;
    }

    m_textChunks = result.metadata.GetSearchableTextChunks();
    PopulateDerivedValues(result);
    m_currentChunk = 0;
    m_currentOffset = 0;
    m_pendingDataStateValue = false;
    m_initialized = false;
    return S_OK;
}

IFACEMETHODIMP CXISFFilter::Save(LPCOLESTR, BOOL)
{
    return E_NOTIMPL;
}

IFACEMETHODIMP CXISFFilter::SaveCompleted(LPCOLESTR)
{
    return E_NOTIMPL;
}

IFACEMETHODIMP CXISFFilter::GetCurFile(LPOLESTR*)
{
    return E_NOTIMPL;
}

// ---------------------------------------------------------------------------
// IFilter
// ---------------------------------------------------------------------------

IFACEMETHODIMP CXISFFilter::Init(ULONG /*grfFlags*/, ULONG cAttributes,
                                  const FULLPROPSPEC* /*aAttributes*/,
                                  ULONG* pFlags)
{
    // When cAttributes is 0, the caller wants all available text
    if (cAttributes > 0)
    {
        // We only support CONTENTS — if none of the requested attributes
        // match, we still return everything (simplest valid behavior).
    }

    if (pFlags)
        *pFlags = 0;

    m_currentChunk = 0;
    m_currentOffset = 0;
    m_pendingDataStateValue = false;
    m_initialized = true;

    WriteFilterTelemetry(TRACE_LEVEL_INFORMATION, XISF_FILTER_KEYWORD_FILTER,
                          L"FilterInitialized chunkCount=%u",
                          static_cast<unsigned>(m_textChunks.size()));

    return S_OK;
}

IFACEMETHODIMP CXISFFilter::GetChunk(STAT_CHUNK* pStat)
{
    if (!pStat) return E_POINTER;
    if (!m_initialized) return FILTER_E_END_OF_CHUNKS;

    if (m_currentChunk >= static_cast<ULONG>(m_textChunks.size()))
    {
        if (m_hasDataStateValue && m_currentChunk == static_cast<ULONG>(m_textChunks.size()))
        {
            memset(pStat, 0, sizeof(*pStat));
            pStat->idChunk = m_currentChunk + 1;
            pStat->breakType = CHUNK_EOS;
            pStat->flags = CHUNK_VALUE;
            pStat->locale = GetSystemDefaultLCID();
            pStat->attribute.guidPropSet = s_PSGUID_XISF;
            pStat->attribute.psProperty.ulKind = PRSPEC_PROPID;
            pStat->attribute.psProperty.propid = PID_XISF_DATASTATE;
            pStat->idChunkSource = pStat->idChunk;
            pStat->cwcStartSource = 0;
            pStat->cwcLenSource = 0;

            m_pendingDataStateValue = true;
            ++m_currentChunk;
            return S_OK;
        }
        return FILTER_E_END_OF_CHUNKS;
    }

    memset(pStat, 0, sizeof(*pStat));
    pStat->idChunk = m_currentChunk + 1;
    pStat->breakType = CHUNK_EOS;
    pStat->flags = CHUNK_TEXT;
    pStat->locale = GetSystemDefaultLCID();
    pStat->attribute.guidPropSet = s_PSGUID_STORAGE;
    pStat->attribute.psProperty.ulKind = PRSPEC_PROPID;
    pStat->attribute.psProperty.propid = PID_STG_CONTENTS;
    pStat->idChunkSource = pStat->idChunk;
    pStat->cwcStartSource = 0;
    pStat->cwcLenSource = 0;

    m_currentOffset = 0;

    WriteFilterTelemetry(TRACE_LEVEL_VERBOSE, XISF_FILTER_KEYWORD_FILTER,
                          L"FilterChunkEmitted chunkId=%lu", pStat->idChunk);

    return S_OK;
}

IFACEMETHODIMP CXISFFilter::GetText(ULONG* pcwcBuffer, WCHAR* awcBuffer)
{
    if (!pcwcBuffer || !awcBuffer) return E_INVALIDARG;
    if (!m_initialized) return FILTER_E_NO_MORE_TEXT;

    if (m_currentChunk >= static_cast<ULONG>(m_textChunks.size()))
        return FILTER_E_NO_MORE_TEXT;

    const std::wstring& chunk = m_textChunks[m_currentChunk];
    ULONG remaining = static_cast<ULONG>(chunk.size()) - m_currentOffset;

    if (remaining == 0)
    {
        // Move to next chunk on next GetChunk call
        m_currentChunk++;
        *pcwcBuffer = 0;
        return FILTER_E_NO_MORE_TEXT;
    }

    // Buffer size includes space for null terminator
    ULONG bufferCapacity = *pcwcBuffer;
    if (bufferCapacity == 0) return E_INVALIDARG;

    // Reserve one character for null terminator
    ULONG toCopy = (remaining < bufferCapacity) ? remaining : (bufferCapacity - 1);
    memcpy(awcBuffer, chunk.data() + m_currentOffset, toCopy * sizeof(WCHAR));
    awcBuffer[toCopy] = L'\0';
    *pcwcBuffer = toCopy;
    m_currentOffset += toCopy;

    if (m_currentOffset >= static_cast<ULONG>(chunk.size()))
    {
        m_currentChunk++;
        return FILTER_S_LAST_TEXT;
    }

    return S_OK;
}

void CXISFFilter::PopulateDerivedValues(const xisf::ParseResult& result)
{
    m_hasDataStateValue = false;
    m_dataStateValue.clear();

    auto sfIt = result.metadata.imageAttributes.find("sampleFormat");
    auto csIt = result.metadata.imageAttributes.find("colorSpace");
    const std::string sampleFormat =
        (sfIt != result.metadata.imageAttributes.end()) ? sfIt->second : "";
    const std::string colorSpace =
        (csIt != result.metadata.imageAttributes.end()) ? csIt->second : "";

    if (sampleFormat.empty() && colorSpace.empty())
        return;

    const bool isLinear = DetermineIsLinearFromMetadata(sampleFormat, colorSpace);
    m_dataStateValue = isLinear ? L"Linear" : L"Non-Linear";
    m_hasDataStateValue = true;
}

bool CXISFFilter::DetermineIsLinearFromMetadata(std::string_view sampleFormat,
                                                std::string_view colorSpace)
{
    const bool isFloat32 = (sampleFormat == "Float32");
    const bool isFloat64 = (sampleFormat == "Float64");
    const bool isUInt8   = (sampleFormat == "UInt8");

    bool isLinear = (isFloat32 || isFloat64);
    if (colorSpace == "Gray" || colorSpace == "RGB") isLinear = true;
    if (colorSpace == "GraySRGB" || colorSpace == "RGBSRGB") isLinear = false;
    if (isUInt8) isLinear = false;
    return isLinear;
}

IFACEMETHODIMP CXISFFilter::GetValue(PROPVARIANT** ppPropValue)
{
    if (!ppPropValue) return E_INVALIDARG;
    *ppPropValue = nullptr;
    if (!m_initialized || !m_pendingDataStateValue || !m_hasDataStateValue)
        return FILTER_E_NO_VALUES;

    PROPVARIANT* pv = static_cast<PROPVARIANT*>(CoTaskMemAlloc(sizeof(PROPVARIANT)));
    if (!pv) return E_OUTOFMEMORY;
    PropVariantInit(pv);

    HRESULT hr = InitPropVariantFromString(m_dataStateValue.c_str(), pv);
    if (FAILED(hr)) {
        CoTaskMemFree(pv);
        return hr;
    }

    *ppPropValue = pv;
    m_pendingDataStateValue = false;
    return S_OK;
}

IFACEMETHODIMP CXISFFilter::BindRegion(FILTERREGION, REFIID, void**)
{
    return E_NOTIMPL;
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

HRESULT CXISFFilter::ParseFromStream(IStream* pStm)
{
    if (!pStm) return E_INVALIDARG;

    // Read the 16-byte preamble
    char preamble[16] = {};
    ULONG cbRead = 0;
    HRESULT hr = pStm->Read(preamble, 16, &cbRead);
    if (FAILED(hr) || cbRead != 16) {
        WriteFilterTelemetry(TRACE_LEVEL_WARNING, XISF_FILTER_KEYWORD_PARSE,
                              L"FilterError preamble read failed");
        return E_FAIL;
    }

    // Verify signature
    static const char kSignature[8] = {'X','I','S','F','0','1','0','0'};
    if (memcmp(preamble, kSignature, 8) != 0) {
        WriteFilterTelemetry(TRACE_LEVEL_WARNING, XISF_FILTER_KEYWORD_PARSE,
                              L"FilterError invalid XISF signature");
        return E_FAIL;
    }

    // Decode XML header length (little-endian uint32 at offset 8)
    uint32_t headerLength = 0;
    memcpy(&headerLength, preamble + 8, sizeof(uint32_t));

    if (headerLength > xisf::XISFParser::kMaxHeaderBytes) {
        WriteFilterTelemetry(TRACE_LEVEL_WARNING, XISF_FILTER_KEYWORD_PARSE,
                              L"FilterError header too large: %u bytes", headerLength);
        return E_FAIL;
    }

    // Read the XML header
    std::string xml(headerLength, '\0');
    hr = pStm->Read(xml.data(), headerLength, &cbRead);
    if (FAILED(hr) || cbRead != headerLength) {
        WriteFilterTelemetry(TRACE_LEVEL_WARNING, XISF_FILTER_KEYWORD_PARSE,
                              L"FilterError XML header read failed");
        return E_FAIL;
    }

    xisf::ParseResult result = xisf::XISFParser::ParseXMLString(xml);
    if (!result.ok()) {
        WriteFilterTelemetry(TRACE_LEVEL_WARNING, XISF_FILTER_KEYWORD_PARSE,
                              L"FilterError XML parse failed");
        return E_FAIL;
    }

    m_textChunks = result.metadata.GetSearchableTextChunks();
    PopulateDerivedValues(result);
    m_currentChunk = 0;
    m_currentOffset = 0;
    m_pendingDataStateValue = false;
    m_initialized = false;
    return S_OK;
}
