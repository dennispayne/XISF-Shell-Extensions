// ComputedProperties.h — Computed/derived properties for XISF files.
// Extracted from PropertyStore.cpp to isolate computed operations that go
// beyond simple metadata extraction from the XML header.
#pragma once

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
};

// A single computed property value ready to be added to the property store.
struct ComputedPropertyEntry {
    PROPERTYKEY key;
    enum class Type { String, Double, UInt32, StringList } type;
    std::string stringValue;
    double doubleValue = 0.0;
    uint32_t uint32Value = 0;
    std::vector<std::string> stringListValue;
};

// Compute all tier-gated derived properties. Returns a flat list of entries
// to be added to the property store. This function is pure — no COM state,
// no IStream, no side effects beyond catalog loading (which is thread-safe
// via call_once internally).
std::vector<ComputedPropertyEntry> PopulateComputedProperties(
    const ComputedPropertyInputs& inputs);

} // namespace xisf
