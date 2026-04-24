#include <initguid.h>
#include "PropertyStore.h"
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
std::shared_ptr<xisf::DSOCatalog> CXISFPropertyHandler::s_dsoCatalog;
std::vector<std::string> CXISFPropertyHandler::s_catalogPriority;
double CXISFPropertyHandler::s_matchToleranceDeg = 0.5;
bool CXISFPropertyHandler::s_projectionEnabled = true;
bool CXISFPropertyHandler::s_projectionChecked = false;
static auto s_catalogOnceFlag = std::make_shared<std::once_flag>();

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

bool CXISFPropertyHandler::ReadProjectionEnabled() {
    DWORD val = 1; DWORD cb = sizeof(val);
    LONG st = RegGetValueW(HKEY_CURRENT_USER, L"Software\\XISFPropertyHandler",
        L"EnableSystemPhotoProjection", RRF_RT_REG_DWORD, nullptr, &val, &cb);
    if (st != ERROR_SUCCESS) return true;
    return val != 0;
}

std::wstring CXISFPropertyHandler::Utf8ToWide(const std::string& raw) {
    if (raw.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, raw.c_str(), -1, nullptr, 0);
    if (len <= 0) return {};
    std::wstring result(static_cast<size_t>(len - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, raw.c_str(), -1, &result[0], len);
    return result;
}

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

void CXISFPropertyHandler::SetDSODatabasePath(const std::wstring& path) { s_dsoDbPath = path; s_dsoCatalog.reset(); s_catalogOnceFlag = std::make_shared<std::once_flag>(); }

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

std::string CXISFPropertyHandler::ComputeRAHour(double raDegrees) {
    int hour = static_cast<int>(raDegrees / 15.0);
    if (hour < 0) hour = 0; if (hour > 23) hour = 23;
    char buf[16]; snprintf(buf, sizeof(buf), "%dh", hour);
    return buf;
}

std::string CXISFPropertyHandler::ComputeDecBand(double decDegrees) {
    int band = static_cast<int>(std::floor(decDegrees / 15.0)) * 15;
    if (band < -90) band = -90;
    if (band > 75) band = 75;
    int upper = band + 15;
    if (upper > 90) upper = 90;
    char buf[64];
    snprintf(buf, sizeof(buf), "%+d\xC2\xB0 to %+d\xC2\xB0", band, upper);
    return buf;
}

IFACEMETHODIMP CXISFPropertyHandler::Initialize(IStream* pStream, DWORD grfMode) {
    const ULONGLONG initStart = GetTickCount64();
    TraceLoggingWrite(g_hPropertyProvider, "PropertyStoreInitializeStart",
        TraceLoggingLevel(TRACE_LEVEL_INFORMATION),
        TraceLoggingKeyword(XISF_ETW_KEYWORD_LIFECYCLE),
        TraceLoggingUInt32(grfMode, "Mode"));
    if (g_xisfPropertyHandlerTelemetryHook) {
        wchar_t _buf[256]; swprintf_s(_buf, L"PropertyStoreInitializeStart Mode=%u", grfMode);
        g_xisfPropertyHandlerTelemetryHook(TRACE_LEVEL_INFORMATION, XISF_ETW_KEYWORD_LIFECYCLE, _buf);
    }

    if (m_initialized) {
        { HRESULT _hr = HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED); ULONGLONG _dur = GetTickCount64() - initStart;
        TraceLoggingWrite(g_hPropertyProvider, "PropertyStoreInitializeFailed",
            TraceLoggingLevel(TRACE_LEVEL_WARNING),
            TraceLoggingKeyword(XISF_ETW_KEYWORD_LIFECYCLE | XISF_ETW_KEYWORD_PARSE),
            TraceLoggingString("AlreadyInitialized", "Stage"),
            TraceLoggingHResult(_hr, "Hr"),
            TraceLoggingUInt32(grfMode, "Mode"),
            TraceLoggingUInt64(_dur, "DurationMs"));
        if (g_xisfPropertyHandlerTelemetryHook) {
            wchar_t _buf[256]; swprintf_s(_buf, L"PropertyStoreInitializeFailed Stage=AlreadyInitialized Hr=0x%08X Mode=%u DurationMs=%llu", _hr, grfMode, _dur);
            g_xisfPropertyHandlerTelemetryHook(TRACE_LEVEL_WARNING, XISF_ETW_KEYWORD_LIFECYCLE | XISF_ETW_KEYWORD_PARSE, _buf);
        }}
        return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
    }
    if (grfMode & (STGM_READWRITE | STGM_WRITE)) {
        { ULONGLONG _dur = GetTickCount64() - initStart;
        TraceLoggingWrite(g_hPropertyProvider, "PropertyStoreInitializeFailed",
            TraceLoggingLevel(TRACE_LEVEL_WARNING),
            TraceLoggingKeyword(XISF_ETW_KEYWORD_LIFECYCLE | XISF_ETW_KEYWORD_PARSE),
            TraceLoggingString("WriteModeRejected", "Stage"),
            TraceLoggingHResult(STG_E_ACCESSDENIED, "Hr"),
            TraceLoggingUInt32(grfMode, "Mode"),
            TraceLoggingUInt64(_dur, "DurationMs"));
        if (g_xisfPropertyHandlerTelemetryHook) {
            wchar_t _buf[256]; swprintf_s(_buf, L"PropertyStoreInitializeFailed Stage=WriteModeRejected Hr=0x%08X Mode=%u DurationMs=%llu", STG_E_ACCESSDENIED, grfMode, _dur);
            g_xisfPropertyHandlerTelemetryHook(TRACE_LEVEL_WARNING, XISF_ETW_KEYWORD_LIFECYCLE | XISF_ETW_KEYWORD_PARSE, _buf);
        }}
        return STG_E_ACCESSDENIED;
    }
    if (!pStream) {
        { ULONGLONG _dur = GetTickCount64() - initStart;
        TraceLoggingWrite(g_hPropertyProvider, "PropertyStoreInitializeFailed",
            TraceLoggingLevel(TRACE_LEVEL_WARNING),
            TraceLoggingKeyword(XISF_ETW_KEYWORD_LIFECYCLE | XISF_ETW_KEYWORD_PARSE),
            TraceLoggingString("NullStream", "Stage"),
            TraceLoggingHResult(E_POINTER, "Hr"),
            TraceLoggingUInt32(grfMode, "Mode"),
            TraceLoggingUInt64(_dur, "DurationMs"));
        if (g_xisfPropertyHandlerTelemetryHook) {
            wchar_t _buf[256]; swprintf_s(_buf, L"PropertyStoreInitializeFailed Stage=NullStream Hr=0x%08X Mode=%u DurationMs=%llu", E_POINTER, grfMode, _dur);
            g_xisfPropertyHandlerTelemetryHook(TRACE_LEVEL_WARNING, XISF_ETW_KEYWORD_LIFECYCLE | XISF_ETW_KEYWORD_PARSE, _buf);
        }}
        return E_POINTER;
    }

    BYTE preamble[16] = {};
    ULONG cbRead = 0;
    HRESULT hr = ReadAll(pStream, preamble, 16, &cbRead);
    if (FAILED(hr) || cbRead < 16) {
        { HRESULT _hr = FAILED(hr) ? hr : E_FAIL; ULONGLONG _dur = GetTickCount64() - initStart;
        TraceLoggingWrite(g_hPropertyProvider, "PropertyStoreInitializeFailed",
            TraceLoggingLevel(TRACE_LEVEL_WARNING),
            TraceLoggingKeyword(XISF_ETW_KEYWORD_LIFECYCLE | XISF_ETW_KEYWORD_PARSE),
            TraceLoggingString("ReadPreamble", "Stage"),
            TraceLoggingHResult(_hr, "Hr"),
            TraceLoggingUInt32(grfMode, "Mode"),
            TraceLoggingUInt32(cbRead, "BytesRead"),
            TraceLoggingUInt64(_dur, "DurationMs"));
        if (g_xisfPropertyHandlerTelemetryHook) {
            wchar_t _buf[384]; swprintf_s(_buf, L"PropertyStoreInitializeFailed Stage=ReadPreamble Hr=0x%08X Mode=%u BytesRead=%u DurationMs=%llu", _hr, grfMode, cbRead, _dur);
            g_xisfPropertyHandlerTelemetryHook(TRACE_LEVEL_WARNING, XISF_ETW_KEYWORD_LIFECYCLE | XISF_ETW_KEYWORD_PARSE, _buf);
        }}
        return E_FAIL;
    }
    if (memcmp(preamble, "XISF0100", 8) != 0) {
        { HRESULT _hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA); ULONGLONG _dur = GetTickCount64() - initStart;
        TraceLoggingWrite(g_hPropertyProvider, "PropertyStoreInitializeFailed",
            TraceLoggingLevel(TRACE_LEVEL_WARNING),
            TraceLoggingKeyword(XISF_ETW_KEYWORD_LIFECYCLE | XISF_ETW_KEYWORD_PARSE),
            TraceLoggingString("InvalidSignature", "Stage"),
            TraceLoggingHResult(_hr, "Hr"),
            TraceLoggingUInt32(grfMode, "Mode"),
            TraceLoggingUInt32(cbRead, "BytesRead"),
            TraceLoggingUInt64(_dur, "DurationMs"));
        if (g_xisfPropertyHandlerTelemetryHook) {
            wchar_t _buf[384]; swprintf_s(_buf, L"PropertyStoreInitializeFailed Stage=InvalidSignature Hr=0x%08X Mode=%u BytesRead=%u DurationMs=%llu", _hr, grfMode, cbRead, _dur);
            g_xisfPropertyHandlerTelemetryHook(TRACE_LEVEL_WARNING, XISF_ETW_KEYWORD_LIFECYCLE | XISF_ETW_KEYWORD_PARSE, _buf);
        }}
        return E_FAIL;
    }

    UINT32 headerLength = 0;
    memcpy(&headerLength, preamble + 8, sizeof(UINT32));
    if (headerLength == 0 || headerLength > xisf::XISFParser::kMaxHeaderBytes) {
        { HRESULT _hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA); ULONGLONG _dur = GetTickCount64() - initStart;
        TraceLoggingWrite(g_hPropertyProvider, "PropertyStoreInitializeFailed",
            TraceLoggingLevel(TRACE_LEVEL_WARNING),
            TraceLoggingKeyword(XISF_ETW_KEYWORD_LIFECYCLE | XISF_ETW_KEYWORD_PARSE),
            TraceLoggingString("HeaderLength", "Stage"),
            TraceLoggingHResult(_hr, "Hr"),
            TraceLoggingUInt32(grfMode, "Mode"),
            TraceLoggingUInt32(headerLength, "HeaderBytes"),
            TraceLoggingUInt64(_dur, "DurationMs"));
        if (g_xisfPropertyHandlerTelemetryHook) {
            wchar_t _buf[384]; swprintf_s(_buf, L"PropertyStoreInitializeFailed Stage=HeaderLength Hr=0x%08X Mode=%u HeaderBytes=%u DurationMs=%llu", _hr, grfMode, headerLength, _dur);
            g_xisfPropertyHandlerTelemetryHook(TRACE_LEVEL_WARNING, XISF_ETW_KEYWORD_LIFECYCLE | XISF_ETW_KEYWORD_PARSE, _buf);
        }}
        return E_FAIL;
    }

    std::vector<char> xmlBuf(headerLength + 1, 0);
    hr = ReadAll(pStream, xmlBuf.data(), headerLength, &cbRead);
    if (FAILED(hr) || cbRead < headerLength) {
        { HRESULT _hr = FAILED(hr) ? hr : E_FAIL; ULONGLONG _dur = GetTickCount64() - initStart;
        TraceLoggingWrite(g_hPropertyProvider, "PropertyStoreInitializeFailed",
            TraceLoggingLevel(TRACE_LEVEL_WARNING),
            TraceLoggingKeyword(XISF_ETW_KEYWORD_LIFECYCLE | XISF_ETW_KEYWORD_PARSE),
            TraceLoggingString("ReadHeader", "Stage"),
            TraceLoggingHResult(_hr, "Hr"),
            TraceLoggingUInt32(grfMode, "Mode"),
            TraceLoggingUInt32(cbRead, "BytesRead"),
            TraceLoggingUInt32(headerLength, "HeaderBytes"),
            TraceLoggingUInt64(_dur, "DurationMs"));
        if (g_xisfPropertyHandlerTelemetryHook) {
            wchar_t _buf[384]; swprintf_s(_buf, L"PropertyStoreInitializeFailed Stage=ReadHeader Hr=0x%08X Mode=%u BytesRead=%u HeaderBytes=%u DurationMs=%llu", _hr, grfMode, cbRead, headerLength, _dur);
            g_xisfPropertyHandlerTelemetryHook(TRACE_LEVEL_WARNING, XISF_ETW_KEYWORD_LIFECYCLE | XISF_ETW_KEYWORD_PARSE, _buf);
        }}
        return E_FAIL;
    }

    std::string xml(xmlBuf.data(), headerLength);
    auto result = xisf::XISFParser::ParseXMLString(xml);
    if (!result.ok()) {
        { ULONGLONG _dur = GetTickCount64() - initStart;
        TraceLoggingWrite(g_hPropertyProvider, "PropertyStoreInitializeFailed",
            TraceLoggingLevel(TRACE_LEVEL_WARNING),
            TraceLoggingKeyword(XISF_ETW_KEYWORD_LIFECYCLE | XISF_ETW_KEYWORD_PARSE),
            TraceLoggingString("ParseXml", "Stage"),
            TraceLoggingHResult(E_FAIL, "Hr"),
            TraceLoggingUInt32(grfMode, "Mode"),
            TraceLoggingUInt32(headerLength, "HeaderBytes"),
            TraceLoggingUInt64(_dur, "DurationMs"));
        if (g_xisfPropertyHandlerTelemetryHook) {
            wchar_t _buf[384]; swprintf_s(_buf, L"PropertyStoreInitializeFailed Stage=ParseXml Hr=0x%08X Mode=%u HeaderBytes=%u DurationMs=%llu", E_FAIL, grfMode, headerLength, _dur);
            g_xisfPropertyHandlerTelemetryHook(TRACE_LEVEL_WARNING, XISF_ETW_KEYWORD_LIFECYCLE | XISF_ETW_KEYWORD_PARSE, _buf);
        }}
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
    if (g_xisfPropertyHandlerTelemetryHook) {
        wchar_t _buf[384]; swprintf_s(_buf, L"PropertyStoreMetadataParsed HeaderBytes=%u FitsKeywordCount=%u XisfPropertyCount=%u DurationMs=%llu", headerLength, _fitsCount, _propCount, _dur);
        g_xisfPropertyHandlerTelemetryHook(TRACE_LEVEL_INFORMATION, XISF_ETW_KEYWORD_PARSE | XISF_ETW_KEYWORD_PERF, _buf);
    }}

    bool catalogConfiguredThisCall = false;
    std::wstring catalogSource;
    std::wstring overridePath;
    UINT32 catalogPriorityCount = 0;
    UINT32 catalogEntryCount = 0;
    double matchToleranceDeg = s_matchToleranceDeg;

    std::call_once(*s_catalogOnceFlag, [this, &catalogConfiguredThisCall, &catalogSource, &overridePath, &catalogPriorityCount, &catalogEntryCount, &matchToleranceDeg]() {
        catalogConfiguredThisCall = true;
        catalogSource = L"none";
        overridePath = s_dsoDbPath;

        {
            wchar_t buf[512] = {};
            DWORD cb = sizeof(buf);
            LONG st = RegGetValueW(HKEY_CURRENT_USER, L"Software\\XISFPropertyHandler", L"CatalogPriority", RRF_RT_REG_SZ, nullptr, buf, &cb);
            if (st == ERROR_SUCCESS && buf[0] != L'\0') {
                int len = WideCharToMultiByte(CP_UTF8, 0, buf, -1, nullptr, 0, nullptr, nullptr);
                std::string prio(static_cast<size_t>(len - 1), '\0');
                WideCharToMultiByte(CP_UTF8, 0, buf, -1, prio.data(), len, nullptr, nullptr);
                s_catalogPriority = xisf::DSOCatalog::ParsePriorityString(prio);
            }
            else {
                s_catalogPriority = {"M", "C", "NGC", "IC", "Sh2", "B", "LBN"};
            }
        }
        {
            wchar_t buf[64] = {};
            DWORD cb = sizeof(buf);
            LONG st = RegGetValueW(HKEY_CURRENT_USER, L"Software\\XISFPropertyHandler", L"MatchToleranceDeg", RRF_RT_REG_SZ, nullptr, buf, &cb);
            if (st == ERROR_SUCCESS && buf[0] != L'\0') {
                double d = _wtof(buf);
                if (d > 0.0 && d <= 10.0) s_matchToleranceDeg = d;
            }
        }

        auto cat = std::make_shared<xisf::DSOCatalog>();

        // Catalog files live under %LOCALAPPDATA%\XISFShellExtension\catalogs\
        // and are acquired at runtime by the settings EXE
        // (XISFShellExtensionHost). They are not embedded in this DLL. If a
        // file is missing or fails to parse we continue without that catalog
        // — the handler degrades gracefully and simply omits DSO enrichment.
        auto loadLocalAppDataCsv = [](xisf::DSOCatalog& c, const wchar_t* fileName, bool append) -> bool {
            PWSTR pszBase = nullptr;
            if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &pszBase)) || !pszBase)
                return false;
            std::wstring wpath = std::wstring(pszBase) + L"\\XISFShellExtension\\catalogs\\" + fileName;
            CoTaskMemFree(pszBase);
            int len = WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (len <= 0) return false;
            std::string path(static_cast<size_t>(len - 1), '\0');
            WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, path.data(), len, nullptr, nullptr);
            return append ? c.AppendFromCSVFile(path) : c.LoadFromCSVFile(path);
        };

        bool anyLoaded = loadLocalAppDataCsv(*cat, L"NGC.csv", /*append*/ false);
        if (loadLocalAppDataCsv(*cat, L"addendum.csv", /*append*/ true)) anyLoaded = true;
        if (loadLocalAppDataCsv(*cat, L"sharpless.csv", /*append*/ true)) anyLoaded = true;
        catalogSource = anyLoaded ? L"localappdata-csv" : L"none";

        if (!s_dsoDbPath.empty()) {
            int len = WideCharToMultiByte(CP_UTF8, 0, s_dsoDbPath.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (len > 0) {
                std::string path(static_cast<size_t>(len - 1), '\0');
                WideCharToMultiByte(CP_UTF8, 0, s_dsoDbPath.c_str(), -1, path.data(), len, nullptr, nullptr);
                auto fileCat = std::make_shared<xisf::DSOCatalog>();
                if (fileCat->LoadFromCSVFile(path)) {
                    cat = fileCat;
                    catalogSource = L"file-override";
                }
                else {
                    catalogSource = anyLoaded ? L"localappdata-fallback" : L"none";
                }
            }
        }
        if (cat->Count() > 0) s_dsoCatalog = cat;
        catalogPriorityCount = static_cast<UINT32>(s_catalogPriority.size());
        catalogEntryCount = static_cast<UINT32>(cat->Count());
        matchToleranceDeg = s_matchToleranceDeg;
    });

    if (catalogConfiguredThisCall) {
        { ULONGLONG _dur = GetTickCount64() - initStart;
        TraceLoggingWrite(g_hPropertyProvider, "CatalogConfigured",
            TraceLoggingLevel(TRACE_LEVEL_INFORMATION),
            TraceLoggingKeyword(XISF_ETW_KEYWORD_CATALOG | XISF_ETW_KEYWORD_PERF),
            TraceLoggingWideString(catalogSource.c_str(), "Source"),
            TraceLoggingWideString(overridePath.empty() ? L"" : overridePath.c_str(), "OverridePath"),
            TraceLoggingUInt32(catalogPriorityCount, "PriorityCount"),
            TraceLoggingFloat64(matchToleranceDeg, "MatchToleranceDeg"),
            TraceLoggingUInt32(catalogEntryCount, "EntryCount"),
            TraceLoggingUInt64(_dur, "DurationMs"));
        if (g_xisfPropertyHandlerTelemetryHook) {
            wchar_t _buf[768]; swprintf_s(_buf, L"CatalogConfigured Source=%ls OverridePath=%ls PriorityCount=%u MatchToleranceDeg=%.3f EntryCount=%u DurationMs=%llu",
                catalogSource.c_str(), overridePath.empty() ? L"" : overridePath.c_str(), catalogPriorityCount, matchToleranceDeg, catalogEntryCount, _dur);
            g_xisfPropertyHandlerTelemetryHook(TRACE_LEVEL_INFORMATION, XISF_ETW_KEYWORD_CATALOG | XISF_ETW_KEYWORD_PERF, _buf);
        }}
    }

    if (!s_projectionChecked) {
        s_projectionEnabled = ReadProjectionEnabled();
        s_projectionChecked = true;
        { UINT32 _enabled = s_projectionEnabled ? 1u : 0u;
        TraceLoggingWrite(g_hPropertyProvider, "ProjectionSettingLoaded",
            TraceLoggingLevel(TRACE_LEVEL_INFORMATION),
            TraceLoggingKeyword(XISF_ETW_KEYWORD_PROJECTION),
            TraceLoggingBoolean(_enabled, "Enabled"));
        if (g_xisfPropertyHandlerTelemetryHook) {
            wchar_t _buf[128]; swprintf_s(_buf, L"ProjectionSettingLoaded Enabled=%u", _enabled);
            g_xisfPropertyHandlerTelemetryHook(TRACE_LEVEL_INFORMATION, XISF_ETW_KEYWORD_PROJECTION, _buf);
        }}
    }

    {
        std::lock_guard<std::mutex> lock(m_propertyLock);
        PopulateProperties();
    }
    m_initialized = true;

    // Store stream for lazy pixel stat computation
    m_pStream = pStream;
    m_pStream->AddRef();

    { UINT32 _propCount = static_cast<UINT32>(m_properties.size()); ULONGLONG _dur = GetTickCount64() - initStart;
    TraceLoggingWrite(g_hPropertyProvider, "PropertyStoreInitializeCompleted",
        TraceLoggingLevel(TRACE_LEVEL_INFORMATION),
        TraceLoggingKeyword(XISF_ETW_KEYWORD_LIFECYCLE | XISF_ETW_KEYWORD_PERF),
        TraceLoggingUInt32(_propCount, "PropertyCount"),
        TraceLoggingUInt64(_dur, "DurationMs"));
    if (g_xisfPropertyHandlerTelemetryHook) {
        wchar_t _buf[256]; swprintf_s(_buf, L"PropertyStoreInitializeCompleted PropertyCount=%u DurationMs=%llu", _propCount, _dur);
        g_xisfPropertyHandlerTelemetryHook(TRACE_LEVEL_INFORMATION, XISF_ETW_KEYWORD_LIFECYCLE | XISF_ETW_KEYWORD_PERF, _buf);
    }}
    return S_OK;
}

void CXISFPropertyHandler::PopulateProperties() {
    const ULONGLONG populateStart = GetTickCount64();
    // Exposure Time
    std::string expRaw = GetFITSOrProp("EXPTIME", "");
    if (expRaw.empty()) expRaw = GetFITSOrProp("EXPOSURE", "");
    double expTime = 0; bool hasExp = TryParseDouble(expRaw, &expTime);
    if (hasExp) AddDoubleProp(PKEY_XISF_ExposureTime, expTime);

    // Camera Model
    std::string cam = GetFITSOrProp("INSTRUME", "Instrument:Camera:Name");
    if (!cam.empty()) AddStringProp(PKEY_XISF_CameraModel, cam);

    // Focal Length (FITS in mm, Property in meters)
    std::string flRaw = m_metadata.getFITSValue("FOCALLEN");
    double focalLength = 0; bool hasFL = false;
    if (!flRaw.empty()) { hasFL = TryParseDouble(flRaw, &focalLength); }
    else {
        std::string flProp = m_metadata.getPropertyValue("Instrument:Telescope:FocalLength");
        double flM; if (TryParseDouble(flProp, &flM)) { focalLength = flM * 1000.0; hasFL = true; }
    }
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
    std::string gainRaw = GetFITSOrProp("GAIN", "Instrument:Camera:Gain");
    double gainD; if (TryParseDouble(gainRaw, &gainD)) AddUInt32Prop(PKEY_XISF_Gain, static_cast<uint32_t>(gainD));

    // Offset (UInt32)
    std::string offsetRaw = GetFITSOrProp("OFFSET", "Instrument:Camera:Offset");
    double offD; if (TryParseDouble(offsetRaw, &offD)) AddUInt32Prop(PKEY_XISF_Offset, static_cast<uint32_t>(offD));

    // Sensor Temperature
    std::string sensorT = GetFITSOrProp("CCD-TEMP", "Instrument:Sensor:Temperature");
    double sTemp; if (TryParseDouble(sensorT, &sTemp)) AddDoubleProp(PKEY_XISF_SensorTemperature, sTemp);

    // Telescope
    std::string tel = GetFITSOrProp("TELESCOP", "Instrument:Telescope:Name");
    if (!tel.empty()) AddStringProp(PKEY_XISF_Telescope, tel);

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
    std::string setT = GetFITSOrProp("SET-TEMP", "Instrument:Camera:SetTemperature");
    double setTemp; if (TryParseDouble(setT, &setTemp)) AddDoubleProp(PKEY_XISF_SetTemp, setTemp);

    // Pixel Size
    std::string pxRaw = GetFITSOrProp("XPIXSZ", "Instrument:Sensor:XPixelSize");
    double pxSize; if (TryParseDouble(pxRaw, &pxSize)) AddDoubleProp(PKEY_XISF_PixelSize, pxSize);

    // Readout Mode
    std::string rdMode = GetFITSOrProp("READOUTM", "Instrument:Camera:ReadoutMode");
    if (!rdMode.empty()) AddStringProp(PKEY_XISF_ReadoutMode, rdMode);

    // Bayer Pattern
    std::string bayer = GetFITSOrProp("BAYERPAT", "Instrument:Sensor:BayerPattern");
    if (!bayer.empty()) AddStringProp(PKEY_XISF_BayerPattern, bayer);

    // Site coordinates
    std::string latRaw = GetFITSOrProp("SITELAT", "Observation:Location:Latitude");
    double lat; if (TryParseDouble(latRaw, &lat)) AddDoubleProp(PKEY_XISF_SiteLatitude, lat);
    std::string lonRaw = GetFITSOrProp("SITELONG", "Observation:Location:Longitude");
    double lon; if (TryParseDouble(lonRaw, &lon)) AddDoubleProp(PKEY_XISF_SiteLongitude, lon);
    std::string elevRaw = GetFITSOrProp("SITEELEV", "Observation:Location:Elevation");
    double elev; if (TryParseDouble(elevRaw, &elev)) AddDoubleProp(PKEY_XISF_SiteElevation, elev);

    // Altitude, Azimuth, Airmass
    std::string altRaw = GetFITSOrProp("CENTALT", "Observation:Center:Alt");
    double alt; if (TryParseDouble(altRaw, &alt)) AddDoubleProp(PKEY_XISF_Altitude, alt);
    std::string azRaw = GetFITSOrProp("CENTAZ", "Observation:Center:Az");
    double az; if (TryParseDouble(azRaw, &az)) AddDoubleProp(PKEY_XISF_Azimuth, az);
    std::string amRaw = GetFITSOrProp("AIRMASS", "Observation:Airmass");
    double am; if (TryParseDouble(amRaw, &am)) AddDoubleProp(PKEY_XISF_Airmass, am);

    // Pier Side
    std::string pier = GetFITSOrProp("PIERSIDE", "Instrument:Telescope:PierSide");
    if (!pier.empty()) AddStringProp(PKEY_XISF_PierSide, pier);

    // Object RA/Dec (formatted strings for display)
    std::string objRA = GetFITSOrProp("OBJCTRA", "Observation:Object:RA");
    if (!objRA.empty()) AddStringProp(PKEY_XISF_ObjectRA, objRA);
    std::string objDec = GetFITSOrProp("OBJCTDEC", "Observation:Object:Dec");
    if (!objDec.empty()) AddStringProp(PKEY_XISF_ObjectDec, objDec);

    // Object RA/Dec as numeric degrees — prefer for computed fields.
    // Use the XISF property (already in degrees) rather than FITS HMS/DMS strings.
    double objRaDeg = 0, objDecDeg = 0;
    bool hasObjRA = TryParseDouble(m_metadata.getPropertyValue("Observation:Object:RA"), &objRaDeg);
    bool hasObjDec = TryParseDouble(m_metadata.getPropertyValue("Observation:Object:Dec"), &objDecDeg);

    // Rotation
    std::string rotRaw = GetFITSOrProp("ROTATANG", "Instrument:Rotator:Angle");
    double rot; if (TryParseDouble(rotRaw, &rot)) AddDoubleProp(PKEY_XISF_Rotation, rot);

    // Focuser
    std::string focName = GetFITSOrProp("FOCNAME", "Instrument:Focuser:Name");
    if (!focName.empty()) AddStringProp(PKEY_XISF_FocuserName, focName);
    std::string focPos = GetFITSOrProp("FOCPOS", "Instrument:Focuser:Position");
    double fpD; if (TryParseDouble(focPos, &fpD)) AddUInt32Prop(PKEY_XISF_FocuserPosition, static_cast<uint32_t>(fpD));
    std::string focTemp = GetFITSOrProp("FOCTEMP", "Instrument:Focuser:Temperature");
    double ft; if (TryParseDouble(focTemp, &ft)) AddDoubleProp(PKEY_XISF_FocuserTemp, ft);

    // Rotator
    std::string rotName = GetFITSOrProp("ROTNAME", "Instrument:Rotator:Name");
    if (!rotName.empty()) AddStringProp(PKEY_XISF_RotatorName, rotName);
    std::string rotAng = GetFITSOrProp("ROTATOR", "Instrument:Rotator:MechanicalAngle");
    double ra2; if (TryParseDouble(rotAng, &ra2)) AddDoubleProp(PKEY_XISF_RotatorAngle, ra2);

    // Filter Wheel
    std::string fw = GetFITSOrProp("FWHEEL", "Instrument:FilterWheel:Name");
    if (!fw.empty()) AddStringProp(PKEY_XISF_FilterWheel, fw);

    // Weather
    std::string dpRaw = GetFITSOrProp("DEWPOINT", "Weather:DewPoint");
    double dp; if (TryParseDouble(dpRaw, &dp)) AddDoubleProp(PKEY_XISF_DewPoint, dp);
    std::string humRaw = GetFITSOrProp("HUMIDITY", "Weather:Humidity");
    double hum; if (TryParseDouble(humRaw, &hum)) AddDoubleProp(PKEY_XISF_Humidity, hum);
    std::string ambRaw = GetFITSOrProp("AMBTEMP", "Weather:Temperature");
    double ambT; if (TryParseDouble(ambRaw, &ambT)) AddDoubleProp(PKEY_XISF_AmbientTemp, ambT);

    // Weather — extended (seeing monitor, sky quality, atmospheric)
    std::string starFwhmRaw = GetFITSOrProp("STARFWHM", "Weather:StarFWHM");
    double starFwhm; if (TryParseDouble(starFwhmRaw, &starFwhm)) AddDoubleProp(PKEY_XISF_StarFWHM, starFwhm);
    std::string sqmRaw = GetFITSOrProp("MPSAS", "Weather:SkyQuality");
    double sqm; if (TryParseDouble(sqmRaw, &sqm)) AddDoubleProp(PKEY_XISF_SkyQuality, sqm);
    std::string skyBrtRaw = GetFITSOrProp("SKYBRGHT", "Weather:SkyBrightness");
    double skyBrt; if (TryParseDouble(skyBrtRaw, &skyBrt)) AddDoubleProp(PKEY_XISF_SkyBrightness, skyBrt);
    std::string cloudRaw = GetFITSOrProp("CLOUDCVR", "Weather:CloudCover");
    double cloud; if (TryParseDouble(cloudRaw, &cloud)) AddDoubleProp(PKEY_XISF_CloudCover, cloud);
    std::string pressRaw = GetFITSOrProp("PRESSURE", "Weather:Pressure");
    double press; if (TryParseDouble(pressRaw, &press)) AddDoubleProp(PKEY_XISF_Pressure, press);
    std::string skyTempRaw = GetFITSOrProp("SKYTEMP", "Weather:SkyTemperature");
    double skyTemp; if (TryParseDouble(skyTempRaw, &skyTemp)) AddDoubleProp(PKEY_XISF_SkyTemp, skyTemp);
    std::string windRaw = GetFITSOrProp("WINDSPD", "Weather:WindSpeed");
    double wind; if (TryParseDouble(windRaw, &wind)) AddDoubleProp(PKEY_XISF_WindSpeed, wind);

    // Guiding RMS
    std::string guideRARaw = GetFITSOrProp("GUIDERA", "Guider:RMS:RA");
    double guideRA; if (TryParseDouble(guideRARaw, &guideRA)) AddDoubleProp(PKEY_XISF_GuideRA, guideRA);
    std::string guideDecRaw = GetFITSOrProp("GUIDEDEC", "Guider:RMS:Dec");
    double guideDec; if (TryParseDouble(guideDecRaw, &guideDec)) AddDoubleProp(PKEY_XISF_GuideDec, guideDec);

    // Date Local
    std::string dateLocal = GetFITSOrProp("DATE-LOC", "Observation:Time:Local");
    if (!dateLocal.empty()) AddDateTimeProp(PKEY_XISF_DateLocal, dateLocal);

    // For computed properties, prefer object coords over telescope center
    double computedRA = hasObjRA ? objRaDeg : raDeg;
    double computedDec = hasObjDec ? objDecDeg : decDeg;
    bool hasComputedCoords = (hasObjRA && hasObjDec) || (hasRA && hasDec);
    bool usingObjectCoords = hasObjRA && hasObjDec;

    if (!hasComputedCoords) {
        { UINT32 _hasObj = usingObjectCoords ? 1u : 0u; UINT32 _hasCenter = (hasRA && hasDec) ? 1u : 0u;
        TraceLoggingWrite(g_hPropertyProvider, "PropertyCoordinatesUnavailable",
            TraceLoggingLevel(TRACE_LEVEL_WARNING),
            TraceLoggingKeyword(XISF_ETW_KEYWORD_PARSE),
            TraceLoggingBoolean(_hasObj, "HasObjectCoords"),
            TraceLoggingBoolean(_hasCenter, "HasCenterCoords"));
        if (g_xisfPropertyHandlerTelemetryHook) {
            wchar_t _buf[256]; swprintf_s(_buf, L"PropertyCoordinatesUnavailable HasObjectCoords=%u HasCenterCoords=%u", _hasObj, _hasCenter);
            g_xisfPropertyHandlerTelemetryHook(TRACE_LEVEL_WARNING, XISF_ETW_KEYWORD_PARSE, _buf);
        }}
    }
    else {
        { UINT32 _usingObj = usingObjectCoords ? 1u : 0u;
        TraceLoggingWrite(g_hPropertyProvider, "PropertyCoordinatesResolved",
            TraceLoggingLevel(TRACE_LEVEL_INFORMATION),
            TraceLoggingKeyword(XISF_ETW_KEYWORD_PARSE),
            TraceLoggingBoolean(_usingObj, "UsingObjectCoords"),
            TraceLoggingFloat64(computedRA, "ComputedRA"),
            TraceLoggingFloat64(computedDec, "ComputedDec"));
        if (g_xisfPropertyHandlerTelemetryHook) {
            wchar_t _buf[256]; swprintf_s(_buf, L"PropertyCoordinatesResolved UsingObjectCoords=%u ComputedRA=%.6f ComputedDec=%.6f", _usingObj, computedRA, computedDec);
            g_xisfPropertyHandlerTelemetryHook(TRACE_LEVEL_INFORMATION, XISF_ETW_KEYWORD_PARSE, _buf);
        }}
    }

    // Computed: RA Hour band
    if (hasObjRA || hasRA) AddStringProp(PKEY_XISF_RAHour, ComputeRAHour(computedRA));

    // Computed: Dec Band
    if (hasObjDec || hasDec) AddStringProp(PKEY_XISF_DecBand, ComputeDecBand(computedDec));

    // Computed: Constellation from RA/Dec
    std::string constellation;
    if (hasComputedCoords) {
        constellation = xisf::ConstellationDB::Identify(computedRA, computedDec);
        if (!constellation.empty()) {
            std::string fullName = xisf::ConstellationDB::FullName(constellation);
            AddStringProp(PKEY_XISF_Constellation, fullName);
        }
    }

    // Compute FOV-based cone search radius from sensor geometry when available
    double coneRadiusDeg = s_matchToleranceDeg;
    bool geometryFallbackUsed = false;
    if (hasFL && focalLength > 0.0) {
        double pixSizeUm = 0;
        bool hasPx = TryParseDouble(pxRaw, &pixSizeUm) && pixSizeUm > 0.0;
        if (hasPx) {
            double nax1 = 0, nax2 = 0;
            TryParseDouble(m_metadata.getFITSValue("NAXIS1"), &nax1);
            TryParseDouble(m_metadata.getFITSValue("NAXIS2"), &nax2);
            // Fallback: parse image dimensions from XISF geometry attribute
            if ((nax1 <= 0.0 || nax2 <= 0.0) && !m_metadata.xmlHeader.empty()) {
                auto gp = m_metadata.xmlHeader.find("geometry=\"");
                if (gp != std::string::npos) {
                    gp += 10; // skip geometry="
                    auto ge = m_metadata.xmlHeader.find('"', gp);
                    if (ge != std::string::npos) {
                        std::string geom = m_metadata.xmlHeader.substr(gp, ge - gp);
                        // Format: "W:H:C"
                        auto c1 = geom.find(':');
                        if (c1 != std::string::npos) {
                            auto c2 = geom.find(':', c1 + 1);
                            if (c2 != std::string::npos) {
                                TryParseDouble(geom.substr(0, c1), &nax1);
                                TryParseDouble(geom.substr(c1 + 1, c2 - c1 - 1), &nax2);
                                geometryFallbackUsed = (nax1 > 0.0 && nax2 > 0.0);
                            }
                        }
                    }
                }
            }
            if (nax1 > 0.0 && nax2 > 0.0) {
                double diagPx = std::sqrt(nax1 * nax1 + nax2 * nax2);
                double fovDeg = (diagPx * pixSizeUm) / (focalLength * 1000.0) * (180.0 / 3.14159265358979323846);
                coneRadiusDeg = fovDeg / 2.0;
            }
        }
    }

    if (geometryFallbackUsed) {
        TraceLoggingWrite(g_hPropertyProvider, "GeometryFallbackUsed",
            TraceLoggingLevel(TRACE_LEVEL_INFORMATION),
            TraceLoggingKeyword(XISF_ETW_KEYWORD_PARSE | XISF_ETW_KEYWORD_PERF),
            TraceLoggingFloat64(coneRadiusDeg, "ConeRadiusDeg"));
        if (g_xisfPropertyHandlerTelemetryHook) {
            wchar_t _buf[128]; swprintf_s(_buf, L"GeometryFallbackUsed ConeRadiusDeg=%.6f", coneRadiusDeg);
            g_xisfPropertyHandlerTelemetryHook(TRACE_LEVEL_INFORMATION, XISF_ETW_KEYWORD_PARSE | XISF_ETW_KEYWORD_PERF, _buf);
        }
    }

    // Computed: Matched Objects via cone search (single search, reused for keywords below)
    std::string matchedObjectsStr;
    decltype(s_dsoCatalog->ConeSearch(computedRA, computedDec, coneRadiusDeg)) coneResults;
    if (hasComputedCoords && s_dsoCatalog) {
        coneResults = s_dsoCatalog->ConeSearch(computedRA, computedDec, coneRadiusDeg);
        if (!coneResults.empty()) {
            // Cap at 10 most relevant (nearest) results
            size_t limit = (std::min)(coneResults.size(), static_cast<size_t>(10));
            std::vector<std::string> matchedNames;
            matchedNames.reserve(limit);
            for (size_t j = 0; j < limit; ++j) {
                const auto& entry = s_dsoCatalog->GetEntry(coneResults[j].entryIndex);
                std::string name = s_dsoCatalog->GetPreferredName(entry, s_catalogPriority);
                matchedNames.push_back(name);
            }
            // Build semicolon-separated display string
            for (size_t i = 0; i < matchedNames.size(); ++i) {
                if (i > 0) matchedObjectsStr += "; ";
                matchedObjectsStr += matchedNames[i];
            }
            AddStringProp(PKEY_XISF_MatchedObjects, matchedObjectsStr);
        }

        { UINT32 _usingObj = usingObjectCoords ? 1u : 0u; UINT32 _matchCount = static_cast<UINT32>(coneResults.size());
        TraceLoggingWrite(g_hPropertyProvider, "ConeSearchCompleted",
            TraceLoggingLevel(TRACE_LEVEL_INFORMATION),
            TraceLoggingKeyword(XISF_ETW_KEYWORD_CATALOG | XISF_ETW_KEYWORD_PERF),
            TraceLoggingBoolean(_usingObj, "UsingObjectCoords"),
            TraceLoggingUInt32(_matchCount, "MatchCount"),
            TraceLoggingFloat64(coneRadiusDeg, "ConeRadiusDeg"));
        if (g_xisfPropertyHandlerTelemetryHook) {
            wchar_t _buf[256]; swprintf_s(_buf, L"ConeSearchCompleted UsingObjectCoords=%u MatchCount=%u ConeRadiusDeg=%.6f", _usingObj, _matchCount, coneRadiusDeg);
            g_xisfPropertyHandlerTelemetryHook(TRACE_LEVEL_INFORMATION, XISF_ETW_KEYWORD_CATALOG | XISF_ETW_KEYWORD_PERF, _buf);
        }}
    }

    // Data State (Linear / Non-Linear) derived from Image element attributes
    {
        auto sfIt = m_metadata.imageAttributes.find("sampleFormat");
        auto csIt = m_metadata.imageAttributes.find("colorSpace");
        std::string sampleFormat = (sfIt != m_metadata.imageAttributes.end()) ? sfIt->second : "";
        std::string colorSpace   = (csIt != m_metadata.imageAttributes.end()) ? csIt->second : "";

        if (!sampleFormat.empty() || !colorSpace.empty()) {
            bool isFloat32 = (sampleFormat == "Float32");
            bool isFloat64 = (sampleFormat == "Float64");
            bool isUInt8   = (sampleFormat == "UInt8");

            bool isLinear = (isFloat32 || isFloat64);
            if (colorSpace == "Gray" || colorSpace == "RGB") isLinear = true;
            if (colorSpace == "GraySRGB" || colorSpace == "RGBSRGB") isLinear = false;
            if (isUInt8) isLinear = false;

            AddStringProp(PKEY_XISF_DataState, isLinear ? "Linear" : "Non-Linear");
        }
    }

    // Pixel stat placeholders — computed lazily on first GetValue() request
    {
        PropertyEntry pe;
        pe.key = PKEY_XISF_Median;
        pe.value.vt = VT_EMPTY;
        m_properties.push_back(std::move(pe));
    }
    {
        PropertyEntry pe;
        pe.key = PKEY_XISF_Mean;
        pe.value.vt = VT_EMPTY;
        m_properties.push_back(std::move(pe));
    }
    {
        PropertyEntry pe;
        pe.key = PKEY_XISF_ClippingLow;
        pe.value.vt = VT_EMPTY;
        m_properties.push_back(std::move(pe));
    }
    {
        PropertyEntry pe;
        pe.key = PKEY_XISF_ClippingHigh;
        pe.value.vt = VT_EMPTY;
        m_properties.push_back(std::move(pe));
    }

    // System.Photo projection (registry-controlled)
    UINT32 projectionPropCount = 0;
    if (s_projectionEnabled) {
        if (hasExp) { AddDoubleProp(PKEY_Photo_ExposureTime, expTime); ++projectionPropCount; }
        if (!cam.empty()) { AddStringProp(PKEY_Photo_CameraModel, cam); ++projectionPropCount; }
        if (hasFL) { AddDoubleProp(PKEY_Photo_FocalLength, focalLength); ++projectionPropCount; }
        if (hasFN) { AddDoubleProp(PKEY_Photo_FNumber, fNumber); ++projectionPropCount; }
    }

    { UINT32 _enabled = s_projectionEnabled ? 1u : 0u;
    TraceLoggingWrite(g_hPropertyProvider, "ProjectionEvaluated",
        TraceLoggingLevel(TRACE_LEVEL_INFORMATION),
        TraceLoggingKeyword(XISF_ETW_KEYWORD_PROJECTION),
        TraceLoggingBoolean(_enabled, "Enabled"),
        TraceLoggingUInt32(projectionPropCount, "ProjectedPropertyCount"));
    if (g_xisfPropertyHandlerTelemetryHook) {
        wchar_t _buf[128]; swprintf_s(_buf, L"ProjectionEvaluated Enabled=%u ProjectedPropertyCount=%u", _enabled, projectionPropCount);
        g_xisfPropertyHandlerTelemetryHook(TRACE_LEVEL_INFORMATION, XISF_ETW_KEYWORD_PROJECTION, _buf);
    }}

    // DSO aliases + System.Keywords
    std::vector<std::string> keywords;
    if (!filter.empty()) keywords.push_back(filter);
    if (!imgType.empty()) keywords.push_back(imgType);
    if (!constellation.empty()) keywords.push_back(xisf::ConstellationDB::FullName(constellation));
    if (!objName.empty() && s_dsoCatalog) {
        auto allNames = s_dsoCatalog->GetAllNames(objName);
        for (const auto& n : allNames) keywords.push_back(n);
    }
    // Also add matched object names/aliases for search (reuse earlier cone search)
    if (s_dsoCatalog) {
        for (const auto& r : coneResults) {
            const auto& entry = s_dsoCatalog->GetEntry(r.entryIndex);
            auto names = s_dsoCatalog->GetAllNames(entry);
            for (const auto& n : names) keywords.push_back(n);
        }
    }
    // Deduplicate keywords
    if (!keywords.empty()) {
        std::sort(keywords.begin(), keywords.end());
        keywords.erase(std::unique(keywords.begin(), keywords.end()), keywords.end());
        AddStringListProp(PKEY_Keywords, keywords);
    }

    if (hasComputedCoords && !s_dsoCatalog) {
        TraceLoggingWrite(g_hPropertyProvider, "CatalogUnavailableForConeSearch",
            TraceLoggingLevel(TRACE_LEVEL_WARNING),
            TraceLoggingKeyword(XISF_ETW_KEYWORD_CATALOG),
            TraceLoggingFloat64(computedRA, "ComputedRA"),
            TraceLoggingFloat64(computedDec, "ComputedDec"));
        if (g_xisfPropertyHandlerTelemetryHook) {
            wchar_t _buf[256]; swprintf_s(_buf, L"CatalogUnavailableForConeSearch ComputedRA=%.6f ComputedDec=%.6f", computedRA, computedDec);
            g_xisfPropertyHandlerTelemetryHook(TRACE_LEVEL_WARNING, XISF_ETW_KEYWORD_CATALOG, _buf);
        }
    }

    { UINT32 _fitsCount = static_cast<UINT32>(m_metadata.fitsKeywords.size());
      UINT32 _xisfPropCount = static_cast<UINT32>(m_metadata.properties.size());
      UINT32 _propCount = static_cast<UINT32>(m_properties.size());
      UINT32 _matchCount = static_cast<UINT32>(coneResults.size());
      UINT32 _kwCount = static_cast<UINT32>(keywords.size());
      UINT32 _hasCoords = hasComputedCoords ? 1u : 0u;
      UINT32 _usedObj = (hasObjRA && hasObjDec) ? 1u : 0u;
      UINT32 _projEnabled = s_projectionEnabled ? 1u : 0u;
      ULONGLONG _dur = GetTickCount64() - populateStart;
    TraceLoggingWrite(g_hPropertyProvider, "PropertyPopulation",
        TraceLoggingLevel(TRACE_LEVEL_INFORMATION),
        TraceLoggingKeyword(XISF_ETW_KEYWORD_PARSE | XISF_ETW_KEYWORD_CATALOG | XISF_ETW_KEYWORD_PROJECTION | XISF_ETW_KEYWORD_PERF),
        TraceLoggingUInt32(_fitsCount, "FitsKeywordCount"),
        TraceLoggingUInt32(_xisfPropCount, "XisfPropertyCount"),
        TraceLoggingUInt32(_propCount, "PropertyCount"),
        TraceLoggingUInt32(_matchCount, "MatchedObjectCount"),
        TraceLoggingUInt32(_kwCount, "KeywordCount"),
        TraceLoggingUInt32(projectionPropCount, "ProjectedPhotoPropertyCount"),
        TraceLoggingBoolean(_hasCoords, "HasComputedCoords"),
        TraceLoggingBoolean(_usedObj, "UsedObjectCoordinates"),
        TraceLoggingBoolean(_projEnabled, "ProjectionEnabled"),
        TraceLoggingFloat64(coneRadiusDeg, "ConeRadiusDeg"),
        TraceLoggingUInt64(_dur, "DurationMs"));
    if (g_xisfPropertyHandlerTelemetryHook) {
        wchar_t _buf[768]; swprintf_s(_buf, L"PropertyPopulation FitsKeywordCount=%u XisfPropertyCount=%u PropertyCount=%u MatchedObjectCount=%u KeywordCount=%u ProjectedPhotoPropertyCount=%u HasComputedCoords=%u UsedObjectCoordinates=%u ProjectionEnabled=%u ConeRadiusDeg=%.6f DurationMs=%llu",
            _fitsCount, _xisfPropCount, _propCount, _matchCount, _kwCount, projectionPropCount, _hasCoords, _usedObj, _projEnabled, coneRadiusDeg, _dur);
        g_xisfPropertyHandlerTelemetryHook(TRACE_LEVEL_INFORMATION, XISF_ETW_KEYWORD_PARSE | XISF_ETW_KEYWORD_CATALOG | XISF_ETW_KEYWORD_PROJECTION | XISF_ETW_KEYWORD_PERF, _buf);
    }}
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

    // Trigger lazy pixel stats computation on first request
    if (IsPixelStatKey(key) && m_pixelStatsState == PixelStatsState::NotStarted) {
        ComputePixelStats();
    }

    for (const auto& pe : m_properties) {
        if (IsEqualPropertyKey(pe.key, key)) return PropVariantCopy(pPropVar, &pe.value);
    }
    return S_OK;
}

bool CXISFPropertyHandler::IsPixelStatKey(REFPROPERTYKEY key) {
    return IsEqualPropertyKey(key, PKEY_XISF_Median) ||
           IsEqualPropertyKey(key, PKEY_XISF_Mean) ||
           IsEqualPropertyKey(key, PKEY_XISF_ClippingLow) ||
           IsEqualPropertyKey(key, PKEY_XISF_ClippingHigh);
}

void CXISFPropertyHandler::ComputePixelStats() {
    const ULONGLONG statsStart = GetTickCount64();

    if (!m_pStream || m_metadata.xmlHeader.empty()) {
        m_pixelStatsState = PixelStatsState::Unavailable;
        return;
    }

    const std::string& xml = m_metadata.xmlHeader;

    // Local attribute parser
    auto findAttr = [](const std::string& elemText, const std::string& attr) -> std::string {
        size_t p = 0;
        while (p < elemText.size()) {
            size_t ap = elemText.find(attr, p);
            if (ap == std::string::npos) break;
            if (ap > 0) {
                char prev = elemText[ap - 1];
                if (prev != ' ' && prev != '\t' && prev != '\n' && prev != '\r')
                { p = ap + attr.size(); continue; }
            }
            size_t eq = ap + attr.size();
            while (eq < elemText.size() && (elemText[eq]==' '||elemText[eq]=='\t')) ++eq;
            if (eq >= elemText.size() || elemText[eq] != '=') { p = eq; continue; }
            ++eq;
            while (eq < elemText.size() && (elemText[eq]==' '||elemText[eq]=='\t')) ++eq;
            if (eq >= elemText.size()) break;
            char q = elemText[eq]; if (q!='"' && q!='\'') { p=eq; continue; }
            ++eq;
            size_t end = elemText.find(q, eq);
            if (end == std::string::npos) break;
            return elemText.substr(eq, end - eq);
        }
        return {};
    };

    // Scan for Image elements with attachments
    struct ImageCandidate {
        std::string elemText;
        ULONGLONG attachSize;
        bool isThumbnail;
    };
    std::vector<ImageCandidate> candidates;

    size_t searchPos = 0;
    while (true) {
        size_t imgStart = xml.find("<Image", searchPos);
        if (imgStart == std::string::npos) break;
        size_t imgEnd = xml.find('>', imgStart);
        if (imgEnd == std::string::npos) break;
        searchPos = imgEnd + 1;

        std::string elem = xml.substr(imgStart + 6, imgEnd - imgStart - 6);
        std::string loc = findAttr(elem, "location");
        if (loc.compare(0, 11, "attachment:") != 0) continue;

        std::string id = findAttr(elem, "id");
        bool isThumb = (id == "thumbnail" || id == "Thumbnail");

        ULONGLONG aSize = 0;
        {
            const char* lp = loc.c_str() + 11;
            char* ep = nullptr;
            std::strtoull(lp, &ep, 10);
            if (ep && *ep == ':')
                aSize = std::strtoull(ep + 1, nullptr, 10);
        }
        if (aSize == 0) continue;
        candidates.push_back({std::move(elem), aSize, isThumb});
    }

    if (candidates.empty()) {
        m_pixelStatsState = PixelStatsState::Unavailable;
        WritePropertyHandlerTelemetry(TRACE_LEVEL_INFORMATION, XISF_ETW_KEYWORD_PERF,
            L"PixelStatsUnavailable Reason=NoAttachmentImage");
        return;
    }

    // Pick largest non-thumbnail image
    size_t bestIdx = 0;
    for (size_t i = 0; i < candidates.size(); ++i) {
        if (candidates[i].isThumbnail) continue;
        if (candidates[i].attachSize > candidates[bestIdx].attachSize || candidates[bestIdx].isThumbnail)
            bestIdx = i;
    }
    const std::string& imgElem = candidates[bestIdx].elemText;

    std::string geometry = findAttr(imgElem, "geometry");
    std::string location = findAttr(imgElem, "location");
    std::string sampleFmt = findAttr(imgElem, "sampleFormat");

    if (geometry.empty() || location.empty()) {
        m_pixelStatsState = PixelStatsState::Unavailable;
        WritePropertyHandlerTelemetry(TRACE_LEVEL_INFORMATION, XISF_ETW_KEYWORD_PERF,
            L"PixelStatsUnavailable Reason=MissingGeometryOrLocation");
        return;
    }

    // Parse geometry
    UINT imgW = 0, imgH = 0, imgC = 1;
    {
        char* ep = nullptr;
        imgW = static_cast<UINT>(std::strtoul(geometry.c_str(), &ep, 10));
        if (ep && *ep == ':') {
            imgH = static_cast<UINT>(std::strtoul(ep + 1, &ep, 10));
            if (ep && *ep == ':')
                imgC = static_cast<UINT>(std::strtoul(ep + 1, nullptr, 10));
        }
    }
    if (imgW == 0 || imgH == 0) {
        m_pixelStatsState = PixelStatsState::Unavailable;
        return;
    }

    // Parse location offset/size
    ULONGLONG offset = 0, attachSize = 0;
    if (location.compare(0, 11, "attachment:") == 0) {
        const char* lp = location.c_str() + 11;
        char* ep = nullptr;
        offset = std::strtoull(lp, &ep, 10);
        if (ep && *ep == ':')
            attachSize = std::strtoull(ep + 1, nullptr, 10);
    }
    if (attachSize == 0) {
        m_pixelStatsState = PixelStatsState::Unavailable;
        return;
    }

    // Sample format
    bool isUInt16 = (sampleFmt == "UInt16" || sampleFmt.empty());
    bool isUInt8 = (sampleFmt == "UInt8");
    bool isFloat32 = (sampleFmt == "Float32");
    bool isFloat64 = (sampleFmt == "Float64");
    size_t bps = isUInt8 ? 1 : isFloat32 ? 4 : isFloat64 ? 8 : 2;

    UINT readChannels = (imgC >= 3) ? 3 : 1;
    size_t channelPixels = static_cast<size_t>(imgW) * imgH;

    // Subsample strategy: read every Nth row, target ~1024 rows
    UINT sampleRows = (std::min)(imgH, 1024u);
    UINT rowStride = (std::max)(1u, imgH / sampleRows);

    // Collect normalized pixel samples (pooled across all channels)
    size_t maxSamples = static_cast<size_t>(imgW) * sampleRows * readChannels;
    UINT sampleCols = imgW;
    UINT colStride = 1;
    if (maxSamples > 4 * 1024 * 1024) {
        sampleCols = (std::min)(imgW, 4096u);
        colStride = (std::max)(1u, imgW / sampleCols);
        maxSamples = static_cast<size_t>(sampleCols) * sampleRows * readChannels;
    }

    std::vector<float> samples;
    samples.reserve(maxSamples);

    size_t rowBytes = static_cast<size_t>(imgW) * bps;
    std::vector<uint8_t> rowBuf(rowBytes);

    size_t clipLow = 0, clipHigh = 0;
    double runningSum = 0.0;

    constexpr float kClipLowThresh = 0.001f;
    constexpr float kClipHighThresh = 0.999f;

    for (UINT ch = 0; ch < readChannels; ++ch) {
        ULONGLONG chBase = offset + static_cast<ULONGLONG>(ch) * channelPixels * bps;

        for (UINT row = 0; row < imgH; row += rowStride) {
            LARGE_INTEGER seekPos;
            seekPos.QuadPart = static_cast<LONGLONG>(chBase + static_cast<ULONGLONG>(row) * rowBytes);
            if (FAILED(m_pStream->Seek(seekPos, STREAM_SEEK_SET, nullptr))) {
                m_pixelStatsState = PixelStatsState::Unavailable;
                WritePropertyHandlerTelemetry(TRACE_LEVEL_WARNING, XISF_ETW_KEYWORD_PERF,
                    L"PixelStatsUnavailable Reason=SeekFailed Channel=%u Row=%u", ch, row);
                return;
            }

            ULONG cbRead = 0;
            if (FAILED(m_pStream->Read(rowBuf.data(), static_cast<ULONG>(rowBytes), &cbRead)) || cbRead < rowBytes) {
                m_pixelStatsState = PixelStatsState::Unavailable;
                return;
            }

            for (UINT col = 0; col < imgW; col += colStride) {
                float val = 0.0f;
                if (isUInt16) {
                    uint16_t raw = *reinterpret_cast<const uint16_t*>(rowBuf.data() + col * 2);
                    val = static_cast<float>(raw) / 65535.0f;
                } else if (isUInt8) {
                    val = static_cast<float>(rowBuf[col]) / 255.0f;
                } else if (isFloat32) {
                    val = *reinterpret_cast<const float*>(rowBuf.data() + col * 4);
                    val = (std::max)(0.0f, (std::min)(1.0f, val));
                } else if (isFloat64) {
                    double d = *reinterpret_cast<const double*>(rowBuf.data() + col * 8);
                    val = static_cast<float>((std::max)(0.0, (std::min)(1.0, d)));
                }

                samples.push_back(val);
                runningSum += val;
                if (val <= kClipLowThresh) ++clipLow;
                if (val >= kClipHighThresh) ++clipHigh;
            }
        }
    }

    if (samples.empty()) {
        m_pixelStatsState = PixelStatsState::Unavailable;
        return;
    }

    // Compute stats
    double mean = runningSum / samples.size();

    // Median via nth_element (O(n))
    size_t midIdx = samples.size() / 2;
    std::nth_element(samples.begin(), samples.begin() + midIdx, samples.end());
    double median = samples[midIdx];

    double clipLowPct = 100.0 * clipLow / samples.size();
    double clipHighPct = 100.0 * clipHigh / samples.size();

    // Update the VT_EMPTY placeholder entries
    for (auto& pe : m_properties) {
        if (IsEqualPropertyKey(pe.key, PKEY_XISF_Median)) {
            PropVariantClear(&pe.value);
            InitPropVariantFromDouble(median, &pe.value);
        } else if (IsEqualPropertyKey(pe.key, PKEY_XISF_Mean)) {
            PropVariantClear(&pe.value);
            InitPropVariantFromDouble(mean, &pe.value);
        } else if (IsEqualPropertyKey(pe.key, PKEY_XISF_ClippingLow)) {
            PropVariantClear(&pe.value);
            InitPropVariantFromDouble(clipLowPct, &pe.value);
        } else if (IsEqualPropertyKey(pe.key, PKEY_XISF_ClippingHigh)) {
            PropVariantClear(&pe.value);
            InitPropVariantFromDouble(clipHighPct, &pe.value);
        }
    }

    m_pixelStatsState = PixelStatsState::Computed;

    ULONGLONG statsDur = GetTickCount64() - statsStart;
    WritePropertyHandlerTelemetry(TRACE_LEVEL_INFORMATION, XISF_ETW_KEYWORD_PERF,
        L"PixelStatsComputed Median=%.4f Mean=%.4f ClipLow=%.2f%% ClipHigh=%.2f%% Samples=%zu DurationMs=%llu",
        median, mean, clipLowPct, clipHighPct, samples.size(), statsDur);
}

IFACEMETHODIMP CXISFPropertyHandler::SetValue(REFPROPERTYKEY, REFPROPVARIANT) {
    return STG_E_ACCESSDENIED;
}

IFACEMETHODIMP CXISFPropertyHandler::Commit() {
    return STG_E_ACCESSDENIED;
}

IFACEMETHODIMP CXISFPropertyHandler::IsPropertyWritable(REFPROPERTYKEY) {
    return S_FALSE;
}
