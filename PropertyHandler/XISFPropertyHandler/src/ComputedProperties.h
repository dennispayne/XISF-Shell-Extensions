// ComputedProperties.h — Computed/derived properties for XISF files.
// Extracted from PropertyStore.cpp to isolate computed operations that go
// beyond simple metadata extraction from the XML header.
#pragma once

#include <functional>
#include <string>
#include <vector>
#include <cstdint>
#include <wtypes.h>
#include <propkeydef.h>
#include "XISFParser.h"
#include "HandlerSettings.h"

namespace xisf {

// Parameters needed to populate computed properties.
struct ComputedPropertyInputs {
    const XISFRawMetadata& metadata;
    FeatureTier tier;
    bool projectionEnabled;

    // Parsed coordinate values from basic property population
    double raDeg;
    double decDeg;
    bool hasRA;
    bool hasDec;
    double objRaDeg;
    double objDecDeg;
    bool hasObjRA;
    bool hasObjDec;

    // Sensor geometry for FOV-based cone search
    double focalLength;
    bool hasFocalLength;
    std::string pixelSizeRaw;

    // Values used for keywords and projection
    double exposureTime;
    bool hasExposure;
    std::string cameraModel;
    double fNumber;
    bool hasFNumber;
    std::string filterName;
    std::string imageType;
    std::string objectName;

    // Pixel-data signal for the Linear / Non-Linear DataState heuristic.
    // Populated by the property store after ComputePixelStats has run when
    // the file is at Full feature tier with a readable attached image.
    // When unavailable (Standard tier or below, no attachment) the metadata
    // fallback in xisf::DetermineIsLinear is used. See LinearityHeuristic.h.
    bool hasPixelMedian = false;
    double pixelMedian = 0.0;
    double pixelP95 = 0.0;
};

// Sink interface for emitting computed property values directly to a property
// store, removing the intermediate vector-of-variants previously used.
struct ComputedPropertySink {
    std::function<void(const PROPERTYKEY&, const std::string&)>              addString;
    std::function<void(const PROPERTYKEY&, double)>                          addDouble;
    std::function<void(const PROPERTYKEY&, uint32_t)>                        addUInt32;
    std::function<void(const PROPERTYKEY&, const std::vector<std::string>&)> addStringList;
};

// Compute all tier-gated derived properties and emit them through the sink.
// Pure with respect to COM state — no IStream, no side effects beyond catalog
// loading (which is thread-safe via call_once internally).
void PopulateComputedProperties(const ComputedPropertyInputs& inputs,
                                const ComputedPropertySink& sink);

} // namespace xisf
