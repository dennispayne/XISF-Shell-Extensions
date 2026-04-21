// DSOCatalog.h - Positional DSO catalog with cone search and catalog priority (Property Handler)
// Data sourced from OpenNGC (CC-BY-SA 4.0) + Sharpless supplement.
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <cmath>

namespace xisf {

/// A single deep-sky object with position, size, and multi-catalog designations.
struct DSOEntry {
    double ra  = 0.0;           ///< Right ascension in degrees (J2000)
    double dec = 0.0;           ///< Declination in degrees (J2000)
    double majorAxisArcmin = 0; ///< Major axis in arcminutes (0 = unknown)
    double vMag = 99.0;         ///< Visual magnitude (99 = unknown)
    std::string objectType;     ///< OpenNGC type code (G, Cl+N, HII, PN, etc.)
    std::string constellation;  ///< IAU 3-letter abbreviation
    std::string commonName;     ///< Primary common name (e.g. "Orion Nebula")

    /// All catalog designations: prefix -> number (e.g. "M"->"42", "NGC"->"1976")
    std::unordered_map<std::string, std::string> designations;

    /// The original Name column from OpenNGC (e.g. "NGC1976", "IC0434")
    std::string primaryName;
};

/// Result of a cone search, sorted by distance from search center.
struct ConeSearchResult {
    size_t entryIndex = 0;      ///< Index into the catalog entries vector
    double distanceDeg = 0.0;   ///< Angular separation from search center
};

/// Positional DSO catalog with spatial indexing for fast cone searches.
/// Loads OpenNGC-format CSV data and provides O(1) name lookup and fast
/// spatial queries for coordinate-based object matching.
class DSOCatalog {
public:
    DSOCatalog() = default;

    // -- Loading ----------------------------------------------------------

    /// Load catalog from a semicolon-delimited CSV file (OpenNGC format).
    bool LoadFromCSVFile(const std::string& path);

    /// Load catalog from an in-memory CSV string.
    bool LoadFromCSVString(const std::string& csvContent);

    /// Append additional entries from another CSV (e.g. sharpless.csv, addendum.csv).
    bool AppendFromCSVString(const std::string& csvContent);

    /// Load from an embedded Win32 resource (RT_RCDATA).
    bool LoadFromResource(HMODULE hModule, int resourceId);

    /// Append entries from an embedded Win32 resource (RT_RCDATA).
    bool AppendFromResource(HMODULE hModule, int resourceId);

    // -- Queries ----------------------------------------------------------

    /// Find all catalog objects within radiusDeg of the given RA/Dec.
    /// Results sorted by angular distance (nearest first).
    std::vector<ConeSearchResult> ConeSearch(double raDeg, double decDeg,
                                             double radiusDeg) const;

    /// Look up an entry by any designation or common name (case-insensitive).
    /// Returns nullptr if not found.
    const DSOEntry* FindByName(const std::string& name) const;

    /// Return all names (designations + common name) for a given entry.
    std::vector<std::string> GetAllNames(const DSOEntry& entry) const;

    /// Return all names for an object matched by name (convenience wrapper).
    std::vector<std::string> GetAllNames(const std::string& objectName) const;

    /// Get the preferred display name for an entry based on catalog priority.
    /// priorityList is e.g. {"M","C","NGC","IC","Sh2","B","LBN"}.
    std::string GetPreferredName(const DSOEntry& entry,
                                 const std::vector<std::string>& priorityList) const;

    /// Number of entries loaded.
    size_t Count() const { return m_entries.size(); }

    /// Direct access to an entry by index.
    const DSOEntry& GetEntry(size_t index) const { return m_entries[index]; }

    /// Parse a catalog priority string (e.g. "M,C,NGC,IC,Sh2,B,LBN") into a vector.
    static std::vector<std::string> ParsePriorityString(const std::string& csv);

    // -- Math utilities (public for testing) -------------------------------

    /// Angular separation in degrees between two points on the celestial sphere.
    static double AngularSeparation(double ra1, double dec1,
                                    double ra2, double dec2);

    /// Parse RA string "HH:MM:SS.ss" to degrees.
    static bool ParseRA(const std::string& s, double& outDeg);

    /// Parse Dec string "+DD:MM:SS.s" to degrees.
    static bool ParseDec(const std::string& s, double& outDeg);

private:
    std::vector<DSOEntry> m_entries;

    /// Name index: lowercase name/designation -> entry index (O(1) lookup).
    std::unordered_map<std::string, size_t> m_nameIndex;

    /// Dec-sorted index for fast spatial queries.
    /// Stores indices into m_entries, sorted by declination.
    std::vector<size_t> m_decSorted;

    /// Parse a single CSV line into a DSOEntry.
    bool ParseCSVLine(const std::string& line, DSOEntry& out) const;

    /// Split a string by a delimiter.
    static std::vector<std::string> SplitCSV(const std::string& line, char delim);

    /// Rebuild spatial and name indices after loading.
    void RebuildIndices();

    /// Add all designations and names for an entry to the name index.
    void IndexEntry(size_t idx);

    static std::string ToLower(const std::string& s);
    static std::string Trim(const std::string& s);

    static constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
    static constexpr double kRadToDeg = 180.0 / 3.14159265358979323846;
};

} // namespace xisf
