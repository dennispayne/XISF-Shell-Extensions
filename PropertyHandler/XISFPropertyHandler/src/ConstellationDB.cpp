// ConstellationDB.cpp - IAU constellation boundary lookup (Property Handler)
// Algorithm: The IAU boundaries are defined as horizontal (constant Dec)
// and vertical (constant RA) segments. For a given Dec, we find all boundary
// rows that bracket that Dec, then check which RA range the point falls in.
// Data from Roman (1987), precessed to J2000 epoch.
//
// Data is loaded at runtime from constellations.csv (in the catalog directory).
// If the file is absent, Identify() and FullName() return empty strings.
#include "ConstellationDB.h"
#include "StringUtil.h"
#include <fstream>
#include <sstream>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cstdlib>
#include <mutex>

namespace xisf {

namespace {

struct BoundaryRow {
    float raLow;   // hours
    float raHigh;  // hours
    float decLow;  // degrees
    std::string con; // 3-letter IAU abbreviation
};

// Runtime-loaded data, populated by LoadFromCSV().
static std::vector<BoundaryRow>                 s_boundaries;
static std::unordered_map<std::string, std::string> s_names;
static bool s_loaded = false;
static std::mutex s_mutex;

// Parse a single comma-separated field out of a CSV line starting at pos.
// Advances pos past the comma (or to end-of-string).
static std::string NextField(const std::string& line, size_t& pos)
{
    size_t start = pos;
    size_t end   = line.find(',', start);
    if (end == std::string::npos) {
        pos = line.size();
        return line.substr(start);
    }
    pos = end + 1;
    return line.substr(start, end - start);
}

// Trim leading/trailing whitespace.
static std::string Trim(const std::string& s)
{
    return xisf::str::Trim(s);
}

} // namespace

bool ConstellationDB::LoadFromCSV(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open()) return false;

    std::vector<BoundaryRow> boundaries;
    std::unordered_map<std::string, std::string> names;

    std::string line;
    while (std::getline(f, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#') continue;

        size_t pos = 0;
        std::string type = NextField(line, pos);
        type = Trim(type);

        if (type == "B") {
            // B,raLow,raHigh,decLow,abbrev
            std::string sRaLow  = Trim(NextField(line, pos));
            std::string sRaHigh = Trim(NextField(line, pos));
            std::string sDecLow = Trim(NextField(line, pos));
            std::string sAbbrev = Trim(NextField(line, pos));
            if (sRaLow.empty() || sRaHigh.empty() || sDecLow.empty() || sAbbrev.empty())
                continue;
            char* ep = nullptr;
            float raLow  = static_cast<float>(strtod(sRaLow.c_str(),  &ep));
            if (ep == sRaLow.c_str())  continue;
            float raHigh = static_cast<float>(strtod(sRaHigh.c_str(), &ep));
            if (ep == sRaHigh.c_str()) continue;
            float decLow = static_cast<float>(strtod(sDecLow.c_str(), &ep));
            if (ep == sDecLow.c_str()) continue;
            boundaries.push_back({raLow, raHigh, decLow, sAbbrev});
        } else if (type == "N") {
            // N,abbrev,fullname
            std::string sAbbrev = Trim(NextField(line, pos));
            std::string sName   = Trim(line.substr(pos));  // rest of line = name
            if (!sAbbrev.empty() && !sName.empty())
                names[sAbbrev] = sName;
        }
    }

    if (boundaries.empty()) return false;

    std::lock_guard<std::mutex> lock(s_mutex);
    s_boundaries = std::move(boundaries);
    s_names      = std::move(names);
    s_loaded     = true;
    return true;
}

std::string ConstellationDB::Identify(double raDeg, double decDeg)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_loaded) return {};

    // Convert RA from degrees to hours
    double raHours = raDeg / 15.0;
    if (raHours < 0.0)   raHours += 24.0;
    if (raHours >= 24.0) raHours -= 24.0;

    // Scan boundary rows — ordered by decreasing decLow.
    // First match where dec >= decLow and raHours in [raLow, raHigh) wins.
    for (const auto& b : s_boundaries) {
        if (decDeg >= b.decLow && raHours >= b.raLow && raHours < b.raHigh) {
            return b.con;
        }
    }
    return {};
}

std::string ConstellationDB::FullName(const std::string& abbrev)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_loaded) return {};
    auto it = s_names.find(abbrev);
    if (it != s_names.end()) return it->second;
    return {};
}

} // namespace xisf
