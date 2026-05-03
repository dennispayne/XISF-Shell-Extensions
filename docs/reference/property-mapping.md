# Property Mapping Reference

Complete mapping of XISF metadata to Windows properties, including extraction pipelines, type conversions, and NULL handling.

## Overview

The XISF Shell Extensions implement a **two-phase property mapping**:

**Phase 1: Metadata Extraction** — Parse XISF binary header to extract XML, FITS keywords, XISF Properties
**Phase 2: Property Mapping** — Convert extracted metadata to Windows property types (VT_LPWSTR, VT_R8, etc.)

---

## Extraction Pipeline

### Step 1: Binary Header Reading

```cpp
// PropertyHandler/XISFPropertyHandler/src/PropertyStore.cpp:61-73
static HRESULT ReadAll(IStream* pStream, void* pv, ULONG cbToRead, ULONG* pcbRead) {
    BYTE* dest = static_cast<BYTE*>(pv);
    ULONG totalRead = 0;
    while (totalRead < cbToRead) {
        ULONG cbChunk = 0;
        HRESULT hr = pStream->Read(dest + totalRead, cbToRead - totalRead, &cbChunk);
        if (FAILED(hr)) return hr;
        if (cbChunk == 0) break;
        totalRead += cbChunk;
    }
    if (pcbRead) *pcbRead = totalRead;
    return S_OK;
}
```

**Flow**:
1. Read 8-byte XISF preamble: `"XISF0100"`
2. Read 8-byte header size (little-endian uint64)
3. Allocate XML buffer (max 64 MB per kMaxHeaderBytes)
4. Read XML block
5. Decompress if needed (reserved, currently unused)

### Step 2: XML Metadata Parsing

```cpp
// XISFParser::ParseFile() → ExtractMetadataFromXML()
1. Find all <FITSKeyword> elements
   • Extract name, value, comment attributes
   • Store case-insensitive for lookup

2. Find all <Property> elements
   • Extract id (e.g., "Instrument:ExposureTime"), type, value
   • Build id → (type, value) map

3. Find Image elements
   • Extract geometry (W:H:C), sampleFormat, colorSpace
   • Store image attributes

4. Build indices for O(1) lookup
   • fitsIndex_: uppercase FITS name → FITSKeyword[]
   • propIndex_: XISF property id → XISFProperty[]
```

**Safety Limits**:
- XML header max 64 MB (safety guard against malformed files)
- Property value max 1 MB per value
- Image count typically 1-3 (full + thumbnail)

### Step 3: Property Store Population

```cpp
// PropertyStore::GetValue(key, propvar)
if (IsBasicProperty(key)) {
    return GetBasicProperty(key, propvar);  // XISF property or FITS keyword
} else if (IsComputedProperty(key)) {
    return GetComputedProperty(key, propvar);  // Constellation, RA hour, etc.
} else if (IsPixelStatProperty(key)) {
    return GetPixelStatProperty(key, propvar);  // Median, mean, clipping
} else {
    PropVariantInit(propvar);
    return S_FALSE;  // Property not available
}
```

---

## Property-by-Property Mapping

### Basic Properties (Direct Extraction)

#### ExposureTime (propID 2)

```
┌─ XISF Property: Instrument:ExposureTime (type: Float64)
│  └─ Convert to Double (VT_R8)
└─ Fallback: FITS keyword EXPTIME
   └─ Parse string → double via strtod()
```

**Example**:
```xml
<Property id="Instrument:ExposureTime" type="Float64">60.0</Property>
<!-- or -->
EXPTIME = 60.0 / Exposure time (seconds)
```
Result: `PROPVARIANT { vt: VT_R8, dblVal: 60.0 }`

#### CameraModel (propID 3)

```
┌─ XISF Property: Instrument:Camera:Model (type: String)
│  └─ UTF-8 to UTF-16 via MultiByteToWideChar()
└─ Fallback: FITS keyword INSTRUME
   └─ Direct string (already ASCII)
```

**Example**:
```xml
<Property id="Instrument:Camera:Model" type="String">ZWO ASI6200MM Pro</Property>
```
Result: `PROPVARIANT { vt: VT_LPWSTR, pwszVal: L"ZWO ASI6200MM Pro" }`

#### DateObserved (propID 14)

```
┌─ XISF Property: Observation:Time:Start (type: TimePoint)
│  └─ Parse ISO-8601: "2024-03-15T22:30:00"
│  └─ Convert to SYSTEMTIME, then FILETIME
└─ Fallback: FITS keyword DATE-OBS
   └─ Parse "YYYY-MM-DDTHH:MM:SS" or "YYYY-MM-DD HH:MM:SS"
```

**Example**:
```xml
<Property id="Observation:Time:Start" type="TimePoint">
    2024-03-15T22:30:00
</Property>
```
Result: `PROPVARIANT { vt: VT_FILETIME, filetime: {...} }`

---

### Computed Properties (Derived)

#### Constellation (propID 44)

**Derivation**:
```
1. Extract RA and Dec properties (propID 16, 17)
2. Load constellation database from disk (one-time, thread-safe via std::call_once)
3. Perform point-in-polygon lookup
4. Return IAU 3-letter code or full name
```

**Algorithm** (ConstellationDB.cpp):
```cpp
std::string findConstellation(double raDeg, double decDeg) {
    // Load constellation boundary polygons
    // Use exact RA/Dec coordinate to find containing polygon
    // Return constellation abbreviation (e.g., "Ori" for Orion)
}
```

**Performance**: First call 50-100ms (DB load), subsequent <1ms (cached)

**Example**:
- RA: 83.633°, Dec: -5.391° → **Orion**
- RA: 0°, Dec: +0° → **Cetus**

#### MatchedObjects (propID 45)

**Derivation**:
```
1. Extract RA, Dec, FocalLength, PixelSize (for FOV)
2. Query DSO catalogs (NGC, IC, Sharpless, etc.) via cone search
3. Perform fuzzy name matching on ObjectName
4. Return comma-separated list: "M42, NGC 1976, Orion Nebula"
```

**Algorithm** (DSOCatalog.cpp):
```cpp
std::vector<CatalogEntry> coneSearck(double raDeg, double decDeg, 
                                     double radiusDeg) {
    // Load catalogs from LocalAppData\XISFShellExtension\catalogs\
    // Compute angular distance to each catalog entry
    // Return entries within radius
    // Sort by priority (M > C > NGC > IC > Sh2 > B > LBN)
}
```

**Cone Search Radius**: Default 0.5°, configurable via registry:
```reg
HKEY_CURRENT_USER\Software\XISFPropertyHandler
  MatchToleranceDeg = "0.5"
```

**Catalog Loading** (if enabled):
```
LocalAppData\XISFShellExtension\catalogs\
  ├─ NGC.csv       (Messier, NGC, IC objects)
  ├─ addendum.csv  (recent discoveries)
  └─ sharpless.csv (emission nebulae)
```

**Performance**: First call 500-2000ms (catalog load), subsequent 10-50ms (cached)

**Example**:
- RA: 83.633°, Dec: -5.391° → **M42, NGC 1976, Orion Nebula**

#### RAHour (propID 42)

**Derivation**:
```
RAHour = floor(RA / 15.0) → formatted as "Nh" where N ∈ [0..23]

RA → Hour:
  0° → 0h
  15° → 1h
  90° → 6h
  180° → 12h
  270° → 18h
  359° → 23h
```

**Code** (ComputedProperties.cpp:30-35):
```cpp
static std::string ComputeRAHour(double raDegrees) {
    int hour = static_cast<int>(raDegrees / 15.0);
    if (hour < 0) hour = 0;
    if (hour > 23) hour = 23;
    char buf[16];
    snprintf(buf, sizeof(buf), "%dh", hour);
    return buf;
}
```

#### DecBand (propID 43)

**Derivation**:
```
DecBand = floor(Dec / 15.0) × 15° → formatted as "+Nd° to +(N+15)d°"

Dec → Band:
  -90° to -75° → "-90° to -75°"
  -75° to -60° → "-75° to -60°"
  ...
  +75° to +90° → "+75° to +90°"
```

**Code** (ComputedProperties.cpp:38-48):
```cpp
static std::string ComputeDecBand(double decDegrees) {
    int band = static_cast<int>(std::floor(decDegrees / 15.0)) * 15;
    if (band < -90) band = -90;
    if (band > 75) band = 75;
    int upper = band + 15;
    if (upper > 90) upper = 90;
    char buf[64];
    snprintf(buf, sizeof(buf), "%+d° to %+d°", band, upper);
    return buf;
}
```

#### ObjectRA / ObjectDec (propID 29-30)

**Derivation**:
```
ObjectRA:  degrees → Hour-Minute-Second (HMS) format
ObjectDec: degrees → Degree-Minute-Second (DMS) format

RA (decimal) → H:M:S:
  RA_hours = RA / 15.0
  H = floor(RA_hours)
  M = floor((RA_hours - H) × 60)
  S = ((RA_hours - H) × 60 - M) × 60

Dec (decimal) → D:M:S:
  D = floor(|Dec|)
  M = floor((|Dec| - D) × 60)
  S = ((|Dec| - D) × 60 - M) × 60
```

**Example**:
- RA: 83.633° → **05h34m32.0s**
- Dec: -5.391° → **-05°23m24.0"**

---

### Pixel Statistics (On-Demand Extraction)

#### Median / Mean / ClippingLow / ClippingHigh (propID 56-59)

**Extraction** (PixelStatistics.cpp:24-250):
```
1. Locate largest non-thumbnail image in XISF
2. Parse geometry (width, height, channels)
3. Parse location and sample format (UInt16, Float32, etc.)
4. Seek to image data in stream
5. Subsample ~1M pixels (adaptive based on image size)
6. Compute histogram:
   - Normalize pixel values to [0, 1]
   - Quantize to 256 bins
7. Calculate statistics:
   - Median: value at 50th percentile bin
   - Mean: average of all samples
   - ClippingLow: percentage of pixels ≤ min threshold (e.g., ≤ 0.01)
   - ClippingHigh: percentage of pixels ≥ max threshold (e.g., ≥ 0.99)
```

**Performance**:
- Image 4096×2748 (11MP): 800ms subsampling + 200ms histogram
- Image 1920×1080 (2MP): 300ms
- Results cached in metadata for future access

**Subsampling Algorithm**:
```cpp
// Target ~1M pixels
uint32_t targetSamples = 1000000;
uint32_t totalPixels = width * height * channels;
uint32_t step = std::max(1u, totalPixels / targetSamples);

// Stride through image
for (uint64_t offset = 0; offset < imageSize; offset += step) {
    ReadPixel(stream, offset, sampleFormat);
    histogram[quantized_value]++;
}
```

---

## Type Conversion Rules

### String → Windows Property

```cpp
// PropertyStore.cpp:105-115
std::wstring CXISFPropertyHandler::Utf8ToWide(const std::string& raw) {
    if (raw.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, raw.c_str(), -1, nullptr, 0);
    if (len <= 0) return {};
    std::wstring result(static_cast<size_t>(len - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, raw.c_str(), -1, result.data(), len);
    return result;
}

PROPVARIANT pv{};
pv.vt = VT_LPWSTR;
pv.pwszVal = CoTaskMemAlloc((result.size() + 1) * sizeof(wchar_t));
wcscpy_s(pv.pwszVal, result.size() + 1, result.c_str());
return pv;
```

### Double → Windows Property

```cpp
PROPVARIANT pv{};
pv.vt = VT_R8;
pv.dblVal = parsed_double;
return pv;
```

### UInt32 → Windows Property

```cpp
PROPVARIANT pv{};
pv.vt = VT_UI4;
pv.uintVal = parsed_uint32;
return pv;
```

### DateTime String → FILETIME

```cpp
// PropertyStore.cpp:200-240
SYSTEMTIME st{};
if (!ISOStringToSystemTime("2024-03-15T22:30:00", &st)) {
    return S_FALSE;  // Parse failed
}

FILETIME ft{};
if (!SystemTimeToFileTime(&st, &ft)) {
    return S_FALSE;
}

PROPVARIANT pv{};
pv.vt = VT_FILETIME;
pv.filetime = ft;
return pv;
```

---

## NULL / Missing Value Handling

### Three-Layer Fallback

```cpp
// PropertyStore::GetValue(key, propvar)

1. Try XISF Property by id
   if (!property.value.empty()) return property;

2. Fallback to FITS Keyword by name
   if (!fitsKeyword.value.empty()) return keyword;

3. Return empty (property not present)
   PropVariantInit(propvar);
   pv.vt = VT_EMPTY;
   return S_FALSE;  // S_FALSE = property unavailable (not an error)
```

### Search Indexing Impact

Windows Search **automatically excludes** VT_EMPTY properties from full-text indices:

```
File: M42.xisf
  Properties indexed: RA, Dec, ExposureTime, ObjectName, ...
  Properties skipped: (empty) Airmass, (empty) CloudCover, ...
```

This prevents spurious matches on files with sparse metadata.

---

## Registry Persistence

### Metadata Caching

Computed values optionally cached in file stream as XISF extended properties:

```xml
<!-- Cached pixel statistics in XISF metadata -->
<Property id="Observation:Pixel:Median" type="Float64">0.45</Property>
<Property id="Observation:Pixel:Mean" type="Float64">0.42</Property>
<Property id="Observation:Pixel:ClippingLow" type="Float64">2.5</Property>
<Property id="Observation:Pixel:ClippingHigh" type="Float64">1.2</Property>
```

If present, handler uses cached values instead of recomputing.

### Handler Settings

```reg
HKEY_CURRENT_USER\Software\DennisPayne\XISF Shell Extension
  PropertyEnabled = 1          (DWORD: enable/disable property handler)
  PreviewEnabled = 1           (DWORD: enable/disable preview handler)
  CatalogPriority = "M,C,..."  (REG_SZ: DSO match priority order)
  MatchToleranceDeg = "0.5"    (REG_SZ: cone search radius)
```

---

## Error Handling & Logging

### Telemetry Events

```cpp
// PropertyStore.cpp:330
WritePropertyHandlerTelemetry(TRACE_LEVEL_INFORMATION, XISF_ETW_KEYWORD_PERF,
    L"PropertyRetrieved Key=%s Timing=%lldms Source=%s",
    propertyName, elapsed, source);
```

Logged to ETW provider `XISF-PropertyHandler` with keywords:
- `XISF_ETW_KEYWORD_PERF` — timing, statistics
- `XISF_ETW_KEYWORD_CATALOG` — DSO matching
- `XISF_ETW_KEYWORD_PARSE` — XML parsing

### Graceful Degradation

If a conversion fails, property is **skipped**:

```cpp
// Example: unparseable FITS keyword
if (FAILED(ParseDouble(fitsValue, &out))) {
    WritePropertyHandlerTelemetry(TRACE_LEVEL_WARNING, XISF_ETW_KEYWORD_PARSE,
        L"PropertySkipped Key=%s Reason=ParseFailed Value=%s",
        key, fitsValue.c_str());
    return S_FALSE;
}
```

---

## Related Documentation

- [Property Metadata](../user-guide/property-metadata.md) — All 65 properties reference
- [Handlers Technical Reference](handlers-technical.md) — COM interfaces, registration
- [Pixel Statistics](../features/pixel-statistics.md) — Subsampling algorithm details
- [Computed Properties](../features/computed-properties.md) — Derivation details
- [Feature Tiers](../features/feature-tiers.md) — Tier-gated availability

---

**Updated**: 2024 | **Handler Version**: See version.json
