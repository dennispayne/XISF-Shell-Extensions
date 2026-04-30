// ComputedProperties.cpp — Computed/derived properties for XISF files.
// Extracted from PropertyStore.cpp to isolate computed operations.
#include "ComputedProperties.h"
#include "PropertyStore.h"
#include "ConstellationDB.h"
#include "DSOCatalog.h"
#include "PropertyHandlerTraceLogging.h"
#include <propkey.h>
#include <cmath>
#include <algorithm>
#include <mutex>
#include <shlobj.h>
#include <knownfolders.h>
#include <strsafe.h>

extern "C" XISFPropertyHandlerTelemetryHook g_xisfPropertyHandlerTelemetryHook;
void WritePropertyHandlerTelemetry(UCHAR level, ULONGLONG keyword, PCWSTR format, ...);

namespace xisf {

// ---------------------------------------------------------------------------
// Catalog singleton (shared across all handler instances, loaded once)
// ---------------------------------------------------------------------------

static std::shared_ptr<DSOCatalog> s_dsoCatalog;
static std::vector<std::string> s_catalogPriority;
static double s_matchToleranceDeg = 0.5;
static auto s_catalogOnceFlag = std::make_shared<std::once_flag>();

static std::string ComputeRAHour(double raDegrees)
{
    int hour = static_cast<int>(raDegrees / 15.0);
    if (hour < 0) hour = 0; if (hour > 23) hour = 23;
    char buf[16]; snprintf(buf, sizeof(buf), "%dh", hour);
    return buf;
}

static std::string ComputeDecBand(double decDegrees)
{
    int band = static_cast<int>(std::floor(decDegrees / 15.0)) * 15;
    if (band < -90) band = -90;
    if (band > 75) band = 75;
    int upper = band + 15;
    if (upper > 90) upper = 90;
    char buf[64];
    snprintf(buf, sizeof(buf), "%+d\xC2\xB0 to %+d\xC2\xB0", band, upper);
    return buf;
}

static void AddString(std::vector<ComputedPropertyEntry>& out, const PROPERTYKEY& key, const std::string& val)
{
    if (val.empty()) return;
    ComputedPropertyEntry e;
    e.key = key;
    e.type = ComputedPropertyEntry::Type::String;
    e.stringValue = val;
    out.push_back(std::move(e));
}

static void AddDouble(std::vector<ComputedPropertyEntry>& out, const PROPERTYKEY& key, double val)
{
    ComputedPropertyEntry e;
    e.key = key;
    e.type = ComputedPropertyEntry::Type::Double;
    e.doubleValue = val;
    out.push_back(std::move(e));
}

static void AddStringList(std::vector<ComputedPropertyEntry>& out, const PROPERTYKEY& key, const std::vector<std::string>& vals)
{
    if (vals.empty()) return;
    ComputedPropertyEntry e;
    e.key = key;
    e.type = ComputedPropertyEntry::Type::StringList;
    e.stringListValue = vals;
    out.push_back(std::move(e));
}

static bool TryParse(const std::string& raw, double* out)
{
    if (raw.empty()) return false;
    // Trim
    size_t s = raw.find_first_not_of(" \t\r\n");
    if (s == std::string::npos) return false;
    size_t e = raw.find_last_not_of(" \t\r\n");
    std::string t = raw.substr(s, e - s + 1);
    if (t.empty()) return false;
    char* ep = nullptr;
    double d = strtod(t.c_str(), &ep);
    if (ep == t.c_str()) return false;
    *out = d;
    return true;
}

// ---------------------------------------------------------------------------
// Catalog loading (Full tier only)
// ---------------------------------------------------------------------------

static void EnsureCatalogLoaded()
{
    std::call_once(*s_catalogOnceFlag, []() {
        // Read catalog priority from registry
        {
            wchar_t buf[512] = {};
            DWORD cb = sizeof(buf);
            LONG st = RegGetValueW(HKEY_CURRENT_USER, L"Software\\XISFPropertyHandler", L"CatalogPriority", RRF_RT_REG_SZ, nullptr, buf, &cb);
            if (st == ERROR_SUCCESS && buf[0] != L'\0') {
                int len = WideCharToMultiByte(CP_UTF8, 0, buf, -1, nullptr, 0, nullptr, nullptr);
                std::string prio(static_cast<size_t>(len - 1), '\0');
                WideCharToMultiByte(CP_UTF8, 0, buf, -1, prio.data(), len, nullptr, nullptr);
                s_catalogPriority = DSOCatalog::ParsePriorityString(prio);
            }
            else {
                s_catalogPriority = {"M", "C", "NGC", "IC", "Sh2", "B", "LBN"};
            }
        }
        // Read match tolerance
        {
            wchar_t buf[64] = {};
            DWORD cb = sizeof(buf);
            LONG st = RegGetValueW(HKEY_CURRENT_USER, L"Software\\XISFPropertyHandler", L"MatchToleranceDeg", RRF_RT_REG_SZ, nullptr, buf, &cb);
            if (st == ERROR_SUCCESS && buf[0] != L'\0') {
                double d = _wtof(buf);
                if (d > 0.0 && d <= 10.0) s_matchToleranceDeg = d;
            }
        }

        auto cat = std::make_shared<DSOCatalog>();

        auto loadLocalAppDataCsv = [](DSOCatalog& c, const wchar_t* fileName, bool append) -> bool {
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

        bool anyLoaded = loadLocalAppDataCsv(*cat, L"NGC.csv", false);
        if (loadLocalAppDataCsv(*cat, L"addendum.csv", true)) anyLoaded = true;
        if (loadLocalAppDataCsv(*cat, L"sharpless.csv", true)) anyLoaded = true;

        if (cat->Count() > 0) s_dsoCatalog = cat;

        // Load constellation boundary and name files from the catalog directory.
        // Falls back to compiled-in data if files are absent.
        auto loadConstellationFile = [](const wchar_t* fileName, bool isBoundaries) -> bool {
            PWSTR pszBase = nullptr;
            if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &pszBase)) || !pszBase)
                return false;
            std::wstring wpath = std::wstring(pszBase) + L"\\XISFShellExtension\\catalogs\\" + fileName;
            CoTaskMemFree(pszBase);
            int len = WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (len <= 0) return false;
            std::string path(static_cast<size_t>(len - 1), '\0');
            WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, path.data(), len, nullptr, nullptr);
            return isBoundaries
                ? ConstellationDB::LoadBoundariesFromFile(path.c_str())
                : ConstellationDB::LoadNamesFromFile(path.c_str());
        };
        loadConstellationFile(L"constellation_boundaries.csv", true);
        loadConstellationFile(L"constellation_names.csv", false);

        WritePropertyHandlerTelemetry(TRACE_LEVEL_INFORMATION, XISF_ETW_KEYWORD_CATALOG | XISF_ETW_KEYWORD_PERF,
            L"CatalogConfigured Source=%ls PriorityCount=%u MatchToleranceDeg=%.3f EntryCount=%u",
            anyLoaded ? L"localappdata-csv" : L"none",
            static_cast<UINT32>(s_catalogPriority.size()),
            s_matchToleranceDeg,
            static_cast<UINT32>(cat->Count()));
    });
}

// ---------------------------------------------------------------------------
// Main entry point
// ---------------------------------------------------------------------------

std::vector<ComputedPropertyEntry> PopulateComputedProperties(
    const ComputedPropertyInputs& inputs)
{
    std::vector<ComputedPropertyEntry> result;
    result.reserve(16);

    // Resolve coordinates: prefer object coords over telescope center
    double computedRA = inputs.hasObjRA ? inputs.objRaDeg : inputs.raDeg;
    double computedDec = inputs.hasObjDec ? inputs.objDecDeg : inputs.decDeg;
    bool hasComputedCoords = (inputs.hasObjRA && inputs.hasObjDec) || (inputs.hasRA && inputs.hasDec);

    // --- Standard tier: Constellation, RA/Dec bands, DataState ---

    // RA Hour band
    if (inputs.hasObjRA || inputs.hasRA) {
        AddString(result, PKEY_XISF_RAHour, ComputeRAHour(computedRA));
    }

    // Dec Band
    if (inputs.hasObjDec || inputs.hasDec) {
        AddString(result, PKEY_XISF_DecBand, ComputeDecBand(computedDec));
    }

    // Constellation
    std::string constellation;
    if (hasComputedCoords) {
        constellation = ConstellationDB::Identify(computedRA, computedDec);
        if (!constellation.empty()) {
            AddString(result, PKEY_XISF_Constellation, ConstellationDB::FullName(constellation));
        }
    }

    // Data State (Linear / Non-Linear)
    {
        auto sfIt = inputs.metadata.imageAttributes.find("sampleFormat");
        auto csIt = inputs.metadata.imageAttributes.find("colorSpace");
        std::string sampleFormat = (sfIt != inputs.metadata.imageAttributes.end()) ? sfIt->second : "";
        std::string colorSpace   = (csIt != inputs.metadata.imageAttributes.end()) ? csIt->second : "";

        if (!sampleFormat.empty() || !colorSpace.empty()) {
            bool isFloat32 = (sampleFormat == "Float32");
            bool isFloat64 = (sampleFormat == "Float64");
            bool isUInt8   = (sampleFormat == "UInt8");

            bool isLinear = (isFloat32 || isFloat64);
            if (colorSpace == "Gray" || colorSpace == "RGB") isLinear = true;
            if (colorSpace == "GraySRGB" || colorSpace == "RGBSRGB") isLinear = false;
            if (isUInt8) isLinear = false;

            AddString(result, PKEY_XISF_DataState, isLinear ? "Linear" : "Non-Linear");
        }
    }

    // System.Photo projection (orthogonal to tier, controlled by its own flag)
    UINT32 projectionPropCount = 0;
    if (inputs.projectionEnabled) {
        if (inputs.hasExposure) { AddDouble(result, PKEY_Photo_ExposureTime, inputs.exposureTime); ++projectionPropCount; }
        if (!inputs.cameraModel.empty()) { AddString(result, PKEY_Photo_CameraModel, inputs.cameraModel); ++projectionPropCount; }
        if (inputs.hasFocalLength) { AddDouble(result, PKEY_Photo_FocalLength, inputs.focalLength); ++projectionPropCount; }
        if (inputs.hasFNumber) { AddDouble(result, PKEY_Photo_FNumber, inputs.fNumber); ++projectionPropCount; }
    }

    WritePropertyHandlerTelemetry(TRACE_LEVEL_INFORMATION, XISF_ETW_KEYWORD_PROJECTION,
        L"ProjectionEvaluated Enabled=%u ProjectedPropertyCount=%u",
        inputs.projectionEnabled ? 1u : 0u, projectionPropCount);

    // --- Full tier: DSO search, matched objects, keywords ---

    std::string matchedObjectsStr;
    decltype(s_dsoCatalog->ConeSearch(0, 0, 0)) coneResults;

    if (IsDSOSearchEnabled(inputs.tier)) {
        EnsureCatalogLoaded();

        // Compute FOV-based cone search radius
        double coneRadiusDeg = s_matchToleranceDeg;
        if (inputs.hasFocalLength && inputs.focalLength > 0.0) {
            double pixSizeUm = 0;
            bool hasPx = TryParse(inputs.pixelSizeRaw, &pixSizeUm) && pixSizeUm > 0.0;
            if (hasPx) {
                double nax1 = 0, nax2 = 0;
                TryParse(inputs.metadata.getFITSValue("NAXIS1"), &nax1);
                TryParse(inputs.metadata.getFITSValue("NAXIS2"), &nax2);
                // Fallback: parse image dimensions from XISF geometry attribute
                if ((nax1 <= 0.0 || nax2 <= 0.0) && !inputs.metadata.xmlHeader.empty()) {
                    auto gp = inputs.metadata.xmlHeader.find("geometry=\"");
                    if (gp != std::string::npos) {
                        gp += 10;
                        auto ge = inputs.metadata.xmlHeader.find('"', gp);
                        if (ge != std::string::npos) {
                            std::string geom = inputs.metadata.xmlHeader.substr(gp, ge - gp);
                            auto c1 = geom.find(':');
                            if (c1 != std::string::npos) {
                                auto c2 = geom.find(':', c1 + 1);
                                if (c2 != std::string::npos) {
                                    TryParse(geom.substr(0, c1), &nax1);
                                    TryParse(geom.substr(c1 + 1, c2 - c1 - 1), &nax2);
                                }
                            }
                        }
                    }
                }
                if (nax1 > 0.0 && nax2 > 0.0) {
                    double diagPx = std::sqrt(nax1 * nax1 + nax2 * nax2);
                    double fovDeg = (diagPx * pixSizeUm) / (inputs.focalLength * 1000.0) * (180.0 / 3.14159265358979323846);
                    coneRadiusDeg = fovDeg / 2.0;
                }
            }
        }

        if (hasComputedCoords && s_dsoCatalog) {
            coneResults = s_dsoCatalog->ConeSearch(computedRA, computedDec, coneRadiusDeg);
            if (!coneResults.empty()) {
                size_t limit = (std::min)(coneResults.size(), static_cast<size_t>(10));
                std::vector<std::string> matchedNames;
                matchedNames.reserve(limit);
                for (size_t j = 0; j < limit; ++j) {
                    const auto& entry = s_dsoCatalog->GetEntry(coneResults[j].entryIndex);
                    std::string name = s_dsoCatalog->GetPreferredName(entry, s_catalogPriority);
                    matchedNames.push_back(name);
                }
                for (size_t i = 0; i < matchedNames.size(); ++i) {
                    if (i > 0) matchedObjectsStr += "; ";
                    matchedObjectsStr += matchedNames[i];
                }
                AddString(result, PKEY_XISF_MatchedObjects, matchedObjectsStr);
            }
        }
    }

    // Keywords aggregation (Full tier with catalog, but some keywords come from Standard)
    std::vector<std::string> keywords;
    if (!inputs.filterName.empty()) keywords.push_back(inputs.filterName);
    if (!inputs.imageType.empty()) keywords.push_back(inputs.imageType);
    if (!constellation.empty()) keywords.push_back(ConstellationDB::FullName(constellation));

    if (IsDSOSearchEnabled(inputs.tier) && s_dsoCatalog) {
        if (!inputs.objectName.empty()) {
            auto allNames = s_dsoCatalog->GetAllNames(inputs.objectName);
            for (const auto& n : allNames) keywords.push_back(n);
        }
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
        AddStringList(result, PKEY_Keywords, keywords);
    }

    return result;
}

} // namespace xisf
