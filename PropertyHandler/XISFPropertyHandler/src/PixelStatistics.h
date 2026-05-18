// PixelStatistics.h — Compute pixel-level statistics from XISF image data.
// Extracted from PropertyStore.cpp to isolate expensive stream-reading operations.
#pragma once

#include <windows.h>
#include <propsys.h>
#include <propvarutil.h>
#include <vector>
#include <string>
#include "PropertyStore.h"

namespace xisf {

// Pixel statistics result. Values are normalized [0,1] for median/mean,
// percentages for clipping.
struct PixelStatsResult {
    bool available = false;
    double median = 0.0;
    double p95 = 0.0;
    double mean = 0.0;
    double clippingLowPct = 0.0;
    double clippingHighPct = 0.0;
};

// Compute subsampled pixel statistics from an XISF image's pixel data.
// Reads the largest non-thumbnail attached image, subsamples ~1M pixels,
// and computes median, mean, and clipping percentages.
//
// Parameters:
//   pStream    — positioned IStream from the property handler (already past header)
//   xmlHeader  — the raw XML header string for locating image attachments
PixelStatsResult ComputePixelStats(IStream* pStream, const std::string& xmlHeader);

// Returns true if the given PROPERTYKEY is one of the pixel stat keys.
bool IsPixelStatKey(REFPROPERTYKEY key);

} // namespace xisf
