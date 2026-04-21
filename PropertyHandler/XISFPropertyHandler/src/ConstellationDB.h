// ConstellationDB.h - IAU constellation boundary lookup (Property Handler)
// Identifies which of the 88 IAU constellations contains a given RA/Dec.
#pragma once
#include <string>

namespace xisf {

/// Identifies the IAU constellation for a given equatorial coordinate.
/// Uses the authoritative Delporte (1930) boundaries in J2000 coordinates
/// from the "Roman (1987)" machine-readable edition.
class ConstellationDB {
public:
    /// Return the 3-letter IAU abbreviation for the constellation containing
    /// the given RA (degrees, 0-360) and Dec (degrees, -90 to +90).
    /// Returns empty string if lookup fails.
    static std::string Identify(double raDeg, double decDeg);

    /// Return the full English name for a 3-letter IAU abbreviation.
    /// Returns the abbreviation itself if not found.
    static std::string FullName(const std::string& abbrev);
};

} // namespace xisf
