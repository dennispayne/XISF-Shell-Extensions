// DSOCatalog.cpp - Positional DSO catalog implementation (Property Handler)
// Parses OpenNGC-format CSV, builds spatial index, provides cone search.
#include "DSOCatalog.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <windows.h>

namespace xisf {

// ---------------------------------------------------------------------------
// String utilities
// ---------------------------------------------------------------------------

std::string DSOCatalog::ToLower(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return r;
}

std::string DSOCatalog::Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::vector<std::string> DSOCatalog::SplitCSV(const std::string& line, char delim) {
    std::vector<std::string> fields;
    std::string field;
    for (char c : line) {
        if (c == delim) { fields.push_back(field); field.clear(); }
        else field += c;
    }
    fields.push_back(field);
    return fields;
}

// ---------------------------------------------------------------------------
// Coordinate parsing
// ---------------------------------------------------------------------------

bool DSOCatalog::ParseRA(const std::string& s, double& outDeg) {
    // Format: HH:MM:SS.ss
    std::string t = Trim(s);
    if (t.empty()) return false;
    int h = 0, m = 0;
    double sec = 0.0;
    if (sscanf_s(t.c_str(), "%d:%d:%lf", &h, &m, &sec) < 2) return false;
    outDeg = (static_cast<double>(h) + static_cast<double>(m) / 60.0 + sec / 3600.0) * 15.0;
    return true;
}

bool DSOCatalog::ParseDec(const std::string& s, double& outDeg) {
    // Format: +DD:MM:SS.s or -DD:MM:SS.s
    std::string t = Trim(s);
    if (t.empty()) return false;
    int sign = 1;
    size_t start = 0;
    if (t[0] == '-') { sign = -1; start = 1; }
    else if (t[0] == '+') { start = 1; }
    int d = 0, m = 0;
    double sec = 0.0;
    if (sscanf_s(t.c_str() + start, "%d:%d:%lf", &d, &m, &sec) < 2) return false;
    outDeg = sign * (static_cast<double>(d) + static_cast<double>(m) / 60.0 + sec / 3600.0);
    return true;
}

// ---------------------------------------------------------------------------
// Angular separation (Haversine formula)
// ---------------------------------------------------------------------------

double DSOCatalog::AngularSeparation(double ra1, double dec1,
                                     double ra2, double dec2) {
    double dRA  = (ra2 - ra1) * kDegToRad;
    double dDec = (dec2 - dec1) * kDegToRad;
    double lat1 = dec1 * kDegToRad;
    double lat2 = dec2 * kDegToRad;
    double a = std::sin(dDec / 2.0) * std::sin(dDec / 2.0)
             + std::cos(lat1) * std::cos(lat2)
             * std::sin(dRA / 2.0) * std::sin(dRA / 2.0);
    double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
    return c * kRadToDeg;
}

// ---------------------------------------------------------------------------
// CSV parsing
// ---------------------------------------------------------------------------

// OpenNGC CSV columns (semicolon-delimited):
// 0:Name 1:Type 2:RA 3:Dec 4:Const 5:MajAx 6:MinAx 7:PosAng
// 8:B-Mag 9:V-Mag ... 23:M 24:NGC 25:IC ... 27:Identifiers 28:Common names

bool DSOCatalog::ParseCSVLine(const std::string& line, DSOEntry& out) const {
    auto fields = SplitCSV(line, ';');
    if (fields.size() < 5) return false;

    // Name (column 0) — e.g. "NGC1976", "IC0434", "Sh2-101", "B033"
    out.primaryName = Trim(fields[0]);
    if (out.primaryName.empty()) return false;

    // Type (column 1) — skip non-object rows (e.g. "**" = double star, "*" = star)
    if (fields.size() > 1) out.objectType = Trim(fields[1]);

    // RA (column 2, HH:MM:SS.ss) and Dec (column 3, +DD:MM:SS.s)
    if (fields.size() > 3) {
        if (!ParseRA(fields[2], out.ra) || !ParseDec(fields[3], out.dec))
            return false;
    } else {
        return false;
    }

    // Constellation (column 4)
    if (fields.size() > 4) out.constellation = Trim(fields[4]);

    // Major axis in arcminutes (column 5)
    if (fields.size() > 5) {
        std::string maj = Trim(fields[5]);
        if (!maj.empty()) {
            char* ep = nullptr;
            double d = std::strtod(maj.c_str(), &ep);
            if (ep != maj.c_str()) out.majorAxisArcmin = d;
        }
    }

    // V-Mag (column 9)
    if (fields.size() > 9) {
        std::string vmag = Trim(fields[9]);
        if (!vmag.empty()) {
            char* ep = nullptr;
            double d = std::strtod(vmag.c_str(), &ep);
            if (ep != vmag.c_str()) out.vMag = d;
        }
    }

    // Parse primary name into catalog prefix + number
    // Handles: NGC1976, IC0434, Sh2-101, B033, C009, LBN974
    {
        const std::string& n = out.primaryName;
        size_t numStart = 0;

        if (n.size() > 3 && (n[0] == 'N' || n[0] == 'n') &&
            (n[1] == 'G' || n[1] == 'g') && (n[2] == 'C' || n[2] == 'c')) {
            numStart = 3;
            std::string num = n.substr(numStart);
            // Strip leading zeros
            size_t nz = num.find_first_not_of('0');
            if (nz != std::string::npos) num = num.substr(nz);
            out.designations["NGC"] = num;
        } else if (n.size() > 2 && (n[0] == 'I' || n[0] == 'i') &&
                   (n[1] == 'C' || n[1] == 'c')) {
            numStart = 2;
            std::string num = n.substr(numStart);
            size_t nz = num.find_first_not_of('0');
            if (nz != std::string::npos) num = num.substr(nz);
            out.designations["IC"] = num;
        } else if (n.size() > 3 && n.substr(0, 3) == "Sh2") {
            size_t dash = n.find('-');
            if (dash != std::string::npos) {
                out.designations["Sh2"] = n.substr(dash + 1);
            }
        } else if (n.size() > 1 && (n[0] == 'B' || n[0] == 'b') && std::isdigit(n[1])) {
            std::string num = n.substr(1);
            size_t nz = num.find_first_not_of('0');
            if (nz != std::string::npos) num = num.substr(nz);
            out.designations["B"] = num;
        } else if (n.size() > 1 && (n[0] == 'C' || n[0] == 'c') && std::isdigit(n[1])) {
            std::string num = n.substr(1);
            size_t nz = num.find_first_not_of('0');
            if (nz != std::string::npos) num = num.substr(nz);
            out.designations["C"] = num;
        } else if (n.size() > 3 && n.substr(0, 3) == "LBN") {
            std::string num = n.substr(3);
            size_t nz = num.find_first_not_of('0');
            if (nz != std::string::npos) num = num.substr(nz);
            out.designations["LBN"] = num;
        }
    }

    // Messier number (column 23)
    if (fields.size() > 23) {
        std::string m = Trim(fields[23]);
        if (!m.empty()) {
            size_t nz = m.find_first_not_of('0');
            if (nz != std::string::npos) m = m.substr(nz);
            out.designations["M"] = m;
        }
    }

    // NGC cross-reference (column 24) — for entries not primarily NGC
    if (fields.size() > 24) {
        std::string ngcRef = Trim(fields[24]);
        if (!ngcRef.empty() && out.designations.find("NGC") == out.designations.end()) {
            // Could be "NGC1976" or just "1976"
            if (ngcRef.size() > 3 && ngcRef.substr(0, 3) == "NGC") ngcRef = ngcRef.substr(3);
            size_t nz = ngcRef.find_first_not_of('0');
            if (nz != std::string::npos) ngcRef = ngcRef.substr(nz);
            if (!ngcRef.empty()) out.designations["NGC"] = ngcRef;
        }
    }

    // IC cross-reference (column 25)
    if (fields.size() > 25) {
        std::string icRef = Trim(fields[25]);
        if (!icRef.empty() && out.designations.find("IC") == out.designations.end()) {
            if (icRef.size() > 2 && icRef.substr(0, 2) == "IC") icRef = icRef.substr(2);
            size_t nz = icRef.find_first_not_of('0');
            if (nz != std::string::npos) icRef = icRef.substr(nz);
            if (!icRef.empty()) out.designations["IC"] = icRef;
        }
    }

    // Identifiers (column 27) — parse for Sh2, LBN, Barnard, etc.
    if (fields.size() > 27) {
        std::string ids = fields[27];
        // Split by comma
        std::istringstream idStream(ids);
        std::string token;
        while (std::getline(idStream, token, ',')) {
            token = Trim(token);
            if (token.size() > 4 && token.substr(0, 4) == "LBN ") {
                std::string num = Trim(token.substr(4));
                if (!num.empty() && out.designations.find("LBN") == out.designations.end())
                    out.designations["LBN"] = num;
            } else if (token.size() > 4 && (token.substr(0, 4) == "SH 2" || token.substr(0, 3) == "Sh2")) {
                size_t dash = token.find('-');
                if (dash != std::string::npos) {
                    std::string num = Trim(token.substr(dash + 1));
                    if (!num.empty() && out.designations.find("Sh2") == out.designations.end())
                        out.designations["Sh2"] = num;
                }
            }
        }
    }

    // Common names (column 28)
    if (fields.size() > 28) {
        std::string names = Trim(fields[28]);
        if (!names.empty()) {
            // Take the first common name as primary
            size_t comma = names.find(',');
            out.commonName = (comma != std::string::npos) ? Trim(names.substr(0, comma)) : names;
        }
    }

    // Caldwell from Identifiers or primary name
    if (fields.size() > 27 && out.designations.find("C") == out.designations.end()) {
        // Check if identifiers contain a Caldwell reference
        // Caldwell objects are sometimes labeled as C NNN in the addendum
    }

    return true;
}

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------

bool DSOCatalog::LoadFromCSVString(const std::string& csvContent) {
    m_entries.clear();
    m_nameIndex.clear();
    m_decSorted.clear();

    std::istringstream stream(csvContent);
    std::string line;
    bool headerSkipped = false;

    while (std::getline(stream, line)) {
        if (line.empty() || line[0] == '#') continue;
        if (!headerSkipped) {
            // Skip header line (starts with "Name;")
            if (line.size() > 4 && line.substr(0, 5) == "Name;") {
                headerSkipped = true;
                continue;
            }
        }

        DSOEntry entry;
        if (ParseCSVLine(line, entry)) {
            m_entries.push_back(std::move(entry));
        }
    }

    RebuildIndices();
    return !m_entries.empty();
}

bool DSOCatalog::AppendFromCSVString(const std::string& csvContent) {
    std::istringstream stream(csvContent);
    std::string line;
    bool headerSkipped = false;
    size_t added = 0;

    while (std::getline(stream, line)) {
        if (line.empty() || line[0] == '#') continue;
        if (!headerSkipped) {
            if (line.size() > 4 && line.substr(0, 5) == "Name;") {
                headerSkipped = true;
                continue;
            }
        }

        DSOEntry entry;
        if (ParseCSVLine(line, entry)) {
            m_entries.push_back(std::move(entry));
            ++added;
        }
    }

    if (added > 0) RebuildIndices();
    return added > 0;
}

bool DSOCatalog::LoadFromCSVFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return false;
    std::ostringstream ss;
    ss << file.rdbuf();
    return LoadFromCSVString(ss.str());
}

bool DSOCatalog::AppendFromCSVFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return false;
    std::ostringstream ss;
    ss << file.rdbuf();
    return AppendFromCSVString(ss.str());
}

bool DSOCatalog::LoadFromResource(HMODULE hModule, int resourceId) {
    HRSRC hRes = FindResourceW(hModule, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (!hRes) return false;
    HGLOBAL hData = LoadResource(hModule, hRes);
    if (!hData) return false;
    DWORD size = SizeofResource(hModule, hRes);
    if (size == 0) return false;
    const char* data = static_cast<const char*>(LockResource(hData));
    if (!data) return false;
    std::string csv(data, size);
    return LoadFromCSVString(csv);
}

bool DSOCatalog::AppendFromResource(HMODULE hModule, int resourceId) {
    HRSRC hRes = FindResourceW(hModule, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (!hRes) return false;
    HGLOBAL hData = LoadResource(hModule, hRes);
    if (!hData) return false;
    DWORD size = SizeofResource(hModule, hRes);
    if (size == 0) return false;
    const char* data = static_cast<const char*>(LockResource(hData));
    if (!data) return false;
    std::string csv(data, size);
    return AppendFromCSVString(csv);
}

// ---------------------------------------------------------------------------
// Index building
// ---------------------------------------------------------------------------

void DSOCatalog::IndexEntry(size_t idx) {
    const auto& entry = m_entries[idx];

    // Index primary name
    if (!entry.primaryName.empty())
        m_nameIndex[ToLower(entry.primaryName)] = idx;

    // Index common name
    if (!entry.commonName.empty())
        m_nameIndex[ToLower(entry.commonName)] = idx;

    // Index all designations as "PREFIX NUMBER" and "PREFIXNUMBER"
    for (const auto& [prefix, number] : entry.designations) {
        // "M 42", "M42"
        m_nameIndex[ToLower(prefix + " " + number)] = idx;
        m_nameIndex[ToLower(prefix + number)] = idx;

        // Special forms: "NGC 1976" (with space)
        if (prefix == "NGC" || prefix == "IC" || prefix == "LBN") {
            m_nameIndex[ToLower(prefix + " " + number)] = idx;
        }
        // "Messier 42" for M
        if (prefix == "M") {
            m_nameIndex["messier " + ToLower(number)] = idx;
        }
        // "Caldwell 17" for C
        if (prefix == "C") {
            m_nameIndex["caldwell " + ToLower(number)] = idx;
        }
        // "Barnard 33" for B
        if (prefix == "B") {
            m_nameIndex["barnard " + ToLower(number)] = idx;
            m_nameIndex["b " + ToLower(number)] = idx;
        }
        // "Sharpless 101", "Sh 2-101"
        if (prefix == "Sh2") {
            m_nameIndex["sharpless " + ToLower(number)] = idx;
            m_nameIndex["sh 2-" + ToLower(number)] = idx;
            m_nameIndex["sh2-" + ToLower(number)] = idx;
        }
    }
}

void DSOCatalog::RebuildIndices() {
    m_nameIndex.clear();
    m_nameIndex.reserve(m_entries.size() * 6);

    for (size_t i = 0; i < m_entries.size(); ++i) {
        IndexEntry(i);
    }

    // Build Dec-sorted index for spatial queries
    m_decSorted.resize(m_entries.size());
    for (size_t i = 0; i < m_entries.size(); ++i) m_decSorted[i] = i;
    std::sort(m_decSorted.begin(), m_decSorted.end(),
              [this](size_t a, size_t b) {
                  return m_entries[a].dec < m_entries[b].dec;
              });
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

const DSOEntry* DSOCatalog::FindByName(const std::string& name) const {
    auto it = m_nameIndex.find(ToLower(Trim(name)));
    if (it != m_nameIndex.end()) return &m_entries[it->second];
    return nullptr;
}

std::vector<std::string> DSOCatalog::GetAllNames(const DSOEntry& entry) const {
    std::vector<std::string> names;
    if (!entry.commonName.empty()) names.push_back(entry.commonName);
    for (const auto& [prefix, number] : entry.designations) {
        if (prefix == "Sh2")
            names.push_back("Sh2-" + number);
        else
            names.push_back(prefix + " " + number);
    }
    return names;
}

std::vector<std::string> DSOCatalog::GetAllNames(const std::string& objectName) const {
    const DSOEntry* entry = FindByName(objectName);
    if (!entry) return {};
    return GetAllNames(*entry);
}

std::string DSOCatalog::GetPreferredName(const DSOEntry& entry,
                                         const std::vector<std::string>& priorityList) const {
    // Walk priority list; return first matching designation
    for (const auto& prefix : priorityList) {
        auto it = entry.designations.find(prefix);
        if (it != entry.designations.end()) {
            if (prefix == "Sh2")
                return "Sh2-" + it->second;
            else
                return prefix + " " + it->second;
        }
    }
    // Fallback: common name, then primary name
    if (!entry.commonName.empty()) return entry.commonName;
    return entry.primaryName;
}

std::vector<ConeSearchResult> DSOCatalog::ConeSearch(double raDeg, double decDeg,
                                                     double radiusDeg) const {
    std::vector<ConeSearchResult> results;
    if (m_decSorted.empty()) return results;

    // Narrow by Dec band using binary search on the sorted index
    double decMin = decDeg - radiusDeg;
    double decMax = decDeg + radiusDeg;

    // Find first entry with dec >= decMin
    auto lower = std::lower_bound(m_decSorted.begin(), m_decSorted.end(), decMin,
        [this](size_t idx, double val) { return m_entries[idx].dec < val; });

    // Find first entry with dec > decMax
    auto upper = std::upper_bound(m_decSorted.begin(), m_decSorted.end(), decMax,
        [this](double val, size_t idx) { return val < m_entries[idx].dec; });

    // Check each candidate in the Dec band
    for (auto it = lower; it != upper; ++it) {
        size_t idx = *it;
        double dist = AngularSeparation(raDeg, decDeg,
                                        m_entries[idx].ra, m_entries[idx].dec);
        if (dist <= radiusDeg) {
            results.push_back({idx, dist});
        }
    }

    // Sort by distance (nearest first)
    std::sort(results.begin(), results.end(),
              [](const ConeSearchResult& a, const ConeSearchResult& b) {
                  return a.distanceDeg < b.distanceDeg;
              });

    return results;
}

std::vector<std::string> DSOCatalog::ParsePriorityString(const std::string& csv) {
    std::vector<std::string> result;
    std::istringstream stream(csv);
    std::string token;
    while (std::getline(stream, token, ',')) {
        std::string t = Trim(token);
        if (!t.empty()) result.push_back(t);
    }
    return result;
}

} // namespace xisf
