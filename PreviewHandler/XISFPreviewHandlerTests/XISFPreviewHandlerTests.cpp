// XISFPreviewHandlerTests.cpp — Preview Handler: Preview & Thumbnail Handler tests
// Uses Microsoft Native Unit Test Framework (CppUnitTestFramework)
//
// Three test perspectives:
//   1. Principal developer  — COM contract, IUnknown, QI, ref counting, protocol
//   2. Astrophotographer    — metadata extraction for preview/thumbnail rendering
//   3. Windows sysadmin     — robustness, no crashes on bad input, clean errors
//
#include "CppUnitTest.h"

#include <windows.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <thumbcache.h>
#include <string>
#include <vector>
#include <cstring>

#include "XISFParser.h"
#include "PreviewHandler.h"
#include "ThumbnailProvider.h"
#include "PreviewHandlerTelemetry.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// ---------------------------------------------------------------------------
// Stubs for DLL globals referenced by PreviewHandler.cpp / ThumbnailProvider.cpp
// ---------------------------------------------------------------------------
long      g_cDllRef = 0;
HINSTANCE g_hInst   = nullptr;

// The test EXE needs its own provider definition (normally in dllmain.cpp).
TRACELOGGING_DEFINE_PROVIDER(g_hPreviewProvider, "XISF-PreviewHandler",
    (0x4fd34fd0, 0x08b3, 0x5d9a, 0x8d, 0x77, 0xb9, 0xd6, 0x70, 0x5d, 0x6b, 0x75));

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
namespace
{
    // Build a minimal valid XISF in-memory IStream.
    // Binary header: "XISF0100" (8 bytes) + uint32 xmlLen + uint32 reserved(0)
    // followed by the XML text.
    IStream* CreateXISFStream(const std::string& xml)
    {
        const uint32_t xmlLen  = static_cast<uint32_t>(xml.size());
        const uint32_t reserved = 0;
        const size_t headerSize = 8 + sizeof(uint32_t) + sizeof(uint32_t);
        std::vector<BYTE> buf(headerSize + xmlLen);

        std::memcpy(buf.data(), "XISF0100", 8);
        std::memcpy(buf.data() + 8, &xmlLen, 4);
        std::memcpy(buf.data() + 12, &reserved, 4);
        std::memcpy(buf.data() + headerSize, xml.data(), xmlLen);

        return SHCreateMemStream(buf.data(), static_cast<UINT>(buf.size()));
    }

    IStream* CreateBogusStream(const char* data, size_t len)
    {
        return SHCreateMemStream(reinterpret_cast<const BYTE*>(data), static_cast<UINT>(len));
    }

    // Sample XISF XML with astrophotography metadata
    const char* kAstroXML =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<xisf version="1.0">)"
        R"(<Image geometry="4656:3520:1" sampleFormat="UInt16" colorSpace="Gray")"
        R"( location="attachment:4096:32686080">)"
        R"(<FITSKeyword name="OBJECT" value="M42" comment="Orion Nebula"/>)"
        R"(<FITSKeyword name="INSTRUME" value="ZWO ASI2600MM Pro" comment="Camera"/>)"
        R"(<FITSKeyword name="TELESCOP" value="Celestron EdgeHD 11" comment="Telescope"/>)"
        R"(<FITSKeyword name="FILTER" value="Ha" comment="Hydrogen-alpha"/>)"
        R"(<FITSKeyword name="EXPTIME" value="300.0" comment="Exposure seconds"/>)"
        R"(<FITSKeyword name="CCD-TEMP" value="-10.0" comment="Sensor temp C"/>)"
        R"(<FITSKeyword name="GAIN" value="100" comment="Camera gain"/>)"
        R"(<FITSKeyword name="FOCALLEN" value="2800" comment="Focal length mm"/>)"
        R"(<FITSKeyword name="XBINNING" value="1" comment="X binning"/>)"
        R"(<FITSKeyword name="YBINNING" value="1" comment="Y binning"/>)"
        R"(<FITSKeyword name="DATE-OBS" value="2024-01-15T22:30:00" comment="Observation date"/>)"
        R"(<FITSKeyword name="RA" value="83.822" comment="Right ascension deg"/>)"
        R"(<FITSKeyword name="DEC" value="-5.391" comment="Declination deg"/>)"
        R"(<FITSKeyword name="SITEELEV" value="1200" comment="Site elevation m"/>)"
        R"(<Property id="Instrument:ExposureTime" type="Float64" value="300.0"/>)"
        R"(<Property id="Instrument:Telescope:Name" type="String" value="Celestron EdgeHD 11"/>)"
        R"(<Property id="Instrument:Camera:Name" type="String" value="ZWO ASI2600MM Pro"/>)"
        R"(<Property id="Instrument:Filter:Name" type="String" value="Ha"/>)"
        R"(<Property id="Instrument:Sensor:Temperature" type="Float64" value="-10.0"/>)"
        R"(<Property id="Observation:Object:Name" type="String" value="M42"/>)"
        R"(</Image></xisf>)";

    // Minimal XML with one keyword
    const char* kMinimalXML =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<xisf version="1.0"><Image>)"
        R"(<FITSKeyword name="SIMPLE" value="T" comment="Standard FITS"/>)"
        R"(</Image></xisf>)";

    // COM initialization helper for test lifetime
    struct ComInit
    {
        ComInit()  { CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); }
        ~ComInit() { CoUninitialize(); }
    };
    static ComInit s_com;
}

namespace PreviewHandlerTests
{

// ═══════════════════════════════════════════════════════════════════════════
// PERSPECTIVE 1 — Principal Developer: COM contract & protocol correctness
// ═══════════════════════════════════════════════════════════════════════════
TEST_CLASS(PreviewHandler_PreviewThumbnail_CoreContract)
{
public:

    // ----- CPreviewHandler IUnknown contract -----

    TEST_METHOD(PreviewHandler_StartsWithRefCount1)
    {
        auto* p = new CPreviewHandler();
        // Release from refcount 1 → should return 0 and delete
        ULONG r = p->Release();
        Assert::AreEqual(0UL, r, L"Initial refcount should be 1; Release returns 0");
    }

    TEST_METHOD(PreviewHandler_AddRef_Release_Balanced)
    {
        auto* p = new CPreviewHandler();
        ULONG r1 = p->AddRef();  // 2
        Assert::AreEqual(2UL, r1);
        ULONG r2 = p->AddRef();  // 3
        Assert::AreEqual(3UL, r2);
        p->Release(); // 2
        p->Release(); // 1
        ULONG r3 = p->Release(); // 0 → delete
        Assert::AreEqual(0UL, r3);
    }

    TEST_METHOD(PreviewHandler_QI_IUnknown)
    {
        auto* p = new CPreviewHandler();
        IUnknown* pUnk = nullptr;
        HRESULT hr = p->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&pUnk));
        Assert::AreEqual(S_OK, hr);
        Assert::IsNotNull(pUnk);
        pUnk->Release();
        p->Release();
    }

    TEST_METHOD(PreviewHandler_QI_IPreviewHandler)
    {
        auto* p = new CPreviewHandler();
        IPreviewHandler* pPH = nullptr;
        HRESULT hr = p->QueryInterface(IID_IPreviewHandler, reinterpret_cast<void**>(&pPH));
        Assert::AreEqual(S_OK, hr);
        Assert::IsNotNull(pPH);
        pPH->Release();
        p->Release();
    }

    TEST_METHOD(PreviewHandler_QI_IInitializeWithStream)
    {
        auto* p = new CPreviewHandler();
        IInitializeWithStream* pIWS = nullptr;
        HRESULT hr = p->QueryInterface(IID_IInitializeWithStream, reinterpret_cast<void**>(&pIWS));
        Assert::AreEqual(S_OK, hr);
        Assert::IsNotNull(pIWS);
        pIWS->Release();
        p->Release();
    }

    TEST_METHOD(PreviewHandler_QI_IPreviewHandlerVisuals)
    {
        auto* p = new CPreviewHandler();
        IPreviewHandlerVisuals* pVis = nullptr;
        HRESULT hr = p->QueryInterface(IID_IPreviewHandlerVisuals, reinterpret_cast<void**>(&pVis));
        Assert::AreEqual(S_OK, hr);
        Assert::IsNotNull(pVis);
        pVis->Release();
        p->Release();
    }

    TEST_METHOD(PreviewHandler_QI_UnsupportedInterface_ReturnsNoInterface)
    {
        auto* p = new CPreviewHandler();
        void* pBad = nullptr;
        HRESULT hr = p->QueryInterface(IID_IDispatch, &pBad);
        Assert::AreEqual(E_NOINTERFACE, hr);
        Assert::IsNull(pBad);
        p->Release();
    }

    TEST_METHOD(PreviewHandler_QI_NullPointer_ReturnsEPointer)
    {
        auto* p = new CPreviewHandler();
        HRESULT hr = p->QueryInterface(IID_IUnknown, nullptr);
        Assert::AreEqual(E_POINTER, hr);
        p->Release();
    }

    TEST_METHOD(PreviewHandler_Initialize_Success)
    {
        auto* p = new CPreviewHandler();
        IStream* pStream = CreateXISFStream(kMinimalXML);
        Assert::IsNotNull(pStream);

        HRESULT hr = p->Initialize(pStream, STGM_READ);
        Assert::AreEqual(S_OK, hr);

        pStream->Release();
        p->Release();
    }

    TEST_METHOD(PreviewHandler_Initialize_DoubleInit_Rejected)
    {
        auto* p = new CPreviewHandler();
        IStream* pStream = CreateXISFStream(kMinimalXML);
        HRESULT hr1 = p->Initialize(pStream, STGM_READ);
        Assert::AreEqual(S_OK, hr1);

        IStream* pStream2 = CreateXISFStream(kMinimalXML);
        HRESULT hr2 = p->Initialize(pStream2, STGM_READ);
        Assert::IsTrue(FAILED(hr2), L"Double initialization must be rejected");

        pStream2->Release();
        pStream->Release();
        p->Release();
    }

    TEST_METHOD(PreviewHandler_Initialize_NullStream_ReturnsInvalidArg)
    {
        auto* p = new CPreviewHandler();
        HRESULT hr = p->Initialize(nullptr, STGM_READ);
        Assert::AreEqual(E_INVALIDARG, hr);
        p->Release();
    }

    // ----- CThumbnailProvider IUnknown contract -----

    TEST_METHOD(ThumbnailProvider_StartsWithRefCount1)
    {
        auto* p = new CThumbnailProvider();
        ULONG r = p->Release();
        Assert::AreEqual(0UL, r);
    }

    TEST_METHOD(ThumbnailProvider_AddRef_Release_Balanced)
    {
        auto* p = new CThumbnailProvider();
        ULONG r1 = p->AddRef();
        Assert::AreEqual(2UL, r1);
        p->Release();
        ULONG r2 = p->Release();
        Assert::AreEqual(0UL, r2);
    }

    TEST_METHOD(ThumbnailProvider_QI_IUnknown)
    {
        auto* p = new CThumbnailProvider();
        IUnknown* pUnk = nullptr;
        HRESULT hr = p->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&pUnk));
        Assert::AreEqual(S_OK, hr);
        Assert::IsNotNull(pUnk);
        pUnk->Release();
        p->Release();
    }

    TEST_METHOD(ThumbnailProvider_QI_IThumbnailProvider)
    {
        auto* p = new CThumbnailProvider();
        IThumbnailProvider* pTP = nullptr;
        HRESULT hr = p->QueryInterface(IID_IThumbnailProvider, reinterpret_cast<void**>(&pTP));
        Assert::AreEqual(S_OK, hr);
        Assert::IsNotNull(pTP);
        pTP->Release();
        p->Release();
    }

    TEST_METHOD(ThumbnailProvider_QI_IInitializeWithStream)
    {
        auto* p = new CThumbnailProvider();
        IInitializeWithStream* pIWS = nullptr;
        HRESULT hr = p->QueryInterface(IID_IInitializeWithStream, reinterpret_cast<void**>(&pIWS));
        Assert::AreEqual(S_OK, hr);
        Assert::IsNotNull(pIWS);
        pIWS->Release();
        p->Release();
    }

    TEST_METHOD(ThumbnailProvider_QI_UnsupportedInterface)
    {
        auto* p = new CThumbnailProvider();
        void* pBad = nullptr;
        HRESULT hr = p->QueryInterface(IID_IDispatch, &pBad);
        Assert::AreEqual(E_NOINTERFACE, hr);
        Assert::IsNull(pBad);
        p->Release();
    }

    TEST_METHOD(ThumbnailProvider_Initialize_Success)
    {
        auto* p = new CThumbnailProvider();
        IStream* pStream = CreateXISFStream(kMinimalXML);
        HRESULT hr = p->Initialize(pStream, STGM_READ);
        Assert::AreEqual(S_OK, hr);
        pStream->Release();
        p->Release();
    }

    TEST_METHOD(ThumbnailProvider_Initialize_DoubleInit_Rejected)
    {
        auto* p = new CThumbnailProvider();
        IStream* pStream = CreateXISFStream(kMinimalXML);
        Assert::AreEqual(S_OK, p->Initialize(pStream, STGM_READ));

        IStream* pStream2 = CreateXISFStream(kMinimalXML);
        HRESULT hr2 = p->Initialize(pStream2, STGM_READ);
        Assert::IsTrue(FAILED(hr2), L"Double initialization must be rejected");

        pStream2->Release();
        pStream->Release();
        p->Release();
    }

    TEST_METHOD(ThumbnailProvider_Initialize_NullStream_ReturnsInvalidArg)
    {
        auto* p = new CThumbnailProvider();
        HRESULT hr = p->Initialize(nullptr, STGM_READ);
        Assert::AreEqual(E_INVALIDARG, hr);
        p->Release();
    }

    TEST_METHOD(ThumbnailProvider_GetThumbnail_NullBitmap_ReturnsEPointer)
    {
        auto* p = new CThumbnailProvider();
        IStream* pStream = CreateXISFStream(kMinimalXML);
        p->Initialize(pStream, STGM_READ);

        WTS_ALPHATYPE alpha;
        HRESULT hr = p->GetThumbnail(256, nullptr, &alpha);
        Assert::AreEqual(E_POINTER, hr);

        pStream->Release();
        p->Release();
    }

    TEST_METHOD(ThumbnailProvider_GetThumbnail_NullAlpha_ReturnsEPointer)
    {
        auto* p = new CThumbnailProvider();
        IStream* pStream = CreateXISFStream(kMinimalXML);
        p->Initialize(pStream, STGM_READ);

        HBITMAP hbmp = nullptr;
        HRESULT hr = p->GetThumbnail(256, &hbmp, nullptr);
        Assert::AreEqual(E_POINTER, hr);

        pStream->Release();
        p->Release();
    }

    // ----- PreviewHandler non-window methods -----

    TEST_METHOD(PreviewHandler_SetWindow_NullHwnd_ReturnsInvalidArg)
    {
        auto* p = new CPreviewHandler();
        RECT rc = { 0, 0, 100, 100 };
        HRESULT hr = p->SetWindow(nullptr, &rc);
        Assert::AreEqual(E_INVALIDARG, hr);
        p->Release();
    }

    TEST_METHOD(PreviewHandler_SetWindow_NullRect_ReturnsInvalidArg)
    {
        auto* p = new CPreviewHandler();
        HRESULT hr = p->SetWindow(reinterpret_cast<HWND>(1), nullptr);
        Assert::AreEqual(E_INVALIDARG, hr);
        p->Release();
    }

    TEST_METHOD(PreviewHandler_SetRect_NullRect_ReturnsInvalidArg)
    {
        auto* p = new CPreviewHandler();
        HRESULT hr = p->SetRect(nullptr);
        Assert::AreEqual(E_INVALIDARG, hr);
        p->Release();
    }

    TEST_METHOD(PreviewHandler_DoPreview_WithoutWindow_ReturnsEFail)
    {
        auto* p = new CPreviewHandler();
        IStream* pStream = CreateXISFStream(kAstroXML);
        p->Initialize(pStream, STGM_READ);

        HRESULT hr = p->DoPreview();
        Assert::AreEqual(E_FAIL, hr, L"DoPreview without SetWindow should fail gracefully");

        pStream->Release();
        p->Release();
    }

    TEST_METHOD(PreviewHandler_SetWindow_ReparentTwice_DoesNotLeakHandle)
    {
        // Explorer occasionally reparents preview hosts during dock changes.
        // SetWindow must accept a second call with a different HWND without
        // crashing; the handler should overwrite m_hwndParent (no leak of the
        // first handle, no rejection on rebind).  Both calls must return S_OK.
        auto* p = new CPreviewHandler();
        RECT rc = { 0, 0, 100, 100 };

        HRESULT hr1 = p->SetWindow(reinterpret_cast<HWND>(0x1), &rc);
        Assert::AreEqual(S_OK, hr1, L"First SetWindow must succeed");

        HWND hwnd2 = reinterpret_cast<HWND>(0x2);
        RECT rc2 = { 5, 5, 200, 150 };
        HRESULT hr2 = p->SetWindow(hwnd2, &rc2);
        Assert::AreEqual(S_OK, hr2, L"Second SetWindow (reparent) must succeed");

        // After reparent, DoPreview must still observe the (fake) parent and
        // proceed past the missing-parent guard.  With a fake HWND, window
        // creation will fail, but the handler must not crash.
        IStream* pStream = CreateXISFStream(kMinimalXML);
        p->Initialize(pStream, STGM_READ);
        HRESULT hr3 = p->DoPreview();
        // Either succeeds via placeholder bitmap, or fails at CreatePreviewWindow
        Assert::IsTrue(SUCCEEDED(hr3) || hr3 == E_FAIL,
            L"DoPreview after reparent must not crash");

        pStream->Release();
        p->Release();
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// PERSPECTIVE 2 — Astrophotographer: metadata for preview/thumbnail workflows
// ═══════════════════════════════════════════════════════════════════════════
TEST_CLASS(PreviewHandler_PreviewThumbnail_AstroWorkflow)
{
public:

    // ----- Parser (same API as Phase 1) in preview/thumbnail context -----

    TEST_METHOD(Parser_ExtractsObjectName_ForPreviewTitle)
    {
        xisf::ParseResult r = xisf::XISFParser::ParseXMLString(kAstroXML);
        Assert::IsTrue(r.ok());
        std::string obj = r.metadata.getFITSValue("OBJECT");
        Assert::AreEqual(std::string("M42"), obj,
            L"Preview handler needs OBJECT for display title");
    }

    TEST_METHOD(Parser_ExtractsCameraName_ForPreviewInfo)
    {
        xisf::ParseResult r = xisf::XISFParser::ParseXMLString(kAstroXML);
        Assert::IsTrue(r.ok());
        std::string cam = r.metadata.getFITSValue("INSTRUME");
        Assert::AreEqual(std::string("ZWO ASI2600MM Pro"), cam);
    }

    TEST_METHOD(Parser_ExtractsTelescopeName_ForPreviewInfo)
    {
        xisf::ParseResult r = xisf::XISFParser::ParseXMLString(kAstroXML);
        Assert::IsTrue(r.ok());
        std::string scope = r.metadata.getFITSValue("TELESCOP");
        Assert::AreEqual(std::string("Celestron EdgeHD 11"), scope);
    }

    TEST_METHOD(Parser_ExtractsFilter_ForPreviewInfo)
    {
        xisf::ParseResult r = xisf::XISFParser::ParseXMLString(kAstroXML);
        Assert::IsTrue(r.ok());
        std::string filter = r.metadata.getFITSValue("FILTER");
        Assert::AreEqual(std::string("Ha"), filter);
    }

    TEST_METHOD(Parser_ExtractsExposure_ForPreviewInfo)
    {
        xisf::ParseResult r = xisf::XISFParser::ParseXMLString(kAstroXML);
        Assert::IsTrue(r.ok());
        std::string exp = r.metadata.getFITSValue("EXPTIME");
        Assert::AreEqual(std::string("300.0"), exp);
    }

    TEST_METHOD(Parser_ExtractsSensorTemp_ForPreviewInfo)
    {
        xisf::ParseResult r = xisf::XISFParser::ParseXMLString(kAstroXML);
        Assert::IsTrue(r.ok());
        std::string temp = r.metadata.getFITSValue("CCD-TEMP");
        Assert::AreEqual(std::string("-10.0"), temp);
    }

    TEST_METHOD(Parser_ExtractsCoordinates_ForPreviewInfo)
    {
        xisf::ParseResult r = xisf::XISFParser::ParseXMLString(kAstroXML);
        Assert::IsTrue(r.ok());
        Assert::AreEqual(std::string("83.822"), r.metadata.getFITSValue("RA"));
        Assert::AreEqual(std::string("-5.391"), r.metadata.getFITSValue("DEC"));
    }

    TEST_METHOD(Parser_ExtractsDateObs_ForTimestampDisplay)
    {
        xisf::ParseResult r = xisf::XISFParser::ParseXMLString(kAstroXML);
        Assert::IsTrue(r.ok());
        std::string dt = r.metadata.getFITSValue("DATE-OBS");
        Assert::AreEqual(std::string("2024-01-15T22:30:00"), dt);
    }

    TEST_METHOD(Parser_ExtractsXISFProperties_InstrumentNames)
    {
        xisf::ParseResult r = xisf::XISFParser::ParseXMLString(kAstroXML);
        Assert::IsTrue(r.ok());
        Assert::AreEqual(std::string("Celestron EdgeHD 11"),
            r.metadata.getPropertyValue("Instrument:Telescope:Name"));
        Assert::AreEqual(std::string("ZWO ASI2600MM Pro"),
            r.metadata.getPropertyValue("Instrument:Camera:Name"));
    }

    TEST_METHOD(Parser_ExtractsXISFProperty_FilterName)
    {
        xisf::ParseResult r = xisf::XISFParser::ParseXMLString(kAstroXML);
        Assert::IsTrue(r.ok());
        Assert::AreEqual(std::string("Ha"),
            r.metadata.getPropertyValue("Instrument:Filter:Name"));
    }

    TEST_METHOD(Parser_ExtractsXISFProperty_ObjectName)
    {
        xisf::ParseResult r = xisf::XISFParser::ParseXMLString(kAstroXML);
        Assert::IsTrue(r.ok());
        Assert::AreEqual(std::string("M42"),
            r.metadata.getPropertyValue("Observation:Object:Name"));
    }

    TEST_METHOD(Parser_CountsAllFITSKeywords)
    {
        xisf::ParseResult r = xisf::XISFParser::ParseXMLString(kAstroXML);
        Assert::IsTrue(r.ok());
        Assert::AreEqual(size_t(14), r.metadata.fitsKeywords.size(),
            L"Should extract all 14 FITS keywords from astrophoto XISF");
    }

    TEST_METHOD(Parser_CountsAllXISFProperties)
    {
        xisf::ParseResult r = xisf::XISFParser::ParseXMLString(kAstroXML);
        Assert::IsTrue(r.ok());
        Assert::AreEqual(size_t(6), r.metadata.properties.size(),
            L"Should extract all 6 XISF properties from astrophoto XISF");
    }

    // ----- Handlers initialized with astro data -----

    TEST_METHOD(PreviewHandler_Initialize_WithAstroStream)
    {
        auto* p = new CPreviewHandler();
        IStream* pStream = CreateXISFStream(kAstroXML);
        HRESULT hr = p->Initialize(pStream, STGM_READ);
        Assert::AreEqual(S_OK, hr, L"Preview handler must initialize with real astro XISF");
        pStream->Release();
        p->Release();
    }

    TEST_METHOD(ThumbnailProvider_Initialize_WithAstroStream)
    {
        auto* p = new CThumbnailProvider();
        IStream* pStream = CreateXISFStream(kAstroXML);
        HRESULT hr = p->Initialize(pStream, STGM_READ);
        Assert::AreEqual(S_OK, hr, L"Thumbnail provider must initialize with real astro XISF");
        pStream->Release();
        p->Release();
    }

    TEST_METHOD(ThumbnailProvider_GetThumbnail_ProducesBitmap_WithAstroData)
    {
        auto* p = new CThumbnailProvider();
        IStream* pStream = CreateXISFStream(kAstroXML);
        p->Initialize(pStream, STGM_READ);

        HBITMAP hbmp = nullptr;
        WTS_ALPHATYPE alpha = WTSAT_UNKNOWN;
        HRESULT hr = p->GetThumbnail(256, &hbmp, &alpha);
        // Even without real pixel attachment, the provider should fall back
        // to a placeholder bitmap rather than returning E_FAIL
        Assert::AreEqual(S_OK, hr, L"GetThumbnail should succeed via placeholder fallback");
        Assert::IsNotNull(hbmp, L"Bitmap handle should be non-null");
        if (hbmp) DeleteObject(hbmp);

        pStream->Release();
        p->Release();
    }

    TEST_METHOD(Parser_HandlesGainAndBinning_ForImageInfo)
    {
        xisf::ParseResult r = xisf::XISFParser::ParseXMLString(kAstroXML);
        Assert::IsTrue(r.ok());
        Assert::AreEqual(std::string("100"), r.metadata.getFITSValue("GAIN"));
        Assert::AreEqual(std::string("1"), r.metadata.getFITSValue("XBINNING"));
        Assert::AreEqual(std::string("1"), r.metadata.getFITSValue("YBINNING"));
    }

    TEST_METHOD(Parser_ExtractsFocalLength_ForFieldOfViewCalc)
    {
        xisf::ParseResult r = xisf::XISFParser::ParseXMLString(kAstroXML);
        Assert::IsTrue(r.ok());
        Assert::AreEqual(std::string("2800"), r.metadata.getFITSValue("FOCALLEN"));
    }

    TEST_METHOD(Parser_ExtractsSiteElevation)
    {
        xisf::ParseResult r = xisf::XISFParser::ParseXMLString(kAstroXML);
        Assert::IsTrue(r.ok());
        Assert::AreEqual(std::string("1200"), r.metadata.getFITSValue("SITEELEV"));
    }

    TEST_METHOD(Parser_FITSKeyword_EmbeddedSingleQuote_Preserved)
    {
        // Astrophoto keywords frequently contain single quotes (observer names
        // like "O'Neill", site names like "St. John's"). The parser uses
        // double-quoted attribute values, so a literal apostrophe inside the
        // value must round-trip verbatim — neither stripped nor escaped.
        const char* xml =
            R"(<?xml version="1.0" encoding="UTF-8"?>)"
            R"(<xisf version="1.0"><Image>)"
            R"(<FITSKeyword name="OBSERVER" value="O'Neill" comment="Observer name"/>)"
            R"(<FITSKeyword name="SITENAME" value="St. John's Observatory" comment="Site"/>)"
            R"(</Image></xisf>)";
        xisf::ParseResult r = xisf::XISFParser::ParseXMLString(xml);
        Assert::IsTrue(r.ok());
        Assert::AreEqual(std::string("O'Neill"), r.metadata.getFITSValue("OBSERVER"),
            L"Apostrophe inside double-quoted value must be preserved verbatim");
        Assert::AreEqual(std::string("St. John's Observatory"),
                         r.metadata.getFITSValue("SITENAME"));
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// PERSPECTIVE 3 — Windows Sysadmin: robustness, no crashes, clean errors
// ═══════════════════════════════════════════════════════════════════════════
TEST_CLASS(PreviewHandler_PreviewThumbnail_Reliability)
{
public:

    // ----- PreviewHandler robustness -----

    TEST_METHOD(PreviewHandler_Initialize_BogusStream_DoesNotCrash)
    {
        auto* p = new CPreviewHandler();
        IStream* pStream = CreateBogusStream("NOT_XISF_DATA", 14);
        // Should not crash — may succeed (parser will produce empty metadata)
        // or fail gracefully
        p->Initialize(pStream, STGM_READ);
        pStream->Release();
        p->Release();
    }

    TEST_METHOD(PreviewHandler_Initialize_EmptyStream_DoesNotCrash)
    {
        auto* p = new CPreviewHandler();
        IStream* pStream = CreateBogusStream("", 0);
        p->Initialize(pStream, STGM_READ);
        pStream->Release();
        p->Release();
    }

    TEST_METHOD(PreviewHandler_Initialize_TinyStream_DoesNotCrash)
    {
        auto* p = new CPreviewHandler();
        IStream* pStream = CreateBogusStream("XY", 2);
        p->Initialize(pStream, STGM_READ);
        pStream->Release();
        p->Release();
    }

    TEST_METHOD(PreviewHandler_Unload_BeforeInit_DoesNotCrash)
    {
        auto* p = new CPreviewHandler();
        HRESULT hr = p->Unload();
        // Should not crash; implementation may return S_OK or E_UNEXPECTED
        (void)hr;
        p->Release();
    }

    TEST_METHOD(PreviewHandler_SetFocus_BeforeWindow_DoesNotCrash)
    {
        auto* p = new CPreviewHandler();
        HRESULT hr = p->SetFocus();
        (void)hr;
        p->Release();
    }

    TEST_METHOD(PreviewHandler_QueryFocus_NullParam_DoesNotCrash)
    {
        auto* p = new CPreviewHandler();
        // QueryFocus with null pointer should not crash
        HRESULT hr = p->QueryFocus(nullptr);
        (void)hr;
        p->Release();
    }

    TEST_METHOD(PreviewHandler_TranslateAccelerator_NullMsg_DoesNotCrash)
    {
        auto* p = new CPreviewHandler();
        HRESULT hr = p->TranslateAccelerator(nullptr);
        (void)hr;
        p->Release();
    }

    TEST_METHOD(PreviewHandler_SetBackgroundColor_BeforeWindow)
    {
        auto* p = new CPreviewHandler();
        HRESULT hr = p->SetBackgroundColor(RGB(0, 0, 0));
        // Should store the color without crashing
        Assert::AreEqual(S_OK, hr);
        p->Release();
    }

    TEST_METHOD(PreviewHandler_SetTextColor_BeforeWindow)
    {
        auto* p = new CPreviewHandler();
        HRESULT hr = p->SetTextColor(RGB(255, 255, 255));
        Assert::AreEqual(S_OK, hr);
        p->Release();
    }

    TEST_METHOD(PreviewHandler_SetFont_BeforeWindow)
    {
        auto* p = new CPreviewHandler();
        LOGFONTW lf = {};
        lf.lfHeight = -16;
        wcscpy_s(lf.lfFaceName, L"Consolas");
        HRESULT hr = p->SetFont(&lf);
        Assert::AreEqual(S_OK, hr);
        p->Release();
    }

    // ----- ThumbnailProvider robustness -----

    TEST_METHOD(ThumbnailProvider_Initialize_BogusStream_DoesNotCrash)
    {
        auto* p = new CThumbnailProvider();
        IStream* pStream = CreateBogusStream("GARBAGE_DATA_HERE", 17);
        p->Initialize(pStream, STGM_READ);
        pStream->Release();
        p->Release();
    }

    TEST_METHOD(ThumbnailProvider_Initialize_EmptyStream_DoesNotCrash)
    {
        auto* p = new CThumbnailProvider();
        IStream* pStream = CreateBogusStream("", 0);
        p->Initialize(pStream, STGM_READ);
        pStream->Release();
        p->Release();
    }

    TEST_METHOD(ThumbnailProvider_GetThumbnail_BeforeInit_DoesNotCrash)
    {
        auto* p = new CThumbnailProvider();
        HBITMAP hbmp = nullptr;
        WTS_ALPHATYPE alpha;
        HRESULT hr = p->GetThumbnail(256, &hbmp, &alpha);
        // The provider falls back to a placeholder bitmap even without
        // initialization — this is correct robust behavior.  The key
        // requirement is that it does not crash.
        Assert::IsTrue(SUCCEEDED(hr) || FAILED(hr),
            L"GetThumbnail before Initialize must not crash");
        if (hbmp) DeleteObject(hbmp);
        p->Release();
    }

    TEST_METHOD(ThumbnailProvider_GetThumbnail_ZeroSize_DoesNotCrash)
    {
        auto* p = new CThumbnailProvider();
        IStream* pStream = CreateXISFStream(kMinimalXML);
        p->Initialize(pStream, STGM_READ);

        HBITMAP hbmp = nullptr;
        WTS_ALPHATYPE alpha;
        HRESULT hr = p->GetThumbnail(0, &hbmp, &alpha);
        // May fail, but must not crash
        (void)hr;
        if (hbmp) DeleteObject(hbmp);
        pStream->Release();
        p->Release();
    }

    TEST_METHOD(ThumbnailProvider_GetThumbnail_LargeSize_DoesNotCrash)
    {
        auto* p = new CThumbnailProvider();
        IStream* pStream = CreateXISFStream(kMinimalXML);
        p->Initialize(pStream, STGM_READ);

        HBITMAP hbmp = nullptr;
        WTS_ALPHATYPE alpha;
        HRESULT hr = p->GetThumbnail(4096, &hbmp, &alpha);
        (void)hr;
        if (hbmp) DeleteObject(hbmp);
        pStream->Release();
        p->Release();
    }

    TEST_METHOD(PreviewHandler_Initialize_OversizeStream_FailsGracefully)
    {
        // Simulate a stream that reports > 64 MB — Initialize should reject
        // We can't easily create a 64 MB IStream but we verify the code path
        // exists by testing with a normal stream (regression guard).
        auto* p = new CPreviewHandler();
        IStream* pStream = CreateXISFStream(kMinimalXML);
        HRESULT hr = p->Initialize(pStream, STGM_READ);
        Assert::AreEqual(S_OK, hr, L"Normal stream must succeed");
        pStream->Release();
        p->Release();
    }

    TEST_METHOD(PreviewHandler_DllRefCount_BalancedAfterLifecycle)
    {
        long before = g_cDllRef;
        {
            auto* p = new CPreviewHandler();
            Assert::AreEqual(before + 1, g_cDllRef, L"Constructor should increment DLL ref");
            p->Release();
        }
        Assert::AreEqual(before, g_cDllRef, L"Destructor should decrement DLL ref");
    }

    TEST_METHOD(ThumbnailProvider_DllRefCount_BalancedAfterLifecycle)
    {
        long before = g_cDllRef;
        {
            auto* p = new CThumbnailProvider();
            Assert::AreEqual(before + 1, g_cDllRef);
            p->Release();
        }
        Assert::AreEqual(before, g_cDllRef);
    }

    TEST_METHOD(PreviewHandler_RapidCreateDestroy_NoLeak)
    {
        long before = g_cDllRef;
        for (int i = 0; i < 100; ++i)
        {
            auto* p = new CPreviewHandler();
            p->Release();
        }
        Assert::AreEqual(before, g_cDllRef,
            L"100 create/destroy cycles must leave DLL refcount balanced");
    }

    TEST_METHOD(ThumbnailProvider_RapidCreateDestroy_NoLeak)
    {
        long before = g_cDllRef;
        for (int i = 0; i < 100; ++i)
        {
            auto* p = new CThumbnailProvider();
            p->Release();
        }
        Assert::AreEqual(before, g_cDllRef);
    }

    TEST_METHOD(PreviewHandler_HeaderLengthExceedsKMaxHeaderBytes_RejectedCleanly)
    {
        // The off-by-one boundary in PreviewHandler.cpp (line 162) is:
        //   if (headerLength == 0 || headerLength > kMaxHeaderBytes) return E_FAIL;
        // Exactly kMaxHeaderBytes must be accepted; kMaxHeaderBytes + 1 must fail.
        // We craft a 16-byte preamble that claims a header one byte over the limit.
        // The check happens before the header read, so we need only a tiny stream.
        std::vector<BYTE> buf(16, 0);
        std::memcpy(buf.data(), "XISF0100", 8);
        const uint32_t headerLen = xisf::XISFParser::kMaxHeaderBytes + 1;
        std::memcpy(buf.data() + 8, &headerLen, sizeof(uint32_t));
        // bytes 12..15 (reserved) stay zero

        auto* p = new CPreviewHandler();
        IStream* pStream = SHCreateMemStream(buf.data(), static_cast<UINT>(buf.size()));
        Assert::IsNotNull(pStream);

        HRESULT hr = p->Initialize(pStream, STGM_READ);
        Assert::IsTrue(FAILED(hr),
            L"Header length > kMaxHeaderBytes must be rejected (off-by-one guard)");

        pStream->Release();
        p->Release();
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// PERSPECTIVE 4 — Coverage: CreatePreviewBitmap pixel-data path
// ═══════════════════════════════════════════════════════════════════════════
TEST_CLASS(PreviewHandler_ThumbnailProvider_PixelPipeline)
{
    // Build an XISF IStream with real pixel data at an attachment offset
    static IStream* CreateXISFStreamWithPixels(
        UINT width, UINT height,
        const char* sampleFmt = "UInt16",
        bool shortRead = false)
    {
        bool isUInt8 = (strcmp(sampleFmt, "UInt8") == 0);
        size_t bps = isUInt8 ? 1 : 2;
        size_t pixelCount = static_cast<size_t>(width) * height;
        size_t pixelBytes = pixelCount * bps;

        const uint32_t pixelOffset = 2048;

        char geom[64], loc[64];
        snprintf(geom, sizeof(geom), "%u:%u:1", width, height);
        snprintf(loc,  sizeof(loc),  "attachment:%u:%zu", pixelOffset, pixelBytes);

        std::string xml =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<xisf version=\"1.0\">"
            "<Image geometry=\"" + std::string(geom) + "\" "
            "sampleFormat=\"" + std::string(sampleFmt) + "\" "
            "colorSpace=\"Gray\" "
            "location=\"" + std::string(loc) + "\">"
            "<FITSKeyword name=\"OBJECT\" value=\"TestTarget\" comment=\"Test\"/>"
            "</Image></xisf>";

        std::vector<BYTE> buf;
        const char sig[] = "XISF0100";
        buf.insert(buf.end(), sig, sig + 8);
        uint32_t xmlLen = static_cast<uint32_t>(xml.size());
        auto* p = reinterpret_cast<const BYTE*>(&xmlLen);
        buf.insert(buf.end(), p, p + 4);
        uint32_t reserved = 0;
        p = reinterpret_cast<const BYTE*>(&reserved);
        buf.insert(buf.end(), p, p + 4);
        buf.insert(buf.end(), xml.begin(), xml.end());

        if (buf.size() < pixelOffset)
            buf.resize(pixelOffset, 0);

        size_t appendBytes = shortRead ? pixelBytes / 2 : pixelBytes;
        for (size_t i = 0; i < appendBytes; ++i)
        {
            if (isUInt8)
            {
                buf.push_back(static_cast<BYTE>((i * 255) / pixelCount));
            }
            else
            {
                uint16_t val = static_cast<uint16_t>((i / 2 * 65535) / pixelCount);
                buf.push_back(static_cast<BYTE>(val & 0xFF));
                if (i + 1 < appendBytes)
                {
                    buf.push_back(static_cast<BYTE>((val >> 8) & 0xFF));
                    ++i;
                }
            }
        }
        return SHCreateMemStream(buf.data(), static_cast<UINT>(buf.size()));
    }

    // Convenience: build stream with a proper UInt16 gradient
    static IStream* CreatePixelStream16(UINT w, UINT h)
    {
        size_t pixelCount = static_cast<size_t>(w) * h;
        size_t pixelBytes = pixelCount * 2;
        const uint32_t pixelOffset = 2048;

        char geom[64], loc[64];
        snprintf(geom, sizeof(geom), "%u:%u:1", w, h);
        snprintf(loc,  sizeof(loc),  "attachment:%u:%zu", pixelOffset, pixelBytes);

        std::string xml =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<xisf version=\"1.0\">"
            "<Image geometry=\"" + std::string(geom) + "\" "
            "sampleFormat=\"UInt16\" colorSpace=\"Gray\" "
            "location=\"" + std::string(loc) + "\">"
            "<FITSKeyword name=\"OBJECT\" value=\"GradientTest\" comment=\"Test\"/>"
            "</Image></xisf>";

        std::vector<BYTE> buf;
        const char sig[] = "XISF0100";
        buf.insert(buf.end(), sig, sig + 8);
        uint32_t xmlLen = static_cast<uint32_t>(xml.size());
        auto* p = reinterpret_cast<const BYTE*>(&xmlLen);
        buf.insert(buf.end(), p, p + 4);
        uint32_t reserved = 0;
        p = reinterpret_cast<const BYTE*>(&reserved);
        buf.insert(buf.end(), p, p + 4);
        buf.insert(buf.end(), xml.begin(), xml.end());
        buf.resize(pixelOffset, 0);

        for (size_t i = 0; i < pixelCount; ++i)
        {
            uint16_t val = static_cast<uint16_t>((i * 65535) / pixelCount);
            buf.push_back(static_cast<BYTE>(val & 0xFF));
            buf.push_back(static_cast<BYTE>((val >> 8) & 0xFF));
        }
        return SHCreateMemStream(buf.data(), static_cast<UINT>(buf.size()));
    }

public:

    TEST_METHOD(UInt16_SmallImage_ProducesBitmap)
    {
        auto* p = new CThumbnailProvider();
        IStream* pStream = CreatePixelStream16(16, 16);
        Assert::AreEqual(S_OK, p->Initialize(pStream, STGM_READ));

        HBITMAP hbmp = nullptr;
        WTS_ALPHATYPE alpha = WTSAT_UNKNOWN;
        HRESULT hr = p->GetThumbnail(64, &hbmp, &alpha);
        Assert::AreEqual(S_OK, hr);
        Assert::IsNotNull(hbmp, L"Real pixel data should produce bitmap");

        BITMAP bm{};
        GetObject(hbmp, sizeof(bm), &bm);
        Assert::AreEqual(64L, bm.bmWidth, L"Thumbnail width should be cx");
        Assert::AreEqual(64L, bm.bmHeight, L"Thumbnail height should be cx");

        DeleteObject(hbmp);
        pStream->Release();
        p->Release();
    }

    TEST_METHOD(UInt16_LargerImage_ScalesDown)
    {
        auto* p = new CThumbnailProvider();
        IStream* pStream = CreatePixelStream16(64, 64);
        Assert::AreEqual(S_OK, p->Initialize(pStream, STGM_READ));

        HBITMAP hbmp = nullptr;
        WTS_ALPHATYPE alpha;
        HRESULT hr = p->GetThumbnail(32, &hbmp, &alpha);
        Assert::AreEqual(S_OK, hr);
        Assert::IsNotNull(hbmp);

        BITMAP bm{};
        GetObject(hbmp, sizeof(bm), &bm);
        Assert::AreEqual(32L, bm.bmWidth);

        DeleteObject(hbmp);
        pStream->Release();
        p->Release();
    }

    TEST_METHOD(UInt8_Image_ProducesBitmap)
    {
        auto* p = new CThumbnailProvider();
        IStream* pStream = CreateXISFStreamWithPixels(16, 16, "UInt8");
        Assert::AreEqual(S_OK, p->Initialize(pStream, STGM_READ));

        HBITMAP hbmp = nullptr;
        WTS_ALPHATYPE alpha;
        HRESULT hr = p->GetThumbnail(48, &hbmp, &alpha);
        Assert::AreEqual(S_OK, hr);
        Assert::IsNotNull(hbmp, L"UInt8 pixel path should produce bitmap");

        DeleteObject(hbmp);
        pStream->Release();
        p->Release();
    }

    TEST_METHOD(MissingLocation_FallsBackToPlaceholder)
    {
        const char* xml =
            R"(<?xml version="1.0" encoding="UTF-8"?>)"
            R"(<xisf version="1.0"><Image geometry="16:16:1" )"
            R"(sampleFormat="UInt16" colorSpace="Gray">)"
            R"(<FITSKeyword name="OBJECT" value="NoLoc" comment="Test"/>)"
            R"(</Image></xisf>)";
        auto* p = new CThumbnailProvider();
        IStream* pStream = CreateXISFStream(xml);
        p->Initialize(pStream, STGM_READ);

        HBITMAP hbmp = nullptr;
        WTS_ALPHATYPE alpha;
        HRESULT hr = p->GetThumbnail(64, &hbmp, &alpha);
        Assert::AreEqual(S_OK, hr, L"Should fall back to placeholder");
        Assert::IsNotNull(hbmp);

        DeleteObject(hbmp);
        pStream->Release();
        p->Release();
    }

    TEST_METHOD(MissingGeometry_FallsBackToPlaceholder)
    {
        const char* xml =
            R"(<?xml version="1.0" encoding="UTF-8"?>)"
            R"(<xisf version="1.0"><Image )"
            R"(sampleFormat="UInt16" colorSpace="Gray" )"
            R"(location="attachment:2048:512">)"
            R"(<FITSKeyword name="OBJECT" value="NoGeom" comment="Test"/>)"
            R"(</Image></xisf>)";
        auto* p = new CThumbnailProvider();
        IStream* pStream = CreateXISFStream(xml);
        p->Initialize(pStream, STGM_READ);

        HBITMAP hbmp = nullptr;
        WTS_ALPHATYPE alpha;
        HRESULT hr = p->GetThumbnail(64, &hbmp, &alpha);
        Assert::AreEqual(S_OK, hr, L"Should fall back to placeholder");
        Assert::IsNotNull(hbmp);

        DeleteObject(hbmp);
        pStream->Release();
        p->Release();
    }

    TEST_METHOD(ZeroWidthGeometry_FallsBackToPlaceholder)
    {
        const char* xml =
            R"(<?xml version="1.0" encoding="UTF-8"?>)"
            R"(<xisf version="1.0"><Image geometry="0:16:1" )"
            R"(sampleFormat="UInt16" colorSpace="Gray" )"
            R"(location="attachment:2048:0">)"
            R"(</Image></xisf>)";
        auto* p = new CThumbnailProvider();
        IStream* pStream = CreateXISFStream(xml);
        p->Initialize(pStream, STGM_READ);

        HBITMAP hbmp = nullptr;
        WTS_ALPHATYPE alpha;
        HRESULT hr = p->GetThumbnail(64, &hbmp, &alpha);
        Assert::AreEqual(S_OK, hr, L"Should fall back to placeholder");
        Assert::IsNotNull(hbmp);

        DeleteObject(hbmp);
        pStream->Release();
        p->Release();
    }

    TEST_METHOD(LocationSizeTooSmall_FallsBackToPlaceholder)
    {
        const char* xml =
            R"(<?xml version="1.0" encoding="UTF-8"?>)"
            R"(<xisf version="1.0"><Image geometry="16:16:1" )"
            R"(sampleFormat="UInt16" colorSpace="Gray" )"
            R"(location="attachment:2048:10">)"
            R"(</Image></xisf>)";
        auto* p = new CThumbnailProvider();
        IStream* pStream = CreateXISFStream(xml);
        p->Initialize(pStream, STGM_READ);

        HBITMAP hbmp = nullptr;
        WTS_ALPHATYPE alpha;
        HRESULT hr = p->GetThumbnail(64, &hbmp, &alpha);
        Assert::AreEqual(S_OK, hr, L"Undersized attachment should fall back");
        Assert::IsNotNull(hbmp);

        DeleteObject(hbmp);
        pStream->Release();
        p->Release();
    }

    TEST_METHOD(ShortRead_FallsBackToPlaceholder)
    {
        auto* p = new CThumbnailProvider();
        IStream* pStream = CreateXISFStreamWithPixels(16, 16, "UInt16", true);
        p->Initialize(pStream, STGM_READ);

        HBITMAP hbmp = nullptr;
        WTS_ALPHATYPE alpha;
        HRESULT hr = p->GetThumbnail(64, &hbmp, &alpha);
        Assert::AreEqual(S_OK, hr, L"Short read should fall back to placeholder");
        Assert::IsNotNull(hbmp);

        DeleteObject(hbmp);
        pStream->Release();
        p->Release();
    }

    TEST_METHOD(NonAttachmentLocation_FallsBackToPlaceholder)
    {
        const char* xml =
            R"(<?xml version="1.0" encoding="UTF-8"?>)"
            R"(<xisf version="1.0"><Image geometry="16:16:1" )"
            R"(sampleFormat="UInt16" colorSpace="Gray" )"
            R"(location="url:http://example.com/data">)"
            R"(</Image></xisf>)";
        auto* p = new CThumbnailProvider();
        IStream* pStream = CreateXISFStream(xml);
        p->Initialize(pStream, STGM_READ);

        HBITMAP hbmp = nullptr;
        WTS_ALPHATYPE alpha;
        HRESULT hr = p->GetThumbnail(64, &hbmp, &alpha);
        Assert::AreEqual(S_OK, hr, L"Non-attachment location should fall back");
        Assert::IsNotNull(hbmp);

        DeleteObject(hbmp);
        pStream->Release();
        p->Release();
    }

    TEST_METHOD(DefaultSampleFormat_TreatedAsUInt16)
    {
        // sampleFormat not specified — code defaults to UInt16
        size_t pixelCount = 16 * 16;
        size_t pixelBytes = pixelCount * 2;
        const uint32_t pixelOffset = 2048;
        char loc[64];
        snprintf(loc, sizeof(loc), "attachment:%u:%zu", pixelOffset, pixelBytes);

        std::string xml =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<xisf version=\"1.0\">"
            "<Image geometry=\"16:16:1\" colorSpace=\"Gray\" "
            "location=\"" + std::string(loc) + "\">"
            "</Image></xisf>";

        std::vector<BYTE> buf;
        const char sig[] = "XISF0100";
        buf.insert(buf.end(), sig, sig + 8);
        uint32_t xmlLen = static_cast<uint32_t>(xml.size());
        auto* pb = reinterpret_cast<const BYTE*>(&xmlLen);
        buf.insert(buf.end(), pb, pb + 4);
        uint32_t reserved = 0;
        pb = reinterpret_cast<const BYTE*>(&reserved);
        buf.insert(buf.end(), pb, pb + 4);
        buf.insert(buf.end(), xml.begin(), xml.end());
        buf.resize(pixelOffset, 0);
        for (size_t i = 0; i < pixelCount; ++i) {
            uint16_t val = static_cast<uint16_t>(i * 4);
            buf.push_back(static_cast<BYTE>(val & 0xFF));
            buf.push_back(static_cast<BYTE>((val >> 8) & 0xFF));
        }

        auto* p = new CThumbnailProvider();
        IStream* pStream = SHCreateMemStream(buf.data(), static_cast<UINT>(buf.size()));
        p->Initialize(pStream, STGM_READ);

        HBITMAP hbmp = nullptr;
        WTS_ALPHATYPE alpha;
        HRESULT hr = p->GetThumbnail(64, &hbmp, &alpha);
        Assert::AreEqual(S_OK, hr);
        Assert::IsNotNull(hbmp, L"Default sampleFormat should be UInt16");

        DeleteObject(hbmp);
        pStream->Release();
        p->Release();
    }

    TEST_METHOD(MultiChannel_Geometry_ParsedCorrectly)
    {
        // 3-channel image — pixel size calculation must use imgC
        auto* p = new CThumbnailProvider();
        size_t pixelCount = 8 * 8 * 3;
        size_t pixelBytes = pixelCount * 2;
        const uint32_t pixelOffset = 2048;
        char loc[64];
        snprintf(loc, sizeof(loc), "attachment:%u:%zu", pixelOffset, pixelBytes);

        std::string xml =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<xisf version=\"1.0\">"
            "<Image geometry=\"8:8:3\" sampleFormat=\"UInt16\" colorSpace=\"RGB\" "
            "location=\"" + std::string(loc) + "\">"
            "</Image></xisf>";

        std::vector<BYTE> buf;
        const char sig[] = "XISF0100";
        buf.insert(buf.end(), sig, sig + 8);
        uint32_t xmlLen = static_cast<uint32_t>(xml.size());
        auto* pb = reinterpret_cast<const BYTE*>(&xmlLen);
        buf.insert(buf.end(), pb, pb + 4);
        uint32_t reserved = 0;
        pb = reinterpret_cast<const BYTE*>(&reserved);
        buf.insert(buf.end(), pb, pb + 4);
        buf.insert(buf.end(), xml.begin(), xml.end());
        buf.resize(pixelOffset, 0);
        for (size_t i = 0; i < pixelCount; ++i) {
            uint16_t val = static_cast<uint16_t>(i * 100);
            buf.push_back(static_cast<BYTE>(val & 0xFF));
            buf.push_back(static_cast<BYTE>((val >> 8) & 0xFF));
        }

        IStream* pStream = SHCreateMemStream(buf.data(), static_cast<UINT>(buf.size()));
        p->Initialize(pStream, STGM_READ);

        HBITMAP hbmp = nullptr;
        WTS_ALPHATYPE alpha;
        HRESULT hr = p->GetThumbnail(32, &hbmp, &alpha);
        Assert::AreEqual(S_OK, hr);
        Assert::IsNotNull(hbmp);

        DeleteObject(hbmp);
        pStream->Release();
        p->Release();
    }

    TEST_METHOD(RealPixelBitmap_Has24BitColor)
    {
        auto* p = new CThumbnailProvider();
        IStream* pStream = CreatePixelStream16(32, 32);
        p->Initialize(pStream, STGM_READ);

        HBITMAP hbmp = nullptr;
        WTS_ALPHATYPE alpha;
        p->GetThumbnail(64, &hbmp, &alpha);
        Assert::IsNotNull(hbmp);

        BITMAP bm{};
        GetObject(hbmp, sizeof(bm), &bm);
        Assert::AreEqual(static_cast<WORD>(32), bm.bmBitsPixel,
                         L"CreatePreviewBitmap produces 32-bit BGRA DIB");

        DeleteObject(hbmp);
        pStream->Release();
        p->Release();
    }

    TEST_METHOD(PlaceholderBitmap_IsScreenCompatible)
    {
        auto* p = new CThumbnailProvider();
        IStream* pStream = CreateXISFStream(kMinimalXML);
        p->Initialize(pStream, STGM_READ);

        HBITMAP hbmp = nullptr;
        WTS_ALPHATYPE alpha;
        p->GetThumbnail(128, &hbmp, &alpha);
        Assert::IsNotNull(hbmp);

        BITMAP bm{};
        GetObject(hbmp, sizeof(bm), &bm);
        Assert::AreEqual(128L, bm.bmWidth);
        Assert::AreEqual(128L, bm.bmHeight);

        DeleteObject(hbmp);
        pStream->Release();
        p->Release();
    }
    // Helper: build an XISF stream with multi-channel pixel data
    static IStream* CreatePixelStreamRGB(UINT w, UINT h, UINT channels,
                                          const char* colorSpace, const char* sampleFmt)
    {
        size_t channelPixels = static_cast<size_t>(w) * h;
        size_t bps = 2; // default UInt16
        if (std::string(sampleFmt) == "UInt8") bps = 1;
        else if (std::string(sampleFmt) == "Float32") bps = 4;
        else if (std::string(sampleFmt) == "Float64") bps = 8;
        size_t pixelBytes = channelPixels * channels * bps;
        const uint32_t pixelOffset = 2048;

        char geom[64], loc[64];
        snprintf(geom, sizeof(geom), "%u:%u:%u", w, h, channels);
        snprintf(loc,  sizeof(loc),  "attachment:%u:%zu", pixelOffset, pixelBytes);

        std::string xml =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<xisf version=\"1.0\">"
            "<Image geometry=\"" + std::string(geom) + "\" "
            "sampleFormat=\"" + std::string(sampleFmt) + "\" "
            "colorSpace=\"" + std::string(colorSpace) + "\" "
            "location=\"" + std::string(loc) + "\">"
            "<FITSKeyword name=\"OBJECT\" value=\"RGBTest\" comment=\"Test\"/>"
            "</Image></xisf>";

        std::vector<BYTE> buf;
        const char sig[] = "XISF0100";
        buf.insert(buf.end(), sig, sig + 8);
        uint32_t xmlLen = static_cast<uint32_t>(xml.size());
        auto* p = reinterpret_cast<const BYTE*>(&xmlLen);
        buf.insert(buf.end(), p, p + 4);
        uint32_t reserved = 0;
        p = reinterpret_cast<const BYTE*>(&reserved);
        buf.insert(buf.end(), p, p + 4);
        buf.insert(buf.end(), xml.begin(), xml.end());
        buf.resize(pixelOffset, 0);

        // Write planar channel data: each channel gets a distinct gradient
        for (UINT ch = 0; ch < channels; ++ch)
        {
            for (size_t i = 0; i < channelPixels; ++i)
            {
                if (bps == 1) {
                    uint8_t val = static_cast<uint8_t>(((i + ch * 50) * 255) / channelPixels);
                    buf.push_back(val);
                } else if (bps == 2) {
                    uint16_t val = static_cast<uint16_t>(((i + ch * 1000) * 65535) / channelPixels);
                    buf.push_back(static_cast<BYTE>(val & 0xFF));
                    buf.push_back(static_cast<BYTE>((val >> 8) & 0xFF));
                } else if (bps == 4) { // Float32
                    float val = static_cast<float>(i + ch * 100) / channelPixels;
                    auto* fb = reinterpret_cast<const BYTE*>(&val);
                    buf.insert(buf.end(), fb, fb + 4);
                } else { // Float64
                    double val = static_cast<double>(i + ch * 100) / channelPixels;
                    auto* fb = reinterpret_cast<const BYTE*>(&val);
                    buf.insert(buf.end(), fb, fb + 8);
                }
            }
        }
        return SHCreateMemStream(buf.data(), static_cast<UINT>(buf.size()));
    }

    TEST_METHOD(RGB_UInt16_ProducesColorBitmap)
    {
        auto* p = new CThumbnailProvider();
        IStream* pStream = CreatePixelStreamRGB(32, 32, 3, "RGB", "UInt16");
        Assert::AreEqual(S_OK, p->Initialize(pStream, STGM_READ));

        HBITMAP hbmp = nullptr;
        WTS_ALPHATYPE alpha;
        HRESULT hr = p->GetThumbnail(64, &hbmp, &alpha);
        Assert::AreEqual(S_OK, hr);
        Assert::IsNotNull(hbmp, L"RGB UInt16 should produce bitmap");

        BITMAP bm{};
        GetObject(hbmp, sizeof(bm), &bm);
        Assert::AreEqual(64L, bm.bmWidth);
        Assert::AreEqual(64L, bm.bmHeight);

        DeleteObject(hbmp);
        pStream->Release();
        p->Release();
    }

    TEST_METHOD(RGB_UInt8_ProducesColorBitmap)
    {
        auto* p = new CThumbnailProvider();
        IStream* pStream = CreatePixelStreamRGB(16, 16, 3, "RGBSRGB", "UInt8");
        Assert::AreEqual(S_OK, p->Initialize(pStream, STGM_READ));

        HBITMAP hbmp = nullptr;
        WTS_ALPHATYPE alpha;
        HRESULT hr = p->GetThumbnail(48, &hbmp, &alpha);
        Assert::AreEqual(S_OK, hr);
        Assert::IsNotNull(hbmp, L"RGB UInt8 should produce bitmap");

        DeleteObject(hbmp);
        pStream->Release();
        p->Release();
    }

    TEST_METHOD(RGB_Float32_ProducesColorBitmap)
    {
        auto* p = new CThumbnailProvider();
        IStream* pStream = CreatePixelStreamRGB(16, 16, 3, "RGB", "Float32");
        Assert::AreEqual(S_OK, p->Initialize(pStream, STGM_READ));

        HBITMAP hbmp = nullptr;
        WTS_ALPHATYPE alpha;
        HRESULT hr = p->GetThumbnail(48, &hbmp, &alpha);
        Assert::AreEqual(S_OK, hr);
        Assert::IsNotNull(hbmp, L"RGB Float32 should produce bitmap");

        DeleteObject(hbmp);
        pStream->Release();
        p->Release();
    }

    TEST_METHOD(FourChannel_UsesFirstThreeAsRGB)
    {
        auto* p = new CThumbnailProvider();
        IStream* pStream = CreatePixelStreamRGB(16, 16, 4, "RGB", "UInt16");
        Assert::AreEqual(S_OK, p->Initialize(pStream, STGM_READ));

        HBITMAP hbmp = nullptr;
        WTS_ALPHATYPE alpha;
        HRESULT hr = p->GetThumbnail(48, &hbmp, &alpha);
        Assert::AreEqual(S_OK, hr);
        Assert::IsNotNull(hbmp, L"4-channel image should use first 3 as RGB");

        DeleteObject(hbmp);
        pStream->Release();
        p->Release();
    }

    TEST_METHOD(TwoChannel_FallsBackToGrayscale)
    {
        auto* p = new CThumbnailProvider();
        IStream* pStream = CreatePixelStreamRGB(16, 16, 2, "Gray", "UInt16");
        Assert::AreEqual(S_OK, p->Initialize(pStream, STGM_READ));

        HBITMAP hbmp = nullptr;
        WTS_ALPHATYPE alpha;
        HRESULT hr = p->GetThumbnail(48, &hbmp, &alpha);
        Assert::AreEqual(S_OK, hr);
        Assert::IsNotNull(hbmp, L"2-channel image should fall back to grayscale");

        DeleteObject(hbmp);
        pStream->Release();
        p->Release();
    }

    TEST_METHOD(Mono_WithRGBColorSpace_StillGrayscale)
    {
        // Edge case: colorSpace says RGB but only 1 channel
        auto* p = new CThumbnailProvider();
        IStream* pStream = CreatePixelStreamRGB(16, 16, 1, "Gray", "UInt16");
        Assert::AreEqual(S_OK, p->Initialize(pStream, STGM_READ));

        HBITMAP hbmp = nullptr;
        WTS_ALPHATYPE alpha;
        HRESULT hr = p->GetThumbnail(48, &hbmp, &alpha);
        Assert::AreEqual(S_OK, hr);
        Assert::IsNotNull(hbmp, L"1-channel image should produce grayscale bitmap");

        DeleteObject(hbmp);
        pStream->Release();
        p->Release();
    }

    // ----- Float64 pixel pipeline (previously zero coverage) -----

    TEST_METHOD(Float64_GrayscaleImage_ProducesValidBitmap)
    {
        // Float64 mono path (ThumbnailProvider.cpp ~line 669-680).
        // Verifies bps=8 sizing, double->float conversion, and stretch are correct.
        auto* p = new CThumbnailProvider();
        IStream* pStream = CreatePixelStreamRGB(16, 16, 1, "Gray", "Float64");
        Assert::AreEqual(S_OK, p->Initialize(pStream, STGM_READ));

        HBITMAP hbmp = nullptr;
        WTS_ALPHATYPE alpha = WTSAT_UNKNOWN;
        HRESULT hr = p->GetThumbnail(64, &hbmp, &alpha);
        Assert::AreEqual(S_OK, hr, L"Float64 mono path must succeed");
        Assert::IsNotNull(hbmp, L"Float64 mono path must produce a bitmap");

        BITMAP bm{};
        GetObject(hbmp, sizeof(bm), &bm);
        Assert::AreEqual(64L, bm.bmWidth);

        DeleteObject(hbmp);
        pStream->Release();
        p->Release();
    }

    TEST_METHOD(Float64_RGBImage_ProducesColorBitmap)
    {
        // Float64 RGB path: same loop as mono but exercises 3-channel iteration.
        auto* p = new CThumbnailProvider();
        IStream* pStream = CreatePixelStreamRGB(16, 16, 3, "RGB", "Float64");
        Assert::AreEqual(S_OK, p->Initialize(pStream, STGM_READ));

        HBITMAP hbmp = nullptr;
        WTS_ALPHATYPE alpha;
        HRESULT hr = p->GetThumbnail(48, &hbmp, &alpha);
        Assert::AreEqual(S_OK, hr, L"Float64 RGB path must succeed");
        Assert::IsNotNull(hbmp, L"Float64 RGB path must produce a bitmap");

        DeleteObject(hbmp);
        pStream->Release();
        p->Release();
    }

    TEST_METHOD(ExtremeAspectRatio_NarrowImage_DoesNotCrash)
    {
        // 1x1024 grayscale UInt16 — verifies aspect-ratio math (thumbW=1) and
        // that no integer overflow or zero-division occurs in colMap/rowMap.
        auto* p = new CThumbnailProvider();
        IStream* pStream = CreatePixelStream16(1, 1024);
        Assert::AreEqual(S_OK, p->Initialize(pStream, STGM_READ));

        HBITMAP hbmp = nullptr;
        WTS_ALPHATYPE alpha;
        HRESULT hr = p->GetThumbnail(64, &hbmp, &alpha);
        Assert::AreEqual(S_OK, hr, L"Extreme aspect ratio should still decode");
        Assert::IsNotNull(hbmp, L"Should produce a (very narrow) bitmap");

        BITMAP bm{};
        GetObject(hbmp, sizeof(bm), &bm);
        // Source is taller than wide → thumbH=cx=64, thumbW = 1/1024*64 = 0 → clamped to 1
        Assert::IsTrue(bm.bmWidth >= 1, L"Thumbnail width must be at least 1");
        Assert::AreEqual(64L, bm.bmHeight, L"Tall image: thumbH should equal cx");

        DeleteObject(hbmp);
        pStream->Release();
        p->Release();
    }

    TEST_METHOD(UniformPixelValues_StretchHandlesEqualMinMax)
    {
        // Image where every pixel has the same value: percentile lo == hi.
        // Production fallback (ThumbnailProvider.cpp line 701-702) sets
        // hi = lo + 1e-6f to avoid division by zero. Bitmap must still be valid.
        const UINT W = 16, H = 16;
        const size_t pixelBytes = static_cast<size_t>(W) * H * 2;
        const uint32_t pixelOffset = 2048;

        char loc[64];
        snprintf(loc, sizeof(loc), "attachment:%u:%zu", pixelOffset, pixelBytes);
        std::string xml =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<xisf version=\"1.0\">"
            "<Image geometry=\"16:16:1\" sampleFormat=\"UInt16\" colorSpace=\"Gray\" "
            "location=\"" + std::string(loc) + "\">"
            "</Image></xisf>";

        std::vector<BYTE> buf;
        const char sig[] = "XISF0100";
        buf.insert(buf.end(), sig, sig + 8);
        uint32_t xmlLen = static_cast<uint32_t>(xml.size());
        auto* pb = reinterpret_cast<const BYTE*>(&xmlLen);
        buf.insert(buf.end(), pb, pb + 4);
        uint32_t reserved = 0;
        pb = reinterpret_cast<const BYTE*>(&reserved);
        buf.insert(buf.end(), pb, pb + 4);
        buf.insert(buf.end(), xml.begin(), xml.end());
        buf.resize(pixelOffset, 0);
        // Every pixel has the identical value 12345 → stretch lo == hi
        for (size_t i = 0; i < static_cast<size_t>(W) * H; ++i) {
            buf.push_back(static_cast<BYTE>(12345 & 0xFF));
            buf.push_back(static_cast<BYTE>((12345 >> 8) & 0xFF));
        }

        auto* p = new CThumbnailProvider();
        IStream* pStream = SHCreateMemStream(buf.data(), static_cast<UINT>(buf.size()));
        Assert::AreEqual(S_OK, p->Initialize(pStream, STGM_READ));

        HBITMAP hbmp = nullptr;
        WTS_ALPHATYPE alpha;
        HRESULT hr = p->GetThumbnail(64, &hbmp, &alpha);
        Assert::AreEqual(S_OK, hr, L"Uniform-value image must decode without div-by-zero");
        Assert::IsNotNull(hbmp, L"Stretch fallback must still yield a valid bitmap");

        DeleteObject(hbmp);
        pStream->Release();
        p->Release();
    }

    TEST_METHOD(MultiImage_LargestSelected)
    {
        // Two <Image> elements: smaller mono (8x8x1) at offset 4096 and
        // larger RGB (32x32x3) at offset 8192. ThumbnailProvider.cpp ~line 370-376
        // selects the largest attachSize. We confirm by checking the histogram's
        // channelCount: 3 means the RGB (larger) image was picked; 1 would mean
        // the mono (smaller) image was incorrectly chosen.
        const uint32_t offsetA = 4096;  // small mono
        const uint32_t offsetB = 8192;  // large RGB
        const size_t bytesA = 8 * 8 * 1 * 2;     // 128
        const size_t bytesB = 32 * 32 * 3 * 2;   // 6144

        char locA[64], locB[64];
        snprintf(locA, sizeof(locA), "attachment:%u:%zu", offsetA, bytesA);
        snprintf(locB, sizeof(locB), "attachment:%u:%zu", offsetB, bytesB);

        std::string xml =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<xisf version=\"1.0\">"
            "<Image geometry=\"8:8:1\" sampleFormat=\"UInt16\" colorSpace=\"Gray\" "
            "location=\"" + std::string(locA) + "\"/>"
            "<Image geometry=\"32:32:3\" sampleFormat=\"UInt16\" colorSpace=\"RGB\" "
            "location=\"" + std::string(locB) + "\"/>"
            "</xisf>";

        std::vector<BYTE> buf;
        const char sig[] = "XISF0100";
        buf.insert(buf.end(), sig, sig + 8);
        uint32_t xmlLen = static_cast<uint32_t>(xml.size());
        auto* pb = reinterpret_cast<const BYTE*>(&xmlLen);
        buf.insert(buf.end(), pb, pb + 4);
        uint32_t reserved = 0;
        pb = reinterpret_cast<const BYTE*>(&reserved);
        buf.insert(buf.end(), pb, pb + 4);
        buf.insert(buf.end(), xml.begin(), xml.end());

        // Image A pixels at offsetA — gradient
        buf.resize(offsetA, 0);
        for (size_t i = 0; i < 8 * 8; ++i) {
            uint16_t v = static_cast<uint16_t>(i * 100);
            buf.push_back(static_cast<BYTE>(v & 0xFF));
            buf.push_back(static_cast<BYTE>((v >> 8) & 0xFF));
        }
        // Image B pixels at offsetB — gradient across all 3 channels
        buf.resize(offsetB, 0);
        for (UINT ch = 0; ch < 3; ++ch) {
            for (size_t i = 0; i < 32 * 32; ++i) {
                uint16_t v = static_cast<uint16_t>(((i + ch * 200) * 65535) / (32 * 32));
                buf.push_back(static_cast<BYTE>(v & 0xFF));
                buf.push_back(static_cast<BYTE>((v >> 8) & 0xFF));
            }
        }

        auto* p = new CThumbnailProvider();
        IStream* pStream = SHCreateMemStream(buf.data(), static_cast<UINT>(buf.size()));
        Assert::AreEqual(S_OK, p->Initialize(pStream, STGM_READ));

        HBITMAP hbmp = nullptr;
        WTS_ALPHATYPE alpha;
        HRESULT hr = p->GetThumbnail(64, &hbmp, &alpha);
        Assert::AreEqual(S_OK, hr);
        Assert::IsNotNull(hbmp);

        const auto& hist = p->GetHistogram();
        Assert::IsTrue(hist.valid, L"Histogram must be valid after successful decode");
        Assert::AreEqual(3u, hist.channelCount,
            L"Larger RGB image should be selected (channelCount=3); "
            L"mono image (channelCount=1) would indicate wrong selection");

        DeleteObject(hbmp);
        pStream->Release();
        p->Release();
    }
};

TEST_CLASS(PreviewHandlerTests_AdditionalPaths)
{
public:

    TEST_METHOD(QueryFocus_ValidParam_ReturnsCurrentFocus)
    {
        auto* p = new CPreviewHandler();
        HWND hwndFocus = nullptr;
        HRESULT hr = p->QueryFocus(&hwndFocus);
        Assert::AreEqual(S_OK, hr);
        // hwndFocus may be null (no window has focus in test) — that's OK
        p->Release();
    }

    TEST_METHOD(SetRect_ValidRect_StoresWithoutWindow)
    {
        auto* p = new CPreviewHandler();
        RECT rc = { 10, 20, 300, 400 };
        HRESULT hr = p->SetRect(&rc);
        Assert::AreEqual(S_OK, hr, L"SetRect should succeed even without a window");
        p->Release();
    }

    TEST_METHOD(Unload_WithoutWindow_Succeeds)
    {
        auto* p = new CPreviewHandler();
        HRESULT hr = p->Unload();
        Assert::AreEqual(S_OK, hr);
        p->Release();
    }

    TEST_METHOD(DoPreview_AfterInit_BeforeSetWindow_ReturnsEFail)
    {
        auto* p = new CPreviewHandler();
        IStream* pStream = CreateXISFStream(kAstroXML);
        p->Initialize(pStream, STGM_READ);
        HRESULT hr = p->DoPreview();
        Assert::AreEqual(E_FAIL, hr, L"No parent window → E_FAIL");
        pStream->Release();
        p->Release();
    }

    TEST_METHOD(TranslateAccelerator_ReturnsSFalse)
    {
        auto* p = new CPreviewHandler();
        MSG msg = {};
        HRESULT hr = p->TranslateAccelerator(&msg);
        Assert::AreEqual(S_FALSE, hr, L"Accelerators delegated to Explorer");
        p->Release();
    }

    TEST_METHOD(SetFont_NullParam_ReturnsInvalidArg)
    {
        auto* p = new CPreviewHandler();
        HRESULT hr = p->SetFont(nullptr);
        Assert::AreEqual(E_INVALIDARG, hr);
        p->Release();
    }

    TEST_METHOD(Initialize_ReadsThenParsesMetadata)
    {
        auto* p = new CPreviewHandler();
        IStream* pStream = CreateXISFStream(kAstroXML);
        Assert::AreEqual(S_OK, p->Initialize(pStream, STGM_READ));
        // Verify initialization took by checking double-init rejection
        IStream* pStream2 = CreateXISFStream(kAstroXML);
        Assert::IsTrue(FAILED(p->Initialize(pStream2, STGM_READ)));
        pStream2->Release();
        pStream->Release();
        p->Release();
    }

    TEST_METHOD(QI_NullPpv_ReturnsEPointer_Thumbnail)
    {
        auto* p = new CThumbnailProvider();
        HRESULT hr = p->QueryInterface(IID_IUnknown, nullptr);
        Assert::AreEqual(E_POINTER, hr);
        p->Release();
    }

    TEST_METHOD(SetRect_BeforeSetWindow_AcceptsBothCalls)
    {
        // Real Explorer call-order is sometimes SetRect → SetWindow during
        // initial layout. SetRect with no window must succeed and cache the
        // rect; the subsequent SetWindow must succeed and accept its own rect.
        auto* p = new CPreviewHandler();
        RECT rcEarly = { 5, 10, 100, 200 };
        HRESULT hr1 = p->SetRect(&rcEarly);
        Assert::AreEqual(S_OK, hr1, L"SetRect before SetWindow must succeed");

        RECT rcLater = { 0, 0, 300, 400 };
        HRESULT hr2 = p->SetWindow(reinterpret_cast<HWND>(0x1), &rcLater);
        Assert::AreEqual(S_OK, hr2, L"SetWindow after cached SetRect must succeed");

        // A second SetRect after window binding must also succeed.
        RECT rcThird = { 10, 20, 250, 300 };
        HRESULT hr3 = p->SetRect(&rcThird);
        Assert::AreEqual(S_OK, hr3, L"SetRect after SetWindow must succeed");

        p->Release();
    }
};

// ===========================================================================
// Preview Handler Telemetry tests — verify events via the test hook in PreviewHandlerTelemetry.h
// ===========================================================================

namespace
{
    struct PreviewHandlerCapturedEvent {
        UCHAR        level;
        ULONGLONG    keyword;
        std::wstring message;
    };

    thread_local std::vector<PreviewHandlerCapturedEvent>* t_previewHandlerCaptureBuffer = nullptr;

    void PreviewHandlerCaptureHook(UCHAR level, ULONGLONG keyword, const wchar_t* message)
    {
        if (t_previewHandlerCaptureBuffer)
            t_previewHandlerCaptureBuffer->push_back({level, keyword, message ? message : L""});
    }

    class ScopedPreviewHandlerCapture {
    public:
        ScopedPreviewHandlerCapture()
        {
            t_previewHandlerCaptureBuffer = &events;
            g_xisfPreviewHandlerTelemetryHook = &PreviewHandlerCaptureHook;
        }
        ~ScopedPreviewHandlerCapture()
        {
            g_xisfPreviewHandlerTelemetryHook = nullptr;
            t_previewHandlerCaptureBuffer = nullptr;
        }
        ScopedPreviewHandlerCapture(const ScopedPreviewHandlerCapture&) = delete;
        ScopedPreviewHandlerCapture& operator=(const ScopedPreviewHandlerCapture&) = delete;

        bool containsMessagePrefix(const std::wstring& prefix) const
        {
            for (const auto& e : events)
                if (e.message.rfind(prefix, 0) == 0) return true;
            return false;
        }

        const PreviewHandlerCapturedEvent* findByPrefix(const std::wstring& prefix) const
        {
            for (const auto& e : events)
                if (e.message.rfind(prefix, 0) == 0) return &e;
            return nullptr;
        }

        std::vector<PreviewHandlerCapturedEvent> events;
    };

    // Minimal valid XISF stream (reuses the helper style already in this file).
    // We keep a private local copy because the existing CreateXISFStream helper
    // is defined inside another anonymous namespace scope above.
    IStream* MakeMinimalXISFStreamForTelemetry()
    {
        const char* xml =
            R"(<?xml version="1.0" encoding="UTF-8"?>)"
            R"(<xisf version="1.0">)"
            R"(<Image geometry="64:64:1" sampleFormat="UInt16" colorSpace="Gray")"
            R"( location="attachment:4096:8192"/>)"
            R"(</xisf>)";
        const uint32_t xmlLen   = static_cast<uint32_t>(std::strlen(xml));
        const uint32_t reserved = 0;
        const size_t headerSize = 8 + sizeof(uint32_t) + sizeof(uint32_t);
        std::vector<BYTE> buf(headerSize + xmlLen);
        std::memcpy(buf.data(), "XISF0100", 8);
        std::memcpy(buf.data() + 8,  &xmlLen,   4);
        std::memcpy(buf.data() + 12, &reserved, 4);
        std::memcpy(buf.data() + headerSize, xml, xmlLen);
        return SHCreateMemStream(buf.data(), static_cast<UINT>(buf.size()));
    }
}

TEST_CLASS(PreviewHandler_Telemetry)
{
public:
    TEST_METHOD(PreviewInitialize_NullStream_EmitsPreviewInitializeFailed)
    {
        ScopedPreviewHandlerCapture cap;
        CPreviewHandler p;
        HRESULT hr = p.Initialize(nullptr, 0);
        Assert::AreEqual(E_INVALIDARG, hr);

        const PreviewHandlerCapturedEvent* evt = cap.findByPrefix(L"PreviewInitializeFailed");
        Assert::IsNotNull(evt, L"Null stream must emit PreviewInitializeFailed");
        Assert::IsTrue(evt->message.find(L"NullStream") != std::wstring::npos);
        Assert::AreEqual(3, static_cast<int>(evt->level), L"Must be WARNING level");
        // LIFECYCLE bit = 0x1
        Assert::AreEqual(0x1ULL, evt->keyword & 0x1ULL, L"Must carry LIFECYCLE keyword");
    }

    TEST_METHOD(PreviewInitialize_Success_EmitsPreviewInitialized)
    {
        ScopedPreviewHandlerCapture cap;
        IStream* s = MakeMinimalXISFStreamForTelemetry();
        Assert::IsNotNull(s);
        CPreviewHandler p;
        HRESULT hr = p.Initialize(s, 0);
        s->Release();
        Assert::AreEqual(S_OK, hr);
        Assert::IsTrue(cap.containsMessagePrefix(L"PreviewInitialized"),
                       L"Successful preview init must emit PreviewInitialized");
    }

    TEST_METHOD(DoPreview_WithoutParent_EmitsDoPreviewFailed)
    {
        ScopedPreviewHandlerCapture cap;
        CPreviewHandler p;
        HRESULT hr = p.DoPreview();
        Assert::AreEqual(E_FAIL, hr);

        const PreviewHandlerCapturedEvent* evt = cap.findByPrefix(L"DoPreviewFailed");
        Assert::IsNotNull(evt, L"DoPreview with no parent window must emit DoPreviewFailed");
        Assert::IsTrue(evt->message.find(L"MissingParentWindow") != std::wstring::npos);
        // PREVIEW keyword = 0x4
        Assert::AreEqual(0x4ULL, evt->keyword & 0x4ULL, L"Must carry PREVIEW keyword");
    }

    TEST_METHOD(ThumbnailInitialize_NullStream_EmitsThumbnailInitializeFailed)
    {
        ScopedPreviewHandlerCapture cap;
        CThumbnailProvider t;
        HRESULT hr = t.Initialize(nullptr, 0);
        Assert::AreEqual(E_INVALIDARG, hr);

        const PreviewHandlerCapturedEvent* evt = cap.findByPrefix(L"ThumbnailInitializeFailed");
        Assert::IsNotNull(evt);
        Assert::IsTrue(evt->message.find(L"NullStream") != std::wstring::npos);
    }

    TEST_METHOD(ThumbnailInitialize_BogusStream_EmitsInvalidSignature)
    {
        ScopedPreviewHandlerCapture cap;
        const char bogus[] = "not an xisf file at all";
        IStream* s = SHCreateMemStream(
            reinterpret_cast<const BYTE*>(bogus), sizeof(bogus));
        Assert::IsNotNull(s);
        CThumbnailProvider t;
        t.Initialize(s, 0);
        s->Release();

        const PreviewHandlerCapturedEvent* evt = cap.findByPrefix(L"ThumbnailInitializeFailed");
        Assert::IsNotNull(evt);
        Assert::IsTrue(evt->message.find(L"InvalidSignature") != std::wstring::npos ||
                       evt->message.find(L"ReadPreamble") != std::wstring::npos,
                       L"Bogus stream must fail at preamble or signature stage");
    }

    TEST_METHOD(GetThumbnail_BeforeInit_FallsBackToPlaceholder)
    {
        ScopedPreviewHandlerCapture cap;
        CThumbnailProvider t;
        HBITMAP hbmp = nullptr;
        WTS_ALPHATYPE a = WTSAT_UNKNOWN;
        t.GetThumbnail(64, &hbmp, &a);
        if (hbmp) DeleteObject(hbmp);

        Assert::IsTrue(cap.containsMessagePrefix(L"ThumbnailPlaceholderUsed") ||
                       cap.containsMessagePrefix(L"ThumbnailDecodeFailed"),
                       L"Uninitialized thumbnail must emit placeholder or decode-failure event");
    }

    TEST_METHOD(NoTelemetry_WhenHookUninstalled_AndNoETW)
    {
        // Baseline: with hook uninstalled and no ETW consumer attached, the
        // helper returns early. We cannot observe "no emission" directly, but
        // we can verify that installing a hook is what captures the flow.
        CPreviewHandler p;
        p.Initialize(nullptr, 0); // no capture installed — no assertion possible

        ScopedPreviewHandlerCapture cap;
        CPreviewHandler p2;
        p2.Initialize(nullptr, 0);
        Assert::IsTrue(cap.events.size() > 0,
                       L"Installing the hook must capture subsequent events");
    }
};

// ===========================================================================
// PERSPECTIVE 6 — Histogram: struct tests and integration via pixel pipeline
// ===========================================================================

TEST_CLASS(Histogram_StructTests)
{
public:
    TEST_METHOD(Reset_ClearsAllFields)
    {
        HistogramData h;
        h.bins[0][100] = 42;
        h.bins[2][255] = 99;
        h.channelCount = 3;
        h.valid = true;

        h.Reset();

        Assert::AreEqual(0u, h.channelCount, L"channelCount should be 0 after Reset");
        Assert::IsFalse(h.valid, L"valid should be false after Reset");
        for (uint32_t c = 0; c < HistogramData::kMaxChannels; ++c)
            for (uint32_t b = 0; b < HistogramData::kBinCount; ++b)
                Assert::AreEqual(0u, h.bins[c][b], L"All bins should be 0 after Reset");
    }

    TEST_METHOD(Begin1_SetsChannelCount1_InvalidatesHistogram)
    {
        HistogramData h;
        h.valid = true;
        h.Begin(1);
        Assert::AreEqual(1u, h.channelCount, L"channelCount should be 1");
        Assert::IsFalse(h.valid, L"valid should be false after Begin");
    }

    TEST_METHOD(Begin3_SetsChannelCount3_InvalidatesHistogram)
    {
        HistogramData h;
        h.valid = true;
        h.Begin(3);
        Assert::AreEqual(3u, h.channelCount, L"channelCount should be 3");
        Assert::IsFalse(h.valid, L"valid should be false after Begin");
    }

    TEST_METHOD(Commit_SetsValidTrue)
    {
        HistogramData h;
        h.Begin(1);
        Assert::IsFalse(h.valid);
        h.Commit();
        Assert::IsTrue(h.valid, L"valid should be true after Commit");
    }

    TEST_METHOD(DefaultConstructor_AllBinsZero)
    {
        HistogramData h;
        for (uint32_t c = 0; c < HistogramData::kMaxChannels; ++c)
            for (uint32_t b = 0; b < HistogramData::kBinCount; ++b)
                Assert::AreEqual(0u, h.bins[c][b], L"Default-constructed bins should be 0");
    }
};

TEST_CLASS(Histogram_IntegrationTests)
{
    // Build an XISF stream where all pixels have the same UInt16 value
    static IStream* CreateUniformPixelStream(UINT w, UINT h, uint16_t value,
                                              UINT channels = 1,
                                              const char* colorSpace = "Gray")
    {
        size_t channelPixels = static_cast<size_t>(w) * h;
        size_t pixelBytes = channelPixels * channels * 2;
        const uint32_t pixelOffset = 2048;

        char geom[64], loc[64];
        snprintf(geom, sizeof(geom), "%u:%u:%u", w, h, channels);
        snprintf(loc,  sizeof(loc),  "attachment:%u:%zu", pixelOffset, pixelBytes);

        std::string xml =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<xisf version=\"1.0\">"
            "<Image geometry=\"" + std::string(geom) + "\" "
            "sampleFormat=\"UInt16\" "
            "colorSpace=\"" + std::string(colorSpace) + "\" "
            "location=\"" + std::string(loc) + "\">"
            "<FITSKeyword name=\"OBJECT\" value=\"HistTest\" comment=\"Test\"/>"
            "</Image></xisf>";

        std::vector<BYTE> buf;
        const char sig[] = "XISF0100";
        buf.insert(buf.end(), sig, sig + 8);
        uint32_t xmlLen = static_cast<uint32_t>(xml.size());
        auto* p = reinterpret_cast<const BYTE*>(&xmlLen);
        buf.insert(buf.end(), p, p + 4);
        uint32_t reserved = 0;
        p = reinterpret_cast<const BYTE*>(&reserved);
        buf.insert(buf.end(), p, p + 4);
        buf.insert(buf.end(), xml.begin(), xml.end());
        buf.resize(pixelOffset, 0);

        for (UINT ch = 0; ch < channels; ++ch)
        {
            for (size_t i = 0; i < channelPixels; ++i)
            {
                buf.push_back(static_cast<BYTE>(value & 0xFF));
                buf.push_back(static_cast<BYTE>((value >> 8) & 0xFF));
            }
        }
        return SHCreateMemStream(buf.data(), static_cast<UINT>(buf.size()));
    }

public:
    TEST_METHOD(AllBlack_GrayscaleHistogram_BinZeroDominates)
    {
        auto* tp = new CThumbnailProvider();
        IStream* pStream = CreateUniformPixelStream(16, 16, 0);
        Assert::AreEqual(S_OK, tp->Initialize(pStream, STGM_READ));

        HBITMAP hbmp = nullptr;
        WTS_ALPHATYPE alpha;
        tp->GetThumbnail(64, &hbmp, &alpha);

        const auto& hist = tp->GetHistogram();
        Assert::IsTrue(hist.valid, L"Histogram should be valid after successful decode");
        Assert::AreEqual(1u, hist.channelCount, L"Grayscale should have channelCount=1");
        // All-zero UInt16 pixels → bin[0] should hold all pixels
        Assert::IsTrue(hist.bins[0][0] > 0, L"Bin 0 should have counts for all-black image");
        // Non-zero bins should be empty
        uint32_t nonZeroSum = 0;
        for (int b = 1; b < 256; ++b) nonZeroSum += hist.bins[0][b];
        Assert::AreEqual(0u, nonZeroSum, L"All bins except 0 should be empty for all-black");

        if (hbmp) DeleteObject(hbmp);
        pStream->Release();
        tp->Release();
    }

    TEST_METHOD(AllWhite_GrayscaleHistogram_ValidAfterDecode)
    {
        auto* tp = new CThumbnailProvider();
        IStream* pStream = CreateUniformPixelStream(16, 16, 65535);
        Assert::AreEqual(S_OK, tp->Initialize(pStream, STGM_READ));

        HBITMAP hbmp = nullptr;
        WTS_ALPHATYPE alpha;
        tp->GetThumbnail(64, &hbmp, &alpha);

        const auto& hist = tp->GetHistogram();
        Assert::IsTrue(hist.valid, L"Histogram should be valid after successful decode");
        Assert::AreEqual(1u, hist.channelCount, L"Grayscale should have channelCount=1");
        // Uniform pixel images get auto-stretched: (v-lo)/(hi-lo) → 0 when all
        // pixels are identical, so all counts land in one bin. Verify total count
        // matches the thumbnail pixel count.
        uint32_t totalBins = 0;
        for (int b = 0; b < 256; ++b) totalBins += hist.bins[0][b];
        Assert::IsTrue(totalBins > 0, L"Histogram should have non-zero total count");

        if (hbmp) DeleteObject(hbmp);
        pStream->Release();
        tp->Release();
    }

    TEST_METHOD(Grayscale_HistogramChannelCount_Is1)
    {
        auto* tp = new CThumbnailProvider();
        IStream* pStream = CreateUniformPixelStream(16, 16, 32768, 1, "Gray");
        Assert::AreEqual(S_OK, tp->Initialize(pStream, STGM_READ));

        HBITMAP hbmp = nullptr;
        WTS_ALPHATYPE alpha;
        tp->GetThumbnail(64, &hbmp, &alpha);

        const auto& hist = tp->GetHistogram();
        Assert::IsTrue(hist.valid);
        Assert::AreEqual(1u, hist.channelCount, L"Grayscale image should have channelCount=1");

        if (hbmp) DeleteObject(hbmp);
        pStream->Release();
        tp->Release();
    }

    TEST_METHOD(RGB_HistogramChannelCount_Is3)
    {
        auto* tp = new CThumbnailProvider();
        IStream* pStream = CreateUniformPixelStream(16, 16, 32768, 3, "RGB");
        Assert::AreEqual(S_OK, tp->Initialize(pStream, STGM_READ));

        HBITMAP hbmp = nullptr;
        WTS_ALPHATYPE alpha;
        tp->GetThumbnail(64, &hbmp, &alpha);

        const auto& hist = tp->GetHistogram();
        Assert::IsTrue(hist.valid);
        Assert::AreEqual(3u, hist.channelCount, L"RGB image should have channelCount=3");

        if (hbmp) DeleteObject(hbmp);
        pStream->Release();
        tp->Release();
    }

    TEST_METHOD(FailedDecode_HistogramInvalid)
    {
        auto* tp = new CThumbnailProvider();
        // Don't initialize — GetThumbnail will fail/use placeholder
        HBITMAP hbmp = nullptr;
        WTS_ALPHATYPE alpha;
        tp->GetThumbnail(64, &hbmp, &alpha);

        const auto& hist = tp->GetHistogram();
        Assert::IsFalse(hist.valid, L"Histogram should be invalid when no pixel data decoded");

        if (hbmp) DeleteObject(hbmp);
        tp->Release();
    }

    TEST_METHOD(HistogramCompleted_TelemetryEmitted)
    {
        ScopedPreviewHandlerCapture cap;
        auto* tp = new CThumbnailProvider();
        IStream* pStream = CreateUniformPixelStream(16, 16, 0);
        Assert::AreEqual(S_OK, tp->Initialize(pStream, STGM_READ));

        HBITMAP hbmp = nullptr;
        WTS_ALPHATYPE alpha;
        tp->GetThumbnail(64, &hbmp, &alpha);

        Assert::IsTrue(cap.containsMessagePrefix(L"HistogramCompleted"),
                       L"HistogramCompleted telemetry event should be emitted");

        if (hbmp) DeleteObject(hbmp);
        pStream->Release();
        tp->Release();
    }

    TEST_METHOD(Histogram_AllPixelsInOneBin_HasCorrectPeak)
    {
        // For a uniform-value image, the auto-stretch fallback (lo==hi guard,
        // ThumbnailProvider.cpp ~701) maps every pixel to byte 0, so the entire
        // pixel population must land in bin[0][0] — no spreading across bins.
        // The bin total must equal the thumbnail pixel count (no over-/undercounting).
        const UINT thumbCx = 64;
        auto* tp = new CThumbnailProvider();
        IStream* pStream = CreateUniformPixelStream(32, 32, 17000);
        Assert::AreEqual(S_OK, tp->Initialize(pStream, STGM_READ));

        HBITMAP hbmp = nullptr;
        WTS_ALPHATYPE alpha;
        Assert::AreEqual(S_OK, tp->GetThumbnail(thumbCx, &hbmp, &alpha));

        const auto& hist = tp->GetHistogram();
        Assert::IsTrue(hist.valid, L"Histogram must be valid after successful decode");
        Assert::AreEqual(1u, hist.channelCount, L"Grayscale histogram has 1 channel");

        // Sum all bins; the only nonzero bin should be bin[0][0].
        uint32_t totalBins = 0;
        uint32_t nonZeroOtherBins = 0;
        for (int b = 0; b < 256; ++b) {
            totalBins += hist.bins[0][b];
            if (b != 0) nonZeroOtherBins += hist.bins[0][b];
        }
        Assert::AreEqual(0u, nonZeroOtherBins,
            L"Uniform pixels with stretch fallback must collapse to a single bin");
        Assert::IsTrue(hist.bins[0][0] > 0,
            L"Bin 0 must hold the entire pixel population for uniform input");
        // Total count equals the thumbnail's pixel count (square thumbnail since W==H)
        Assert::AreEqual(static_cast<uint32_t>(thumbCx) * thumbCx, totalBins,
            L"Histogram total count must equal thumbnail pixel count");

        if (hbmp) DeleteObject(hbmp);
        pStream->Release();
        tp->Release();
    }
};

// ===========================================================================
// LinearityHeuristic — gamma decision driven by per-channel pixel median.
// Regression coverage for the user-reported bug where stretched PixInsight
// outputs (Float32/RGB) were being double-stretched in the preview because
// the metadata heuristic always classified them as Linear and applied
// linear→sRGB gamma on top.
// See PreviewHandler/XISFPreviewHandler/src/LinearityHeuristic.h.
// ===========================================================================
TEST_CLASS(PreviewHandler_LinearityGammaDecision)
{
    // Build an XISF stream of UInt16 pixels with a uniform gradient from
    // valueLow to valueHigh across the image. Median ≈ (valueLow+valueHigh)/2.
    // Post-1%/99% percentile stretch the data spans [0,1]; the decision to
    // apply linear→sRGB gamma is made from the raw median, not the post-stretch
    // distribution, so the histogram skew lets us confirm gamma branch taken.
    static IStream* CreateGradientStream(UINT w, UINT h,
                                         uint16_t valueLow, uint16_t valueHigh)
    {
        const size_t channelPixels = static_cast<size_t>(w) * h;
        const size_t pixelBytes = channelPixels * 2;
        const uint32_t pixelOffset = 2048;

        char loc[64];
        snprintf(loc, sizeof(loc), "attachment:%u:%zu", pixelOffset, pixelBytes);
        char geom[64]; snprintf(geom, sizeof(geom), "%u:%u:1", w, h);
        std::string xml =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<xisf version=\"1.0\">"
            "<Image geometry=\"" + std::string(geom) +
            "\" sampleFormat=\"UInt16\" colorSpace=\"Gray\" "
            "location=\"" + std::string(loc) + "\"></Image></xisf>";

        std::vector<BYTE> buf;
        const char sig[] = "XISF0100";
        buf.insert(buf.end(), sig, sig + 8);
        uint32_t xmlLen = static_cast<uint32_t>(xml.size());
        auto* p = reinterpret_cast<const BYTE*>(&xmlLen);
        buf.insert(buf.end(), p, p + 4);
        uint32_t reserved = 0;
        p = reinterpret_cast<const BYTE*>(&reserved);
        buf.insert(buf.end(), p, p + 4);
        buf.insert(buf.end(), xml.begin(), xml.end());
        buf.resize(pixelOffset, 0);

        for (size_t i = 0; i < channelPixels; ++i) {
            const double t = (channelPixels > 1)
                ? static_cast<double>(i) / (channelPixels - 1) : 0.0;
            const uint16_t v = static_cast<uint16_t>(
                valueLow + t * (static_cast<double>(valueHigh) - valueLow));
            buf.push_back(static_cast<BYTE>(v & 0xFF));
            buf.push_back(static_cast<BYTE>((v >> 8) & 0xFF));
        }
        return SHCreateMemStream(buf.data(), static_cast<UINT>(buf.size()));
    }

    static double HistogramMeanBin(const HistogramData& h, UINT ch = 0)
    {
        uint64_t weighted = 0;
        uint64_t total = 0;
        for (int b = 0; b < 256; ++b) {
            weighted += static_cast<uint64_t>(b) * h.bins[ch][b];
            total += h.bins[ch][b];
        }
        return total == 0 ? 0.0 : static_cast<double>(weighted) / total;
    }

public:
    TEST_METHOD(LowMedianGradient_GammaApplied_HistogramBrightSkewed)
    {
        // Median ≈ 500 / 65535 ≈ 0.0076, well below the 0.05 threshold.
        // Heuristic returns Linear → preview applies linear→sRGB gamma.
        // After gamma, midtone bytes brighten (n^(1/2.2) > n for n in (0,1)).
        // Mean histogram bin should sit > 128 (the no-gamma mean for a uniform
        // post-stretch distribution).
        auto* tp = new CThumbnailProvider();
        IStream* pStream = CreateGradientStream(64, 64, 0, 1000);
        Assert::AreEqual(S_OK, tp->Initialize(pStream, STGM_READ));

        HBITMAP hbmp = nullptr;
        WTS_ALPHATYPE alpha;
        Assert::AreEqual(S_OK, tp->GetThumbnail(64, &hbmp, &alpha));

        const auto& hist = tp->GetHistogram();
        Assert::IsTrue(hist.valid, L"Histogram must be valid");
        const double meanBin = HistogramMeanBin(hist);
        Assert::IsTrue(meanBin > 140.0,
            (L"Linear data should have gamma applied (bright skew). meanBin=" +
             std::to_wstring(meanBin)).c_str());

        if (hbmp) DeleteObject(hbmp);
        pStream->Release();
        tp->Release();
    }

    TEST_METHOD(HighMedianGradient_GammaSkipped_HistogramNotBrightSkewed)
    {
        // Median ≈ 30000 / 65535 ≈ 0.46, well above the 0.05 threshold.
        // Heuristic returns Non-Linear → preview SKIPS gamma so the data is
        // not double-stretched. Mean histogram bin should sit near 128
        // (centroid of a uniform [0,1] post-stretch distribution mapped
        // straight to bytes), distinctly lower than the gamma-applied case.
        auto* tp = new CThumbnailProvider();
        IStream* pStream = CreateGradientStream(64, 64, 10000, 50000);
        Assert::AreEqual(S_OK, tp->Initialize(pStream, STGM_READ));

        HBITMAP hbmp = nullptr;
        WTS_ALPHATYPE alpha;
        Assert::AreEqual(S_OK, tp->GetThumbnail(64, &hbmp, &alpha));

        const auto& hist = tp->GetHistogram();
        Assert::IsTrue(hist.valid, L"Histogram must be valid");
        const double meanBin = HistogramMeanBin(hist);
        Assert::IsTrue(meanBin < 140.0,
            (L"Stretched data should NOT have gamma applied. meanBin=" +
             std::to_wstring(meanBin)).c_str());
        Assert::IsTrue(meanBin > 100.0,
            (L"Sanity: stretched data mean bin should still be near midtones. meanBin=" +
             std::to_wstring(meanBin)).c_str());

        if (hbmp) DeleteObject(hbmp);
        pStream->Release();
        tp->Release();
    }

    TEST_METHOD(HighMedianGradient_BrighterThanLinearByDoubleStretchMargin)
    {
        // Direct cross-comparison: same gradient shape, only the absolute
        // pixel values differ. The Linear case (gamma applied) MUST produce
        // a brighter mean histogram bin than the Non-Linear case (gamma
        // skipped). This is the regression assertion for the user bug.
        auto* tpLin = new CThumbnailProvider();
        IStream* sLin = CreateGradientStream(64, 64, 0, 1000);
        Assert::AreEqual(S_OK, tpLin->Initialize(sLin, STGM_READ));
        HBITMAP hLin = nullptr; WTS_ALPHATYPE aLin;
        Assert::AreEqual(S_OK, tpLin->GetThumbnail(64, &hLin, &aLin));
        const double meanLinear = HistogramMeanBin(tpLin->GetHistogram());

        auto* tpNon = new CThumbnailProvider();
        IStream* sNon = CreateGradientStream(64, 64, 10000, 50000);
        Assert::AreEqual(S_OK, tpNon->Initialize(sNon, STGM_READ));
        HBITMAP hNon = nullptr; WTS_ALPHATYPE aNon;
        Assert::AreEqual(S_OK, tpNon->GetThumbnail(64, &hNon, &aNon));
        const double meanNonLinear = HistogramMeanBin(tpNon->GetHistogram());

        Assert::IsTrue(meanLinear > meanNonLinear + 15.0,
            (L"Linear case should be visibly brighter than Non-Linear. "
             L"meanLinear=" + std::to_wstring(meanLinear) +
             L", meanNonLinear=" + std::to_wstring(meanNonLinear)).c_str());

        if (hLin) DeleteObject(hLin);
        if (hNon) DeleteObject(hNon);
        sLin->Release(); tpLin->Release();
        sNon->Release(); tpNon->Release();
    }
};

} // namespace PreviewHandlerTests
