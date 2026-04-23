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
#include "ClassFactory.h"
#include "DSOCatalog.h"
#include "ConstellationDB.h"

// Compile these units into the test module for class-factory runtime-toggle coverage.
#include "..\XISFPropertyHandler\src\HandlerSettings.cpp"
#include "..\XISFPropertyHandler\src\ClassFactory.cpp"

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

    class PropertyEnabledGuard {
    public:
        PropertyEnabledGuard() {
            DWORD value = 0;
            DWORD cb = sizeof(value);
            LONG st = RegGetValueW(
                HKEY_CURRENT_USER,
                L"Software\\DennisPayne\\XISF Shell Extension",
                L"PropertyEnabled",
                RRF_RT_REG_DWORD,
                nullptr,
                &value,
                &cb);
            if (st == ERROR_SUCCESS) {
                m_hadValue = true;
                m_prevValue = value;
            }
        }

        ~PropertyEnabledGuard() {
            HKEY hKey = nullptr;
            if (RegCreateKeyExW(HKEY_CURRENT_USER,
                                L"Software\\DennisPayne\\XISF Shell Extension",
                                0, nullptr, REG_OPTION_NON_VOLATILE,
                                KEY_SET_VALUE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
                if (m_hadValue) {
                    RegSetValueExW(hKey, L"PropertyEnabled", 0, REG_DWORD,
                                   reinterpret_cast<const BYTE*>(&m_prevValue), sizeof(m_prevValue));
                } else {
                    RegDeleteValueW(hKey, L"PropertyEnabled");
                }
                RegCloseKey(hKey);
            }
        }

        void Set(bool enabled) {
            HKEY hKey = nullptr;
            if (RegCreateKeyExW(HKEY_CURRENT_USER,
                                L"Software\\DennisPayne\\XISF Shell Extension",
                                0, nullptr, REG_OPTION_NON_VOLATILE,
                                KEY_SET_VALUE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
                DWORD dw = enabled ? 1u : 0u;
                RegSetValueExW(hKey, L"PropertyEnabled", 0, REG_DWORD,
                               reinterpret_cast<const BYTE*>(&dw), sizeof(dw));
                RegCloseKey(hKey);
            }
        }

    private:
        bool m_hadValue = false;
        DWORD m_prevValue = 1;
    };

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
// PERSPECTIVE 3a: Runtime toggle enforcement in class factory
// ===========================================================================

TEST_CLASS(PropertyHandler_ClassFactory_RuntimeToggle) {
public:
    TEST_METHOD(CreateInstance_WhenEnabled_ReturnsRequestedInterface) {
        PropertyEnabledGuard guard;
        guard.Set(true);

        CClassFactory factory;
        IPropertyStore* ps = nullptr;
        HRESULT hr = factory.CreateInstance(nullptr, IID_IPropertyStore, reinterpret_cast<void**>(&ps));
        Assert::AreEqual(S_OK, hr);
        Assert::IsNotNull(ps);
        if (ps) ps->Release();
    }

    TEST_METHOD(CreateInstance_WhenDisabled_ReturnsClassNotAvailable) {
        PropertyEnabledGuard guard;
        guard.Set(false);

        CClassFactory factory;
        IPropertyStore* ps = nullptr;
        HRESULT hr = factory.CreateInstance(nullptr, IID_IPropertyStore, reinterpret_cast<void**>(&ps));
        Assert::AreEqual(CLASS_E_CLASSNOTAVAILABLE, hr,
            L"PropertyEnabled=0 must prevent handler activation");
        Assert::IsNull(ps);
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
        IStream* s = CreateXISFStream(kHandlerXML);
        IInitializeWithStream* pi = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&pi));
        pi->Initialize(s, STGM_READ);
        IPropertyStore* ps = nullptr;
        h->QueryInterface(IID_PPV_ARGS(&ps));
        Assert::AreEqual(STG_E_ACCESSDENIED, ps->Commit());
        ps->Release(); pi->Release(); s->Release(); h->Release();
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

    TEST_METHOD(DllUnregisterServer_InvalidatesPropertySchema) {
        if (!IsProcessElevated()) { Logger::WriteMessage("SKIPPED: not elevated"); return; }
        auto dllPath = GetHandlerDllPath();
        if (!fs::exists(dllPath)) { Logger::WriteMessage("SKIPPED: DLL not built"); return; }

        // Register then immediately unregister
        {
            ScopedHandlerRegistration reg(dllPath);
            Assert::IsTrue(reg.ok);
        } // destructor calls DllUnregisterServer

        // Properties should not be marked as indexed after unregister
        const GUID xisfFmtId =
            {0x7C54FA8B, 0x9D63, 0x4C10, {0x8F, 0xBE, 0x1A, 0x5A, 0x0F, 0x9A, 0x3B, 0x2E}};
        const ULONG expectedKeys[] = {
            PKEY_XISF_ExposureTime.pid,
            PKEY_XISF_ObjectName.pid,
            PKEY_XISF_FilterName.pid,
            PKEY_XISF_CameraModel.pid,
            PKEY_XISF_Telescope.pid,
            PKEY_XISF_ImageType.pid,
            PKEY_XISF_Gain.pid,
            PKEY_XISF_Offset.pid,
            PKEY_XISF_SensorTemperature.pid,
            PKEY_XISF_Binning.pid,
            PKEY_XISF_DateObserved.pid,
            PKEY_XISF_Software.pid,
            PKEY_XISF_RA.pid,
            PKEY_XISF_Dec.pid,
            PKEY_XISF_SetTemp.pid,
            PKEY_XISF_ReadoutMode.pid,
            PKEY_XISF_BayerPattern.pid,
            PKEY_XISF_SiteLatitude.pid,
            PKEY_XISF_SiteLongitude.pid,
            PKEY_XISF_SiteElevation.pid,
            PKEY_XISF_Airmass.pid,
            PKEY_XISF_PierSide.pid,
            PKEY_XISF_ObjectRA.pid,
            PKEY_XISF_ObjectDec.pid,
            PKEY_XISF_Rotation.pid,
            PKEY_XISF_FocuserName.pid,
            PKEY_XISF_FocuserPosition.pid,
            PKEY_XISF_FocuserTemp.pid,
            PKEY_XISF_RotatorName.pid,
            PKEY_XISF_RotatorAngle.pid,
            PKEY_XISF_FilterWheel.pid,
            PKEY_XISF_DewPoint.pid,
            PKEY_XISF_Humidity.pid,
            PKEY_XISF_AmbientTemp.pid,
            PKEY_XISF_DateLocal.pid,
            PKEY_XISF_RAHour.pid,
            PKEY_XISF_DecBand.pid,
            PKEY_XISF_Constellation.pid,
            PKEY_XISF_MatchedObjects.pid,
            PKEY_XISF_StarFWHM.pid,
            PKEY_XISF_SkyQuality.pid,
            PKEY_XISF_SkyBrightness.pid,
            PKEY_XISF_CloudCover.pid,
            PKEY_XISF_Pressure.pid,
            PKEY_XISF_SkyTemp.pid,
            PKEY_XISF_WindSpeed.pid,
            PKEY_XISF_GuideRA.pid,
            PKEY_XISF_GuideDec.pid,
        };

        for (const auto& pid : expectedKeys) {
            PROPERTYKEY pk = {};
            pk.fmtid = xisfFmtId;
            pk.pid = pid;
            IPropertyDescription* pd = nullptr;
            HRESULT hr = PSGetPropertyDescription(pk, IID_PPV_ARGS(&pd));
            Assert::IsTrue(FAILED(hr), L"Property description should be unavailable after unregister");
            if (pd) pd->Release();
        }
    }
};

} // namespace PropertyHandlerTests
