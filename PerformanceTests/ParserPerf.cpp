// ParserPerf.cpp — Microbenchmarks for parser and catalog sub-phases.
//
// Isolates the hot-path components of Initialize:
//   1. XISFParser::ParseXml    — 1000 iterations, reports avg µs/parse
//   2. DSOCatalog::LoadFromCSVFile — single load, reports ms
//   3. DSOCatalog::ConeSearch  — 1000 iterations, reports avg µs/search
//   4. Keyword deduplication   — sort+unique on a 1000-entry vector
//
// Tests that depend on catalog CSV files skip gracefully when files are
// not present (Logger message, no assertion failure).

#include "CppUnitTest.h"
#include <windows.h>
#include <initguid.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <knownfolders.h>
#include <shlwapi.h>
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>
#include "XISFParser.h"
#include "DSOCatalog.h"

#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Shlwapi.lib")

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace {

    const std::string kParserXML = R"(<?xml version="1.0" encoding="UTF-8"?>
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
    <FITSKeyword name="SITELAT" value="33.45" comment="Latitude"/>
    <FITSKeyword name="SITELONG" value="-112.07" comment="Longitude"/>
    <FITSKeyword name="AIRMASS" value="1.23" comment="Airmass"/>
    <FITSKeyword name="PIERSIDE" value="West" comment="Pier side"/>
    <Property id="Instrument:ExposureTime" type="Float64" value="300.0"/>
    <Property id="Observation:Object:Name" type="String" value="Orion Nebula"/>
    <Property id="Instrument:Filter" type="String" value="Ha"/>
  </Image>
</xisf>
)";

    std::string GetCatalogPath(const wchar_t* fileName) {
        PWSTR pszBase = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &pszBase)) || !pszBase)
            return {};
        std::wstring wpath = std::wstring(pszBase) + L"\\DennisPayne\\XISFShellExtension\\catalogs\\" + fileName;
        CoTaskMemFree(pszBase);
        int len = WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (len <= 0) return {};
        std::string path(static_cast<size_t>(len - 1), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, path.data(), len, nullptr, nullptr);
        return path;
    }
}

namespace PropertyHandler_Performance {

    TEST_CLASS(ParserMicrobenchmarks)
    {
    public:

        TEST_METHOD(ParseXml_1000Iterations_MeasuresThroughput)
        {
            const int kIterations = 1000;
            LARGE_INTEGER freq, start, end;
            QueryPerformanceFrequency(&freq);
            QueryPerformanceCounter(&start);

            for (int i = 0; i < kIterations; ++i) {
                auto result = xisf::XISFParser::ParseXMLString(kParserXML);
                Assert::IsTrue(result.ok(), L"ParseXMLString failed");
            }

            QueryPerformanceCounter(&end);
            double totalUs = static_cast<double>(end.QuadPart - start.QuadPart) * 1000000.0 / freq.QuadPart;
            double avgUs = totalUs / kIterations;

            wchar_t msg[256];
            swprintf_s(msg, L"ParseXMLString: %d iterations in %.1f ms (%.1f µs/parse)",
                kIterations, totalUs / 1000.0, avgUs);
            Logger::WriteMessage(msg);

            // Sanity threshold: XML parsing should complete reasonably
            // (Debug builds are ~3x slower than Release due to disabled optimizations)
            Assert::IsTrue(avgUs < 5000.0, L"ParseXMLString too slow (>5ms per iteration)");
        }

        TEST_METHOD(CatalogLoad_NGC_MeasuresDuration)
        {
            std::string path = GetCatalogPath(L"NGC.csv");
            if (path.empty() || !std::filesystem::exists(path)) {
                Logger::WriteMessage("SKIPPED — NGC.csv not found in ProgramData catalogs");
                return;
            }

            LARGE_INTEGER freq, start, end;
            QueryPerformanceFrequency(&freq);

            xisf::DSOCatalog cat;
            QueryPerformanceCounter(&start);
            bool ok = cat.LoadFromCSVFile(path);
            QueryPerformanceCounter(&end);

            double ms = static_cast<double>(end.QuadPart - start.QuadPart) * 1000.0 / freq.QuadPart;

            wchar_t msg[256];
            swprintf_s(msg, L"CatalogLoad NGC.csv: %s, %u entries in %.2f ms",
                ok ? L"OK" : L"FAIL",
                static_cast<unsigned>(cat.Count()), ms);
            Logger::WriteMessage(msg);

            Assert::IsTrue(ok, L"Failed to load NGC.csv");
            Assert::IsTrue(ms < 5000.0, L"Catalog load took >5 seconds");
        }

        TEST_METHOD(ConeSearch_1000Iterations_MeasuresThroughput)
        {
            std::string path = GetCatalogPath(L"NGC.csv");
            if (path.empty() || !std::filesystem::exists(path)) {
                Logger::WriteMessage("SKIPPED — NGC.csv not found for cone search benchmark");
                return;
            }

            xisf::DSOCatalog cat;
            if (!cat.LoadFromCSVFile(path) || cat.Count() == 0) {
                Logger::WriteMessage("SKIPPED — NGC.csv loaded but empty");
                return;
            }

            // M42 coordinates: RA=83.82°, Dec=-5.39°
            const double ra = 83.82, dec = -5.39, radius = 0.5;
            const int kIterations = 1000;

            LARGE_INTEGER freq, start, end;
            QueryPerformanceFrequency(&freq);
            QueryPerformanceCounter(&start);

            size_t totalMatches = 0;
            for (int i = 0; i < kIterations; ++i) {
                auto results = cat.ConeSearch(ra, dec, radius);
                totalMatches += results.size();
            }

            QueryPerformanceCounter(&end);
            double totalUs = static_cast<double>(end.QuadPart - start.QuadPart) * 1000000.0 / freq.QuadPart;
            double avgUs = totalUs / kIterations;

            wchar_t msg[256];
            swprintf_s(msg, L"ConeSearch: %d iterations in %.1f ms (%.1f µs/search, avg %zu matches)",
                kIterations, totalUs / 1000.0, avgUs, totalMatches / kIterations);
            Logger::WriteMessage(msg);

            // Cone search over full catalog should complete in <1ms each
            Assert::IsTrue(avgUs < 1000.0, L"ConeSearch too slow (>1ms per iteration)");
        }

        TEST_METHOD(KeywordDedup_1000Entries_MeasuresDuration)
        {
            // Build a 1000-entry keyword vector with ~30% duplicates
            std::vector<std::string> keywords;
            keywords.reserve(1000);
            for (int i = 0; i < 700; ++i)
                keywords.push_back("UniqueKeyword_" + std::to_string(i));
            for (int i = 0; i < 300; ++i)
                keywords.push_back("UniqueKeyword_" + std::to_string(i % 100));

            LARGE_INTEGER freq, start, end;
            QueryPerformanceFrequency(&freq);

            const int kIterations = 10000;
            QueryPerformanceCounter(&start);

            for (int i = 0; i < kIterations; ++i) {
                auto copy = keywords;
                std::sort(copy.begin(), copy.end());
                copy.erase(std::unique(copy.begin(), copy.end()), copy.end());
            }

            QueryPerformanceCounter(&end);
            double totalUs = static_cast<double>(end.QuadPart - start.QuadPart) * 1000000.0 / freq.QuadPart;
            double avgUs = totalUs / kIterations;

            wchar_t msg[256];
            swprintf_s(msg, L"KeywordDedup: %d iterations of 1000 entries in %.1f ms (%.1f µs/dedup)",
                kIterations, totalUs / 1000.0, avgUs);
            Logger::WriteMessage(msg);

            // Sort+unique on 1000 strings — generous threshold for Debug builds
            Assert::IsTrue(avgUs < 10000.0, L"Keyword dedup too slow (>10ms per iteration)");
        }
    };
}
