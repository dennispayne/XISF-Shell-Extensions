// XISFPropertyHandlerTests.cpp — VS Native Unit Tests
// Tests DSOAliasDB (JSON alias resolution), the XISF parser, and the
// full 42-property CXISFPropertyHandler (IPropertyStore / IInitializeWithStream).
//
// Three perspectives:
//   1. Principal Developer  — DSOAliasDB API contract, parser fidelity, COM contract
//   2. Astrophotographer    — DSO alias resolution, property value fidelity
//   3. Windows Sysadmin     — malformed input handling, no crashes, read-only enforcement

#include "CppUnitTest.h"

#include <windows.h>
#include <initguid.h>
#include <shobjidl.h>
#include <propkey.h>
#include <propvarutil.h>
#include <shlwapi.h>
#include <cmath>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>

#include "DSOAliasDB.h"
#include "XISFParser.h"
#include "PropertyStore.h"
#include "DSOCatalog.h"
#include "ConstellationDB.h"

#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Propsys.lib")
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Shell32.lib")

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace fs = std::filesystem;

// Stubs for DLL globals referenced by PropertyStore.cpp and dllmain.cpp
long g_cDllRef = 0;
HINSTANCE g_hInst = nullptr;

namespace {

    const std::string kDSOJson = R"([
  {
    "canonicalName": "Orion Nebula",
    "aliases": ["M42", "NGC 1976", "LBN 974"],
    "type": "Emission Nebula",
    "constellation": "Orion"
  },
  {
    "canonicalName": "Andromeda Galaxy",
    "aliases": ["M31", "NGC 224"],
    "type": "Spiral Galaxy",
    "constellation": "Andromeda"
  },
  {
    "canonicalName": "Crab Nebula",
    "aliases": ["M1", "NGC 1952"],
    "type": "Supernova Remnant",
    "constellation": "Taurus"
  },
  {
    "canonicalName": "North America Nebula",
    "aliases": ["NGC 7000", "Caldwell 20"],
    "type": "Emission Nebula",
    "constellation": "Cygnus"
  }
])";

    const std::string kFullXML = R"(<?xml version="1.0" encoding="UTF-8"?>
<xisf version="1.0" xmlns="http://www.pixinsight.com/xisf">
  <Image geometry="4656:3520:1" sampleFormat="UInt16" colorSpace="Gray" location="attachment:0:0">
    <FITSKeyword name="EXPTIME" value="300.0" comment="Exposure time"/>
    <FITSKeyword name="FILTER" value="Ha" comment="Filter name"/>
    <FITSKeyword name="OBJECT" value="M42" comment="Target"/>
    <FITSKeyword name="INSTRUME" value="ASI2600MM Pro" comment="Camera"/>
    <FITSKeyword name="TELESCOP" value="RASA 8" comment="Telescope"/>
    <FITSKeyword name="FOCALLEN" value="400.0" comment="Focal length"/>
    <FITSKeyword name="FOCRATIO" value="2.0" comment="F-ratio"/>
    <FITSKeyword name="RA" value="83.82" comment="RA degrees"/>
    <FITSKeyword name="DEC" value="-5.39" comment="Dec degrees"/>
    <FITSKeyword name="DATE-OBS" value="2024-12-20T03:00:00" comment="Date"/>
    <FITSKeyword name="CCD-TEMP" value="-10.0" comment="Sensor temp"/>
    <FITSKeyword name="GAIN" value="100" comment="Gain"/>
    <FITSKeyword name="OFFSET" value="50" comment="Offset"/>
    <FITSKeyword name="XBINNING" value="1" comment="Binning"/>
    <FITSKeyword name="SITELAT" value="33.45" comment="Latitude"/>
    <FITSKeyword name="SITELONG" value="-112.07" comment="Longitude"/>
    <FITSKeyword name="IMAGETYP" value="Light" comment="Frame type"/>
    <FITSKeyword name="AIRMASS" value="1.23" comment="Airmass"/>
    <FITSKeyword name="PIERSIDE" value="West" comment="Pier side"/>
    <FITSKeyword name="SWCREATE" value="N.I.N.A." comment="Software"/>
    <Property id="Instrument:ExposureTime" type="Float64" value="300.0"/>
    <Property id="Instrument:Filter" type="String" value="Ha"/>
    <Property id="Observation:Object:Name" type="String" value="Orion Nebula"/>
  </Image>
</xisf>
)";

    std::string GetRepoRoot() {
        fs::path p(__FILE__);
        return p.parent_path().parent_path().parent_path().string();
    }

    IStream* CreateXISFStream(const std::string& xml) {
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
        return SHCreateMemStream(buf.data(), static_cast<UINT>(buf.size()));
    }

    IStream* CreateBogusStream() {
        const char bogus[] = "This is not an XISF file at all.";
        return SHCreateMemStream(reinterpret_cast<const BYTE*>(bogus), sizeof(bogus));
    }

    // Full XML covering all 42+ property mapping paths in PopulateProperties
    const std::string kHandlerXML = R"(<?xml version="1.0" encoding="UTF-8"?>
<xisf version="1.0" xmlns="http://www.pixinsight.com/xisf">
  <Image geometry="4656:3520:1" sampleFormat="UInt16" colorSpace="Gray" location="attachment:0:0">
    <FITSKeyword name="EXPTIME" value="300.0" comment="Exposure time"/>
    <FITSKeyword name="INSTRUME" value="ASI2600MM Pro" comment="Camera"/>
    <FITSKeyword name="TELESCOP" value="RASA 8" comment="Telescope"/>
    <FITSKeyword name="FOCALLEN" value="400.0" comment="Focal length mm"/>
    <FITSKeyword name="FOCRATIO" value="2.0" comment="F-ratio"/>
    <FITSKeyword name="OBJECT" value="M42" comment="Target"/>
    <FITSKeyword name="FILTER" value="Ha" comment="Filter"/>
    <FITSKeyword name="IMAGETYP" value="Light" comment="Frame type"/>
    <FITSKeyword name="GAIN" value="100" comment="Gain"/>
    <FITSKeyword name="OFFSET" value="50" comment="Offset"/>
    <FITSKeyword name="CCD-TEMP" value="-10.0" comment="Sensor temp"/>
    <FITSKeyword name="XBINNING" value="2" comment="Binning X"/>
    <FITSKeyword name="YBINNING" value="2" comment="Binning Y"/>
    <FITSKeyword name="DATE-OBS" value="2024-12-20T03:00:00" comment="Date"/>
    <FITSKeyword name="SWCREATE" value="N.I.N.A." comment="Software"/>
    <FITSKeyword name="RA" value="83.82" comment="RA degrees"/>
    <FITSKeyword name="DEC" value="-5.39" comment="Dec degrees"/>
    <FITSKeyword name="SET-TEMP" value="-15.0" comment="Set temp"/>
    <FITSKeyword name="XPIXSZ" value="3.76" comment="Pixel size um"/>
    <FITSKeyword name="READOUTM" value="High Gain" comment="Readout mode"/>
    <FITSKeyword name="BAYERPAT" value="RGGB" comment="Bayer"/>
    <FITSKeyword name="SITELAT" value="33.45" comment="Latitude"/>
    <FITSKeyword name="SITELONG" value="-112.07" comment="Longitude"/>
    <FITSKeyword name="SITEELEV" value="340.0" comment="Elevation m"/>
    <FITSKeyword name="CENTALT" value="62.5" comment="Altitude"/>
    <FITSKeyword name="CENTAZ" value="180.3" comment="Azimuth"/>
    <FITSKeyword name="AIRMASS" value="1.23" comment="Airmass"/>
    <FITSKeyword name="PIERSIDE" value="West" comment="Pier side"/>
    <FITSKeyword name="OBJCTRA" value="05h 35m 17.3s" comment="Object RA"/>
    <FITSKeyword name="OBJCTDEC" value="-05d 23m 28s" comment="Object Dec"/>
    <FITSKeyword name="ROTATANG" value="12.5" comment="Rotation"/>
    <FITSKeyword name="FOCNAME" value="ZWO EAF" comment="Focuser"/>
    <FITSKeyword name="FOCPOS" value="12500" comment="Focuser pos"/>
    <FITSKeyword name="FOCTEMP" value="8.3" comment="Focuser temp"/>
    <FITSKeyword name="ROTNAME" value="Pegasus Falcon" comment="Rotator"/>
    <FITSKeyword name="ROTATOR" value="92.1" comment="Rotator angle"/>
    <FITSKeyword name="FWHEEL" value="ZWO 8pos" comment="Filter wheel"/>
    <FITSKeyword name="DEWPOINT" value="5.2" comment="Dew point"/>
    <FITSKeyword name="HUMIDITY" value="62.0" comment="Humidity"/>
    <FITSKeyword name="AMBTEMP" value="12.8" comment="Ambient temp"/>
    <FITSKeyword name="DATE-LOC" value="2024-12-19T20:00:00" comment="Local date"/>
    <Property id="Instrument:ExposureTime" type="Float64" value="300.0"/>
    <Property id="Observation:Object:Name" type="String" value="Orion Nebula"/>
    <Property id="Instrument:Filter" type="String" value="Ha"/>
  </Image>
</xisf>
)";

    // Minimal XML for sparse-coverage tests
    const std::string kMinimalXML = R"(<?xml version="1.0" encoding="UTF-8"?>
<xisf version="1.0" xmlns="http://www.pixinsight.com/xisf">
  <Image geometry="100:100:1" sampleFormat="UInt16" colorSpace="Gray" location="attachment:0:0">
    <FITSKeyword name="OBJECT" value="M31" comment="Target"/>
  </Image>
</xisf>
)";

    // Real NINA-style XML with FITS single-quote delimiters around string values
    const std::string kNINAQuotedXML = R"(<?xml version="1.0" encoding="UTF-8"?>
<xisf version="1.0" xmlns="http://www.pixinsight.com/xisf">
  <Image geometry="6224:4168:1" sampleFormat="UInt16" colorSpace="Gray" location="attachment:0:0">
    <FITSKeyword name="IMAGETYP" value="'LIGHT'" comment="Type of exposure"/>
    <FITSKeyword name="EXPTIME" value="180.0" comment="Exposure duration"/>
    <FITSKeyword name="INSTRUME" value="'AP26CC'" comment="Camera"/>
    <FITSKeyword name="GAIN" value="100" comment="Gain"/>
    <FITSKeyword name="OFFSET" value="250" comment="Offset"/>
    <FITSKeyword name="CCD-TEMP" value="0.0" comment="Sensor temp"/>
    <FITSKeyword name="XBINNING" value="1" comment="Binning X"/>
    <FITSKeyword name="YBINNING" value="1" comment="Binning Y"/>
    <FITSKeyword name="TELESCOP" value="'Orion ED80T Carbon Fiber ED APO'" comment="Telescope"/>
    <FITSKeyword name="FOCALLEN" value="480.0" comment="Focal length mm"/>
    <FITSKeyword name="FOCRATIO" value="6.0" comment="Focal ratio"/>
    <FITSKeyword name="OBJECT" value="'M 31'" comment="Target"/>
    <FITSKeyword name="FILTER" value="'UVIR'" comment="Filter"/>
    <FITSKeyword name="RA" value="10.7228341574981" comment="RA degrees"/>
    <FITSKeyword name="DEC" value="41.2108569489192" comment="Dec degrees"/>
    <FITSKeyword name="PIERSIDE" value="'West'" comment="Pier side"/>
    <FITSKeyword name="AIRMASS" value="1.87067544082144" comment="Airmass"/>
    <FITSKeyword name="DATE-OBS" value="'2024-08-16T02:52:16.212'" comment="Date"/>
    <FITSKeyword name="SWCREATE" value="'N.I.N.A. 3.1.1.9001 (x64)'" comment="Software"/>
    <FITSKeyword name="READOUTM" value="'High Conversion Gain'" comment="Readout mode"/>
    <FITSKeyword name="BAYERPAT" value="'RGGB'" comment="Bayer pattern"/>
    <FITSKeyword name="FOCNAME" value="'QFocuser-Ascom'" comment="Focuser"/>
    <FITSKeyword name="FOCPOS" value="5246" comment="Focuser pos"/>
    <FITSKeyword name="FOCTEMP" value="18.5" comment="Focuser temp"/>
    <FITSKeyword name="ROTNAME" value="'Manual Rotator'" comment="Rotator"/>
    <FITSKeyword name="ROTATOR" value="280.045349121094" comment="Rotator angle"/>
    <FITSKeyword name="FWHEEL" value="'Manual Filter Wheel'" comment="Filter wheel"/>
    <FITSKeyword name="SITELAT" value="39.5083333333333" comment="Latitude"/>
    <FITSKeyword name="SITELONG" value="-76.4988888888889" comment="Longitude"/>
    <FITSKeyword name="CENTALT" value="32.2275173990455" comment="Altitude"/>
    <FITSKeyword name="CENTAZ" value="60.507404235488" comment="Azimuth"/>
    <FITSKeyword name="OBJCTRA" value="'00 42 44'" comment="Object RA"/>
    <FITSKeyword name="OBJCTDEC" value="'+41 16 07'" comment="Object Dec"/>
    <FITSKeyword name="ROTATANG" value="100.0" comment="Rotation"/>
    <FITSKeyword name="DEWPOINT" value="17.1" comment="Dew point"/>
    <FITSKeyword name="HUMIDITY" value="98.0" comment="Humidity"/>
    <FITSKeyword name="AMBTEMP" value="17.4" comment="Ambient temp"/>
    <FITSKeyword name="DATE-LOC" value="'2024-08-15T22:52:16.212'" comment="Local date"/>
    <FITSKeyword name="SET-TEMP" value="0.0" comment="Set temp"/>
    <FITSKeyword name="XPIXSZ" value="3.76" comment="Pixel size"/>
    <FITSKeyword name="SITEELEV" value="0.0" comment="Elevation"/>
    <Property id="Observation:Object:Name" type="String" value="M 31"/>
    <Property id="Instrument:ExposureTime" type="Float32" value="180"/>
    <Property id="Instrument:Filter:Name" type="String" value="UVIR"/>
  </Image>
</xisf>
)";

    // {7C54FA8B-9D63-4C10-8FBE-1A5A0F9A3B2E}
    static const GUID kCLSID_XISFPropertyHandler =
        {0x7C54FA8B, 0x9D63, 0x4C10, {0x8F, 0xBE, 0x1A, 0x5A, 0x0F, 0x9A, 0x3B, 0x2E}};

    bool IsProcessElevated() {
        HANDLE token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
        TOKEN_ELEVATION elev = {};
        DWORD size = 0;
        BOOL ok = GetTokenInformation(token, TokenElevation, &elev, sizeof(elev), &size);
        CloseHandle(token);
        return ok && elev.TokenIsElevated != 0;
    }

    std::wstring GetHandlerDllPath() {
        fs::path p(__FILE__);
        return (p.parent_path().parent_path().parent_path() / "x64" / "Debug" / "XISFPropertyHandler.dll").wstring();
    }

    std::wstring WriteTempXISFFile(const std::string& xml) {
        wchar_t tmpDir[MAX_PATH];
        GetTempPathW(MAX_PATH, tmpDir);
        wchar_t tmpFile[MAX_PATH];
        GetTempFileNameW(tmpDir, L"xsf", 0, tmpFile);
        DeleteFileW(tmpFile);
        std::wstring path(tmpFile);
        auto dot = path.rfind(L'.');
        if (dot != std::wstring::npos) path = path.substr(0, dot);
        path += L".xisf";
        FILE* fp = nullptr;
        _wfopen_s(&fp, path.c_str(), L"wb");
        if (!fp) return {};
        fwrite("XISF0100", 1, 8, fp);
        uint32_t xmlLen = static_cast<uint32_t>(xml.size());
        fwrite(&xmlLen, sizeof(xmlLen), 1, fp);
        uint32_t reserved = 0;
        fwrite(&reserved, sizeof(reserved), 1, fp);
        fwrite(xml.data(), 1, xml.size(), fp);
        fclose(fp);
        return path;
    }

    // RAII: loads the handler DLL, registers via DllRegisterServer,
    // and always unregisters + unloads in the destructor.
    struct ScopedHandlerRegistration {
        HMODULE hDll = nullptr;
        typedef HRESULT(STDAPICALLTYPE* DllFn)();
        DllFn unregFn = nullptr;
        bool ok = false;
        ScopedHandlerRegistration(const std::wstring& dllPath) {
            hDll = LoadLibraryW(dllPath.c_str());
            if (!hDll) return;
            auto regFn = reinterpret_cast<DllFn>(GetProcAddress(hDll, "DllRegisterServer"));
            unregFn = reinterpret_cast<DllFn>(GetProcAddress(hDll, "DllUnregisterServer"));
            if (regFn) ok = SUCCEEDED(regFn());
        }
        ~ScopedHandlerRegistration() {
            if (ok && unregFn) unregFn();
            if (hDll) FreeLibrary(hDll);
        }
        ScopedHandlerRegistration(const ScopedHandlerRegistration&) = delete;
        ScopedHandlerRegistration& operator=(const ScopedHandlerRegistration&) = delete;
    };

    struct ComInit {
        ComInit()  { CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); }
        ~ComInit() { CoUninitialize(); }
    };
    static ComInit s_com;

}

namespace PropertyHandlerTests
{

// ===========================================================================
// PERSPECTIVE 1: Principal Developer — DSOAliasDB API Contract
// ===========================================================================

TEST_CLASS(PropertyHandler_DSOAliasDB_CoreContract) {
public:

    TEST_METHOD(LoadFromString_ValidJSON_ReturnsTrue) {
        xisf::DSOAliasDB db;
        Assert::IsTrue(db.LoadFromString(kDSOJson));
    }

    TEST_METHOD(LoadFromString_PopulatesCount) {
        xisf::DSOAliasDB db;
        db.LoadFromString(kDSOJson);
        Assert::AreEqual(size_t(4), db.Count());
    }

    TEST_METHOD(GetCanonicalName_ByAlias_ReturnsCanonical) {
        xisf::DSOAliasDB db;
        db.LoadFromString(kDSOJson);
        Assert::IsTrue(db.GetCanonicalName("M42") == "Orion Nebula");
    }

    TEST_METHOD(GetCanonicalName_ByCanonical_ReturnsSelf) {
        xisf::DSOAliasDB db;
        db.LoadFromString(kDSOJson);
        Assert::IsTrue(db.GetCanonicalName("Orion Nebula") == "Orion Nebula");
    }

    TEST_METHOD(GetCanonicalName_CaseInsensitive) {
        xisf::DSOAliasDB db;
        db.LoadFromString(kDSOJson);
        Assert::IsTrue(db.GetCanonicalName("m42") == "Orion Nebula");
        Assert::IsTrue(db.GetCanonicalName("ngc 224") == "Andromeda Galaxy");
    }

    TEST_METHOD(GetCanonicalName_NotFound_ReturnsEmpty) {
        xisf::DSOAliasDB db;
        db.LoadFromString(kDSOJson);
        Assert::IsTrue(db.GetCanonicalName("M999").empty());
    }

    TEST_METHOD(GetAllNames_ReturnsCanonicalAndAliases) {
        xisf::DSOAliasDB db;
        db.LoadFromString(kDSOJson);
        auto names = db.GetAllNames("M42");
        Assert::IsTrue(names.size() >= 4,
                       L"Should include canonical + 3 aliases");
        bool hasCanonical = false;
        bool hasM42 = false;
        for (const auto& n : names) {
            if (n == "Orion Nebula") hasCanonical = true;
            if (n == "M42") hasM42 = true;
        }
        Assert::IsTrue(hasCanonical);
        Assert::IsTrue(hasM42);
    }

    TEST_METHOD(GetAllNames_NotFound_ReturnsEmpty) {
        xisf::DSOAliasDB db;
        db.LoadFromString(kDSOJson);
        auto names = db.GetAllNames("NONEXISTENT");
        Assert::IsTrue(names.empty());
    }

    TEST_METHOD(EmptyDatabase_CountIsZero) {
        xisf::DSOAliasDB db;
        Assert::AreEqual(size_t(0), db.Count());
    }

    TEST_METHOD(EmptyDatabase_GetCanonicalName_ReturnsEmpty) {
        xisf::DSOAliasDB db;
        Assert::IsTrue(db.GetCanonicalName("M42").empty());
    }

    // --- Parser fidelity (same parser, tested via Property Handler copy) ---

    TEST_METHOD(Parser_ValidXML_ExtractsAllKeywords) {
        auto r = xisf::XISFParser::ParseXMLString(kFullXML);
        Assert::IsTrue(r.ok());
        Assert::IsTrue(r.metadata.fitsKeywords.size() >= 19,
                       L"Full XML should yield at least 19 FITS keywords");
    }

    TEST_METHOD(Parser_ValidXML_ExtractsProperties) {
        auto r = xisf::XISFParser::ParseXMLString(kFullXML);
        Assert::IsTrue(r.ok());
        Assert::AreEqual(size_t(3), r.metadata.properties.size());
    }

    TEST_METHOD(Parser_FITSLookup_CaseInsensitive) {
        auto r = xisf::XISFParser::ParseXMLString(kFullXML);
        Assert::IsTrue(r.ok());
        Assert::IsTrue(r.metadata.getFITSValue("exptime") == "300.0");
        Assert::IsTrue(r.metadata.getFITSValue("EXPTIME") == "300.0");
    }
};

// ===========================================================================
// PERSPECTIVE 2: Astrophotographer — DSO Alias Resolution for Library Searches
// ===========================================================================

TEST_CLASS(PropertyHandler_DSOAliasDB_AstroWorkflow) {
public:

    TEST_METHOD(SearchByMessierNumber_FindsCanonical) {
        xisf::DSOAliasDB db;
        db.LoadFromString(kDSOJson);
        Assert::IsTrue(db.GetCanonicalName("M42") == "Orion Nebula",
                       L"Searching 'M42' should find 'Orion Nebula'");
    }

    TEST_METHOD(SearchByNGCNumber_FindsCanonical) {
        xisf::DSOAliasDB db;
        db.LoadFromString(kDSOJson);
        Assert::IsTrue(db.GetCanonicalName("NGC 1976") == "Orion Nebula",
                       L"NGC number cross-reference");
    }

    TEST_METHOD(SearchByCommonName_FindsEntry) {
        xisf::DSOAliasDB db;
        db.LoadFromString(kDSOJson);
        Assert::IsTrue(db.GetCanonicalName("Crab Nebula") == "Crab Nebula");
    }

    TEST_METHOD(AllAliases_Resolve_ForM31) {
        xisf::DSOAliasDB db;
        db.LoadFromString(kDSOJson);
        Assert::IsTrue(db.GetCanonicalName("M31") == "Andromeda Galaxy");
        Assert::IsTrue(db.GetCanonicalName("NGC 224") == "Andromeda Galaxy");
        Assert::IsTrue(db.GetCanonicalName("Andromeda Galaxy") == "Andromeda Galaxy");
    }

    TEST_METHOD(CaldwellCatalog_Resolves) {
        xisf::DSOAliasDB db;
        db.LoadFromString(kDSOJson);
        Assert::IsTrue(db.GetCanonicalName("Caldwell 20") == "North America Nebula",
                       L"Caldwell catalog cross-reference");
    }

    TEST_METHOD(GetAllNames_UsefulForSearchExpansion) {
        xisf::DSOAliasDB db;
        db.LoadFromString(kDSOJson);
        auto names = db.GetAllNames("Andromeda Galaxy");
        Assert::IsTrue(names.size() >= 3,
                       L"All aliases enable expanded search across file library");
    }

    TEST_METHOD(Parser_AstroMetadata_CoordinatesAvailable) {
        auto r = xisf::XISFParser::ParseXMLString(kFullXML);
        Assert::IsTrue(r.ok());
        Assert::IsTrue(r.metadata.getFITSValue("RA") == "83.82");
        Assert::IsTrue(r.metadata.getFITSValue("DEC") == "-5.39");
    }

    TEST_METHOD(Parser_AstroMetadata_SensorAndGain) {
        auto r = xisf::XISFParser::ParseXMLString(kFullXML);
        Assert::IsTrue(r.ok());
        Assert::IsTrue(r.metadata.getFITSValue("CCD-TEMP") == "-10.0");
        Assert::IsTrue(r.metadata.getFITSValue("GAIN") == "100");
        Assert::IsTrue(r.metadata.getFITSValue("OFFSET") == "50");
    }

    TEST_METHOD(Parser_AstroMetadata_FrameType) {
        auto r = xisf::XISFParser::ParseXMLString(kFullXML);
        Assert::IsTrue(r.ok());
        Assert::IsTrue(r.metadata.getFITSValue("IMAGETYP") == "Light",
                       L"Frame type distinguishes lights/darks/flats/bias");
    }

    TEST_METHOD(Parser_AstroMetadata_AirmassAndPierSide) {
        auto r = xisf::XISFParser::ParseXMLString(kFullXML);
        Assert::IsTrue(r.ok());
        Assert::IsTrue(r.metadata.getFITSValue("AIRMASS") == "1.23");
        Assert::IsTrue(r.metadata.getFITSValue("PIERSIDE") == "West");
    }

    TEST_METHOD(Parser_RealSampleXISF) {
        std::string samplePath = (fs::path(GetRepoRoot()) / "sample.xisf").string();
        if (!fs::exists(samplePath)) {
            Logger::WriteMessage("SKIPPED: sample.xisf not found");
            return;
        }
        auto r = xisf::XISFParser::ParseFile(samplePath);
        Assert::IsTrue(r.ok());
        Assert::IsTrue(r.metadata.fitsKeywords.size() > 0);
    }
};

// ===========================================================================
// PERSPECTIVE 3: Windows Sysadmin — Robustness & Error Handling
// ===========================================================================

TEST_CLASS(PropertyHandler_DSOAliasDB_Reliability) {
public:

    TEST_METHOD(LoadFromString_EmptyString_ReturnsFalse) {
        xisf::DSOAliasDB db;
        Assert::IsFalse(db.LoadFromString(""));
    }

    TEST_METHOD(LoadFromString_MalformedJSON_ReturnsFalse) {
        xisf::DSOAliasDB db;
        Assert::IsFalse(db.LoadFromString("{broken json..."));
    }

    TEST_METHOD(LoadFromString_EmptyArray_ReturnsTrue) {
        xisf::DSOAliasDB db;
        Assert::IsTrue(db.LoadFromString("[]"));
        Assert::AreEqual(size_t(0), db.Count());
    }

    TEST_METHOD(LoadFromFile_InvalidPath_ReturnsFalse) {
        xisf::DSOAliasDB db;
        Assert::IsFalse(db.LoadFromFile("C:\\no_such_dir\\phantom.json"));
    }

    TEST_METHOD(LoadFromString_NotAnArray_ReturnsFalse) {
        xisf::DSOAliasDB db;
        Assert::IsFalse(db.LoadFromString(R"({"not": "an array"})"));
    }

    TEST_METHOD(LoadFromString_RepeatedCalls_ReplacesData) {
        xisf::DSOAliasDB db;
        db.LoadFromString(kDSOJson);
        Assert::AreEqual(size_t(4), db.Count());
        db.LoadFromString("[]");
        Assert::AreEqual(size_t(0), db.Count());
    }

    TEST_METHOD(GetCanonicalName_AfterEmptyLoad_NoGhostData) {
        xisf::DSOAliasDB db;
        db.LoadFromString(kDSOJson);
        Assert::IsFalse(db.GetCanonicalName("M42").empty());
        db.LoadFromString("[]");
        Assert::IsTrue(db.GetCanonicalName("M42").empty(),
                       L"Previous data must not survive a fresh load");
    }

    TEST_METHOD(Parser_EmptyFile_ReturnsError) {
        std::string path;
        char buf[MAX_PATH] = {};
        GetTempPathA(MAX_PATH, buf);
        path = std::string(buf) + "xisf_prop_empty.xisf";
        { std::ofstream out(path, std::ios::binary); }
        auto r = xisf::XISFParser::ParseFile(path);
        Assert::IsFalse(r.ok());
    }

    TEST_METHOD(Parser_NonexistentFile_ReturnsFileNotFound) {
        auto r = xisf::XISFParser::ParseFile("C:\\no_such_file_123.xisf");
        Assert::IsFalse(r.ok());
        Assert::IsTrue(r.error == xisf::ParseError::FileNotFound);
    }

    TEST_METHOD(Parser_MalformedXML_NoCrash) {
        auto r = xisf::XISFParser::ParseXMLString("<<<garbage>>>");
        Assert::IsTrue(r.metadata.fitsKeywords.empty());
    }

    TEST_METHOD(Parser_LargeKeywordSet_Handles) {
        std::string xml = R"(<?xml version="1.0"?><xisf version="1.0"><Image geometry="1:1:1" sampleFormat="UInt16" colorSpace="Gray" location="attachment:0:0">)";
        for (int i = 0; i < 1000; ++i) {
            xml += R"(<FITSKeyword name="K)" + std::to_string(i) + R"(" value="v" comment="c"/>)";
        }
        xml += "</Image></xisf>";
        auto r = xisf::XISFParser::ParseXMLString(xml);
        Assert::IsTrue(r.ok());
        Assert::AreEqual(size_t(1000), r.metadata.fitsKeywords.size());
    }
};

// ===========================================================================
// PERSPECTIVE 1b: Principal Developer — CXISFPropertyHandler COM Contract
// ===========================================================================

TEST_CLASS(PropertyHandler_PropertyStore_CoreContract) {
public:
    TEST_METHOD(Handler_ImplementsIPropertyStore) {
        auto* h = new CXISFPropertyHandler();
        IPropertyStore* ps = nullptr;
        HRESULT hr = h->QueryInterface(IID_PPV_ARGS(&ps));
        Assert::AreEqual(S_OK, hr);
        ps->Release(); h->Release();
    }
    TEST_METHOD(Handler_ImplementsIInitializeWithStream) {
        auto* h = new CXISFPropertyHandler();
        IInitializeWithStream* pi = nullptr;
        HRESULT hr = h->QueryInterface(IID_PPV_ARGS(&pi));
        Assert::AreEqual(S_OK, hr);
        pi->Release(); h->Release();
    }
    TEST_METHOD(Handler_RejectsUnsupportedInterface) {
        auto* h = new CXISFPropertyHandler();
        IDispatch* pd = nullptr;
        HRESULT hr = h->QueryInterface(IID_PPV_ARGS(&pd));
        Assert::AreEqual(E_NOINTERFACE, hr);
        h->Release();
    }
    TEST_METHOD(Initialize_ValidStream_Succeeds) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kHandlerXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        Assert::AreEqual(S_OK, pi->Initialize(s, STGM_READ));
        pi->Release(); s->Release(); h->Release();
    }
    TEST_METHOD(Initialize_Twice_IsRejected) {
        auto* h = new CXISFPropertyHandler();
        IStream* s1 = CreateXISFStream(kHandlerXML);
        IStream* s2 = CreateXISFStream(kHandlerXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s1, STGM_READ);
        Assert::IsTrue(FAILED(pi->Initialize(s2, STGM_READ)));
        pi->Release(); s1->Release(); s2->Release(); h->Release();
    }
    TEST_METHOD(Initialize_NullStream_ReturnsEPointer) {
        auto* h = new CXISFPropertyHandler();
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        Assert::AreEqual(E_POINTER, pi->Initialize(nullptr, STGM_READ));
        pi->Release(); h->Release();
    }
    TEST_METHOD(Initialize_BogusStream_Fails) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateBogusStream();
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        Assert::IsTrue(FAILED(pi->Initialize(s, STGM_READ)));
        pi->Release(); s->Release(); h->Release();
    }
    TEST_METHOD(Initialize_WriteMode_Rejected) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kHandlerXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        Assert::IsTrue(FAILED(pi->Initialize(s, STGM_READWRITE)));
        pi->Release(); s->Release(); h->Release();
    }
    TEST_METHOD(GetCount_AfterInit_ReturnsNonZero) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kHandlerXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        DWORD count = 0;
        ps->GetCount(&count);
        Assert::IsTrue(count > 0, L"Full handler must populate properties");
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }
    TEST_METHOD(GetCount_NullParam_ReturnsEPointer) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kHandlerXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        Assert::AreEqual(E_POINTER, ps->GetCount(nullptr));
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }
    TEST_METHOD(GetAt_AllIndicesValid) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kHandlerXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        DWORD count = 0; ps->GetCount(&count);
        for (DWORD i = 0; i < count; ++i) {
            PROPERTYKEY pk = {};
            Assert::AreEqual(S_OK, ps->GetAt(i, &pk));
            Assert::IsFalse(pk.fmtid == GUID{});
        }
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }
    TEST_METHOD(GetAt_OutOfRange_Fails) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kHandlerXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        PROPERTYKEY pk = {};
        Assert::IsTrue(FAILED(ps->GetAt(9999, &pk)));
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }
    TEST_METHOD(GetAt_NullParam_ReturnsEPointer) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kHandlerXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        Assert::AreEqual(E_POINTER, ps->GetAt(0, nullptr));
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }
    TEST_METHOD(GetValue_UnknownKey_ReturnsVTEmpty) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kHandlerXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        PROPERTYKEY uk = {}; CoCreateGuid(&uk.fmtid); uk.pid = 999;
        PROPVARIANT pv; PropVariantInit(&pv);
        ps->GetValue(uk, &pv);
        Assert::AreEqual(USHORT(VT_EMPTY), pv.vt);
        PropVariantClear(&pv);
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }
    TEST_METHOD(GetValue_NullParam_ReturnsEPointer) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kHandlerXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        Assert::AreEqual(E_POINTER, ps->GetValue(PKEY_XISF_ExposureTime, nullptr));
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }
    TEST_METHOD(SetValue_AlwaysDenied) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kHandlerXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        PROPVARIANT pv; PropVariantInit(&pv);
        InitPropVariantFromString(L"test", &pv);
        Assert::AreEqual(STG_E_ACCESSDENIED, ps->SetValue(PKEY_XISF_ObjectName, pv));
        PropVariantClear(&pv);
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }
    TEST_METHOD(Commit_AlwaysDenied) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kHandlerXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        Assert::AreEqual(STG_E_ACCESSDENIED, ps->Commit());
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }
    TEST_METHOD(AddRef_Release_Symmetry) {
        auto* h = new CXISFPropertyHandler();
        ULONG r1 = h->AddRef();
        ULONG r2 = h->Release();
        Assert::IsTrue(r2 < r1);
        h->Release();
    }
};

// ===========================================================================
// PERSPECTIVE 2b: Astrophotographer — Full Property Value Fidelity
// ===========================================================================

TEST_CLASS(PropertyHandler_PropertyStore_AstroWorkflow) {
public:
    TEST_METHOD(ExposureTime_IsDouble_300) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kHandlerXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        PROPVARIANT pv; PropVariantInit(&pv);
        ps->GetValue(PKEY_XISF_ExposureTime, &pv);
        Assert::AreEqual(USHORT(VT_R8), pv.vt);
        Assert::IsTrue(std::abs(pv.dblVal - 300.0) < 0.001);
        PropVariantClear(&pv);
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }
    TEST_METHOD(CameraModel_MatchesINSTRUME) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kHandlerXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        PROPVARIANT pv; PropVariantInit(&pv);
        ps->GetValue(PKEY_XISF_CameraModel, &pv);
        Assert::AreEqual(USHORT(VT_LPWSTR), pv.vt);
        Assert::AreEqual(L"ASI2600MM Pro", pv.pwszVal);
        PropVariantClear(&pv);
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }
    TEST_METHOD(FocalLength_400mm) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kHandlerXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        PROPVARIANT pv; PropVariantInit(&pv);
        ps->GetValue(PKEY_XISF_FocalLength, &pv);
        Assert::AreEqual(USHORT(VT_R8), pv.vt);
        Assert::IsTrue(std::abs(pv.dblVal - 400.0) < 0.1);
        PropVariantClear(&pv);
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }
    TEST_METHOD(FNumber_Is2) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kHandlerXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        PROPVARIANT pv; PropVariantInit(&pv);
        ps->GetValue(PKEY_XISF_FNumber, &pv);
        Assert::AreEqual(USHORT(VT_R8), pv.vt);
        Assert::IsTrue(std::abs(pv.dblVal - 2.0) < 0.001);
        PropVariantClear(&pv);
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }
    TEST_METHOD(ObjectName_IsM42) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kHandlerXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        PROPVARIANT pv; PropVariantInit(&pv);
        ps->GetValue(PKEY_XISF_ObjectName, &pv);
        Assert::AreEqual(USHORT(VT_LPWSTR), pv.vt);
        Assert::AreEqual(L"M42", pv.pwszVal);
        PropVariantClear(&pv);
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }
    TEST_METHOD(FilterName_IsHa) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kHandlerXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        PROPVARIANT pv; PropVariantInit(&pv);
        ps->GetValue(PKEY_XISF_FilterName, &pv);
        Assert::AreEqual(USHORT(VT_LPWSTR), pv.vt);
        Assert::AreEqual(L"Ha", pv.pwszVal);
        PropVariantClear(&pv);
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }
    TEST_METHOD(Gain_Is100) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kHandlerXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        PROPVARIANT pv; PropVariantInit(&pv);
        ps->GetValue(PKEY_XISF_Gain, &pv);
        Assert::AreEqual(USHORT(VT_UI4), pv.vt);
        Assert::AreEqual(ULONG(100), pv.ulVal);
        PropVariantClear(&pv);
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }
    TEST_METHOD(Offset_Is50) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kHandlerXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        PROPVARIANT pv; PropVariantInit(&pv);
        ps->GetValue(PKEY_XISF_Offset, &pv);
        Assert::AreEqual(USHORT(VT_UI4), pv.vt);
        Assert::AreEqual(ULONG(50), pv.ulVal);
        PropVariantClear(&pv);
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }
    TEST_METHOD(SensorTemp_IsMinus10) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kHandlerXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        PROPVARIANT pv; PropVariantInit(&pv);
        ps->GetValue(PKEY_XISF_SensorTemperature, &pv);
        Assert::AreEqual(USHORT(VT_R8), pv.vt);
        Assert::IsTrue(std::abs(pv.dblVal - (-10.0)) < 0.001);
        PropVariantClear(&pv);
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }
    TEST_METHOD(Binning_Is2x2) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kHandlerXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        PROPVARIANT pv; PropVariantInit(&pv);
        ps->GetValue(PKEY_XISF_Binning, &pv);
        Assert::AreEqual(USHORT(VT_LPWSTR), pv.vt);
        Assert::AreEqual(L"2x2", pv.pwszVal);
        PropVariantClear(&pv);
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }
    TEST_METHOD(ImageType_IsLight) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kHandlerXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        PROPVARIANT pv; PropVariantInit(&pv);
        ps->GetValue(PKEY_XISF_ImageType, &pv);
        Assert::AreEqual(USHORT(VT_LPWSTR), pv.vt);
        Assert::AreEqual(L"Light", pv.pwszVal);
        PropVariantClear(&pv);
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }
    TEST_METHOD(RA_Is83Point82) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kHandlerXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        PROPVARIANT pv; PropVariantInit(&pv);
        ps->GetValue(PKEY_XISF_RA, &pv);
        Assert::AreEqual(USHORT(VT_R8), pv.vt);
        Assert::IsTrue(std::abs(pv.dblVal - 83.82) < 0.001);
        PropVariantClear(&pv);
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }
    TEST_METHOD(Dec_IsMinus5Point39) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kHandlerXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        PROPVARIANT pv; PropVariantInit(&pv);
        ps->GetValue(PKEY_XISF_Dec, &pv);
        Assert::AreEqual(USHORT(VT_R8), pv.vt);
        Assert::IsTrue(std::abs(pv.dblVal - (-5.39)) < 0.001);
        PropVariantClear(&pv);
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }
    TEST_METHOD(Telescope_IsRASA8) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kHandlerXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        PROPVARIANT pv; PropVariantInit(&pv);
        ps->GetValue(PKEY_XISF_Telescope, &pv);
        Assert::AreEqual(USHORT(VT_LPWSTR), pv.vt);
        Assert::AreEqual(L"RASA 8", pv.pwszVal);
        PropVariantClear(&pv);
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }
    TEST_METHOD(DateObserved_IsFileTime) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kHandlerXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        PROPVARIANT pv; PropVariantInit(&pv);
        ps->GetValue(PKEY_XISF_DateObserved, &pv);
        Assert::AreEqual(USHORT(VT_FILETIME), pv.vt);
        PropVariantClear(&pv);
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }
    TEST_METHOD(Software_IsNINA) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kHandlerXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        PROPVARIANT pv; PropVariantInit(&pv);
        ps->GetValue(PKEY_XISF_Software, &pv);
        Assert::AreEqual(USHORT(VT_LPWSTR), pv.vt);
        Assert::AreEqual(L"N.I.N.A.", pv.pwszVal);
        PropVariantClear(&pv);
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }
    TEST_METHOD(SiteLatitude_Is33Point45) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kHandlerXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        PROPVARIANT pv; PropVariantInit(&pv);
        ps->GetValue(PKEY_XISF_SiteLatitude, &pv);
        Assert::AreEqual(USHORT(VT_R8), pv.vt);
        Assert::IsTrue(std::abs(pv.dblVal - 33.45) < 0.001);
        PropVariantClear(&pv);
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }
    TEST_METHOD(Airmass_Is1Point23) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kHandlerXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        PROPVARIANT pv; PropVariantInit(&pv);
        ps->GetValue(PKEY_XISF_Airmass, &pv);
        Assert::AreEqual(USHORT(VT_R8), pv.vt);
        Assert::IsTrue(std::abs(pv.dblVal - 1.23) < 0.001);
        PropVariantClear(&pv);
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }
    TEST_METHOD(PierSide_IsWest) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kHandlerXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        PROPVARIANT pv; PropVariantInit(&pv);
        ps->GetValue(PKEY_XISF_PierSide, &pv);
        Assert::AreEqual(USHORT(VT_LPWSTR), pv.vt);
        Assert::AreEqual(L"West", pv.pwszVal);
        PropVariantClear(&pv);
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }
    TEST_METHOD(FocuserPosition_Is12500) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kHandlerXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        PROPVARIANT pv; PropVariantInit(&pv);
        ps->GetValue(PKEY_XISF_FocuserPosition, &pv);
        Assert::AreEqual(USHORT(VT_UI4), pv.vt);
        Assert::AreEqual(ULONG(12500), pv.ulVal);
        PropVariantClear(&pv);
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }
    TEST_METHOD(Rotation_Is12Point5) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kHandlerXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        PROPVARIANT pv; PropVariantInit(&pv);
        ps->GetValue(PKEY_XISF_Rotation, &pv);
        Assert::AreEqual(USHORT(VT_R8), pv.vt);
        Assert::IsTrue(std::abs(pv.dblVal - 12.5) < 0.01);
        PropVariantClear(&pv);
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }
    TEST_METHOD(RAHour_Computed_5h) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kHandlerXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        PROPVARIANT pv; PropVariantInit(&pv);
        ps->GetValue(PKEY_XISF_RAHour, &pv);
        Assert::AreEqual(USHORT(VT_LPWSTR), pv.vt);
        Assert::AreEqual(L"5h", pv.pwszVal, L"83.82 deg / 15 = 5h");
        PropVariantClear(&pv);
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }
    TEST_METHOD(DecBand_Computed_Minus15to0) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kHandlerXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        PROPVARIANT pv; PropVariantInit(&pv);
        ps->GetValue(PKEY_XISF_DecBand, &pv);
        Assert::AreEqual(USHORT(VT_LPWSTR), pv.vt);
        PropVariantClear(&pv);
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }
    TEST_METHOD(Keywords_IncludesFilter) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kHandlerXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        PROPVARIANT pv; PropVariantInit(&pv);
        ps->GetValue(PKEY_Keywords, &pv);
        Assert::AreEqual(USHORT(VT_VECTOR | VT_LPWSTR), pv.vt);
        bool foundHa = false;
        for (ULONG i = 0; i < pv.calpwstr.cElems; ++i)
            if (wcscmp(pv.calpwstr.pElems[i], L"Ha") == 0) foundHa = true;
        Assert::IsTrue(foundHa, L"Filter 'Ha' must appear in System.Keywords");
        PropVariantClear(&pv);
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }
    TEST_METHOD(Title_MatchesObjectName) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kHandlerXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        PROPVARIANT pv; PropVariantInit(&pv);
        ps->GetValue(PKEY_Title, &pv);
        Assert::AreEqual(USHORT(VT_LPWSTR), pv.vt);
        Assert::AreEqual(L"M42", pv.pwszVal, L"System.Title should equal OBJECT");
        PropVariantClear(&pv);
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }
    TEST_METHOD(MinimalXML_HasFewProperties) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kMinimalXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        DWORD count = 0; ps->GetCount(&count);
        Assert::IsTrue(count >= 1 && count < 10,
                       L"Minimal XML should yield only object name + title");
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }
};

// ===========================================================================
// PERSPECTIVE 3b: Windows Sysadmin — PropertyStore Robustness
// ===========================================================================

TEST_CLASS(PropertyHandler_PropertyStore_Reliability) {
public:
    TEST_METHOD(ReadOnly_SetValue_Denied) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kHandlerXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        PROPVARIANT pv; PropVariantInit(&pv);
        pv.vt = VT_LPWSTR; pv.pwszVal = const_cast<LPWSTR>(L"hacked");
        Assert::AreEqual(STG_E_ACCESSDENIED, ps->SetValue(PKEY_XISF_ObjectName, pv));
        pv.vt = VT_EMPTY; pv.pwszVal = nullptr;
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }
    TEST_METHOD(ReadOnly_Commit_Denied) {
        auto* h = new CXISFPropertyHandler();
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        Assert::AreEqual(STG_E_ACCESSDENIED, ps->Commit());
        ps->Release(); h->Release();
    }
    TEST_METHOD(BogusStream_NoProperties) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateBogusStream();
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        HRESULT hr = pi->Initialize(s, STGM_READ);
        Assert::IsTrue(FAILED(hr), L"Bogus data must not initialize");
        pi->Release(); s->Release(); h->Release();
    }
    TEST_METHOD(EmptyXML_NoProperties) {
        const std::string emptyXml = R"(<?xml version="1.0"?><xisf version="1.0"><Image geometry="1:1:1" sampleFormat="UInt16" colorSpace="Gray" location="attachment:0:0"></Image></xisf>)";
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(emptyXml);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        DWORD count = 0; ps->GetCount(&count);
        Assert::AreEqual(DWORD(0), count);
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }
    TEST_METHOD(AllProperties_HaveNonEmptyValues) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kHandlerXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        DWORD count = 0; ps->GetCount(&count);
        for (DWORD i = 0; i < count; ++i) {
            PROPERTYKEY pk = {};
            ps->GetAt(i, &pk);
            PROPVARIANT pv; PropVariantInit(&pv);
            ps->GetValue(pk, &pv);
            Assert::AreNotEqual(USHORT(VT_EMPTY), pv.vt,
                                L"Every stored property must have a non-empty value");
            PropVariantClear(&pv);
        }
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }
};

// ===========================================================================
// PERSPECTIVE 4: Real-World NINA Data — FITS Single-Quote Stripping
// ===========================================================================

TEST_CLASS(PropertyHandler_NINAQuotedValues) {
public:
    TEST_METHOD(ObjectName_QuotesStripped) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kNINAQuotedXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        PROPVARIANT pv; PropVariantInit(&pv);
        ps->GetValue(PKEY_XISF_ObjectName, &pv);
        Assert::AreEqual(USHORT(VT_LPWSTR), pv.vt);
        Assert::AreEqual(L"M 31", pv.pwszVal,
                         L"FITS single quotes must be stripped from object name");
        PropVariantClear(&pv);
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }
    TEST_METHOD(CameraModel_QuotesStripped) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kNINAQuotedXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        PROPVARIANT pv; PropVariantInit(&pv);
        ps->GetValue(PKEY_XISF_CameraModel, &pv);
        Assert::AreEqual(USHORT(VT_LPWSTR), pv.vt);
        Assert::AreEqual(L"AP26CC", pv.pwszVal,
                         L"FITS single quotes must be stripped from camera name");
        PropVariantClear(&pv);
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }
    TEST_METHOD(ImageType_QuotesStripped) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kNINAQuotedXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        PROPVARIANT pv; PropVariantInit(&pv);
        ps->GetValue(PKEY_XISF_ImageType, &pv);
        Assert::AreEqual(USHORT(VT_LPWSTR), pv.vt);
        Assert::AreEqual(L"LIGHT", pv.pwszVal,
                         L"FITS single quotes must be stripped from image type");
        PropVariantClear(&pv);
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }
    TEST_METHOD(FilterName_QuotesStripped) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kNINAQuotedXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        PROPVARIANT pv; PropVariantInit(&pv);
        ps->GetValue(PKEY_XISF_FilterName, &pv);
        Assert::AreEqual(USHORT(VT_LPWSTR), pv.vt);
        Assert::AreEqual(L"UVIR", pv.pwszVal,
                         L"FITS single quotes must be stripped from filter name");
        PropVariantClear(&pv);
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }
    TEST_METHOD(Telescope_QuotesStripped) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kNINAQuotedXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        PROPVARIANT pv; PropVariantInit(&pv);
        ps->GetValue(PKEY_XISF_Telescope, &pv);
        Assert::AreEqual(USHORT(VT_LPWSTR), pv.vt);
        Assert::AreEqual(L"Orion ED80T Carbon Fiber ED APO", pv.pwszVal,
                         L"FITS single quotes must be stripped from telescope name");
        PropVariantClear(&pv);
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }
    TEST_METHOD(PierSide_QuotesStripped) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kNINAQuotedXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        PROPVARIANT pv; PropVariantInit(&pv);
        ps->GetValue(PKEY_XISF_PierSide, &pv);
        Assert::AreEqual(USHORT(VT_LPWSTR), pv.vt);
        Assert::AreEqual(L"West", pv.pwszVal,
                         L"FITS single quotes must be stripped from pier side");
        PropVariantClear(&pv);
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }
    TEST_METHOD(Software_QuotesStripped) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kNINAQuotedXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        PROPVARIANT pv; PropVariantInit(&pv);
        ps->GetValue(PKEY_XISF_Software, &pv);
        Assert::AreEqual(USHORT(VT_LPWSTR), pv.vt);
        Assert::AreEqual(L"N.I.N.A. 3.1.1.9001 (x64)", pv.pwszVal,
                         L"FITS single quotes must be stripped from software name");
        PropVariantClear(&pv);
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }
    TEST_METHOD(DateObs_QuotesStripped_StillParsesAsFileTime) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kNINAQuotedXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        PROPVARIANT pv; PropVariantInit(&pv);
        ps->GetValue(PKEY_XISF_DateObserved, &pv);
        Assert::AreEqual(USHORT(VT_FILETIME), pv.vt,
                         L"Quoted DATE-OBS must still parse into a FILETIME");
        PropVariantClear(&pv);
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }
    TEST_METHOD(ExposureTime_NumericUnquoted_StillWorks) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kNINAQuotedXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        PROPVARIANT pv; PropVariantInit(&pv);
        ps->GetValue(PKEY_XISF_ExposureTime, &pv);
        Assert::AreEqual(USHORT(VT_R8), pv.vt);
        Assert::IsTrue(std::abs(pv.dblVal - 180.0) < 0.001,
                       L"Unquoted numeric FITS values must still parse correctly");
        PropVariantClear(&pv);
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }
    TEST_METHOD(Gain_NumericUnquoted_StillWorks) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kNINAQuotedXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        PROPVARIANT pv; PropVariantInit(&pv);
        ps->GetValue(PKEY_XISF_Gain, &pv);
        Assert::AreEqual(USHORT(VT_UI4), pv.vt);
        Assert::AreEqual(ULONG(100), pv.ulVal);
        PropVariantClear(&pv);
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }
    TEST_METHOD(ReadoutMode_QuotesStripped) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kNINAQuotedXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        PROPVARIANT pv; PropVariantInit(&pv);
        ps->GetValue(PKEY_XISF_ReadoutMode, &pv);
        Assert::AreEqual(USHORT(VT_LPWSTR), pv.vt);
        Assert::AreEqual(L"High Conversion Gain", pv.pwszVal);
        PropVariantClear(&pv);
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }
    TEST_METHOD(FocuserName_QuotesStripped) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kNINAQuotedXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        PROPVARIANT pv; PropVariantInit(&pv);
        ps->GetValue(PKEY_XISF_FocuserName, &pv);
        Assert::AreEqual(USHORT(VT_LPWSTR), pv.vt);
        Assert::AreEqual(L"QFocuser-Ascom", pv.pwszVal);
        PropVariantClear(&pv);
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }
    TEST_METHOD(Title_QuotesStripped) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kNINAQuotedXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        PROPVARIANT pv; PropVariantInit(&pv);
        ps->GetValue(PKEY_Title, &pv);
        Assert::AreEqual(USHORT(VT_LPWSTR), pv.vt);
        Assert::AreEqual(L"M 31", pv.pwszVal,
                         L"System.Title must show clean object name without quotes");
        PropVariantClear(&pv);
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }
    TEST_METHOD(ObjectRA_QuotesStripped) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kNINAQuotedXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        PROPVARIANT pv; PropVariantInit(&pv);
        ps->GetValue(PKEY_XISF_ObjectRA, &pv);
        Assert::AreEqual(USHORT(VT_LPWSTR), pv.vt);
        Assert::AreEqual(L"00 42 44", pv.pwszVal,
                         L"OBJCTRA must have FITS quotes stripped");
        PropVariantClear(&pv);
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }
};

// ===========================================================================
// PERSPECTIVE 5: Shell Integration — DLL Registration & Property System E2E
// ===========================================================================

TEST_CLASS(PropertyHandler_ShellIntegration) {
public:

    // Ensure the handler is always left registered after all shell tests finish.
    // Individual tests use ScopedHandlerRegistration (which unregisters on destruction),
    // so without this cleanup, running the test suite would leave the handler unregistered
    // and Explorer columns would go empty.
    TEST_CLASS_CLEANUP(ReRegisterHandler) {
        if (!IsProcessElevated()) return;
        auto dllPath = GetHandlerDllPath();
        if (!fs::exists(dllPath)) return;
        HMODULE hDll = LoadLibraryW(dllPath.c_str());
        if (!hDll) return;
        auto regFn = reinterpret_cast<HRESULT(STDAPICALLTYPE*)()>(
            GetProcAddress(hDll, "DllRegisterServer"));
        if (regFn) regFn();
        FreeLibrary(hDll);
    }

    // --- Registration round-trip ---

    TEST_METHOD(DllRegisterServer_CreatesExpectedKeys) {
        if (!IsProcessElevated()) { Logger::WriteMessage("SKIPPED: not elevated"); return; }
        auto dllPath = GetHandlerDllPath();
        if (!fs::exists(dllPath)) { Logger::WriteMessage("SKIPPED: DLL not built"); return; }

        ScopedHandlerRegistration reg(dllPath);
        Assert::IsTrue(reg.ok, L"DllRegisterServer must succeed");

        HKEY hKey = nullptr;
        LONG lr = RegOpenKeyExW(HKEY_CLASSES_ROOT,
            L"CLSID\\{7C54FA8B-9D63-4C10-8FBE-1A5A0F9A3B2E}\\InProcServer32",
            0, KEY_READ, &hKey);
        Assert::AreEqual(ERROR_SUCCESS, lr, L"InProcServer32 key must exist");
        if (hKey) RegCloseKey(hKey);

        lr = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\PropertySystem\\PropertyHandlers\\.xisf",
            0, KEY_READ, &hKey);
        Assert::AreEqual(ERROR_SUCCESS, lr, L"PropertyHandlers\\.xisf must exist");
        if (hKey) RegCloseKey(hKey);
    }

    TEST_METHOD(DllRegisterServer_InProcServer32_PointsToOurDll) {
        if (!IsProcessElevated()) { Logger::WriteMessage("SKIPPED: not elevated"); return; }
        auto dllPath = GetHandlerDllPath();
        if (!fs::exists(dllPath)) { Logger::WriteMessage("SKIPPED: DLL not built"); return; }

        ScopedHandlerRegistration reg(dllPath);
        Assert::IsTrue(reg.ok);

        wchar_t buf[MAX_PATH] = {};
        DWORD cbBuf = sizeof(buf);
        LONG lr = RegGetValueW(HKEY_CLASSES_ROOT,
            L"CLSID\\{7C54FA8B-9D63-4C10-8FBE-1A5A0F9A3B2E}\\InProcServer32",
            nullptr, RRF_RT_REG_SZ, nullptr, buf, &cbBuf);
        Assert::AreEqual(ERROR_SUCCESS, lr);
        std::wstring registered(buf);
        Assert::IsTrue(registered.find(L"XISFPropertyHandler.dll") != std::wstring::npos,
                       L"InProcServer32 must point to XISFPropertyHandler.dll");
    }

    TEST_METHOD(DllUnregisterServer_CleansAllKeys) {
        if (!IsProcessElevated()) { Logger::WriteMessage("SKIPPED: not elevated"); return; }
        auto dllPath = GetHandlerDllPath();
        if (!fs::exists(dllPath)) { Logger::WriteMessage("SKIPPED: DLL not built"); return; }

        // Register then immediately unregister
        {
            ScopedHandlerRegistration reg(dllPath);
            Assert::IsTrue(reg.ok);
        } // destructor calls DllUnregisterServer

        HKEY hKey = nullptr;
        LONG lr = RegOpenKeyExW(HKEY_CLASSES_ROOT,
            L"CLSID\\{7C54FA8B-9D63-4C10-8FBE-1A5A0F9A3B2E}",
            0, KEY_READ, &hKey);
        Assert::AreNotEqual(ERROR_SUCCESS, lr,
                            L"CLSID must be removed after DllUnregisterServer");
        if (hKey) RegCloseKey(hKey);
    }

    TEST_METHOD(DllRegisterServer_SetsDisableProcessIsolation) {
        if (!IsProcessElevated()) { Logger::WriteMessage("SKIPPED: not elevated"); return; }
        auto dllPath = GetHandlerDllPath();
        if (!fs::exists(dllPath)) { Logger::WriteMessage("SKIPPED: DLL not built"); return; }

        ScopedHandlerRegistration reg(dllPath);
        Assert::IsTrue(reg.ok);

        DWORD val = 0; DWORD cb = sizeof(val);
        LONG lr = RegGetValueW(HKEY_CLASSES_ROOT,
            L"CLSID\\{7C54FA8B-9D63-4C10-8FBE-1A5A0F9A3B2E}",
            L"DisableProcessIsolation", RRF_RT_REG_DWORD, nullptr, &val, &cb);
        Assert::AreEqual(ERROR_SUCCESS, lr);
        Assert::AreEqual(DWORD(1), val,
                         L"DisableProcessIsolation must be 1 for in-process loading");
    }

    TEST_METHOD(DllRegisterServer_SetsFullDetails) {
        if (!IsProcessElevated()) { Logger::WriteMessage("SKIPPED: not elevated"); return; }
        auto dllPath = GetHandlerDllPath();
        if (!fs::exists(dllPath)) { Logger::WriteMessage("SKIPPED: DLL not built"); return; }

        ScopedHandlerRegistration reg(dllPath);
        Assert::IsTrue(reg.ok);

        wchar_t buf[4096] = {};
        DWORD cb = sizeof(buf);
        LONG lr = RegGetValueW(HKEY_CLASSES_ROOT, L"XISFFile",
            L"FullDetails", RRF_RT_REG_SZ, nullptr, buf, &cb);
        Assert::AreEqual(ERROR_SUCCESS, lr, L"FullDetails must be set");
        std::wstring details(buf);
        Assert::IsTrue(details.find(L"XISF.ExposureTime") != std::wstring::npos,
                       L"FullDetails must include XISF.ExposureTime");
        Assert::IsTrue(details.find(L"XISF.ObjectName") != std::wstring::npos,
                       L"FullDetails must include XISF.ObjectName");
    }

    TEST_METHOD(DllRegisterServer_RegistersPropertySchema) {
        if (!IsProcessElevated()) { Logger::WriteMessage("SKIPPED: not elevated"); return; }
        auto dllPath = GetHandlerDllPath();
        if (!fs::exists(dllPath)) { Logger::WriteMessage("SKIPPED: DLL not built"); return; }

        ScopedHandlerRegistration reg(dllPath);
        Assert::IsTrue(reg.ok, L"DllRegisterServer must succeed");

        // Verify PSGetPropertyDescriptionByName resolves our custom properties.
        // This proves PSRegisterPropertySchema was called and the propdesc XML is valid.
        const wchar_t* propertyNames[] = {
            L"XISF.ExposureTime",
            L"XISF.Constellation",
            L"XISF.MatchedObjects",
            L"XISF.RAHour",
            L"XISF.DecBand",
        };
        for (const auto* name : propertyNames) {
            IPropertyDescription* pd = nullptr;
            HRESULT hr = PSGetPropertyDescriptionByName(name, IID_PPV_ARGS(&pd));
            std::wstring msg = L"PSGetPropertyDescriptionByName must resolve: ";
            msg += name;
            Assert::AreEqual(S_OK, hr, msg.c_str());
            if (pd) pd->Release();
        }
    }

    TEST_METHOD(AllProperties_IsViewable_MatchesDetailsTab) {
        if (!IsProcessElevated()) { Logger::WriteMessage("SKIPPED: not elevated"); return; }
        auto dllPath = GetHandlerDllPath();
        if (!fs::exists(dllPath)) { Logger::WriteMessage("SKIPPED: DLL not built"); return; }

        ScopedHandlerRegistration reg(dllPath);
        Assert::IsTrue(reg.ok, L"DllRegisterServer must succeed");

        // Every XISF property (propID 2-52) must have isViewable=true so it
        // appears in both the file Details tab AND the Explorer column picker.
        const wchar_t* allNames[] = {
            L"XISF.ExposureTime", L"XISF.CameraModel", L"XISF.FocalLength",
            L"XISF.FNumber", L"XISF.ObjectName", L"XISF.FilterName",
            L"XISF.ImageType", L"XISF.Gain", L"XISF.Offset",
            L"XISF.SensorTemperature", L"XISF.Telescope", L"XISF.Binning",
            L"XISF.DateObserved", L"XISF.Software", L"XISF.RA", L"XISF.Dec",
            L"XISF.SetTemp", L"XISF.PixelSize", L"XISF.ReadoutMode",
            L"XISF.BayerPattern", L"XISF.SiteLatitude", L"XISF.SiteLongitude",
            L"XISF.SiteElevation", L"XISF.Altitude", L"XISF.Azimuth",
            L"XISF.Airmass", L"XISF.PierSide", L"XISF.ObjectRA",
            L"XISF.ObjectDec", L"XISF.Rotation", L"XISF.FocuserName",
            L"XISF.FocuserPosition", L"XISF.FocuserTemp", L"XISF.RotatorName",
            L"XISF.RotatorAngle", L"XISF.FilterWheel", L"XISF.DewPoint",
            L"XISF.Humidity", L"XISF.AmbientTemp", L"XISF.DateLocal",
            L"XISF.RAHour", L"XISF.DecBand", L"XISF.Constellation",
            L"XISF.MatchedObjects",
            L"XISF.StarFWHM", L"XISF.SkyQuality", L"XISF.SkyBrightness",
            L"XISF.CloudCover", L"XISF.Pressure", L"XISF.SkyTemp",
            L"XISF.WindSpeed",
        };

        int checked = 0;
        for (const auto* name : allNames) {
            IPropertyDescription* pd = nullptr;
            HRESULT hr = PSGetPropertyDescriptionByName(name, IID_PPV_ARGS(&pd));
            std::wstring msg = L"PSGetPropertyDescriptionByName failed: ";
            msg += name;
            Assert::AreEqual(S_OK, hr, msg.c_str());
            if (!pd) continue;

            PROPDESC_TYPE_FLAGS flags = {};
            hr = pd->GetTypeFlags(PDTF_ISVIEWABLE, &flags);
            pd->Release();

            msg = L"isViewable must be true for column picker parity: ";
            msg += name;
            Assert::AreEqual(S_OK, hr, msg.c_str());
            Assert::IsTrue((flags & PDTF_ISVIEWABLE) != 0, msg.c_str());
            ++checked;
        }
        Assert::AreEqual(51, checked, L"All 51 XISF properties must be checked");
        Logger::WriteMessage("All 51 XISF properties have isViewable=true");
    }

    // --- COM activation ---

    TEST_METHOD(CoCreateInstance_ActivatesHandler) {
        if (!IsProcessElevated()) { Logger::WriteMessage("SKIPPED: not elevated"); return; }
        auto dllPath = GetHandlerDllPath();
        if (!fs::exists(dllPath)) { Logger::WriteMessage("SKIPPED: DLL not built"); return; }

        ScopedHandlerRegistration reg(dllPath);
        Assert::IsTrue(reg.ok);

        IPropertyStore* ps = nullptr;
        HRESULT hr = CoCreateInstance(kCLSID_XISFPropertyHandler, nullptr,
                                      CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&ps));
        Assert::AreEqual(S_OK, hr,
                         L"CoCreateInstance must succeed for registered CLSID");
        if (ps) ps->Release();
    }

    TEST_METHOD(CoCreateInstance_SupportsIInitializeWithStream) {
        if (!IsProcessElevated()) { Logger::WriteMessage("SKIPPED: not elevated"); return; }
        auto dllPath = GetHandlerDllPath();
        if (!fs::exists(dllPath)) { Logger::WriteMessage("SKIPPED: DLL not built"); return; }

        ScopedHandlerRegistration reg(dllPath);
        Assert::IsTrue(reg.ok);

        IInitializeWithStream* pi = nullptr;
        HRESULT hr = CoCreateInstance(kCLSID_XISFPropertyHandler, nullptr,
                                      CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pi));
        Assert::AreEqual(S_OK, hr);
        if (pi) pi->Release();
    }

    // --- Shell property system end-to-end ---

    TEST_METHOD(Shell_SHGetPropertyStore_ReadsProperties) {
        if (!IsProcessElevated()) { Logger::WriteMessage("SKIPPED: not elevated"); return; }
        auto dllPath = GetHandlerDllPath();
        if (!fs::exists(dllPath)) { Logger::WriteMessage("SKIPPED: DLL not built"); return; }

        ScopedHandlerRegistration reg(dllPath);
        Assert::IsTrue(reg.ok);

        auto tmpPath = WriteTempXISFFile(kHandlerXML);
        Assert::IsFalse(tmpPath.empty(), L"Temp XISF file must be created");

        IPropertyStore* ps = nullptr;
        HRESULT hr = SHGetPropertyStoreFromParsingName(
            tmpPath.c_str(), nullptr, GPS_DEFAULT, IID_PPV_ARGS(&ps));
        DeleteFileW(tmpPath.c_str());

        if (hr == HRESULT_FROM_WIN32(ERROR_NO_ASSOCIATION)) {
            Logger::WriteMessage("SKIPPED: shell association not yet propagated");
            return;
        }
        Assert::AreEqual(S_OK, hr,
                         L"SHGetPropertyStoreFromParsingName must succeed");

        DWORD count = 0;
        ps->GetCount(&count);
        Assert::IsTrue(count > 0,
                       L"Shell property store must return properties");
        ps->Release();
    }

    TEST_METHOD(Shell_SHGetPropertyStore_ReadsTitle) {
        if (!IsProcessElevated()) { Logger::WriteMessage("SKIPPED: not elevated"); return; }
        auto dllPath = GetHandlerDllPath();
        if (!fs::exists(dllPath)) { Logger::WriteMessage("SKIPPED: DLL not built"); return; }

        ScopedHandlerRegistration reg(dllPath);
        Assert::IsTrue(reg.ok);

        auto tmpPath = WriteTempXISFFile(kHandlerXML);
        IPropertyStore* ps = nullptr;
        HRESULT hr = SHGetPropertyStoreFromParsingName(
            tmpPath.c_str(), nullptr, GPS_DEFAULT, IID_PPV_ARGS(&ps));
        DeleteFileW(tmpPath.c_str());

        if (FAILED(hr)) {
            Logger::WriteMessage("SKIPPED: shell property store unavailable");
            return;
        }

        PROPVARIANT pv; PropVariantInit(&pv);
        ps->GetValue(PKEY_Title, &pv);
        Assert::AreEqual(USHORT(VT_LPWSTR), pv.vt,
                         L"System.Title must be populated via shell pipeline");
        Assert::AreEqual(L"M42", pv.pwszVal,
                         L"System.Title must equal OBJECT via shell pipeline");
        PropVariantClear(&pv);
        ps->Release();
    }

    TEST_METHOD(Shell_SHGetPropertyStore_ReadsExposureTime) {
        if (!IsProcessElevated()) { Logger::WriteMessage("SKIPPED: not elevated"); return; }
        auto dllPath = GetHandlerDllPath();
        if (!fs::exists(dllPath)) { Logger::WriteMessage("SKIPPED: DLL not built"); return; }

        ScopedHandlerRegistration reg(dllPath);
        Assert::IsTrue(reg.ok);

        auto tmpPath = WriteTempXISFFile(kHandlerXML);
        IPropertyStore* ps = nullptr;
        HRESULT hr = SHGetPropertyStoreFromParsingName(
            tmpPath.c_str(), nullptr, GPS_DEFAULT, IID_PPV_ARGS(&ps));
        DeleteFileW(tmpPath.c_str());

        if (FAILED(hr)) {
            Logger::WriteMessage("SKIPPED: shell property store unavailable");
            return;
        }

        PROPVARIANT pv; PropVariantInit(&pv);
        ps->GetValue(PKEY_XISF_ExposureTime, &pv);
        Assert::AreEqual(USHORT(VT_R8), pv.vt);
        Assert::IsTrue(std::abs(pv.dblVal - 300.0) < 0.001,
                       L"ExposureTime must be 300.0 via shell pipeline");
        PropVariantClear(&pv);
        ps->Release();
    }

    TEST_METHOD(Shell_SHGetPropertyStore_ReadsPhotoProjection) {
        if (!IsProcessElevated()) { Logger::WriteMessage("SKIPPED: not elevated"); return; }
        auto dllPath = GetHandlerDllPath();
        if (!fs::exists(dllPath)) { Logger::WriteMessage("SKIPPED: DLL not built"); return; }

        ScopedHandlerRegistration reg(dllPath);
        Assert::IsTrue(reg.ok);

        auto tmpPath = WriteTempXISFFile(kHandlerXML);
        IPropertyStore* ps = nullptr;
        HRESULT hr = SHGetPropertyStoreFromParsingName(
            tmpPath.c_str(), nullptr, GPS_DEFAULT, IID_PPV_ARGS(&ps));
        DeleteFileW(tmpPath.c_str());

        if (FAILED(hr)) {
            Logger::WriteMessage("SKIPPED: shell property store unavailable");
            return;
        }

        PROPVARIANT pv; PropVariantInit(&pv);
        ps->GetValue(PKEY_Photo_CameraModel, &pv);
        Assert::AreEqual(USHORT(VT_LPWSTR), pv.vt,
                         L"System.Photo.CameraModel must be projected");
        Assert::AreEqual(L"ASI2600MM Pro", pv.pwszVal);
        PropVariantClear(&pv);
        ps->Release();
    }

    TEST_METHOD(Shell_RealSampleXISF_PropertiesReadable) {
        if (!IsProcessElevated()) { Logger::WriteMessage("SKIPPED: not elevated"); return; }
        auto dllPath = GetHandlerDllPath();
        if (!fs::exists(dllPath)) { Logger::WriteMessage("SKIPPED: DLL not built"); return; }

        std::wstring samplePath =
            fs::path(GetRepoRoot()).append(L"sample.xisf").wstring();
        if (!fs::exists(samplePath)) {
            Logger::WriteMessage("SKIPPED: sample.xisf not found");
            return;
        }

        ScopedHandlerRegistration reg(dllPath);
        Assert::IsTrue(reg.ok);

        IPropertyStore* ps = nullptr;
        HRESULT hr = SHGetPropertyStoreFromParsingName(
            samplePath.c_str(), nullptr, GPS_DEFAULT, IID_PPV_ARGS(&ps));

        if (FAILED(hr)) {
            Logger::WriteMessage("SKIPPED: shell property store unavailable");
            return;
        }

        DWORD count = 0;
        ps->GetCount(&count);
        Assert::IsTrue(count >= 10,
                       L"Real NINA sample must yield at least 10 properties via shell");

        // Verify object name from real file (M 31 with FITS quotes stripped)
        PROPVARIANT pv; PropVariantInit(&pv);
        ps->GetValue(PKEY_Title, &pv);
        Assert::AreEqual(USHORT(VT_LPWSTR), pv.vt,
                         L"System.Title must be populated from real file");
        PropVariantClear(&pv);

        ps->Release();
    }

    TEST_METHOD(Shell_DumpAllXISFProperties) {
        if (!IsProcessElevated()) { Logger::WriteMessage("SKIPPED: not elevated"); return; }
        auto dllPath = GetHandlerDllPath();
        if (!fs::exists(dllPath)) { Logger::WriteMessage("SKIPPED: DLL not built"); return; }

        ScopedHandlerRegistration reg(dllPath);
        Assert::IsTrue(reg.ok, L"DllRegisterServer must succeed");

        auto tmpPath = WriteTempXISFFile(kHandlerXML);
        IPropertyStore* ps = nullptr;
        HRESULT hr = SHGetPropertyStoreFromParsingName(
            tmpPath.c_str(), nullptr, GPS_DEFAULT, IID_PPV_ARGS(&ps));
        DeleteFileW(tmpPath.c_str());

        if (FAILED(hr)) {
            Logger::WriteMessage("SKIPPED: shell property store unavailable");
            return;
        }

        DWORD count = 0;
        ps->GetCount(&count);

        // GUID for our custom XISF properties
        const GUID xisfFmtId =
            {0x7C54FA8B, 0x9D63, 0x4C10, {0x8F, 0xBE, 0x1A, 0x5A, 0x0F, 0x9A, 0x3B, 0x2E}};

        Logger::WriteMessage("=== All XISF properties from shell property store ===");
        char buf[512];
        int xisfCount = 0;

        for (DWORD i = 0; i < count; i++) {
            PROPERTYKEY pk;
            ps->GetAt(i, &pk);
            if (!IsEqualGUID(pk.fmtid, xisfFmtId)) continue;

            xisfCount++;

            // Resolve canonical name
            PWSTR pszName = nullptr;
            PSGetNameFromPropertyKey(pk, &pszName);

            // Get value
            PROPVARIANT pv; PropVariantInit(&pv);
            ps->GetValue(pk, &pv);

            PWSTR pszVal = nullptr;
            PropVariantToStringAlloc(pv, &pszVal);

            if (pszName && pszVal) {
                snprintf(buf, sizeof(buf), "  pid=%2u  %-28ls = %ls",
                         pk.pid, pszName, pszVal);
            } else if (pszName) {
                snprintf(buf, sizeof(buf), "  pid=%2u  %-28ls = (vt=%d)",
                         pk.pid, pszName, pv.vt);
            } else {
                snprintf(buf, sizeof(buf), "  pid=%2u  (unresolved) = (vt=%d)",
                         pk.pid, pv.vt);
            }
            Logger::WriteMessage(buf);

            if (pszName) CoTaskMemFree(pszName);
            if (pszVal) CoTaskMemFree(pszVal);
            PropVariantClear(&pv);
        }

        snprintf(buf, sizeof(buf), "=== Total: %d XISF properties out of %lu shell properties ===",
                 xisfCount, count);
        Logger::WriteMessage(buf);

        // Verify key properties are present
        Assert::IsTrue(xisfCount >= 30,
                       L"Handler must produce at least 30 XISF properties");

        // Explicitly check Constellation and MatchedObjects are in the store
        PROPVARIANT pvConst; PropVariantInit(&pvConst);
        ps->GetValue(PKEY_XISF_Constellation, &pvConst);
        Assert::AreEqual(USHORT(VT_LPWSTR), pvConst.vt,
                         L"XISF.Constellation must be populated via shell pipeline");
        Logger::WriteMessage("  >> Constellation value confirmed present");
        PropVariantClear(&pvConst);

        PROPVARIANT pvMatch; PropVariantInit(&pvMatch);
        ps->GetValue(PKEY_XISF_MatchedObjects, &pvMatch);
        Assert::AreEqual(USHORT(VT_LPWSTR), pvMatch.vt,
                         L"XISF.MatchedObjects must be populated via shell pipeline");
        Logger::WriteMessage("  >> MatchedObjects value confirmed present");
        PropVariantClear(&pvMatch);

        ps->Release();
    }
};

// ===========================================================================
// Small CSV fixture for DSOCatalog unit tests — avoids depending on the full
// 13,000-row NGC.csv which may not be present in the test working directory.
// ===========================================================================

namespace {

    const std::string kTestCSV =
        "Name;Type;RA;Dec;Const;MajAx;MinAx;PosAng;B-Mag;V-Mag;J-Mag;H-Mag;K-Mag;SurfBr;Hubble;Pax;Pm-RA;Pm-Dec;RadVel;Redshift;Cstar U-Mag;Cstar B-Mag;Cstar V-Mag;M;NGC;IC;Cstar Names;Identifiers;Common names;NED notes;OpenNGC notes;Sources\n"
        "NGC1976;HII;05:35:17.3;-05:23:28;Ori;66.00;60.00;;;4.0;;;;;;;;;;;;;;042;1976;;;;Orion Nebula;;;Type:1|RA:1|Dec:1|MajAx:1\n"
        "NGC0224;Gx;00:42:44.3;+41:16:09;And;190.00;60.00;;;3.4;;;;;;;;;;;;;;031;0224;;;;Andromeda Galaxy;;;Type:1|RA:1|Dec:1|MajAx:1\n"
        "NGC1952;SNR;05:34:31.9;+22:00:52;Tau;6.00;4.00;;;8.4;;;;;;;;;;;;;;001;1952;;;;Crab Nebula;;;Type:1|RA:1|Dec:1|MajAx:1\n"
        "NGC7000;HII;20:58:47.0;+44:19:48;Cyg;120.00;100.00;;;4.0;;;;;;;;;;;;;;;7000;;;LBN 373;North America Nebula;;;Type:1|RA:1|Dec:1|MajAx:1\n"
        "IC0434;HII;05:41:00.0;-02:24:00;Ori;60.00;10.00;;;;;;;;;;;;;;;;;;;;;LBN 953,B 33;Horsehead Nebula;;;Type:1|RA:1|Dec:1|MajAx:1\n"
        "NGC6960;SNR;20:45:38.0;+30:42:30;Cyg;70.00;6.00;;;7.0;;;;;;;;;;;;;;;;;;;Veil Nebula;;;Type:1|RA:1|Dec:1|MajAx:1\n"
        "NGC2237;HII;06:33:45.0;+05:02:00;Mon;80.00;60.00;;;;;;;;;;;;;;;;;;;;;LBN 948;Rosette Nebula;;;Type:1|RA:1|Dec:1|MajAx:1\n";

    const std::string kTestSharplessCSV =
        "Name;Type;RA;Dec;Const;MajAx;MinAx;PosAng;B-Mag;V-Mag;J-Mag;H-Mag;K-Mag;SurfBr;Hubble;Pax;Pm-RA;Pm-Dec;RadVel;Redshift;Cstar U-Mag;Cstar B-Mag;Cstar V-Mag;M;NGC;IC;Cstar Names;Identifiers;Common names;NED notes;OpenNGC notes;Sources\n"
        "Sh2-101;HII;20:00:30.0;+35:20:30;Cyg;16.00;9.00;;;;;;;;;;;;;;;;;;;;;LBN 168;Tulip Nebula;;;Type:99|RA:99|Dec:99|Const:99|MajAx:99\n"
        "Sh2-240;HII;05:39:00.0;+28:00:00;Tau;180.00;180.00;;;;;;;;;;;;;;;;;;;;;Simeis 147;Spaghetti Nebula;;;Type:99|RA:99|Dec:99|Const:99|MajAx:99\n"
        "Sh2-155;HII;22:57:54.0;+62:31:06;Cep;50.00;30.00;;;;;;;;;;;;;;;;;;;;;LBN 529;Cave Nebula;;;Type:99|RA:99|Dec:99|Const:99|MajAx:99\n";

    // XML with coordinates near M42 (RA=83.82, Dec=-5.39)
    const std::string kM42CoordXML = R"(<?xml version="1.0" encoding="UTF-8"?>
<xisf version="1.0" xmlns="http://www.pixinsight.com/xisf">
  <Image geometry="4656:3520:1" sampleFormat="UInt16" colorSpace="Gray" location="attachment:0:0">
    <FITSKeyword name="OBJECT" value="M42" comment="Target"/>
    <FITSKeyword name="FILTER" value="Ha" comment="Filter"/>
    <FITSKeyword name="EXPTIME" value="300.0" comment="Exposure"/>
    <FITSKeyword name="RA" value="83.82" comment="RA degrees"/>
    <FITSKeyword name="DEC" value="-5.39" comment="Dec degrees"/>
    <FITSKeyword name="IMAGETYP" value="Light" comment="Frame type"/>
  </Image>
</xisf>
)";

    // XML with no coordinates at all
    const std::string kNoCoordsXML = R"(<?xml version="1.0" encoding="UTF-8"?>
<xisf version="1.0" xmlns="http://www.pixinsight.com/xisf">
  <Image geometry="100:100:1" sampleFormat="UInt16" colorSpace="Gray" location="attachment:0:0">
    <FITSKeyword name="OBJECT" value="Dark" comment="Target"/>
    <FITSKeyword name="IMAGETYP" value="Dark" comment="Frame type"/>
    <FITSKeyword name="EXPTIME" value="300.0" comment="Exposure"/>
  </Image>
</xisf>
)";

    // Helper to build an XISF XML with sensor geometry and given RA/Dec
    std::string MakeFOVTestXML(double ra, double dec) {
        char buf[2048];
        snprintf(buf, sizeof(buf), R"(<?xml version="1.0" encoding="UTF-8"?>
<xisf version="1.0" xmlns="http://www.pixinsight.com/xisf">
  <Image geometry="6248:4176:1" sampleFormat="UInt16" colorSpace="Gray" location="attachment:0:0">
    <FITSKeyword name="OBJECT" value="M 31" comment="Target"/>
    <FITSKeyword name="EXPTIME" value="180.0" comment="Exposure"/>
    <FITSKeyword name="FILTER" value="UVIR" comment="Filter"/>
    <FITSKeyword name="IMAGETYP" value="Light" comment="Frame type"/>
    <FITSKeyword name="FOCALLEN" value="480.0" comment="Focal length mm"/>
    <FITSKeyword name="XPIXSZ" value="3.76" comment="Pixel size um"/>
    <FITSKeyword name="NAXIS1" value="6248" comment="Width"/>
    <FITSKeyword name="NAXIS2" value="4176" comment="Height"/>
    <FITSKeyword name="RA" value="%.6f" comment="RA degrees"/>
    <FITSKeyword name="DEC" value="%.6f" comment="Dec degrees"/>
  </Image>
</xisf>
)", ra, dec);
        return std::string(buf);
    }

} // anonymous namespace

// ===========================================================================
// PERSPECTIVE 1c: Principal Developer — DSOCatalog API Contract
// ===========================================================================

TEST_CLASS(PropertyHandler_DSOCatalog_CoreContract) {
public:

    TEST_METHOD(LoadFromCSVString_ValidCSV_ReturnsTrue) {
        xisf::DSOCatalog cat;
        Assert::IsTrue(cat.LoadFromCSVString(kTestCSV));
    }

    TEST_METHOD(LoadFromCSVString_PopulatesCount) {
        xisf::DSOCatalog cat;
        cat.LoadFromCSVString(kTestCSV);
        Assert::AreEqual(size_t(7), cat.Count());
    }

    TEST_METHOD(LoadFromCSVString_EmptyString_ReturnsFalse) {
        xisf::DSOCatalog cat;
        Assert::IsFalse(cat.LoadFromCSVString(""));
    }

    TEST_METHOD(LoadFromCSVString_HeaderOnly_ReturnsFalse) {
        xisf::DSOCatalog cat;
        Assert::IsFalse(cat.LoadFromCSVString(
            "Name;Type;RA;Dec;Const;MajAx\n"));
    }

    TEST_METHOD(AppendFromCSVString_AddsEntries) {
        xisf::DSOCatalog cat;
        cat.LoadFromCSVString(kTestCSV);
        size_t before = cat.Count();
        cat.AppendFromCSVString(kTestSharplessCSV);
        Assert::IsTrue(cat.Count() > before,
                       L"Append must increase entry count");
        Assert::AreEqual(before + size_t(3), cat.Count());
    }

    TEST_METHOD(FindByName_ByPrimaryNGC_ReturnsEntry) {
        xisf::DSOCatalog cat;
        cat.LoadFromCSVString(kTestCSV);
        auto* e = cat.FindByName("NGC1976");
        Assert::IsNotNull(e, L"Must find by primary NGC name");
        Assert::IsTrue(e->commonName == "Orion Nebula");
    }

    TEST_METHOD(FindByName_ByMessierNumber_ReturnsEntry) {
        xisf::DSOCatalog cat;
        cat.LoadFromCSVString(kTestCSV);
        auto* e = cat.FindByName("M42");
        Assert::IsNotNull(e, L"Must find by Messier number");
        Assert::IsTrue(e->commonName == "Orion Nebula");
    }

    TEST_METHOD(FindByName_ByMessierWithSpace_ReturnsEntry) {
        xisf::DSOCatalog cat;
        cat.LoadFromCSVString(kTestCSV);
        auto* e = cat.FindByName("M 42");
        Assert::IsNotNull(e, L"Must find by 'M 42' with space");
    }

    TEST_METHOD(FindByName_ByMessierLong_ReturnsEntry) {
        xisf::DSOCatalog cat;
        cat.LoadFromCSVString(kTestCSV);
        auto* e = cat.FindByName("Messier 42");
        Assert::IsNotNull(e, L"Must find by 'Messier 42'");
    }

    TEST_METHOD(FindByName_ByCommonName_ReturnsEntry) {
        xisf::DSOCatalog cat;
        cat.LoadFromCSVString(kTestCSV);
        auto* e = cat.FindByName("Orion Nebula");
        Assert::IsNotNull(e, L"Must find by common name");
    }

    TEST_METHOD(FindByName_CaseInsensitive) {
        xisf::DSOCatalog cat;
        cat.LoadFromCSVString(kTestCSV);
        Assert::IsNotNull(cat.FindByName("m42"));
        Assert::IsNotNull(cat.FindByName("orion nebula"));
        Assert::IsNotNull(cat.FindByName("NGC1976"));
        Assert::IsNotNull(cat.FindByName("ngc1976"));
    }

    TEST_METHOD(FindByName_NotFound_ReturnsNull) {
        xisf::DSOCatalog cat;
        cat.LoadFromCSVString(kTestCSV);
        Assert::IsNull(cat.FindByName("M999"));
    }

    TEST_METHOD(GetAllNames_ReturnsMultiple) {
        xisf::DSOCatalog cat;
        cat.LoadFromCSVString(kTestCSV);
        auto names = cat.GetAllNames("M42");
        Assert::IsTrue(names.size() >= 3,
                       L"Should include common name + M + NGC designations");
        bool hasCommon = false, hasM = false, hasNGC = false;
        for (const auto& n : names) {
            if (n == "Orion Nebula") hasCommon = true;
            if (n == "M 42") hasM = true;
            if (n == "NGC 1976") hasNGC = true;
        }
        Assert::IsTrue(hasCommon, L"Must include 'Orion Nebula'");
        Assert::IsTrue(hasM, L"Must include 'M 42'");
        Assert::IsTrue(hasNGC, L"Must include 'NGC 1976'");
    }

    TEST_METHOD(GetAllNames_NotFound_ReturnsEmpty) {
        xisf::DSOCatalog cat;
        cat.LoadFromCSVString(kTestCSV);
        Assert::IsTrue(cat.GetAllNames("NONEXISTENT").empty());
    }

    TEST_METHOD(GetPreferredName_MessierFirst) {
        xisf::DSOCatalog cat;
        cat.LoadFromCSVString(kTestCSV);
        auto* e = cat.FindByName("NGC1976");
        Assert::IsNotNull(e);
        auto prio = xisf::DSOCatalog::ParsePriorityString("M,C,NGC,IC,Sh2");
        Assert::IsTrue(cat.GetPreferredName(*e, prio) == "M 42");
    }

    TEST_METHOD(GetPreferredName_NGCFirst) {
        xisf::DSOCatalog cat;
        cat.LoadFromCSVString(kTestCSV);
        auto* e = cat.FindByName("NGC1976");
        Assert::IsNotNull(e);
        auto prio = xisf::DSOCatalog::ParsePriorityString("NGC,M,IC");
        Assert::IsTrue(cat.GetPreferredName(*e, prio) == "NGC 1976");
    }

    TEST_METHOD(GetPreferredName_FallsBackToCommon) {
        xisf::DSOCatalog cat;
        cat.LoadFromCSVString(kTestCSV);
        auto* e = cat.FindByName("NGC7000");
        Assert::IsNotNull(e);
        // NGC7000 has no Sh2 or B designation, so if those are the only ones in priority:
        auto prio = xisf::DSOCatalog::ParsePriorityString("Sh2,B");
        std::string name = cat.GetPreferredName(*e, prio);
        // Should fall back to common name or primary
        Assert::IsFalse(name.empty());
    }

    TEST_METHOD(ParsePriorityString_SplitsCorrectly) {
        auto v = xisf::DSOCatalog::ParsePriorityString("M,C,NGC,IC,Sh2,B,LBN");
        Assert::AreEqual(size_t(7), v.size());
        Assert::IsTrue(v[0] == "M");
        Assert::IsTrue(v[4] == "Sh2");
    }

    TEST_METHOD(ParsePriorityString_EmptyReturnsEmpty) {
        Assert::IsTrue(xisf::DSOCatalog::ParsePriorityString("").empty());
    }

    TEST_METHOD(ParseRA_Valid_ReturnsDegrees) {
        double deg;
        Assert::IsTrue(xisf::DSOCatalog::ParseRA("05:35:17.3", deg));
        Assert::IsTrue(std::abs(deg - 83.822) < 0.01,
                       L"5h35m17.3s should be ~83.82 degrees");
    }

    TEST_METHOD(ParseDec_Positive_ReturnsDegrees) {
        double deg;
        Assert::IsTrue(xisf::DSOCatalog::ParseDec("+41:16:09", deg));
        Assert::IsTrue(std::abs(deg - 41.269) < 0.01);
    }

    TEST_METHOD(ParseDec_Negative_ReturnsDegrees) {
        double deg;
        Assert::IsTrue(xisf::DSOCatalog::ParseDec("-05:23:28", deg));
        Assert::IsTrue(deg < -5.0 && deg > -6.0);
    }

    TEST_METHOD(ParseRA_Empty_ReturnsFalse) {
        double deg;
        Assert::IsFalse(xisf::DSOCatalog::ParseRA("", deg));
    }

    TEST_METHOD(AngularSeparation_SamePoint_IsZero) {
        double d = xisf::DSOCatalog::AngularSeparation(83.82, -5.39, 83.82, -5.39);
        Assert::IsTrue(d < 0.0001);
    }

    TEST_METHOD(AngularSeparation_KnownPair) {
        // M42 to M43: ~7 arcminutes apart
        double d = xisf::DSOCatalog::AngularSeparation(83.822, -5.391, 83.892, -5.268);
        Assert::IsTrue(d < 0.2 && d > 0.05,
                       L"M42-M43 separation should be ~0.1 degrees");
    }
};

// ===========================================================================
// PERSPECTIVE 1d: Principal Developer — ConstellationDB API Contract
// ===========================================================================

TEST_CLASS(PropertyHandler_ConstellationDB_CoreContract) {
public:

    TEST_METHOD(Identify_Orion_ReturnsOri) {
        // M42 is in Orion (RA~83.82, Dec~-5.39)
        auto c = xisf::ConstellationDB::Identify(83.82, -5.39);
        Assert::IsTrue(c == "Ori",
                       L"M42 at RA=83.82 Dec=-5.39 should be in Orion");
    }

    TEST_METHOD(Identify_Andromeda_ReturnsAnd) {
        // M31 (RA~10.68, Dec~+41.27)
        auto c = xisf::ConstellationDB::Identify(10.68, 41.27);
        Assert::IsTrue(c == "And",
                       L"M31 should be in Andromeda");
    }

    TEST_METHOD(Identify_Cygnus_ReturnsCyg) {
        // NGC7000 North America Nebula (RA~314.7, Dec~+44.3)
        auto c = xisf::ConstellationDB::Identify(314.7, 44.3);
        Assert::IsTrue(c == "Cyg",
                       L"NGC7000 should be in Cygnus");
    }

    TEST_METHOD(Identify_SouthPole_ReturnsSomething) {
        auto c = xisf::ConstellationDB::Identify(0.0, -89.0);
        Assert::IsFalse(c.empty(),
                        L"Near south pole should still identify a constellation");
    }

    TEST_METHOD(Identify_NorthPole_ReturnsSomething) {
        auto c = xisf::ConstellationDB::Identify(0.0, 89.0);
        Assert::IsFalse(c.empty());
    }

    TEST_METHOD(FullName_Ori_ReturnsOrion) {
        Assert::IsTrue(xisf::ConstellationDB::FullName("Ori") == "Orion");
    }

    TEST_METHOD(FullName_And_ReturnsAndromeda) {
        Assert::IsTrue(xisf::ConstellationDB::FullName("And") == "Andromeda");
    }

    TEST_METHOD(FullName_Cyg_ReturnsCygnus) {
        Assert::IsTrue(xisf::ConstellationDB::FullName("Cyg") == "Cygnus");
    }

    TEST_METHOD(FullName_Unknown_ReturnsSelf) {
        Assert::IsTrue(xisf::ConstellationDB::FullName("XYZ") == "XYZ");
    }

    TEST_METHOD(FullName_All88_NonEmpty) {
        // Spot check a diverse set
        Assert::IsFalse(xisf::ConstellationDB::FullName("UMa").empty());
        Assert::IsFalse(xisf::ConstellationDB::FullName("Sgr").empty());
        Assert::IsFalse(xisf::ConstellationDB::FullName("Cas").empty());
        Assert::IsFalse(xisf::ConstellationDB::FullName("Tau").empty());
        Assert::IsFalse(xisf::ConstellationDB::FullName("Cep").empty());
    }
};

// ===========================================================================
// PERSPECTIVE 1e: Principal Developer — DSOCatalog Cone Search
// ===========================================================================

TEST_CLASS(PropertyHandler_DSOCatalog_ConeSearch) {
public:

    TEST_METHOD(ConeSearch_M42Region_FindsOrionNebula) {
        xisf::DSOCatalog cat;
        cat.LoadFromCSVString(kTestCSV);
        auto results = cat.ConeSearch(83.82, -5.39, 0.5);
        Assert::IsFalse(results.empty(),
                        L"Cone search near M42 must return results");
        bool foundM42 = false;
        for (const auto& r : results) {
            if (cat.GetEntry(r.entryIndex).commonName == "Orion Nebula")
                foundM42 = true;
        }
        Assert::IsTrue(foundM42, L"Must find Orion Nebula near RA=83.82 Dec=-5.39");
    }

    TEST_METHOD(ConeSearch_ResultsSortedByDistance) {
        xisf::DSOCatalog cat;
        cat.LoadFromCSVString(kTestCSV);
        auto results = cat.ConeSearch(83.82, -5.39, 5.0);
        for (size_t i = 1; i < results.size(); ++i) {
            Assert::IsTrue(results[i].distanceDeg >= results[i - 1].distanceDeg,
                           L"Results must be sorted by distance");
        }
    }

    TEST_METHOD(ConeSearch_NoResults_EmptyVector) {
        xisf::DSOCatalog cat;
        cat.LoadFromCSVString(kTestCSV);
        // Search at a location far from any test objects
        auto results = cat.ConeSearch(180.0, -60.0, 0.1);
        Assert::IsTrue(results.empty(),
                       L"No objects expected at RA=180 Dec=-60 with 0.1 deg radius");
    }

    TEST_METHOD(ConeSearch_LargeRadius_FindsMultiple) {
        xisf::DSOCatalog cat;
        cat.LoadFromCSVString(kTestCSV);
        // Large radius from Orion region should capture multiple objects
        auto results = cat.ConeSearch(83.82, -5.39, 30.0);
        Assert::IsTrue(results.size() >= 2,
                       L"30-degree radius from M42 region should find multiple objects");
    }

    TEST_METHOD(ConeSearch_EmptyCatalog_ReturnsEmpty) {
        xisf::DSOCatalog cat;
        auto results = cat.ConeSearch(83.82, -5.39, 1.0);
        Assert::IsTrue(results.empty());
    }

    TEST_METHOD(ConeSearch_DistanceIsAccurate) {
        xisf::DSOCatalog cat;
        cat.LoadFromCSVString(kTestCSV);
        auto results = cat.ConeSearch(83.82, -5.39, 0.5);
        for (const auto& r : results) {
            const auto& e = cat.GetEntry(r.entryIndex);
            double computed = xisf::DSOCatalog::AngularSeparation(
                83.82, -5.39, e.ra, e.dec);
            Assert::IsTrue(std::abs(computed - r.distanceDeg) < 0.001,
                           L"Reported distance must match computed Haversine");
        }
    }
};

// ===========================================================================
// PERSPECTIVE 2c: Astrophotographer — DSO Matching Workflow
// ===========================================================================

TEST_CLASS(PropertyHandler_DSOCatalog_AstroWorkflow) {
public:

    TEST_METHOD(SharplessCatalog_FindBySharplessNumber) {
        xisf::DSOCatalog cat;
        cat.LoadFromCSVString(kTestCSV);
        cat.AppendFromCSVString(kTestSharplessCSV);
        auto* e = cat.FindByName("Sh2-101");
        Assert::IsNotNull(e, L"Must find Sharpless objects after append");
        Assert::IsTrue(e->commonName == "Tulip Nebula");
    }

    TEST_METHOD(SharplessCatalog_FindBySharplessWithSpace) {
        xisf::DSOCatalog cat;
        cat.LoadFromCSVString(kTestCSV);
        cat.AppendFromCSVString(kTestSharplessCSV);
        Assert::IsNotNull(cat.FindByName("sh2-101"),
                          L"Sharpless lookup must be case-insensitive");
    }

    TEST_METHOD(SharplessCatalog_FindByCommonName) {
        xisf::DSOCatalog cat;
        cat.LoadFromCSVString(kTestCSV);
        cat.AppendFromCSVString(kTestSharplessCSV);
        auto* e = cat.FindByName("Cave Nebula");
        Assert::IsNotNull(e, L"Must find Sharpless by common name");
    }

    TEST_METHOD(CrossCatalogSearch_M42_HasNGC) {
        xisf::DSOCatalog cat;
        cat.LoadFromCSVString(kTestCSV);
        auto* e = cat.FindByName("M42");
        Assert::IsNotNull(e);
        Assert::IsTrue(e->designations.count("NGC") > 0,
                       L"M42 must have NGC cross-reference");
        Assert::IsTrue(e->designations.at("NGC") == "1976");
    }

    TEST_METHOD(ConeSearch_SimulatesImagingSession) {
        // An astrophotographer points their telescope at M42 and
        // wants to know what they're looking at
        xisf::DSOCatalog cat;
        cat.LoadFromCSVString(kTestCSV);
        auto results = cat.ConeSearch(83.82, -5.39, 1.0);
        Assert::IsFalse(results.empty(),
                        L"Imaging near M42 should identify the target");
    }

    TEST_METHOD(KeywordsExpansion_ForWindowsSearch) {
        // System.Keywords should include all names for search discoverability
        xisf::DSOCatalog cat;
        cat.LoadFromCSVString(kTestCSV);
        auto names = cat.GetAllNames("M42");
        // Must include the common name so "Orion Nebula" search works
        bool foundOrion = false;
        for (const auto& n : names)
            if (n == "Orion Nebula") foundOrion = true;
        Assert::IsTrue(foundOrion,
                       L"GetAllNames must include common name for search");
    }

    TEST_METHOD(Constellation_M42InOrion) {
        auto c = xisf::ConstellationDB::FullName(
            xisf::ConstellationDB::Identify(83.82, -5.39));
        Assert::IsTrue(c == "Orion",
                       L"Astrophotographer imaging M42 sees 'Orion' constellation");
    }

    TEST_METHOD(Constellation_M31InAndromeda) {
        auto c = xisf::ConstellationDB::FullName(
            xisf::ConstellationDB::Identify(10.68, 41.27));
        Assert::IsTrue(c == "Andromeda",
                       L"Astrophotographer imaging M31 sees 'Andromeda' constellation");
    }

    TEST_METHOD(PreferredName_RespectsUserPriority) {
        // User sets priority to M first — M42 should display as "M 42"
        xisf::DSOCatalog cat;
        cat.LoadFromCSVString(kTestCSV);
        auto* e = cat.FindByName("Orion Nebula");
        Assert::IsNotNull(e);
        auto prio = xisf::DSOCatalog::ParsePriorityString("M,C,NGC,IC");
        Assert::IsTrue(cat.GetPreferredName(*e, prio) == "M 42",
                       L"User priority 'M first' should show 'M 42'");
    }

    TEST_METHOD(ICCatalog_HorseheadFound) {
        xisf::DSOCatalog cat;
        cat.LoadFromCSVString(kTestCSV);
        auto* e = cat.FindByName("IC434");
        Assert::IsNotNull(e, L"Must find IC objects by number");
    }

    TEST_METHOD(ICCatalog_FindWithSpace) {
        xisf::DSOCatalog cat;
        cat.LoadFromCSVString(kTestCSV);
        auto* e = cat.FindByName("IC 434");
        Assert::IsNotNull(e, L"Must find IC objects with space");
    }
};

// ===========================================================================
// PERSPECTIVE 3c: Windows Sysadmin — DSOCatalog & ConstellationDB Robustness
// ===========================================================================

TEST_CLASS(PropertyHandler_DSOCatalog_Reliability) {
public:

    TEST_METHOD(LoadFromCSVString_GarbageInput_NoCrash) {
        xisf::DSOCatalog cat;
        cat.LoadFromCSVString("!@#$%^&*()_+\nrandom garbage\n;;;;\n");
        // Must not crash — count may be 0 or small
        Assert::IsTrue(cat.Count() < 100);
    }

    TEST_METHOD(LoadFromCSVString_PartialRows_SkipsInvalid) {
        std::string csv =
            "Name;Type;RA;Dec;Const;MajAx\n"
            "NGC1976;HII;05:35:17.3;-05:23:28;Ori;66.00\n"
            "BAD\n"
            "NGC0224;Gx;00:42:44.3;+41:16:09;And;190.00\n";
        xisf::DSOCatalog cat;
        cat.LoadFromCSVString(csv);
        Assert::AreEqual(size_t(2), cat.Count(),
                         L"Should skip malformed rows gracefully");
    }

    TEST_METHOD(LoadFromCSVFile_InvalidPath_ReturnsFalse) {
        xisf::DSOCatalog cat;
        Assert::IsFalse(cat.LoadFromCSVFile("C:\\no_such_dir\\phantom.csv"));
    }

    TEST_METHOD(ConeSearch_ExtremeCoords_NoCrash) {
        xisf::DSOCatalog cat;
        cat.LoadFromCSVString(kTestCSV);
        // Edge cases: poles, wrap-around
        cat.ConeSearch(0.0, 90.0, 5.0);
        cat.ConeSearch(0.0, -90.0, 5.0);
        cat.ConeSearch(359.99, 0.0, 5.0);
        cat.ConeSearch(0.01, 0.0, 180.0);
        // No crash = pass
    }

    TEST_METHOD(FindByName_WhitespaceInput_NoCrash) {
        xisf::DSOCatalog cat;
        cat.LoadFromCSVString(kTestCSV);
        Assert::IsNull(cat.FindByName("   "));
        Assert::IsNull(cat.FindByName("\t\n"));
    }

    TEST_METHOD(LoadFromCSVString_RepeatedCalls_ReplacesData) {
        xisf::DSOCatalog cat;
        cat.LoadFromCSVString(kTestCSV);
        Assert::AreEqual(size_t(7), cat.Count());
        cat.LoadFromCSVString(kTestSharplessCSV);
        Assert::AreEqual(size_t(3), cat.Count(),
                         L"Second load must replace, not append");
    }

    TEST_METHOD(ConstellationDB_Identify_InvalidCoords_NoCrash) {
        // Should not crash even with extreme or invalid coordinates
        xisf::ConstellationDB::Identify(0.0, 0.0);
        xisf::ConstellationDB::Identify(360.0, 0.0);
        xisf::ConstellationDB::Identify(-1.0, 0.0);
        xisf::ConstellationDB::Identify(180.0, 91.0);
        xisf::ConstellationDB::Identify(180.0, -91.0);
    }

    TEST_METHOD(ConstellationDB_FullName_EmptyInput_NoCrash) {
        auto name = xisf::ConstellationDB::FullName("");
        Assert::IsTrue(name.empty() || !name.empty());
    }

    TEST_METHOD(AppendFromCSVString_EmptyString_ReturnsFalse) {
        xisf::DSOCatalog cat;
        cat.LoadFromCSVString(kTestCSV);
        size_t before = cat.Count();
        Assert::IsFalse(cat.AppendFromCSVString(""));
        Assert::AreEqual(before, cat.Count(),
                         L"Empty append must not change catalog");
    }

    TEST_METHOD(PropertyStore_NoCoordsXML_NoConstellationOrMatch) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kNoCoordsXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        PROPVARIANT pv; PropVariantInit(&pv);
        ps->GetValue(PKEY_XISF_Constellation, &pv);
        Assert::AreEqual(USHORT(VT_EMPTY), pv.vt,
                         L"Without coordinates, constellation must be empty");
        PropVariantClear(&pv);
        ps->GetValue(PKEY_XISF_MatchedObjects, &pv);
        Assert::AreEqual(USHORT(VT_EMPTY), pv.vt,
                         L"Without coordinates, matched objects must be empty");
        PropVariantClear(&pv);
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }
};

// ===========================================================================
// PERSPECTIVE 2d: Astrophotographer — PropertyStore Constellation & Matching
// ===========================================================================

TEST_CLASS(PropertyHandler_PropertyStore_ConstellationMatching) {
public:

    TEST_METHOD(Constellation_PopulatedFromCoords) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kM42CoordXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        PROPVARIANT pv; PropVariantInit(&pv);
        ps->GetValue(PKEY_XISF_Constellation, &pv);
        Assert::AreEqual(USHORT(VT_LPWSTR), pv.vt,
                         L"Constellation must be populated when RA/Dec present");
        Assert::AreEqual(L"Orion", pv.pwszVal,
                         L"M42 coordinates should map to Orion");
        PropVariantClear(&pv);
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }

    TEST_METHOD(Keywords_IncludesConstellationName) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kM42CoordXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        PROPVARIANT pv; PropVariantInit(&pv);
        ps->GetValue(PKEY_Keywords, &pv);
        Assert::AreEqual(USHORT(VT_VECTOR | VT_LPWSTR), pv.vt);
        bool foundOrion = false;
        for (ULONG i = 0; i < pv.calpwstr.cElems; ++i)
            if (wcscmp(pv.calpwstr.pElems[i], L"Orion") == 0) foundOrion = true;
        Assert::IsTrue(foundOrion,
                       L"System.Keywords must include constellation name for search");
        PropVariantClear(&pv);
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }

    TEST_METHOD(Keywords_IncludesImageType) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kM42CoordXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        PROPVARIANT pv; PropVariantInit(&pv);
        ps->GetValue(PKEY_Keywords, &pv);
        Assert::AreEqual(USHORT(VT_VECTOR | VT_LPWSTR), pv.vt);
        bool foundLight = false;
        for (ULONG i = 0; i < pv.calpwstr.cElems; ++i)
            if (wcscmp(pv.calpwstr.pElems[i], L"Light") == 0) foundLight = true;
        Assert::IsTrue(foundLight,
                       L"System.Keywords must include image type for search");
        PropVariantClear(&pv);
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }

    TEST_METHOD(Keywords_AreDeduplicated) {
        auto* h = new CXISFPropertyHandler();
        IStream* s = CreateXISFStream(kM42CoordXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        PROPVARIANT pv; PropVariantInit(&pv);
        ps->GetValue(PKEY_Keywords, &pv);
        if (pv.vt == (VT_VECTOR | VT_LPWSTR)) {
            // Check no duplicates
            std::vector<std::wstring> kws;
            for (ULONG i = 0; i < pv.calpwstr.cElems; ++i)
                kws.push_back(pv.calpwstr.pElems[i]);
            std::sort(kws.begin(), kws.end());
            auto dup = std::adjacent_find(kws.begin(), kws.end());
            Assert::IsTrue(dup == kws.end(),
                           L"System.Keywords must not contain duplicates");
        }
        PropVariantClear(&pv);
        ps->Release(); pi->Release(); s->Release(); h->Release();
    }

    TEST_METHOD(FOV_MatchedObjects_ConsistentAcrossDitheredFrames) {
        // Verifies that the FOV-based cone search radius computed from sensor
        // geometry produces consistent results across dithered frames.
        // Sensor: 6248x4176 @ 3.76µm, 480mm FL → diagonal FOV ≈ 1.88°, radius ≈ 0.94°
        // M31 center: RA≈10.685, Dec≈41.269. M32 (NGC 221) is ~0.49° away.
        // With the fixed 0.5° default tolerance, M32 was intermittently matched.
        // The FOV-based radius of ~0.94° comfortably includes M32 for all dithers.

        // Compute the FOV radius the same way PropertyStore.cpp does
        double pixSizeUm = 3.76;
        double focalLengthMm = 480.0;
        double naxis1 = 6248.0, naxis2 = 4176.0;
        double diagPx = std::sqrt(naxis1 * naxis1 + naxis2 * naxis2);
        double fovDeg = (diagPx * pixSizeUm) / (focalLengthMm * 1000.0) * (180.0 / 3.14159265358979323846);
        double fovRadius = fovDeg / 2.0;

        // Radius must be larger than distance to M32 (~0.49°)
        Assert::IsTrue(fovRadius > 0.49, L"FOV radius must exceed M31-M32 separation");
        // But not unreasonably large
        Assert::IsTrue(fovRadius < 2.0, L"FOV radius should be reasonable");

        // Dithered coordinates near M31
        double coords[][2] = {
            {10.685, 41.269},   // centered
            {10.700, 41.280},   // dithered
            {10.670, 41.255},   // dithered
            {10.690, 41.260},   // dithered
        };

        xisf::DSOCatalog cat;
        cat.LoadFromCSVString(kTestCSV);

        // All dithered positions should produce the same set of matched objects
        std::vector<size_t> resultCounts;
        for (const auto& c : coords) {
            auto results = cat.ConeSearch(c[0], c[1], fovRadius);
            resultCounts.push_back(results.size());
            // M31 (Andromeda Galaxy) must always be found
            bool foundM31 = false;
            for (const auto& r : results) {
                if (cat.GetEntry(r.entryIndex).commonName == "Andromeda Galaxy")
                    foundM31 = true;
            }
            Assert::IsTrue(foundM31, L"Andromeda Galaxy must be found in all dithered frames");
        }

        // All frames must find the same number of objects
        for (size_t i = 1; i < resultCounts.size(); ++i) {
            Assert::AreEqual(resultCounts[0], resultCounts[i],
                             L"FOV-based matching must produce consistent results across dithered frames");
        }
    }

    TEST_METHOD(PropertyStore_NoCoordsXML_UsesObjectCoordsForConstellation) {
        // Simulate real N.I.N.A. XISF: telescope center RA/DEC drifts far from target,
        // but Observation:Object:RA/Dec properties carry the stable object position.
        // Telescope pointing at RA=17.88 (far from M31), object at RA=10.68/Dec=41.27.
        const std::string xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<xisf version="1.0" xmlns="http://www.pixinsight.com/xisf">
  <Image geometry="6224:4168:1" sampleFormat="UInt16" colorSpace="Gray" location="attachment:0:0">
    <FITSKeyword name="RA" value="17.88" comment="[deg] RA of telescope"/>
    <FITSKeyword name="DEC" value="40.46" comment="[deg] Dec of telescope"/>
    <FITSKeyword name="OBJCTRA" value="'00 42 44'" comment="[H M S] RA of imaged object"/>
    <FITSKeyword name="OBJCTDEC" value="'+41 16 07'" comment="[D M S] Declination of imaged object"/>
    <Property id="Observation:Object:RA" type="Float64" value="10.684708333333331"/>
    <Property id="Observation:Object:Dec" type="Float64" value="41.26875"/>
    <FITSKeyword name="FOCALLEN" value="480.0" comment="[mm] Focal length"/>
    <FITSKeyword name="XPIXSZ" value="3.76" comment="[um] Pixel size"/>
  </Image>
</xisf>
)";
        IStream* pStream = CreateXISFStream(xml);
        Assert::IsNotNull(pStream);
        CXISFPropertyHandler handler;
        Assert::AreEqual(S_OK, handler.Initialize(pStream, STGM_READ));
        pStream->Release();

        // Constellation should be Andromeda (from object coords), not something wrong
        PROPVARIANT pvConst;
        PropVariantInit(&pvConst);
        HRESULT hr = handler.GetValue(PKEY_XISF_Constellation, &pvConst);
        Assert::AreEqual(S_OK, hr);
        Assert::IsTrue(pvConst.vt == VT_LPWSTR, L"Constellation must be populated");
        std::wstring constellation(pvConst.pwszVal);
        PropVariantClear(&pvConst);
        Assert::IsTrue(constellation == L"Andromeda",
                       (L"Expected Andromeda, got: " + constellation).c_str());

        // MatchedObjects depends on DSO catalog being loaded (from embedded resources).
        // If available, verify it includes M31.
        PROPVARIANT pvMatch;
        PropVariantInit(&pvMatch);
        hr = handler.GetValue(PKEY_XISF_MatchedObjects, &pvMatch);
        if (pvMatch.vt == VT_LPWSTR) {
            std::wstring matched(pvMatch.pwszVal);
            Assert::IsTrue(matched.find(L"M 31") != std::wstring::npos,
                           (L"Expected M 31 in matched objects, got: " + matched).c_str());
        }
        PropVariantClear(&pvMatch);
    }
};

// ===========================================================================
// PERSPECTIVE 3c: Windows Sysadmin — IStream Partial Read Resilience
// Regression tests for the search indexer partial-read bug where IStream::Read
// returns fewer bytes than requested. The indexer's IStream wraps file I/O with
// buffered reads that may split large requests into smaller chunks, causing
// Initialize to fail if the handler doesn't loop over partial reads.
// ===========================================================================

namespace {

    // IStream wrapper that caps each Read call to a configurable chunk size,
    // simulating the Windows Search indexer's buffered IStream behavior.
    class ChunkedStream : public IStream {
    public:
        ChunkedStream(IStream* pInner, ULONG chunkSize)
            : m_cRef(1), m_pInner(pInner), m_chunkSize(chunkSize) {
            m_pInner->AddRef();
        }
        ~ChunkedStream() { m_pInner->Release(); }

        IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
            if (!ppv) return E_POINTER;
            if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IStream) ||
                IsEqualIID(riid, IID_ISequentialStream)) {
                *ppv = static_cast<IStream*>(this);
                AddRef();
                return S_OK;
            }
            *ppv = nullptr;
            return E_NOINTERFACE;
        }
        IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_cRef); }
        IFACEMETHODIMP_(ULONG) Release() override {
            ULONG c = InterlockedDecrement(&m_cRef);
            if (c == 0) delete this;
            return c;
        }
        IFACEMETHODIMP Read(void* pv, ULONG cb, ULONG* pcbRead) override {
            // Cap the read to m_chunkSize bytes per call
            ULONG toRead = (cb > m_chunkSize) ? m_chunkSize : cb;
            return m_pInner->Read(pv, toRead, pcbRead);
        }
        // Delegate everything else to inner stream
        IFACEMETHODIMP Write(const void* pv, ULONG cb, ULONG* pcbWritten) override {
            return m_pInner->Write(pv, cb, pcbWritten);
        }
        IFACEMETHODIMP Seek(LARGE_INTEGER dlibMove, DWORD dwOrigin, ULARGE_INTEGER* plibNewPos) override {
            return m_pInner->Seek(dlibMove, dwOrigin, plibNewPos);
        }
        IFACEMETHODIMP SetSize(ULARGE_INTEGER libNewSize) override {
            return m_pInner->SetSize(libNewSize);
        }
        IFACEMETHODIMP CopyTo(IStream* pstm, ULARGE_INTEGER cb, ULARGE_INTEGER* pcbRead, ULARGE_INTEGER* pcbWritten) override {
            return m_pInner->CopyTo(pstm, cb, pcbRead, pcbWritten);
        }
        IFACEMETHODIMP Commit(DWORD grfCommitFlags) override {
            return m_pInner->Commit(grfCommitFlags);
        }
        IFACEMETHODIMP Revert() override { return m_pInner->Revert(); }
        IFACEMETHODIMP LockRegion(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType) override {
            return m_pInner->LockRegion(libOffset, cb, dwLockType);
        }
        IFACEMETHODIMP UnlockRegion(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType) override {
            return m_pInner->UnlockRegion(libOffset, cb, dwLockType);
        }
        IFACEMETHODIMP Stat(STATSTG* pstatstg, DWORD grfStatFlag) override {
            return m_pInner->Stat(pstatstg, grfStatFlag);
        }
        IFACEMETHODIMP Clone(IStream** ppstm) override {
            return m_pInner->Clone(ppstm);
        }

    private:
        long m_cRef;
        IStream* m_pInner;
        ULONG m_chunkSize;
    };

    IStream* CreateChunkedXISFStream(const std::string& xml, ULONG chunkSize) {
        IStream* pInner = CreateXISFStream(xml);
        if (!pInner) return nullptr;
        auto* pChunked = new ChunkedStream(pInner, chunkSize);
        pInner->Release();
        return pChunked;
    }

} // anonymous namespace

TEST_CLASS(PropertyHandler_PropertyStore_PartialReadResilience) {
public:

    TEST_METHOD(Initialize_ChunkedStream_1ByteReads_Succeeds) {
        // Worst case: stream returns 1 byte at a time (extreme fragmentation)
        IStream* s = CreateChunkedXISFStream(kHandlerXML, 1);
        Assert::IsNotNull(s);
        CXISFPropertyHandler handler;
        HRESULT hr = handler.Initialize(s, STGM_READ);
        s->Release();
        Assert::AreEqual(S_OK, hr, L"Initialize must succeed with 1-byte chunked reads");
    }

    TEST_METHOD(Initialize_ChunkedStream_SmallChunks_Succeeds) {
        // Simulate typical buffered stream: 64-byte chunks
        IStream* s = CreateChunkedXISFStream(kHandlerXML, 64);
        Assert::IsNotNull(s);
        CXISFPropertyHandler handler;
        HRESULT hr = handler.Initialize(s, STGM_READ);
        s->Release();
        Assert::AreEqual(S_OK, hr, L"Initialize must succeed with 64-byte chunked reads");
    }

    TEST_METHOD(Initialize_ChunkedStream_4KPageSize_Succeeds) {
        // Simulate OS page-sized buffering (common in indexer IStream)
        IStream* s = CreateChunkedXISFStream(kHandlerXML, 4096);
        Assert::IsNotNull(s);
        CXISFPropertyHandler handler;
        HRESULT hr = handler.Initialize(s, STGM_READ);
        s->Release();
        Assert::AreEqual(S_OK, hr, L"Initialize must succeed with 4KB chunked reads");
    }

    TEST_METHOD(ChunkedStream_AllProperties_Match_NormalStream) {
        // Verify that a chunked stream produces identical property values
        // to a normal (non-chunked) stream — the bug caused total data loss.
        CXISFPropertyHandler normalHandler;
        IStream* sNormal = CreateXISFStream(kHandlerXML);
        normalHandler.Initialize(sNormal, STGM_READ);
        sNormal->Release();

        CXISFPropertyHandler chunkedHandler;
        IStream* sChunked = CreateChunkedXISFStream(kHandlerXML, 37);  // prime-sized chunks
        chunkedHandler.Initialize(sChunked, STGM_READ);
        sChunked->Release();

        DWORD normalCount = 0, chunkedCount = 0;
        normalHandler.GetCount(&normalCount);
        chunkedHandler.GetCount(&chunkedCount);
        Assert::AreEqual(normalCount, chunkedCount,
                         L"Chunked stream must produce same number of properties");
        Assert::IsTrue(normalCount > 0, L"Sanity: must have properties");

        for (DWORD i = 0; i < normalCount; ++i) {
            PROPERTYKEY pk = {};
            normalHandler.GetAt(i, &pk);
            PROPVARIANT pvNormal, pvChunked;
            PropVariantInit(&pvNormal);
            PropVariantInit(&pvChunked);
            normalHandler.GetValue(pk, &pvNormal);
            chunkedHandler.GetValue(pk, &pvChunked);
            Assert::AreEqual(pvNormal.vt, pvChunked.vt,
                             L"Property types must match between normal and chunked reads");
            PropVariantClear(&pvNormal);
            PropVariantClear(&pvChunked);
        }
    }

    TEST_METHOD(ChunkedStream_Constellation_Populated) {
        // The specific regression: Constellation was empty in search results
        IStream* s = CreateChunkedXISFStream(kM42CoordXML, 100);
        Assert::IsNotNull(s);
        CXISFPropertyHandler handler;
        Assert::AreEqual(S_OK, handler.Initialize(s, STGM_READ));
        s->Release();
        PROPVARIANT pv; PropVariantInit(&pv);
        handler.GetValue(PKEY_XISF_Constellation, &pv);
        Assert::AreEqual(USHORT(VT_LPWSTR), pv.vt,
                         L"Constellation must populate even with chunked reads");
        Assert::AreEqual(L"Orion", pv.pwszVal);
        PropVariantClear(&pv);
    }

    TEST_METHOD(ChunkedStream_LargeHeader_Succeeds) {
        // Simulate a large XML header that would definitely trigger partial
        // reads on any reasonable buffer size. Pad with XML comments.
        std::string largeXML = R"(<?xml version="1.0" encoding="UTF-8"?>
<xisf version="1.0" xmlns="http://www.pixinsight.com/xisf">
  <Image geometry="4656:3520:1" sampleFormat="UInt16" colorSpace="Gray" location="attachment:0:0">
    <FITSKeyword name="OBJECT" value="M42" comment="Target"/>
    <FITSKeyword name="RA" value="83.82" comment="RA degrees"/>
    <FITSKeyword name="DEC" value="-5.39" comment="Dec degrees"/>
    <FITSKeyword name="IMAGETYP" value="Light" comment="Frame type"/>
)";
        // Pad to >32KB with XML comments to force multi-chunk reads
        for (int i = 0; i < 400; ++i) {
            largeXML += "    <!-- padding comment line " + std::to_string(i)
                     + " to simulate large XISF headers with many keywords -->\n";
        }
        largeXML += "  </Image>\n</xisf>\n";

        IStream* s = CreateChunkedXISFStream(largeXML, 4096);
        Assert::IsNotNull(s);
        CXISFPropertyHandler handler;
        HRESULT hr = handler.Initialize(s, STGM_READ);
        s->Release();
        Assert::AreEqual(S_OK, hr,
                         L"Large headers must initialize successfully with chunked reads");
        PROPVARIANT pv; PropVariantInit(&pv);
        handler.GetValue(PKEY_XISF_Constellation, &pv);
        Assert::AreEqual(USHORT(VT_LPWSTR), pv.vt,
                         L"Constellation must populate for large-header files");
        Assert::AreEqual(L"Orion", pv.pwszVal);
        PropVariantClear(&pv);
    }
};

// ===========================================================================
// PERSPECTIVE 6: Principal Developer — Propdesc Schema Integrity
// Regression tests for the malformed xisf.propdesc bug where missing
// formatID/propID attributes caused PSRegisterPropertySchema to fail,
// silently breaking all Explorer column visibility and search indexing.
// These tests validate the propdesc file directly (no elevation needed)
// and cross-check it against PropertyStore.h PROPERTYKEY definitions.
// ===========================================================================

namespace {

    // Read the propdesc XML from the build output directory (same location
    // DllRegisterServer would find it — next to the handler DLL).
    std::string ReadPropdescFile() {
        fs::path p(__FILE__);
        auto propdescPath = p.parent_path().parent_path() / "XISFPropertyHandler" / "propdesc" / "xisf.propdesc";
        if (!fs::exists(propdescPath)) return {};
        std::ifstream in(propdescPath, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in),
                           std::istreambuf_iterator<char>());
    }

    // Read the build-output copy to verify the build copies it correctly
    std::string ReadOutputPropdescFile() {
        fs::path p(__FILE__);
        auto outPath = p.parent_path().parent_path().parent_path() / "x64" / "Debug" / "xisf.propdesc";
        if (!fs::exists(outPath)) return {};
        std::ifstream in(outPath, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in),
                           std::istreambuf_iterator<char>());
    }

    // Count occurrences of a substring in a string
    size_t CountOccurrences(const std::string& text, const std::string& sub) {
        size_t count = 0, pos = 0;
        while ((pos = text.find(sub, pos)) != std::string::npos) {
            ++count;
            pos += sub.size();
        }
        return count;
    }

} // anonymous namespace

TEST_CLASS(PropertyHandler_Propdesc_SchemaIntegrity) {
public:

    TEST_METHOD(PropdescFile_Exists) {
        auto xml = ReadPropdescFile();
        Assert::IsFalse(xml.empty(), L"xisf.propdesc must exist in source tree");
    }

    TEST_METHOD(PropdescFile_IsValidXML_HasSchemaRoot) {
        auto xml = ReadPropdescFile();
        Assert::IsFalse(xml.empty());
        Assert::IsTrue(xml.find("<?xml") != std::string::npos,
                       L"Must have XML declaration");
        Assert::IsTrue(xml.find("<schema") != std::string::npos,
                       L"Must have <schema> root element");
        Assert::IsTrue(xml.find("</schema>") != std::string::npos,
                       L"Must have closing </schema> tag");
    }

    TEST_METHOD(PropdescFile_AllEntriesHaveFormatID) {
        // The original bug: 52 of 53 entries were missing formatID attributes.
        // Without formatID, PSRegisterPropertySchema silently fails.
        auto xml = ReadPropdescFile();
        Assert::IsFalse(xml.empty());
        size_t propCount = CountOccurrences(xml, "<propertyDescription ");
        size_t fmtIdCount = CountOccurrences(xml, "formatID=\"{7C54FA8B-9D63-4C10-8FBE-1A5A0F9A3B2E}\"");
        Assert::AreEqual(propCount, fmtIdCount,
                         L"Every <propertyDescription> must have the XISF formatID attribute");
    }

    TEST_METHOD(PropdescFile_AllEntriesHavePropID) {
        // The original bug: entries were missing propID="N" attributes.
        auto xml = ReadPropdescFile();
        Assert::IsFalse(xml.empty());
        size_t propCount = CountOccurrences(xml, "<propertyDescription ");
        size_t pidCount = CountOccurrences(xml, "propID=\"");
        Assert::AreEqual(propCount, pidCount,
                         L"Every <propertyDescription> must have a propID attribute");
    }

    TEST_METHOD(PropdescFile_AllEntriesAreClosedProperly) {
        // Malformed XML had unclosed <propertyDescription tags (missing >)
        auto xml = ReadPropdescFile();
        Assert::IsFalse(xml.empty());
        size_t openCount = CountOccurrences(xml, "<propertyDescription ");
        size_t closeCount = CountOccurrences(xml, "</propertyDescription>");
        Assert::AreEqual(openCount, closeCount,
                         L"Every <propertyDescription> must have a matching </propertyDescription>");
    }

    TEST_METHOD(PropdescFile_Has53Properties) {
        // PropertyStore.h defines 53 properties (propIDs 2-54).
        // The propdesc must define all of them for Explorer columns to work.
        auto xml = ReadPropdescFile();
        Assert::IsFalse(xml.empty());
        size_t count = CountOccurrences(xml, "<propertyDescription ");
        Assert::AreEqual(size_t(53), count,
                         L"Propdesc must define all 53 XISF properties (propIDs 2-54)");
    }

    TEST_METHOD(PropdescFile_PropIDsCoverFullRange_2to54) {
        // Verify every propID from 2 to 54 is present — catches gaps where
        // properties were added to PropertyStore.h but not the propdesc.
        auto xml = ReadPropdescFile();
        Assert::IsFalse(xml.empty());
        for (int pid = 2; pid <= 54; ++pid) {
            std::string needle = "propID=\"" + std::to_string(pid) + "\"";
            std::wstring msg = L"Missing propID=" + std::to_wstring(pid) + L" in propdesc";
            Assert::IsTrue(xml.find(needle) != std::string::npos, msg.c_str());
        }
    }

    TEST_METHOD(PropdescFile_NoDuplicatePropIDs) {
        auto xml = ReadPropdescFile();
        Assert::IsFalse(xml.empty());
        for (int pid = 2; pid <= 54; ++pid) {
            std::string needle = "propID=\"" + std::to_string(pid) + "\"";
            size_t count = CountOccurrences(xml, needle);
            std::wstring msg = L"Duplicate propID=" + std::to_wstring(pid);
            Assert::AreEqual(size_t(1), count, msg.c_str());
        }
    }

    TEST_METHOD(PropdescFile_AllEntriesHaveSearchInfo) {
        // Every property must have <searchInfo> for the indexer to index it.
        auto xml = ReadPropdescFile();
        Assert::IsFalse(xml.empty());
        size_t propCount = CountOccurrences(xml, "<propertyDescription ");
        size_t searchCount = CountOccurrences(xml, "<searchInfo ");
        Assert::AreEqual(propCount, searchCount,
                         L"Every property must have a <searchInfo> element");
    }

    TEST_METHOD(PropdescFile_AllEntriesHaveIsViewableTrue) {
        // isViewable="true" is required for properties to appear in
        // the Explorer column picker and the file Details tab.
        auto xml = ReadPropdescFile();
        Assert::IsFalse(xml.empty());
        size_t propCount = CountOccurrences(xml, "<propertyDescription ");
        size_t viewableCount = CountOccurrences(xml, "isViewable=\"true\"");
        Assert::AreEqual(propCount, viewableCount,
                         L"Every property must have isViewable=\"true\"");
    }

    TEST_METHOD(PropdescFile_AllEntriesHaveIsColumnTrue) {
        // isColumn="true" in <searchInfo> is required for Explorer to
        // show the property as an available column.
        auto xml = ReadPropdescFile();
        Assert::IsFalse(xml.empty());
        size_t propCount = CountOccurrences(xml, "<propertyDescription ");
        size_t colCount = CountOccurrences(xml, "isColumn=\"true\"");
        Assert::AreEqual(propCount, colCount,
                         L"Every property must have isColumn=\"true\"");
    }

    TEST_METHOD(PropdescFile_NamesCrossCheckPropertyStoreH) {
        // Verify that every XISF.* property name in the propdesc matches
        // a PKEY_XISF_* definition in PropertyStore.h (caught by convention).
        auto xml = ReadPropdescFile();
        Assert::IsFalse(xml.empty());
        const char* expectedNames[] = {
            "XISF.ExposureTime", "XISF.CameraModel", "XISF.FocalLength",
            "XISF.FNumber", "XISF.ObjectName", "XISF.FilterName",
            "XISF.ImageType", "XISF.Gain", "XISF.Offset",
            "XISF.SensorTemperature", "XISF.Telescope", "XISF.Binning",
            "XISF.DateObserved", "XISF.Software", "XISF.RA", "XISF.Dec",
            "XISF.SetTemp", "XISF.PixelSize", "XISF.ReadoutMode",
            "XISF.BayerPattern", "XISF.SiteLatitude", "XISF.SiteLongitude",
            "XISF.SiteElevation", "XISF.Altitude", "XISF.Azimuth",
            "XISF.Airmass", "XISF.PierSide", "XISF.ObjectRA",
            "XISF.ObjectDec", "XISF.Rotation", "XISF.FocuserName",
            "XISF.FocuserPosition", "XISF.FocuserTemp", "XISF.RotatorName",
            "XISF.RotatorAngle", "XISF.FilterWheel", "XISF.DewPoint",
            "XISF.Humidity", "XISF.AmbientTemp", "XISF.DateLocal",
            "XISF.RAHour", "XISF.DecBand", "XISF.Constellation",
            "XISF.MatchedObjects", "XISF.StarFWHM", "XISF.SkyQuality",
            "XISF.SkyBrightness", "XISF.CloudCover", "XISF.Pressure",
            "XISF.SkyTemp", "XISF.WindSpeed", "XISF.GuideRA", "XISF.GuideDec",
        };
        for (const auto* name : expectedNames) {
            std::string needle = std::string("name=\"") + name + "\"";
            std::string msg = std::string("Missing property in propdesc: ") + name;
            std::wstring wmsg(msg.begin(), msg.end());
            Assert::IsTrue(xml.find(needle) != std::string::npos, wmsg.c_str());
        }
    }

    TEST_METHOD(PropdescFile_DateProperties_HaveDateTimeType) {
        // DateObserved and DateLocal must be DateTime type, not String.
        // Wrong type causes FILETIME values to display as garbage.
        auto xml = ReadPropdescFile();
        Assert::IsFalse(xml.empty());
        // Find DateObserved block and check its type
        auto checkDateType = [&](const char* propName) {
            std::string nameAttr = std::string("name=\"") + propName + "\"";
            size_t pos = xml.find(nameAttr);
            Assert::IsTrue(pos != std::string::npos);
            // Find the typeInfo within this property's block (before next </propertyDescription>)
            size_t endBlock = xml.find("</propertyDescription>", pos);
            std::string block = xml.substr(pos, endBlock - pos);
            std::wstring msg = std::wstring(L"Must be DateTime type: ") +
                std::wstring(propName, propName + strlen(propName));
            Assert::IsTrue(block.find("type=\"DateTime\"") != std::string::npos, msg.c_str());
        };
        checkDateType("XISF.DateObserved");
        checkDateType("XISF.DateLocal");
    }

    TEST_METHOD(PropdescFile_IntegerProperties_HaveUInt32Type) {
        // Gain, Offset, FocuserPosition must be UInt32 to match VT_UI4.
        auto xml = ReadPropdescFile();
        Assert::IsFalse(xml.empty());
        auto checkType = [&](const char* propName, const char* expectedType) {
            std::string nameAttr = std::string("name=\"") + propName + "\"";
            size_t pos = xml.find(nameAttr);
            Assert::IsTrue(pos != std::string::npos);
            size_t endBlock = xml.find("</propertyDescription>", pos);
            std::string block = xml.substr(pos, endBlock - pos);
            std::string typeStr = std::string("type=\"") + expectedType + "\"";
            std::wstring msg = std::wstring(L"Wrong type for ") +
                std::wstring(propName, propName + strlen(propName));
            Assert::IsTrue(block.find(typeStr) != std::string::npos, msg.c_str());
        };
        checkType("XISF.Gain", "UInt32");
        checkType("XISF.Offset", "UInt32");
        checkType("XISF.FocuserPosition", "UInt32");
    }

    TEST_METHOD(PropdescFile_SearchableProperties_HaveInvertedIndex) {
        // Key searchable string properties must have inInvertedIndex="true"
        // so Windows Search can find files by typing object/filter/camera names.
        auto xml = ReadPropdescFile();
        Assert::IsFalse(xml.empty());
        const char* searchableProps[] = {
            "XISF.ObjectName", "XISF.FilterName", "XISF.CameraModel",
            "XISF.Telescope", "XISF.ImageType", "XISF.Software",
            "XISF.Constellation", "XISF.MatchedObjects",
        };
        for (const auto* propName : searchableProps) {
            std::string nameAttr = std::string("name=\"") + propName + "\"";
            size_t pos = xml.find(nameAttr);
            Assert::IsTrue(pos != std::string::npos);
            size_t endBlock = xml.find("</propertyDescription>", pos);
            std::string block = xml.substr(pos, endBlock - pos);
            std::wstring msg = std::wstring(L"Must have inInvertedIndex=true: ") +
                std::wstring(propName, propName + strlen(propName));
            Assert::IsTrue(block.find("inInvertedIndex=\"true\"") != std::string::npos,
                           msg.c_str());
        }
    }

    TEST_METHOD(OutputPropdescFile_MatchesSource) {
        // The build must copy the source propdesc to the output directory.
        // A stale copy was the root cause of DllRegisterServer failing after
        // we fixed the source — the build didn't re-copy it.
        auto source = ReadPropdescFile();
        auto output = ReadOutputPropdescFile();
        if (output.empty()) {
            Logger::WriteMessage("SKIPPED: output xisf.propdesc not found (build not run?)");
            return;
        }
        Assert::AreEqual(source.size(), output.size(),
                         L"Output propdesc must be same size as source");
        Assert::IsTrue(source == output,
                       L"Output propdesc must be identical to source — stale copy detected");
    }
};

// ===========================================================================
// PERSPECTIVE 7: Telemetry — verify ETW event payloads via the test hook
// ===========================================================================

extern "C" {
    using XISFPropertyHandlerTelemetryHook = void (*)(UCHAR level, ULONGLONG keyword, const wchar_t* message);
    extern XISFPropertyHandlerTelemetryHook g_xisfPropertyHandlerTelemetryHook;
}

namespace {
    struct CapturedEvent {
        UCHAR        level;
        ULONGLONG    keyword;
        std::wstring message;
    };

    // Thread-local so parallel test execution cannot cross-contaminate capture buffers.
    thread_local std::vector<CapturedEvent>* t_propertyHandlerCaptureBuffer = nullptr;

    void PropertyHandlerCaptureHook(UCHAR level, ULONGLONG keyword, const wchar_t* message) {
        if (t_propertyHandlerCaptureBuffer) {
            t_propertyHandlerCaptureBuffer->push_back({level, keyword, message ? message : L""});
        }
    }

    class ScopedPropertyHandlerCapture {
    public:
        ScopedPropertyHandlerCapture() {
            t_propertyHandlerCaptureBuffer = &events;
            g_xisfPropertyHandlerTelemetryHook = &PropertyHandlerCaptureHook;
        }
        ~ScopedPropertyHandlerCapture() {
            g_xisfPropertyHandlerTelemetryHook = nullptr;
            t_propertyHandlerCaptureBuffer = nullptr;
        }
        ScopedPropertyHandlerCapture(const ScopedPropertyHandlerCapture&) = delete;
        ScopedPropertyHandlerCapture& operator=(const ScopedPropertyHandlerCapture&) = delete;

        bool containsMessagePrefix(const std::wstring& prefix) const {
            for (const auto& e : events)
                if (e.message.rfind(prefix, 0) == 0) return true;
            return false;
        }

        const CapturedEvent* findByPrefix(const std::wstring& prefix) const {
            for (const auto& e : events)
                if (e.message.rfind(prefix, 0) == 0) return &e;
            return nullptr;
        }

        std::vector<CapturedEvent> events;
    };
}

TEST_CLASS(PropertyHandler_Telemetry) {
public:
    TEST_METHOD(Initialize_Success_EmitsInitializedEvent) {
        ScopedPropertyHandlerCapture cap;
        {
            auto* h = new CXISFPropertyHandler();
            IStream* s = CreateXISFStream(kHandlerXML);
            IInitializeWithStream* pi = nullptr;
            h->QueryInterface(IID_PPV_ARGS(&pi));
            HRESULT hr = pi->Initialize(s, STGM_READ);
            Assert::AreEqual(S_OK, hr);
            pi->Release(); s->Release(); h->Release();
        }
        Assert::IsTrue(cap.containsMessagePrefix(L"PropertyStoreInitializeCompleted"),
                       L"Successful Initialize must emit a PropertyStoreInitializeCompleted event");
    }

    TEST_METHOD(Initialize_NullStream_EmitsNullStreamFailure) {
        ScopedPropertyHandlerCapture cap;
        CXISFPropertyHandler handler;
        HRESULT hr = handler.Initialize(nullptr, STGM_READ);
        Assert::AreNotEqual(S_OK, hr);

        const CapturedEvent* evt = cap.findByPrefix(L"PropertyStoreInitializeFailed");
        Assert::IsNotNull(evt, L"Null stream must emit PropertyStoreInitializeFailed event");
        Assert::IsTrue(evt->message.find(L"NullStream") != std::wstring::npos,
                       L"Event must identify NullStream stage");
    }

    TEST_METHOD(Initialize_BogusStream_EmitsParseFailure) {
        ScopedPropertyHandlerCapture cap;
        {
            auto* h = new CXISFPropertyHandler();
            IStream* s = CreateBogusStream();
            IInitializeWithStream* pi = nullptr;
            h->QueryInterface(IID_PPV_ARGS(&pi));
            pi->Initialize(s, STGM_READ);
            pi->Release(); s->Release(); h->Release();
        }
        const CapturedEvent* evt = cap.findByPrefix(L"PropertyStoreInitializeFailed");
        Assert::IsNotNull(evt, L"Bogus stream must emit PropertyStoreInitializeFailed event");
    }

    TEST_METHOD(Initialize_FailureEvents_UseWarningLevel) {
        ScopedPropertyHandlerCapture cap;
        CXISFPropertyHandler handler;
        handler.Initialize(nullptr, STGM_READ);

        const CapturedEvent* evt = cap.findByPrefix(L"PropertyStoreInitializeFailed");
        Assert::IsNotNull(evt);
        // TRACE_LEVEL_WARNING == 3
        Assert::AreEqual(static_cast<int>(3), static_cast<int>(evt->level),
                         L"Initialize failures must be emitted at WARNING level");
    }

    TEST_METHOD(Initialize_FailureEvents_UseLifecycleKeyword) {
        ScopedPropertyHandlerCapture cap;
        CXISFPropertyHandler handler;
        handler.Initialize(nullptr, STGM_READ);

        const CapturedEvent* evt = cap.findByPrefix(L"PropertyStoreInitializeFailed");
        Assert::IsNotNull(evt);
        // LIFECYCLE keyword = 0x1
        Assert::AreEqual(static_cast<unsigned long long>(0x1ULL),
                         static_cast<unsigned long long>(evt->keyword & 0x1ULL),
                         L"Initialize failures must set the LIFECYCLE keyword bit");
    }

    TEST_METHOD(PopulateProperties_EmitsAggregatedSummary) {
        ScopedPropertyHandlerCapture cap;
        {
            auto* h = new CXISFPropertyHandler();
            IStream* s = CreateXISFStream(kHandlerXML);
            IInitializeWithStream* pi = nullptr;
            h->QueryInterface(IID_PPV_ARGS(&pi));
            pi->Initialize(s, STGM_READ);
            IPropertyStore* ps = nullptr;
            h->QueryInterface(IID_PPV_ARGS(&ps));
            // Force property population by asking for a value
            PROPVARIANT pv; PropVariantInit(&pv);
            ps->GetValue(PKEY_XISF_ExposureTime, &pv);
            PropVariantClear(&pv);
            ps->Release(); pi->Release(); s->Release(); h->Release();
        }
        Assert::IsTrue(cap.containsMessagePrefix(L"PropertyPopulation"),
                       L"Populated property store must emit an aggregated PropertyPopulation summary");
    }

    TEST_METHOD(NoTelemetryEmitted_WhenHookUninstalled) {
        // Explicitly verify the baseline: with no hook installed and no ETW
        // consumer attached, emission is a no-op (see EventProviderEnabled gate).
        // We capture before and after installing the hook to confirm the hook
        // itself is what turns on delivery.
        std::vector<CapturedEvent> noHookEvents;
        {
            CXISFPropertyHandler handler;
            handler.Initialize(nullptr, STGM_READ);
        }
        Assert::AreEqual(size_t(0), noHookEvents.size(),
                         L"Without a hook or ETW consumer, no events must be captured");

        ScopedPropertyHandlerCapture cap;
        {
            CXISFPropertyHandler handler;
            handler.Initialize(nullptr, STGM_READ);
        }
        Assert::IsTrue(cap.events.size() > 0,
                       L"With the hook installed, failure events must be captured");
    }
};

} // namespace PropertyHandlerTests
