// LinearityHeuristic.h — shared "is this image still in linear stage?" logic.
//
// IMPORTANT: This header is intentionally duplicated in
// PropertyHandler/XISFPropertyHandler/src/LinearityHeuristic.h so the two
// independent COM DLLs make the SAME determination from the SAME signals
// (pixel median + image element attributes). Keep them in sync.
//
// Decision rule (in priority order):
//
//   1. If the caller has computed a pixel-data median (subsampled across the
//      largest non-thumbnail image), use it as the authoritative signal.
//
//        median <  kStretchedMedianThreshold  →  Linear
//        median >= kStretchedMedianThreshold  →  Non-Linear
//
//      Rationale: linear astrophotography data (raw subexposures, calibrated
//      lights, integrated stacks before stretching) has the vast majority of
//      pixels concentrated near zero — the bias pedestal plus faint sky
//      background sits well under 1 % of full scale, with stars and bright
//      nebular regions occupying a small minority of pixels. After applying
//      a stretching transform (HistogramTransformation, MaskedStretch,
//      ArcsinhStretch, GeneralizedHyperbolicStretch, etc.) the histogram is
//      pulled out into the [0,1] range and the median lands well above this
//      threshold.
//
//      Empirically measured on real files (see docs/features/computed-properties.md):
//        Linear single subexposures (300s broadband, 14 s broadband):  median ~0.00 - 0.01
//        Linear integrated stacks (PixInsight ImageIntegration output): median ~0.00
//        Permanently stretched PixInsight outputs (6 distinct targets): median 0.12 - 0.32
//      Threshold of 0.05 sits in the wide gap between these two populations.
//
//   2. If pixel statistics are not available, fall back to the legacy metadata
//      heuristic based on sampleFormat and colorSpace:
//
//        Float32/Float64 + RGB/Gray  →  Linear
//        UInt8                       →  Non-Linear   (display-ready integer)
//        *SRGB colorSpace            →  Non-Linear   (explicit sRGB tag)
//        anything else (UInt16)      →  Non-Linear   (camera-native integer)
//
//      This fallback is necessarily lossy: PixInsight does not change
//      sampleFormat or colorSpace when permanently stretching, so a stretched
//      Float32/RGB file is indistinguishable from a linear one without
//      looking at pixel values.
//
// Edge cases the median heuristic intentionally rolls into "Non-Linear":
//   - Extremely bright single subs (e.g. moon, planet) where the linear sensor
//     output really does fill the [0,1] range. From a preview-rendering
//     perspective this is correct: applying linear→sRGB gamma on top would
//     wash the image out.
//   - Display-ready JPEG/PNG-derived data ingested into XISF.
//
#pragma once

#include <string>
#include <string_view>

namespace xisf {

// Median-based threshold. See the long comment above for empirical grounding.
inline constexpr double kStretchedMedianThreshold = 0.05;

// Return true if the image data should be treated as still-linear (i.e. would
// benefit from a linear→sRGB gamma correction when displayed).
//
// Parameters:
//   hasPixelMedian  true if pixelMedian is meaningful. When true, drives the
//                   decision exclusively.
//   pixelMedian     median of subsampled pixel values, normalized to [0,1].
//   sampleFormat    XISF Image element sampleFormat attribute
//                   ("UInt8" | "UInt16" | "Float32" | "Float64" | "").
//   colorSpace      XISF Image element colorSpace attribute
//                   ("Gray" | "RGB" | "GraySRGB" | "RGBSRGB" | "").
inline bool DetermineIsLinear(bool hasPixelMedian,
                              double pixelMedian,
                              std::string_view sampleFormat,
                              std::string_view colorSpace)
{
    if (hasPixelMedian) {
        return pixelMedian < kStretchedMedianThreshold;
    }

    // Metadata fallback (legacy heuristic).
    const bool isFloat32 = (sampleFormat == "Float32");
    const bool isFloat64 = (sampleFormat == "Float64");
    const bool isUInt8   = (sampleFormat == "UInt8");

    bool isLinear = (isFloat32 || isFloat64);
    if (colorSpace == "Gray" || colorSpace == "RGB") isLinear = true;
    if (colorSpace == "GraySRGB" || colorSpace == "RGBSRGB") isLinear = false;
    if (isUInt8) isLinear = false;
    return isLinear;
}

} // namespace xisf
