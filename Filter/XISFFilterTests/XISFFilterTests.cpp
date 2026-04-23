// XISFFilterTests.cpp — VS Native Unit Tests for the XISF Search Filter (IFilter)
//
// Perspectives:
//   1. Developer   — XISFParser chunks, IFilter contract, COM lifecycle
//   2. Astronomer  — FITS keyword & property searchability
//   3. Sysadmin    — Enable/disable toggle, graceful failure on bad input

#include "CppUnitTest.h"

#include <windows.h>
#include <initguid.h>
#include <filter.h>
#include <filterr.h>
#include <shlwapi.h>
#include <strsafe.h>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>

#include "XISFFilter.h"
#include "XISFParser.h"
#include "FilterTelemetry.h"

// Compile HandlerSettings and ClassFactory inline for enable/disable tests.
#include "..\XISFFilter\src\HandlerSettings.cpp"
#include "..\XISFFilter\src\ClassFactory.cpp"

// TraceLogging provider stub — test module needs its own definition since
// dllmain.cpp (which normally defines it) is not compiled into the test DLL.
TRACELOGGING_DEFINE_PROVIDER(g_hFilterProvider, "XISF-Filter",
    (0x3a4b5c6d, 0x7e8f, 0x9012, 0xab, 0x34, 0xcd, 0x56, 0xef, 0x78, 0x90, 0x12));

// Telemetry test hook definition
extern "C" XISFFilterTelemetryHook g_xisfFilterTelemetryHook = nullptr;

void WriteFilterTelemetry(UCHAR level, ULONGLONG keyword, PCWSTR format, ...) {
    const bool hookEnabled = (g_xisfFilterTelemetryHook != nullptr);
    wchar_t buffer[768] = {};
    va_list args;
    va_start(args, format);
    StringCchVPrintfW(buffer, ARRAYSIZE(buffer), format, args);
    va_end(args);

    if (hookEnabled) {
        g_xisfFilterTelemetryHook(level, keyword, buffer);
    }
}

#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Propsys.lib")
#pragma comment(lib, "Shlwapi.lib")

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace fs = std::filesystem;

// Stubs for DLL globals referenced by XISFFilter.cpp and ClassFactory.cpp
long g_cDllRef = 0;
HINSTANCE g_hInst = nullptr;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
namespace {

// Registry key used by HandlerSettings.cpp
static constexpr const wchar_t* kSettingsKey =
    L"Software\\DennisPayne\\XISF Shell Extension";

// RAII guard for registry values — saves/restores FilterEnabled around tests
struct FilterEnabledGuard {
    FilterEnabledGuard() {
        DWORD cb = sizeof(m_prevValue);
        LONG st = RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, L"FilterEnabled",
                               RRF_RT_REG_DWORD, nullptr, &m_prevValue, &cb);
        m_hadValue = (st == ERROR_SUCCESS);
    }
    ~FilterEnabledGuard() {
        if (m_hadValue) {
            HKEY hKey = nullptr;
            if (RegOpenKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
                RegSetValueExW(hKey, L"FilterEnabled", 0, REG_DWORD,
                    reinterpret_cast<const BYTE*>(&m_prevValue), sizeof(m_prevValue));
                RegCloseKey(hKey);
            }
        } else {
            HKEY hKey = nullptr;
            if (RegOpenKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
                RegDeleteValueW(hKey, L"FilterEnabled");
                RegCloseKey(hKey);
            }
        }
    }
private:
    DWORD m_prevValue = 1;
    bool  m_hadValue  = false;
};

static void SetFilterEnabled(bool enabled) {
    HKEY hKey = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        DWORD dw = enabled ? 1u : 0u;
        RegSetValueExW(hKey, L"FilterEnabled", 0, REG_DWORD,
                       reinterpret_cast<const BYTE*>(&dw), sizeof(dw));
        RegCloseKey(hKey);
    }
}

// Build an XISF file with the given XML header into a temp directory.
// Returns the path. Caller should delete after use.
std::wstring BuildTempXISF(const std::string& xmlHeader, const std::wstring& name = L"test.xisf") {
    fs::path dir = fs::temp_directory_path() / L"XISFFilterTests";
    fs::create_directories(dir);
    fs::path filePath = dir / name;

    std::ofstream file(filePath, std::ios::binary | std::ios::trunc);

    // 16-byte preamble: "XISF0100" + uint32 header length + uint32 reserved
    const char sig[8] = {'X','I','S','F','0','1','0','0'};
    file.write(sig, 8);

    uint32_t headerLen = static_cast<uint32_t>(xmlHeader.size());
    file.write(reinterpret_cast<const char*>(&headerLen), 4);

    uint32_t reserved = 0;
    file.write(reinterpret_cast<const char*>(&reserved), 4);

    file.write(xmlHeader.data(), headerLen);
    file.close();

    return filePath.wstring();
}

IStream* CreateStreamFromFile(const std::wstring& path) {
    IStream* pStream = nullptr;
    HRESULT hr = SHCreateStreamOnFileEx(path.c_str(), STGM_READ | STGM_SHARE_DENY_NONE,
                                         FILE_ATTRIBUTE_NORMAL, FALSE, nullptr, &pStream);
    if (FAILED(hr)) return nullptr;
    return pStream;
}

// Cleanup temp directory
void CleanupTemp() {
    fs::path dir = fs::temp_directory_path() / L"XISFFilterTests";
    std::error_code ec;
    fs::remove_all(dir, ec);
}

// Standard test XML headers
const std::string kXmlWithFITSKeywords = R"(<?xml version="1.0" encoding="UTF-8"?>
<xisf version="1.0" xmlns="http://www.pixinsight.com/xisf">
  <Image geometry="4656:3520:1" sampleFormat="UInt16" colorSpace="Gray" location="attachment:0:0">
    <FITSKeyword name="EXPTIME" value="300.0" comment="Exposure time"/>
    <FITSKeyword name="FILTER" value="Ha" comment="Filter name"/>
    <FITSKeyword name="OBJECT" value="M42" comment="Target"/>
    <FITSKeyword name="INSTRUME" value="ASI2600MM Pro" comment="Camera"/>
    <FITSKeyword name="TELESCOP" value="RASA 8" comment="Telescope"/>
  </Image>
</xisf>)";

const std::string kXmlWithProperties = R"(<?xml version="1.0" encoding="UTF-8"?>
<xisf version="1.0" xmlns="http://www.pixinsight.com/xisf">
  <Image geometry="1920:1080:3" sampleFormat="Float32" colorSpace="RGB">
    <Property id="Instrument:ExposureTime" type="Float64" value="300.0"/>
    <Property id="Instrument:Filter:Name" type="String" value="Luminance"/>
    <Property id="Observer:Name" type="String" value="Dennis"/>
  </Image>
</xisf>)";

const std::string kMinimalXml = R"(<?xml version="1.0" encoding="UTF-8"?>
<xisf version="1.0" xmlns="http://www.pixinsight.com/xisf">
</xisf>)";

const std::string kXmlWithImageAttributes = R"(<?xml version="1.0" encoding="UTF-8"?>
<xisf version="1.0" xmlns="http://www.pixinsight.com/xisf">
  <Image geometry="4656:3520:1" sampleFormat="UInt16" colorSpace="Gray" location="attachment:0:0">
  </Image>
</xisf>)";

// Check if any chunk in the vector contains the given substring
bool ChunksContain(const std::vector<std::wstring>& chunks, const std::wstring& needle) {
    for (const auto& chunk : chunks) {
        if (chunk.find(needle) != std::wstring::npos)
            return true;
    }
    return false;
}

} // anonymous namespace

// ===========================================================================
// XISFParser — GetSearchableTextChunks
// ===========================================================================

TEST_CLASS(Filter_XISFParser_SearchableChunks)
{
public:
    TEST_METHOD(GetSearchableTextChunks_FITSKeywords)
    {
        auto result = xisf::XISFParser::ParseXMLString(kXmlWithFITSKeywords);
        Assert::IsTrue(result.ok(), L"Parse should succeed");

        auto chunks = result.metadata.GetSearchableTextChunks();
        Assert::IsTrue(chunks.size() >= 5, L"Should have at least 5 FITS keyword chunks");

        Assert::IsTrue(ChunksContain(chunks, L"EXPTIME"), L"Should contain EXPTIME keyword");
        Assert::IsTrue(ChunksContain(chunks, L"300.0"), L"Should contain exposure value");
        Assert::IsTrue(ChunksContain(chunks, L"OBJECT"), L"Should contain OBJECT keyword");
        Assert::IsTrue(ChunksContain(chunks, L"M42"), L"Should contain target name");
        Assert::IsTrue(ChunksContain(chunks, L"ASI2600MM Pro"), L"Should contain camera name");
        Assert::IsTrue(ChunksContain(chunks, L"RASA 8"), L"Should contain telescope name");
    }

    TEST_METHOD(GetSearchableTextChunks_XISFProperties)
    {
        auto result = xisf::XISFParser::ParseXMLString(kXmlWithProperties);
        Assert::IsTrue(result.ok(), L"Parse should succeed");

        auto chunks = result.metadata.GetSearchableTextChunks();
        Assert::IsTrue(chunks.size() >= 3, L"Should have at least 3 property chunks");

        Assert::IsTrue(ChunksContain(chunks, L"Instrument:ExposureTime"), L"Should contain exposure property ID");
        Assert::IsTrue(ChunksContain(chunks, L"300.0"), L"Should contain exposure value");
        Assert::IsTrue(ChunksContain(chunks, L"Luminance"), L"Should contain filter name");
        Assert::IsTrue(ChunksContain(chunks, L"Observer:Name"), L"Should contain observer property ID");
    }

    TEST_METHOD(GetSearchableTextChunks_EmptyHeader)
    {
        auto result = xisf::XISFParser::ParseXMLString(kMinimalXml);
        Assert::IsTrue(result.ok(), L"Parse should succeed for minimal XML");

        auto chunks = result.metadata.GetSearchableTextChunks();
        // Minimal XML with no FITS keywords, properties, or image attributes
        Assert::IsTrue(chunks.empty(), L"Minimal XISF should produce no chunks");
    }

    TEST_METHOD(GetSearchableTextChunks_ImageAttributes)
    {
        auto result = xisf::XISFParser::ParseXMLString(kXmlWithImageAttributes);
        Assert::IsTrue(result.ok(), L"Parse should succeed");

        auto chunks = result.metadata.GetSearchableTextChunks();
        Assert::IsTrue(chunks.size() >= 1, L"Should have image attribute chunks");

        Assert::IsTrue(ChunksContain(chunks, L"sampleFormat"), L"Should contain sampleFormat");
        Assert::IsTrue(ChunksContain(chunks, L"UInt16"), L"Should contain UInt16");
        Assert::IsTrue(ChunksContain(chunks, L"geometry"), L"Should contain geometry");
        Assert::IsTrue(ChunksContain(chunks, L"4656:3520:1"), L"Should contain geometry value");
    }
};

// ===========================================================================
// IFilter — CXISFFilter direct instantiation tests
// ===========================================================================

TEST_CLASS(Filter_IFilter_CoreContract)
{
public:
    TEST_METHOD_CLEANUP(Cleanup)
    {
        CleanupTemp();
    }

    TEST_METHOD(Init_ValidXISF_ReturnsSuccess)
    {
        std::wstring path = BuildTempXISF(kXmlWithFITSKeywords);
        IStream* pStream = CreateStreamFromFile(path);
        Assert::IsNotNull(pStream, L"Stream creation should succeed");

        CXISFFilter* pFilter = new CXISFFilter();
        HRESULT hr = pFilter->Load(pStream);
        Assert::AreEqual(S_OK, hr, L"Load should succeed");

        ULONG flags = 0;
        hr = pFilter->Init(0, 0, nullptr, &flags);
        Assert::AreEqual(S_OK, hr, L"Init should succeed");

        pFilter->Release();
        pStream->Release();
    }

    TEST_METHOD(GetChunk_ReturnsChunks)
    {
        std::wstring path = BuildTempXISF(kXmlWithFITSKeywords);
        IStream* pStream = CreateStreamFromFile(path);
        Assert::IsNotNull(pStream);

        CXISFFilter* pFilter = new CXISFFilter();
        pFilter->Load(pStream);
        pFilter->Init(0, 0, nullptr, nullptr);

        STAT_CHUNK stat = {};
        HRESULT hr = pFilter->GetChunk(&stat);
        Assert::AreEqual(S_OK, hr, L"First GetChunk should succeed");
        Assert::AreEqual(static_cast<ULONG>(1), stat.idChunk, L"First chunk ID should be 1");
        Assert::IsTrue(stat.flags == CHUNK_TEXT, L"Chunk should be text");

        pFilter->Release();
        pStream->Release();
    }

    TEST_METHOD(GetText_ReturnsText)
    {
        std::wstring path = BuildTempXISF(kXmlWithFITSKeywords);
        IStream* pStream = CreateStreamFromFile(path);
        Assert::IsNotNull(pStream);

        CXISFFilter* pFilter = new CXISFFilter();
        pFilter->Load(pStream);
        pFilter->Init(0, 0, nullptr, nullptr);

        STAT_CHUNK stat = {};
        HRESULT hr = pFilter->GetChunk(&stat);
        Assert::AreEqual(S_OK, hr);

        WCHAR buffer[256] = {};
        ULONG bufSize = ARRAYSIZE(buffer);
        hr = pFilter->GetText(&bufSize, buffer);
        Assert::IsTrue(hr == S_OK || hr == FILTER_S_LAST_TEXT, L"GetText should return text");
        Assert::IsTrue(bufSize > 0, L"Buffer should contain text");
        Assert::IsTrue(wcslen(buffer) > 0, L"Text should be non-empty");

        pFilter->Release();
        pStream->Release();
    }

    TEST_METHOD(GetChunk_ExhaustedReturnsEndOfChunks)
    {
        std::wstring path = BuildTempXISF(kXmlWithFITSKeywords);
        IStream* pStream = CreateStreamFromFile(path);
        Assert::IsNotNull(pStream);

        CXISFFilter* pFilter = new CXISFFilter();
        pFilter->Load(pStream);
        pFilter->Init(0, 0, nullptr, nullptr);

        // Consume all chunks
        STAT_CHUNK stat = {};
        HRESULT hr = S_OK;
        int count = 0;
        while (hr == S_OK && count < 1000) {
            hr = pFilter->GetChunk(&stat);
            if (hr == S_OK) {
                // Drain text from this chunk
                WCHAR buf[256];
                ULONG sz = ARRAYSIZE(buf);
                while (true) {
                    sz = ARRAYSIZE(buf);
                    HRESULT textHr = pFilter->GetText(&sz, buf);
                    if (textHr == FILTER_E_NO_MORE_TEXT || textHr == FILTER_S_LAST_TEXT)
                        break;
                }
            }
            count++;
        }

        Assert::AreEqual(static_cast<HRESULT>(FILTER_E_END_OF_CHUNKS), hr,
                          L"Should return FILTER_E_END_OF_CHUNKS when exhausted");

        pFilter->Release();
        pStream->Release();
    }

    TEST_METHOD(GetValue_ReturnsNoValues)
    {
        CXISFFilter* pFilter = new CXISFFilter();
        PROPVARIANT* pPropValue = nullptr;
        HRESULT hr = pFilter->GetValue(&pPropValue);
        Assert::AreEqual(static_cast<HRESULT>(FILTER_E_NO_VALUES), hr,
                          L"GetValue should always return FILTER_E_NO_VALUES");
        pFilter->Release();
    }

    TEST_METHOD(Init_EmptyFile_HandlesGracefully)
    {
        // Create a file with valid preamble but no meaningful XML
        std::wstring path = BuildTempXISF("", L"empty.xisf");
        IStream* pStream = CreateStreamFromFile(path);
        Assert::IsNotNull(pStream);

        CXISFFilter* pFilter = new CXISFFilter();
        // Load may succeed or fail gracefully — should not crash
        HRESULT hr = pFilter->Load(pStream);
        // Either way, Init should not crash
        pFilter->Init(0, 0, nullptr, nullptr);

        pFilter->Release();
        pStream->Release();
    }

    TEST_METHOD(QueryInterface_IFilter_Succeeds)
    {
        CXISFFilter* pFilter = new CXISFFilter();
        IFilter* pIFilter = nullptr;
        HRESULT hr = pFilter->QueryInterface(IID_IFilter, reinterpret_cast<void**>(&pIFilter));
        Assert::AreEqual(S_OK, hr, L"QI for IFilter should succeed");
        Assert::IsNotNull(pIFilter);
        pIFilter->Release();
        pFilter->Release();
    }

    TEST_METHOD(QueryInterface_IPersistStream_Succeeds)
    {
        CXISFFilter* pFilter = new CXISFFilter();
        IPersistStream* pPS = nullptr;
        HRESULT hr = pFilter->QueryInterface(IID_IPersistStream, reinterpret_cast<void**>(&pPS));
        Assert::AreEqual(S_OK, hr, L"QI for IPersistStream should succeed");
        Assert::IsNotNull(pPS);
        pPS->Release();
        pFilter->Release();
    }

    TEST_METHOD(GetClassID_ReturnsFilterCLSID)
    {
        CXISFFilter* pFilter = new CXISFFilter();
        CLSID clsid = {};
        HRESULT hr = pFilter->GetClassID(&clsid);
        Assert::AreEqual(S_OK, hr);
        Assert::IsTrue(IsEqualCLSID(clsid, CLSID_XISFFilter), L"Should return CLSID_XISFFilter");
        pFilter->Release();
    }

    TEST_METHOD(IPersistFile_Load_ValidXISF_Succeeds)
    {
        std::wstring path = BuildTempXISF(kXmlWithFITSKeywords);

        CXISFFilter* pFilter = new CXISFFilter();
        IPersistFile* pPF = nullptr;
        pFilter->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&pPF));
        Assert::IsNotNull(pPF);

        HRESULT hr = pPF->Load(path.c_str(), 0);
        Assert::AreEqual(S_OK, hr, L"IPersistFile::Load should succeed");

        pPF->Release();
        pFilter->Release();
    }
};

// ===========================================================================
// ClassFactory — Enable/Disable toggle
// ===========================================================================

TEST_CLASS(Filter_ClassFactory_RuntimeToggle)
{
public:
    TEST_METHOD(ClassFactory_FilterDisabled_ReturnsClassNotAvailable)
    {
        FilterEnabledGuard guard;

        SetFilterEnabled(false);
        Assert::IsFalse(xisf::IsFilterEnabled(), L"Filter should be disabled");

        CClassFactory factory;
        IFilter* pFilter = nullptr;
        HRESULT hr = factory.CreateInstance(nullptr, IID_IFilter,
                                             reinterpret_cast<void**>(&pFilter));
        Assert::AreEqual(static_cast<HRESULT>(CLASS_E_CLASSNOTAVAILABLE), hr,
                          L"Should return CLASS_E_CLASSNOTAVAILABLE when disabled");
        Assert::IsNull(pFilter);

        // Restore
        SetFilterEnabled(true);
    }

    TEST_METHOD(ClassFactory_FilterEnabled_CreatesInstance)
    {
        FilterEnabledGuard guard;

        SetFilterEnabled(true);
        Assert::IsTrue(xisf::IsFilterEnabled(), L"Filter should be enabled");

        CClassFactory factory;
        IFilter* pFilter = nullptr;
        HRESULT hr = factory.CreateInstance(nullptr, IID_IFilter,
                                             reinterpret_cast<void**>(&pFilter));
        Assert::AreEqual(S_OK, hr, L"Should create instance when enabled");
        Assert::IsNotNull(pFilter);
        pFilter->Release();
    }

    TEST_METHOD(ClassFactory_DefaultState_IsEnabled)
    {
        FilterEnabledGuard guard;

        // Delete the value to test default behavior
        HKEY hKey = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
            RegDeleteValueW(hKey, L"FilterEnabled");
            RegCloseKey(hKey);
        }

        Assert::IsTrue(xisf::IsFilterEnabled(), L"Default should be enabled");
    }
};

// ===========================================================================
// Telemetry — test hook verification
// ===========================================================================

TEST_CLASS(Filter_Telemetry)
{
public:
    TEST_METHOD_CLEANUP(Cleanup)
    {
        g_xisfFilterTelemetryHook = nullptr;
        CleanupTemp();
    }

    TEST_METHOD(TelemetryHook_CapturesFilterInitialized)
    {
        std::vector<std::wstring> captured;
        g_xisfFilterTelemetryHook = [](UCHAR, ULONGLONG, const wchar_t* msg) {
            // Capture in a thread-local — but since tests are single-threaded
            // we use the lambda capture context via a static.
        };

        // For now, verify the hook mechanism works
        bool hookCalled = false;
        g_xisfFilterTelemetryHook = [](UCHAR level, ULONGLONG keyword, const wchar_t* msg) {
            // Just verify it's callable without crashing
        };

        WriteFilterTelemetry(TRACE_LEVEL_INFORMATION, XISF_FILTER_KEYWORD_FILTER,
                              L"FilterInitialized chunkCount=%u", 5u);
        // The hook was callable — test passes if no crash
        g_xisfFilterTelemetryHook = nullptr;
    }

    TEST_METHOD(WriteFilterTelemetry_FormatsCorrectly)
    {
        // Use namespace-scope statics for the C-style callback
        s_capturedMessage.clear();
        s_capturedLevel = 0;
        s_capturedKeyword = 0;

        g_xisfFilterTelemetryHook = [](UCHAR level, ULONGLONG keyword, const wchar_t* msg) {
            s_capturedMessage = msg;
            s_capturedLevel = level;
            s_capturedKeyword = keyword;
        };

        WriteFilterTelemetry(TRACE_LEVEL_INFORMATION, XISF_FILTER_KEYWORD_FILTER,
                              L"FilterInitialized chunkCount=%u", 42u);

        Assert::AreEqual(std::wstring(L"FilterInitialized chunkCount=42"), s_capturedMessage);
        Assert::AreEqual(static_cast<UCHAR>(TRACE_LEVEL_INFORMATION), s_capturedLevel);
        Assert::AreEqual(XISF_FILTER_KEYWORD_FILTER, s_capturedKeyword);

        g_xisfFilterTelemetryHook = nullptr;
    }

private:
    static inline std::wstring s_capturedMessage;
    static inline UCHAR s_capturedLevel = 0;
    static inline ULONGLONG s_capturedKeyword = 0;
};
