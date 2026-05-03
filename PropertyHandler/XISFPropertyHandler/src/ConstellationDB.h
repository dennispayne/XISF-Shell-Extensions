// ConstellationDB.h - IAU constellation boundary lookup (Property Handler)
// Identifies which of the 88 IAU constellations contains a given RA/Dec.
#pragma once
#include <string>

namespace xisf {

/// Identifies the IAU constellation for a given equatorial coordinate.
/// Uses the authoritative Delporte (1930) boundaries in J2000 coordinates
/// from the "Roman (1987)" machine-readable edition.
///
/// Data is loaded at runtime from a CSV file (constellations.csv in the
/// catalog directory). If the file is absent both Identify() and FullName()
/// return empty strings (graceful degradation).
class ConstellationDB {
public:
    /// Load boundary and name data from a combined CSV file.
    /// Format: lines starting with 'B' are boundary records
    ///   (B,raLow_hrs,raHigh_hrs,decLow_deg,abbrev)
    /// Lines starting with 'N' are name records
    ///   (N,abbrev,fullname)
    /// Comment lines (starting with '#') and blank lines are ignored.
    /// Returns true if at least one boundary record was loaded.
    static bool LoadFromCSV(const std::string& path);

    /// Return the 3-letter IAU abbreviation for the constellation containing
    /// the given RA (degrees, 0-360) and Dec (degrees, -90 to +90).
    /// Returns empty string if data is not loaded or lookup fails.
    static std::string Identify(double raDeg, double decDeg);

    /// Return the full English name for a 3-letter IAU abbreviation.
    /// Returns empty string if data is not loaded or abbreviation not found.
    static std::string FullName(const std::string& abbrev);

    /// Load boundary data from a semicolon-delimited CSV file.
    /// Expected header: raLow;raHigh;decLow;con
    /// Rows after the header are parsed as boundary records.
    /// Returns true if at least one row was loaded. When loaded, the
    /// runtime data takes precedence over the compiled-in fallback.
    static bool LoadBoundariesFromFile(const char* path);

    /// Load constellation name mappings from a semicolon-delimited CSV file.
    /// Expected header: abbrev;name
    /// Returns true if at least one entry was loaded. When loaded, the
    /// runtime data takes precedence over the compiled-in fallback.
    static bool LoadNamesFromFile(const char* path);
};

} // namespace xisf
