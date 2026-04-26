// HandlerSettings.h - Runtime-toggle helpers for the Property Handler.
// Reads HKCU\Software\DennisPayne\XISF Shell Extension settings.
#pragma once

#include <cstdint>

namespace xisf {

// Returns true if the Property Handler class factory should create instances.
// Defaults to true when the registry value is absent or unreadable.
bool IsPropertyHandlerEnabled();

// ---------------------------------------------------------------------------
// Feature Tiers — control how much work the property handler does per file.
// ---------------------------------------------------------------------------

enum class FeatureTier : uint32_t {
    Basic    = 0,   // XML header metadata only (FITS keywords, XISF properties, image attributes)
    Standard = 1,   // + Constellation, RA/Dec bands, DataState
    Full     = 2    // + DSO cone search, pixel statistics, Keywords aggregation, MatchedObjects
};

// Read from HKCU\Software\DennisPayne\XISF Shell Extension\FeatureTier.
// Defaults to Full when absent or unreadable. Called per-Initialize(), not cached.
FeatureTier GetFeatureTier();

// Convenience helpers derived from the tier.
inline bool IsConstellationEnabled(FeatureTier t) { return t >= FeatureTier::Standard; }
inline bool IsDSOSearchEnabled(FeatureTier t)     { return t >= FeatureTier::Full; }
inline bool IsPixelStatsEnabled(FeatureTier t)    { return t >= FeatureTier::Full; }

// System.Photo projection is orthogonal to tier — controlled by its own flag.
// Read from HKCU\Software\XISFPropertyHandler\EnableSystemPhotoProjection.
// Defaults to true. Called per-Initialize(), not cached.
bool IsProjectionEnabled();

} // namespace xisf
