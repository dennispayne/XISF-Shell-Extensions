#include <initguid.h>
#include "PropertyStore.h"
#include "ComputedProperties.h"
#include "PixelStatistics.h"
#include "PropertyHandlerTraceLogging.h"
#include <strsafe.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <knownfolders.h>
#include <new>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <mutex>
#include <cstdarg>
#include "resource.h"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "advapi32.lib")

extern long g_cDllRef;

std::wstring CXISFPropertyHandler::s_dsoDbPath;

// Single definition for the test hook pointer.
extern "C" XISFPropertyHandlerTelemetryHook g_xisfPropertyHandlerTelemetryHook = nullptr;

void WritePropertyHandlerTelemetry(UCHAR level, ULONGLONG keyword, PCWSTR format, ...) {
    const bool hookEnabled = (g_xisfPropertyHandlerTelemetryHook != nullptr);
    wchar_t buffer[768] = {};
    va_list args;
    va_start(args, format);
    StringCchVPrintfW(buffer, ARRAYSIZE(buffer), format, args);
    va_end(args);

    // EventWriteString accepts runtime level/keyword (TraceLoggingWrite requires
    // compile-time constants).  Access the underlying REGHANDLE from the
    // TraceLogging provider to emit a flat string event.
    EventWriteString(g_hPropertyProvider->RegHandle, level, keyword, buffer);

    if (hookEnabled) {
        g_xisfPropertyHandlerTelemetryHook(level, keyword, buffer);
    }
}

static LPWSTR NarrowToWide(const std::string& s) {
    if (s.empty()) return nullptr;
    int cch = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    if (cch <= 0) return nullptr;
    LPWSTR pw = static_cast<LPWSTR>(CoTaskMemAlloc((static_cast<size_t>(cch) + 1) * sizeof(wchar_t)));
    if (!pw) return nullptr;
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), pw, cch);
    pw[cch] = L'\0';
    return pw;
}

// Read exactly cbToRead bytes from pStream, looping over partial reads.
// IStream::Read is not guaranteed to fill the buffer in a single call.
static HRESULT ReadAll(IStream* pStream, void* pv, ULONG cbToRead, ULONG* pcbRead) {
    BYTE* dest = static_cast<BYTE*>(pv);
    ULONG totalRead = 0;
    while (totalRead < cbToRead) {
        ULONG cbChunk = 0;
        HRESULT hr = pStream->Read(dest + totalRead, cbToRead - totalRead, &cbChunk);
        if (FAILED(hr)) return hr;
        if (cbChunk == 0) break;   // EOF
        totalRead += cbChunk;
    }
    if (pcbRead) *pcbRead = totalRead;
    return S_OK;
}

CXISFPropertyHandler::CXISFPropertyHandler() : m_cRef(1), m_initialized(false), m_pStream(nullptr) { InterlockedIncrement(&g_cDllRef); }
CXISFPropertyHandler::~CXISFPropertyHandler() {
    if (m_pStream) { m_pStream->Release(); m_pStream = nullptr; }
    InterlockedDecrement(&g_cDllRef);
}

IFACEMETHODIMP CXISFPropertyHandler::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IPropertyStore))
        *ppv = static_cast<IPropertyStore*>(this);
    else if (IsEqualIID(riid, IID_IInitializeWithStream))
        *ppv = static_cast<IInitializeWithStream*>(this);
    else if (IsEqualIID(riid, IID_IPropertyStoreCapabilities))
        *ppv = static_cast<IPropertyStoreCapabilities*>(this);
    else return E_NOINTERFACE;
    AddRef(); return S_OK;
}
IFACEMETHODIMP_(ULONG) CXISFPropertyHandler::AddRef() { return InterlockedIncrement(&m_cRef); }
IFACEMETHODIMP_(ULONG) CXISFPropertyHandler::Release() { ULONG c = InterlockedDecrement(&m_cRef); if (c == 0) delete this; return c; }

std::string CXISFPropertyHandler::TrimValue(const std::string& raw) const {
    size_t s = raw.find_first_not_of(" \t\r\n");
    if (s == std::string::npos) return {};
    size_t e = raw.find_last_not_of(" \t\r\n");
    std::string result = raw.substr(s, e - s + 1);
    // Strip FITS single-quote delimiters (e.g. "'LIGHT'" → "LIGHT")
    if (result.size() >= 2 && result.front() == '\'' && result.back() == '\'') {
        result = result.substr(1, result.size() - 2);
        // FITS pads with trailing spaces inside quotes — trim again
        size_t te = result.find_last_not_of(' ');
        if (te == std::string::npos) return {};
        result = result.substr(0, te + 1);
    }
    return result;
}

bool CXISFPropertyHandler::TryParseDouble(const std::string& raw, double* out) const {
    std::string t = TrimValue(raw);
    if (t.empty()) return false;
    char* ep = nullptr;
    double d = strtod(t.c_str(), &ep);
    if (ep == t.c_str()) return false;
    *out = d; return true;
}

std::string CXISFPropertyHandler::GetFITSOrProp(const std::string& fitsKey, const std::string& propId) const {
    std::string v = m_metadata.getFITSValue(fitsKey);
    if (v.empty()) v = m_metadata.getPropertyValue(propId);
    return TrimValue(v);
}

void CXISFPropertyHandler::SetDSODatabasePath(const std::wstring& path) { s_dsoDbPath = path; }

bool CXISFPropertyHandler::TryAddDoubleProp(const PROPERTYKEY& key, const char* fitsKey, const char* propId) {
    std::string raw = GetFITSOrProp(fitsKey, propId);
    double v;
    if (!TryParseDouble(raw, &v)) return false;
    AddDoubleProp(key, v);
    return true;
}

bool CXISFPropertyHandler::TryAddUInt32Prop(const PROPERTYKEY& key, const char* fitsKey, const char* propId) {
    std::string raw = GetFITSOrProp(fitsKey, propId);
    double v;
    if (!TryParseDouble(raw, &v)) return false;
    AddUInt32Prop(key, static_cast<uint32_t>(v));
    return true;
}

bool CXISFPropertyHandler::TryAddStringProp(const PROPERTYKEY& key, const char* fitsKey, const char* propId) {
    std::string raw = GetFITSOrProp(fitsKey, propId);
    if (raw.empty()) return false;
    AddStringProp(key, raw);
    return true;
}

std::optional<double> CXISFPropertyHandler::TryResolveFocalLengthMM() const {
    std::string flRaw = m_metadata.getFITSValue("FOCALLEN");
    double v;
    if (!flRaw.empty()) {
        if (TryParseDouble(flRaw, &v)) return v;
        return std::nullopt;
    }
    std::string flProp = m_metadata.getPropertyValue("Instrument:Telescope:FocalLength");
    if (TryParseDouble(flProp, &v)) return v * 1000.0;
    return std::nullopt;
}

void CXISFPropertyHandler::AddStringProp(const PROPERTYKEY& key, const std::string& value) {
    if (value.empty()) return;
    PropertyEntry pe; pe.key = key;
    LPWSTR pw = NarrowToWide(value);
    if (!pw) return;
    pe.value.vt = VT_LPWSTR; pe.value.pwszVal = pw;
    m_properties.push_back(std::move(pe));
}

void CXISFPropertyHandler::AddDoubleProp(const PROPERTYKEY& key, double value) {
    PropertyEntry pe; pe.key = key;
    InitPropVariantFromDouble(value, &pe.value);
    m_properties.push_back(std::move(pe));
}

void CXISFPropertyHandler::AddUInt32Prop(const PROPERTYKEY& key, uint32_t value) {
    PropertyEntry pe; pe.key = key;
    InitPropVariantFromUInt32(value, &pe.value);
    m_properties.push_back(std::move(pe));
}

bool CXISFPropertyHandler::ParseISO8601(const std::string& iso, FILETIME* ft) {
    if (iso.size() < 19) return false;
    SYSTEMTIME st = {};
    if (sscanf_s(iso.c_str(), "%hu-%hu-%huT%hu:%hu:%hu",
        &st.wYear, &st.wMonth, &st.wDay, &st.wHour, &st.wMinute, &st.wSecond) < 6) return false;
    return SystemTimeToFileTime(&st, ft) != 0;
}

void CXISFPropertyHandler::AddDateTimeProp(const PROPERTYKEY& key, const std::string& isoDate) {
    FILETIME ft;
    if (!ParseISO8601(isoDate, &ft)) return;
    PropertyEntry pe; pe.key = key;
    InitPropVariantFromFileTime(&ft, &pe.value);
    m_properties.push_back(std::move(pe));
}

void CXISFPropertyHandler::AddStringListProp(const PROPERTYKEY& key, const std::vector<std::string>& values) {
    if (values.empty()) return;
    LPWSTR* pws = static_cast<LPWSTR*>(CoTaskMemAlloc(values.size() * sizeof(LPWSTR)));
    if (!pws) return;
    size_t count = 0;
    for (const auto& s : values) { LPWSTR pw = NarrowToWide(s); if (pw) pws[count++] = pw; }
    if (count == 0) { CoTaskMemFree(pws); return; }
    PropertyEntry pe; pe.key = key;
    pe.value.vt = VT_VECTOR | VT_LPWSTR;
    pe.value.calpwstr.cElems = static_cast<ULONG>(count);
    pe.value.calpwstr.pElems = pws;
    m_properties.push_back(std::move(pe));
}

IFACEMETHODIMP CXISFPropertyHandler::Initialize(IStream* pStream, DWORD grfMode) {
    const ULONGLONG initStart = GetTickCount64();
    TraceLoggingWrite(g_hPropertyProvider, "PropertyStoreInitializeStart",
        TraceLoggingLevel(TRACE_LEVEL_INFORMATION),
        TraceLoggingKeyword(XISF_ETW_KEYWORD_LIFECYCLE),
        TraceLoggingUInt32(grfMode, "Mode"));
    WritePropertyHandlerTelemetry(TRACE_LEVEL_INFORMATION, XISF_ETW_KEYWORD_LIFECYCLE,
        L"PropertyStoreInitializeStart Mode=%u", grfMode);

    if (m_initialized) {
        const HRESULT _hr = HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
        const ULONGLONG _dur = GetTickCount64() - initStart;
        TraceLoggingWrite(g_hPropertyProvider, "PropertyStoreInitializeFailed",
            TraceLoggingLevel(TRACE_LEVEL_WARNING),
            TraceLoggingKeyword(XISF_ETW_KEYWORD_LIFECYCLE | XISF_ETW_KEYWORD_PARSE),
            TraceLoggingString("AlreadyInitialized", "Stage"),
            TraceLoggingHResult(_hr, "Hr"),
            TraceLoggingUInt32(grfMode, "Mode"),
            TraceLoggingUInt64(_dur, "DurationMs"));
        WritePropertyHandlerTelemetry(TRACE_LEVEL_WARNING, XISF_ETW_KEYWORD_LIFECYCLE | XISF_ETW_KEYWORD_PARSE,
            L"PropertyStoreInitializeFailed Stage=AlreadyInitialized Hr=0x%08X Mode=%u DurationMs=%llu", _hr, grfMode, _dur);
        return _hr;
    }
    if (grfMode & (STGM_READWRITE | STGM_WRITE)) {
        const ULONGLONG _dur = GetTickCount64() - initStart;
        TraceLoggingWrite(g_hPropertyProvider, "PropertyStoreInitializeFailed",
            TraceLoggingLevel(TRACE_LEVEL_WARNING),
            TraceLoggingKeyword(XISF_ETW_KEYWORD_LIFECYCLE | XISF_ETW_KEYWORD_PARSE),
            TraceLoggingString("WriteModeRejected", "Stage"),
            TraceLoggingHResult(STG_E_ACCESSDENIED, "Hr"),
            TraceLoggingUInt32(grfMode, "Mode"),
            TraceLoggingUInt64(_dur, "DurationMs"));
        WritePropertyHandlerTelemetry(TRACE_LEVEL_WARNING, XISF_ETW_KEYWORD_LIFECYCLE | XISF_ETW_KEYWORD_PARSE,
            L"PropertyStoreInitializeFailed Stage=WriteModeRejected Hr=0x%08X Mode=%u DurationMs=%llu", STG_E_ACCESSDENIED, grfMode, _dur);
        return STG_E_ACCESSDENIED;
    }
    if (!pStream) {
        const ULONGLONG _dur = GetTickCount64() - initStart;
        TraceLoggingWrite(g_hPropertyProvider, "PropertyStoreInitializeFailed",
            TraceLoggingLevel(TRACE_LEVEL_WARNING),
            TraceLoggingKeyword(XISF_ETW_KEYWORD_LIFECYCLE | XISF_ETW_KEYWORD_PARSE),
            TraceLoggingString("NullStream", "Stage"),
            TraceLoggingHResult(E_POINTER, "Hr"),
            TraceLoggingUInt32(grfMode, "Mode"),
            TraceLoggingUInt64(_dur, "DurationMs"));
        WritePropertyHandlerTelemetry(TRACE_LEVEL_WARNING, XISF_ETW_KEYWORD_LIFECYCLE | XISF_ETW_KEYWORD_PARSE,
            L"PropertyStoreInitializeFailed Stage=NullStream Hr=0x%08X Mode=%u DurationMs=%llu", E_POINTER, grfMode, _dur);
        return E_POINTER;
    }

    BYTE preamble[16] = {};
    ULONG cbRead = 0;
    HRESULT hr = ReadAll(pStream, preamble, 16, &cbRead);
    if (FAILED(hr) || cbRead < 16) {
        const HRESULT _hr = FAILED(hr) ? hr : E_FAIL;
        const ULONGLONG _dur = GetTickCount64() - initStart;
        TraceLoggingWrite(g_hPropertyProvider, "PropertyStoreInitializeFailed",
            TraceLoggingLevel(TRACE_LEVEL_WARNING),
            TraceLoggingKeyword(XISF_ETW_KEYWORD_LIFECYCLE | XISF_ETW_KEYWORD_PARSE),
            TraceLoggingString("ReadPreamble", "Stage"),
            TraceLoggingHResult(_hr, "Hr"),
            TraceLoggingUInt32(grfMode, "Mode"),
            TraceLoggingUInt32(cbRead, "BytesRead"),
            TraceLoggingUInt64(_dur, "DurationMs"));
        WritePropertyHandlerTelemetry(TRACE_LEVEL_WARNING, XISF_ETW_KEYWORD_LIFECYCLE | XISF_ETW_KEYWORD_PARSE,
            L"PropertyStoreInitializeFailed Stage=ReadPreamble Hr=0x%08X Mode=%u BytesRead=%u DurationMs=%llu", _hr, grfMode, cbRead, _dur);
        return E_FAIL;
    }
    if (memcmp(preamble, "XISF0100", 8) != 0) {
        const HRESULT _hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        const ULONGLONG _dur = GetTickCount64() - initStart;
        TraceLoggingWrite(g_hPropertyProvider, "PropertyStoreInitializeFailed",
            TraceLoggingLevel(TRACE_LEVEL_WARNING),
            TraceLoggingKeyword(XISF_ETW_KEYWORD_LIFECYCLE | XISF_ETW_KEYWORD_PARSE),
            TraceLoggingString("InvalidSignature", "Stage"),
            TraceLoggingHResult(_hr, "Hr"),
            TraceLoggingUInt32(grfMode, "Mode"),
            TraceLoggingUInt32(cbRead, "BytesRead"),
            TraceLoggingUInt64(_dur, "DurationMs"));
        WritePropertyHandlerTelemetry(TRACE_LEVEL_WARNING, XISF_ETW_KEYWORD_LIFECYCLE | XISF_ETW_KEYWORD_PARSE,
            L"PropertyStoreInitializeFailed Stage=InvalidSignature Hr=0x%08X Mode=%u BytesRead=%u DurationMs=%llu", _hr, grfMode, cbRead, _dur);
        return E_FAIL;
    }

    UINT32 headerLength = 0;
    memcpy(&headerLength, preamble + 8, sizeof(UINT32));
    if (headerLength == 0 || headerLength > xisf::XISFParser::kMaxHeaderBytes) {
        const HRESULT _hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        const ULONGLONG _dur = GetTickCount64() - initStart;
        TraceLoggingWrite(g_hPropertyProvider, "PropertyStoreInitializeFailed",
            TraceLoggingLevel(TRACE_LEVEL_WARNING),
            TraceLoggingKeyword(XISF_ETW_KEYWORD_LIFECYCLE | XISF_ETW_KEYWORD_PARSE),
            TraceLoggingString("HeaderLength", "Stage"),
            TraceLoggingHResult(_hr, "Hr"),
            TraceLoggingUInt32(grfMode, "Mode"),
            TraceLoggingUInt32(headerLength, "HeaderBytes"),
            TraceLoggingUInt64(_dur, "DurationMs"));
        WritePropertyHandlerTelemetry(TRACE_LEVEL_WARNING, XISF_ETW_KEYWORD_LIFECYCLE | XISF_ETW_KEYWORD_PARSE,
            L"PropertyStoreInitializeFailed Stage=HeaderLength Hr=0x%08X Mode=%u HeaderBytes=%u DurationMs=%llu", _hr, grfMode, headerLength, _dur);
        return E_FAIL;
    }

    std::vector<char> xmlBuf(headerLength + 1, 0);
    hr = ReadAll(pStream, xmlBuf.data(), headerLength, &cbRead);
    if (FAILED(hr) || cbRead < headerLength) {
        const HRESULT _hr = FAILED(hr) ? hr : E_FAIL;
        const ULONGLONG _dur = GetTickCount64() - initStart;
        TraceLoggingWrite(g_hPropertyProvider, "PropertyStoreInitializeFailed",
            TraceLoggingLevel(TRACE_LEVEL_WARNING),
            TraceLoggingKeyword(XISF_ETW_KEYWORD_LIFECYCLE | XISF_ETW_KEYWORD_PARSE),
            TraceLoggingString("ReadHeader", "Stage"),
            TraceLoggingHResult(_hr, "Hr"),
            TraceLoggingUInt32(grfMode, "Mode"),
            TraceLoggingUInt32(cbRead, "BytesRead"),
            TraceLoggingUInt32(headerLength, "HeaderBytes"),
            TraceLoggingUInt64(_dur, "DurationMs"));
        WritePropertyHandlerTelemetry(TRACE_LEVEL_WARNING, XISF_ETW_KEYWORD_LIFECYCLE | XISF_ETW_KEYWORD_PARSE,
            L"PropertyStoreInitializeFailed Stage=ReadHeader Hr=0x%08X Mode=%u BytesRead=%u HeaderBytes=%u DurationMs=%llu", _hr, grfMode, cbRead, headerLength, _dur);
        return E_FAIL;
    }

    std::string xml(xmlBuf.data(), headerLength);
    auto result = xisf::XISFParser::ParseXMLString(xml);
    if (!result.ok()) {
        const ULONGLONG _dur = GetTickCount64() - initStart;
        TraceLoggingWrite(g_hPropertyProvider, "PropertyStoreInitializeFailed",
            TraceLoggingLevel(TRACE_LEVEL_WARNING),
            TraceLoggingKeyword(XISF_ETW_KEYWORD_LIFECYCLE | XISF_ETW_KEYWORD_PARSE),
            TraceLoggingString("ParseXml", "Stage"),
            TraceLoggingHResult(E_FAIL, "Hr"),
            TraceLoggingUInt32(grfMode, "Mode"),
            TraceLoggingUInt32(headerLength, "HeaderBytes"),
            TraceLoggingUInt64(_dur, "DurationMs"));
        WritePropertyHandlerTelemetry(TRACE_LEVEL_WARNING, XISF_ETW_KEYWORD_LIFECYCLE | XISF_ETW_KEYWORD_PARSE,
            L"PropertyStoreInitializeFailed Stage=ParseXml Hr=0x%08X Mode=%u HeaderBytes=%u DurationMs=%llu", E_FAIL, grfMode, headerLength, _dur);
        return E_FAIL;
    }
    m_metadata = std::move(result.metadata);

    { UINT32 _fitsCount = static_cast<UINT32>(m_metadata.fitsKeywords.size());
      UINT32 _propCount = static_cast<UINT32>(m_metadata.properties.size());
      ULONGLONG _dur = GetTickCount64() - initStart;
    TraceLoggingWrite(g_hPropertyProvider, "PropertyStoreMetadataParsed",
        TraceLoggingLevel(TRACE_LEVEL_INFORMATION),
        TraceLoggingKeyword(XISF_ETW_KEYWORD_PARSE | XISF_ETW_KEYWORD_PERF),
        TraceLoggingUInt32(headerLength, "HeaderBytes"),
        TraceLoggingUInt32(_fitsCount, "FitsKeywordCount"),
        TraceLoggingUInt32(_propCount, "XisfPropertyCount"),
        TraceLoggingUInt64(_dur, "DurationMs"));
    WritePropertyHandlerTelemetry(TRACE_LEVEL_INFORMATION, XISF_ETW_KEYWORD_PARSE | XISF_ETW_KEYWORD_PERF,
        L"PropertyStoreMetadataParsed HeaderBytes=%u FitsKeywordCount=%u XisfPropertyCount=%u DurationMs=%llu", headerLength, _fitsCount, _propCount, _dur);
    }

    // Read feature tier and projection flag per-Initialize (not cached)
    const xisf::FeatureTier tier = xisf::GetFeatureTier();
    const bool projectionEnabled = xisf::IsProjectionEnabled();

    { UINT32 _tier = static_cast<UINT32>(tier); UINT32 _proj = projectionEnabled ? 1u : 0u;
    TraceLoggingWrite(g_hPropertyProvider, "FeatureTierLoaded",
        TraceLoggingLevel(TRACE_LEVEL_INFORMATION),
        TraceLoggingKeyword(XISF_ETW_KEYWORD_LIFECYCLE),
        TraceLoggingUInt32(_tier, "Tier"),
        TraceLoggingBoolean(_proj, "ProjectionEnabled"));
    WritePropertyHandlerTelemetry(TRACE_LEVEL_INFORMATION, XISF_ETW_KEYWORD_LIFECYCLE,
        L"FeatureTierLoaded Tier=%u ProjectionEnabled=%u", _tier, _proj);
    }

    {
        std::lock_guard<std::mutex> lock(m_propertyLock);
        // Pixel stats: computed BEFORE PopulateProperties so median/p95 can
        // drive the AstroDataState (Linear / Non-Linear) heuristic. Only Full
        // tier reads pixel data; Standard tier and below skip this step and
        // fall back to the metadata heuristic in xisf::DetermineIsLinear.
        xisf::PixelStatsResult pixelStats;
        if (xisf::IsPixelStatsEnabled(tier)) {
            m_pStream = pStream;
            m_pStream->AddRef();
            pixelStats = xisf::ComputePixelStats(m_pStream, m_metadata.xmlHeader);
        }

        PopulateProperties(tier, projectionEnabled, pixelStats);

        // Apply pixel stats to the placeholder properties allocated inside
        // PopulateProperties. Done under the same lock so consumers never
        // observe a half-populated property store.
        if (xisf::IsPixelStatsEnabled(tier)) {
            if (pixelStats.available) {
                for (auto& pe : m_properties) {
                    if (IsEqualPropertyKey(pe.key, PKEY_XISF_Median)) {
                        PropVariantClear(&pe.value);
                        InitPropVariantFromDouble(pixelStats.median, &pe.value);
                    } else if (IsEqualPropertyKey(pe.key, PKEY_XISF_Mean)) {
                        PropVariantClear(&pe.value);
                        InitPropVariantFromDouble(pixelStats.mean, &pe.value);
                    } else if (IsEqualPropertyKey(pe.key, PKEY_XISF_ClippingLow)) {
                        PropVariantClear(&pe.value);
                        InitPropVariantFromDouble(pixelStats.clippingLowPct, &pe.value);
                    } else if (IsEqualPropertyKey(pe.key, PKEY_XISF_ClippingHigh)) {
                        PropVariantClear(&pe.value);
                        InitPropVariantFromDouble(pixelStats.clippingHighPct, &pe.value);
                    }
                }
                m_pixelStatsState = PixelStatsState::Computed;
            } else {
                m_pixelStatsState = PixelStatsState::Unavailable;
            }
        }
    }
    m_initialized = true;

    { UINT32 _propCount = static_cast<UINT32>(m_properties.size()); ULONGLONG _dur = GetTickCount64() - initStart;
    TraceLoggingWrite(g_hPropertyProvider, "PropertyStoreInitializeCompleted",
        TraceLoggingLevel(TRACE_LEVEL_INFORMATION),
        TraceLoggingKeyword(XISF_ETW_KEYWORD_LIFECYCLE | XISF_ETW_KEYWORD_PERF),
        TraceLoggingUInt32(_propCount, "PropertyCount"),
        TraceLoggingUInt64(_dur, "DurationMs"));
    WritePropertyHandlerTelemetry(TRACE_LEVEL_INFORMATION, XISF_ETW_KEYWORD_LIFECYCLE | XISF_ETW_KEYWORD_PERF,
        L"PropertyStoreInitializeCompleted PropertyCount=%u DurationMs=%llu", _propCount, _dur);
    }
    return S_OK;
}

void CXISFPropertyHandler::PopulateProperties(xisf::FeatureTier tier, bool projectionEnabled,
                                              const xisf::PixelStatsResult& pixelStats) {
    const ULONGLONG populateStart = GetTickCount64();

    // --- Basic tier: Direct metadata extraction from XML header ---

    // Exposure Time
    std::string expRaw = GetFITSOrProp("EXPTIME", "");
    if (expRaw.empty()) expRaw = GetFITSOrProp("EXPOSURE", "");
    double expTime = 0; bool hasExp = TryParseDouble(expRaw, &expTime);
    if (hasExp) AddDoubleProp(PKEY_XISF_ExposureTime, expTime);

    // Camera Model
    std::string cam = GetFITSOrProp("INSTRUME", "Instrument:Camera:Name");
    if (!cam.empty()) AddStringProp(PKEY_XISF_CameraModel, cam);

    // Focal Length (FITS in mm, Property in meters)
    auto focalLengthOpt = TryResolveFocalLengthMM();
    double focalLength = focalLengthOpt.value_or(0.0);
    bool hasFL = focalLengthOpt.has_value();
    if (hasFL) AddDoubleProp(PKEY_XISF_FocalLength, focalLength);

    // F-Number
    std::string fnRaw = m_metadata.getFITSValue("FOCRATIO");
    double fNumber = 0; bool hasFN = false;
    if (!fnRaw.empty()) { hasFN = TryParseDouble(fnRaw, &fNumber); }
    else {
        std::string apProp = m_metadata.getPropertyValue("Instrument:Telescope:Aperture");
        double ap; if (TryParseDouble(apProp, &ap) && ap > 0.0 && hasFL) { fNumber = focalLength / (ap * 1000.0); hasFN = true; }
    }
    if (hasFN) AddDoubleProp(PKEY_XISF_FNumber, fNumber);

    // Object Name
    std::string objName = GetFITSOrProp("OBJECT", "Observation:Object:Name");
    if (!objName.empty()) { AddStringProp(PKEY_XISF_ObjectName, objName); AddStringProp(PKEY_Title, objName); }

    // Filter Name
    std::string filter = GetFITSOrProp("FILTER", "Instrument:Filter:Name");
    if (!filter.empty()) AddStringProp(PKEY_XISF_FilterName, filter);

    // Image Type
    std::string imgType = GetFITSOrProp("IMAGETYP", "Observation:ImageType");
    if (!imgType.empty()) AddStringProp(PKEY_XISF_ImageType, imgType);

    // Gain (UInt32)
    TryAddUInt32Prop(PKEY_XISF_Gain, "GAIN", "Instrument:Camera:Gain");

    // Offset (UInt32)
    TryAddUInt32Prop(PKEY_XISF_Offset, "OFFSET", "Instrument:Camera:Offset");

    // Sensor Temperature
    TryAddDoubleProp(PKEY_XISF_SensorTemperature, "CCD-TEMP", "Instrument:Sensor:Temperature");

    // Telescope
    TryAddStringProp(PKEY_XISF_Telescope, "TELESCOP", "Instrument:Telescope:Name");

    // Binning
    std::string binX = m_metadata.getFITSValue("XBINNING");
    std::string binY = m_metadata.getFITSValue("YBINNING");
    if (!binX.empty()) { std::string bin = TrimValue(binX) + "x" + TrimValue(binY.empty() ? binX : binY); AddStringProp(PKEY_XISF_Binning, bin); }

    // Date Observed
    std::string dateObs = GetFITSOrProp("DATE-OBS", "Observation:Time:Start");
    if (!dateObs.empty()) AddDateTimeProp(PKEY_XISF_DateObserved, dateObs);

    // Software
    std::string sw = GetFITSOrProp("SWCREATE", "Processing:Software");
    if (!sw.empty()) AddStringProp(PKEY_XISF_Software, sw);

    // RA and Dec (degrees)
    std::string raRaw = GetFITSOrProp("RA", "Observation:Center:RA");
    double raDeg = 0; bool hasRA = TryParseDouble(raRaw, &raDeg);
    if (hasRA) AddDoubleProp(PKEY_XISF_RA, raDeg);

    std::string decRaw = GetFITSOrProp("DEC", "Observation:Center:Dec");
    double decDeg = 0; bool hasDec = TryParseDouble(decRaw, &decDeg);
    if (hasDec) AddDoubleProp(PKEY_XISF_Dec, decDeg);

    // Set Temperature
    TryAddDoubleProp(PKEY_XISF_SetTemp, "SET-TEMP", "Instrument:Camera:SetTemperature");

    // Pixel Size
    std::string pxRaw = GetFITSOrProp("XPIXSZ", "Instrument:Sensor:XPixelSize");
    double pxSize; if (TryParseDouble(pxRaw, &pxSize)) AddDoubleProp(PKEY_XISF_PixelSize, pxSize);

    // Readout Mode
    TryAddStringProp(PKEY_XISF_ReadoutMode, "READOUTM", "Instrument:Camera:ReadoutMode");

    // Bayer Pattern
    TryAddStringProp(PKEY_XISF_BayerPattern, "BAYERPAT", "Instrument:Sensor:BayerPattern");

    // Site coordinates
    TryAddDoubleProp(PKEY_XISF_SiteLatitude, "SITELAT", "Observation:Location:Latitude");
    TryAddDoubleProp(PKEY_XISF_SiteLongitude, "SITELONG", "Observation:Location:Longitude");
    TryAddDoubleProp(PKEY_XISF_SiteElevation, "SITEELEV", "Observation:Location:Elevation");

    // Altitude, Azimuth, Airmass
    TryAddDoubleProp(PKEY_XISF_Altitude, "CENTALT", "Observation:Center:Alt");
    TryAddDoubleProp(PKEY_XISF_Azimuth, "CENTAZ", "Observation:Center:Az");
    TryAddDoubleProp(PKEY_XISF_Airmass, "AIRMASS", "Observation:Airmass");

    // Pier Side
    TryAddStringProp(PKEY_XISF_PierSide, "PIERSIDE", "Instrument:Telescope:PierSide");

    // Object RA/Dec (formatted strings for display)
    TryAddStringProp(PKEY_XISF_ObjectRA, "OBJCTRA", "Observation:Object:RA");
    TryAddStringProp(PKEY_XISF_ObjectDec, "OBJCTDEC", "Observation:Object:Dec");

    // Object RA/Dec as numeric degrees
    double objRaDeg = 0, objDecDeg = 0;
    bool hasObjRA = TryParseDouble(m_metadata.getPropertyValue("Observation:Object:RA"), &objRaDeg);
    bool hasObjDec = TryParseDouble(m_metadata.getPropertyValue("Observation:Object:Dec"), &objDecDeg);

    // Rotation
    TryAddDoubleProp(PKEY_XISF_Rotation, "ROTATANG", "Instrument:Rotator:Angle");

    // Focuser
    TryAddStringProp(PKEY_XISF_FocuserName, "FOCNAME", "Instrument:Focuser:Name");
    TryAddUInt32Prop(PKEY_XISF_FocuserPosition, "FOCPOS", "Instrument:Focuser:Position");
    TryAddDoubleProp(PKEY_XISF_FocuserTemp, "FOCTEMP", "Instrument:Focuser:Temperature");

    // Rotator
    TryAddStringProp(PKEY_XISF_RotatorName, "ROTNAME", "Instrument:Rotator:Name");
    TryAddDoubleProp(PKEY_XISF_RotatorAngle, "ROTATOR", "Instrument:Rotator:MechanicalAngle");

    // Filter Wheel
    TryAddStringProp(PKEY_XISF_FilterWheel, "FWHEEL", "Instrument:FilterWheel:Name");

    // Weather
    TryAddDoubleProp(PKEY_XISF_DewPoint, "DEWPOINT", "Weather:DewPoint");
    TryAddDoubleProp(PKEY_XISF_Humidity, "HUMIDITY", "Weather:Humidity");
    TryAddDoubleProp(PKEY_XISF_AmbientTemp, "AMBTEMP", "Weather:Temperature");

    // Weather — extended
    TryAddDoubleProp(PKEY_XISF_StarFWHM, "STARFWHM", "Weather:StarFWHM");
    TryAddDoubleProp(PKEY_XISF_SkyQuality, "MPSAS", "Weather:SkyQuality");
    TryAddDoubleProp(PKEY_XISF_SkyBrightness, "SKYBRGHT", "Weather:SkyBrightness");
    TryAddDoubleProp(PKEY_XISF_CloudCover, "CLOUDCVR", "Weather:CloudCover");
    TryAddDoubleProp(PKEY_XISF_Pressure, "PRESSURE", "Weather:Pressure");
    TryAddDoubleProp(PKEY_XISF_SkyTemp, "SKYTEMP", "Weather:SkyTemperature");
    TryAddDoubleProp(PKEY_XISF_WindSpeed, "WINDSPD", "Weather:WindSpeed");

    // Guiding RMS
    TryAddDoubleProp(PKEY_XISF_GuideRA, "GUIDERA", "Guider:RMS:RA");
    TryAddDoubleProp(PKEY_XISF_GuideDec, "GUIDEDEC", "Guider:RMS:Dec");

    // Date Local
    std::string dateLocal = GetFITSOrProp("DATE-LOC", "Observation:Time:Local");
    if (!dateLocal.empty()) AddDateTimeProp(PKEY_XISF_DateLocal, dateLocal);

    // Color Space
    {
        auto csIt = m_metadata.imageAttributes.find("colorSpace");
        if (csIt != m_metadata.imageAttributes.end() && !csIt->second.empty()) {
            AddStringProp(PKEY_XISF_ColorSpace, csIt->second);
        }
    }

    // Sample Format
    {
        auto sfIt = m_metadata.imageAttributes.find("sampleFormat");
        if (sfIt != m_metadata.imageAttributes.end() && !sfIt->second.empty()) {
            AddStringProp(PKEY_XISF_SampleFormat, sfIt->second);
        }
    }

    // Image dimensions
    {
        auto gIt = m_metadata.imageAttributes.find("geometry");
        if (gIt != m_metadata.imageAttributes.end() && !gIt->second.empty()) {
            const std::string& geom = gIt->second;
            auto c1 = geom.find(':');
            if (c1 != std::string::npos) {
                auto c2 = geom.find(':', c1 + 1);
                if (c2 != std::string::npos) {
                    double w = 0, h = 0, c = 0;
                    if (TryParseDouble(geom.substr(0, c1), &w) && w > 0)
                        AddUInt32Prop(PKEY_XISF_ImageWidth, static_cast<uint32_t>(w));
                    if (TryParseDouble(geom.substr(c1 + 1, c2 - c1 - 1), &h) && h > 0)
                        AddUInt32Prop(PKEY_XISF_ImageHeight, static_cast<uint32_t>(h));
                    if (TryParseDouble(geom.substr(c2 + 1), &c) && c > 0)
                        AddUInt32Prop(PKEY_XISF_ChannelCount, static_cast<uint32_t>(c));
                }
            }
        }
    }

    // Image Count
    if (m_metadata.imageCount > 0) {
        AddUInt32Prop(PKEY_XISF_ImageCount, m_metadata.imageCount);
    }

    // Pixel stat placeholders (filled by ComputePixelStats for Full tier)
    if (xisf::IsPixelStatsEnabled(tier)) {
        for (const auto& pk : {PKEY_XISF_Median, PKEY_XISF_Mean, PKEY_XISF_ClippingLow, PKEY_XISF_ClippingHigh}) {
            PropertyEntry pe;
            pe.key = pk;
            pe.value.vt = VT_EMPTY;
            m_properties.push_back(std::move(pe));
        }
    }

    // --- Standard+ tier: Computed properties ---
    if (tier >= xisf::FeatureTier::Standard) {
        xisf::ComputedPropertyInputs inputs{
            m_metadata, tier, projectionEnabled,
            raDeg, decDeg, hasRA, hasDec,
            objRaDeg, objDecDeg, hasObjRA, hasObjDec,
            focalLength, hasFL, pxRaw,
            expTime, hasExp, cam,
            fNumber, hasFN,
            filter, imgType, objName,
            pixelStats.available, pixelStats.median, pixelStats.p95
        };

        xisf::ComputedPropertySink sink{
            [this](const PROPERTYKEY& k, const std::string& v) { AddStringProp(k, v); },
            [this](const PROPERTYKEY& k, double v) { AddDoubleProp(k, v); },
            [this](const PROPERTYKEY& k, uint32_t v) { AddUInt32Prop(k, v); },
            [this](const PROPERTYKEY& k, const std::vector<std::string>& v) { AddStringListProp(k, v); }
        };
        xisf::PopulateComputedProperties(inputs, sink);
    }

    { UINT32 _propCount = static_cast<UINT32>(m_properties.size());
      UINT32 _tier = static_cast<UINT32>(tier);
      ULONGLONG _dur = GetTickCount64() - populateStart;
    TraceLoggingWrite(g_hPropertyProvider, "PropertyPopulation",
        TraceLoggingLevel(TRACE_LEVEL_INFORMATION),
        TraceLoggingKeyword(XISF_ETW_KEYWORD_PARSE | XISF_ETW_KEYWORD_PERF),
        TraceLoggingUInt32(_propCount, "PropertyCount"),
        TraceLoggingUInt32(_tier, "FeatureTier"),
        TraceLoggingUInt64(_dur, "DurationMs"));
    WritePropertyHandlerTelemetry(TRACE_LEVEL_INFORMATION, XISF_ETW_KEYWORD_PARSE | XISF_ETW_KEYWORD_PERF,
        L"PropertyPopulation PropertyCount=%u FeatureTier=%u DurationMs=%llu", _propCount, _tier, _dur);
    }
}

IFACEMETHODIMP CXISFPropertyHandler::GetCount(DWORD* cProps) {
    if (!cProps) return E_POINTER;
    std::lock_guard<std::mutex> lock(m_propertyLock);
    *cProps = static_cast<DWORD>(m_properties.size());
    return S_OK;
}

IFACEMETHODIMP CXISFPropertyHandler::GetAt(DWORD iProp, PROPERTYKEY* pkey) {
    if (!pkey) return E_POINTER;
    std::lock_guard<std::mutex> lock(m_propertyLock);
    if (iProp >= m_properties.size()) return E_INVALIDARG;
    *pkey = m_properties[iProp].key;
    return S_OK;
}

IFACEMETHODIMP CXISFPropertyHandler::GetValue(REFPROPERTYKEY key, PROPVARIANT* pPropVar) {
    if (!pPropVar) return E_POINTER;
    PropVariantInit(pPropVar);
    std::lock_guard<std::mutex> lock(m_propertyLock);

    for (const auto& pe : m_properties) {
        if (IsEqualPropertyKey(pe.key, key)) return PropVariantCopy(pPropVar, &pe.value);
    }
    return S_OK;
}

// Pixel stats computation moved to PixelStatistics.cpp

IFACEMETHODIMP CXISFPropertyHandler::SetValue(REFPROPERTYKEY, REFPROPVARIANT) {
    return STG_E_ACCESSDENIED;
}

IFACEMETHODIMP CXISFPropertyHandler::Commit() {
    return STG_E_ACCESSDENIED;
}

IFACEMETHODIMP CXISFPropertyHandler::IsPropertyWritable(REFPROPERTYKEY) {
    return S_FALSE;
}
