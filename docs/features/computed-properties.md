# Computed Properties

Advanced property calculations and derivations for XISF files. This document covers constellation resolution, RA/Dec band computation, and DSO catalog matching.

## Overview

**Computed Properties** are derived from XISF/FITS metadata and external databases. Unlike basic properties extracted directly from file headers, computed properties require algorithms and lookups:

| Property | Type | Source | Tier | Example |
|----------|------|--------|------|---------|
| **RAHour** (42) | String | Computed from RA | Free+ | "05h" |
| **DecBand** (43) | String | Computed from Dec | Free+ | "+0° to +15°" |
| **Constellation** (44) | String | IAU boundaries | Full | "Orion" |
| **MatchedObjects** (45) | String | DSO catalogs | Full | "M42, NGC 1976" |
| **StarFWHM** (46) | Double | FITS keyword | Free+ | 2.1 arcseconds |

---

## Constellation Resolution

### Algorithm: Point-in-Polygon Lookup

```cpp
// ComputedProperties.cpp: Constellation derivation
1. Extract RA and Dec from XISF properties
2. Load IAU constellation database (ConstellationDB)
3. Iterate through 88 constellation polygons
4. Find polygon containing (RA, Dec) point
5. Return constellation name or abbreviation
```

### Data Structure

```cpp
// ConstellationDB.h
struct ConstellationBoundary {
    std::string name;        // "Orion", "Cetus", etc.
    std::string abbreviation; // "Ori", "Cet"
    std::vector<Vec2D> vertices;  // Polygon boundary vertices
};

std::vector<ConstellationBoundary> g_constellations;
```

### Lookup Method

```cpp
// ConstellationDB.cpp:50-150
bool isPointInPolygon(double ra, double dec, const ConstellationBoundary& poly) {
    // Ray casting algorithm: Cast ray from point to infinity
    // Count boundary crossings
    // If even: outside, if odd: inside
    
    const double RAY_END = 360.0;  // Ray extends to RA 360°
    int crossings = 0;
    
    for (size_t i = 0; i < poly.vertices.size(); i++) {
        const Vec2D& p1 = poly.vertices[i];
        const Vec2D& p2 = poly.vertices[(i + 1) % poly.vertices.size()];
        
        if (SegmentCrossesRay(p1, p2, ra, dec, RAY_END)) {
            crossings++;
        }
    }
    
    return (crossings % 2) == 1;
}
```

### IAU Constellation Boundaries

Source: NASA/SIMBAD IAU constellation definitions (all 88 constellations)

```
Sample boundaries (in decimal degrees):
  Orion: RA [4.7°, 6.3°], Dec [-11°, +23°]
  Cetus: RA [0°, 3°], Dec [-25°, +10°]
  Ursa Major: RA [8°, 14.5°], Dec [+28°, +73°]
  Scorpius: RA [15.6°, 17.8°], Dec [-44°, -19°]
```

### Performance

- **First call**: 50-100ms (load boundaries)
- **Subsequent calls**: <1ms (cached in static)
- **Memory**: ~500 KB (88 polygons + vertices)

### Example

```cpp
// Input: RA=83.633°, Dec=-5.391° (M42 location)
std::string constellation = FindConstellation(83.633, -5.391);
// Output: "Orion"
```

---

## RA/Dec Band Computation

### RAHour: Right Ascension Hourly Zones

**Purpose**: Divide sky into 24 hourly zones for coarse sky searches

**Algorithm**:
```cpp
// ComputedProperties.cpp:30-36
RAHour = floor(RA / 15.0)

// Map:
RA 0°–15° → "0h"
RA 15°–30° → "1h"
RA 30°–45° → "2h"
...
RA 345°–360° → "23h"
```

**Example**:
```
Input: RA = 83.633°
83.633 / 15.0 = 5.575
floor(5.575) = 5
Output: "5h"
```

### DecBand: Declination Bands (15° wide)

**Purpose**: Divide sky into 6 declination bands (±90° to ±75°, ±60°, ...)

**Algorithm**:
```cpp
// ComputedProperties.cpp:38-48
DecBand = floor(Dec / 15.0) × 15°

// Map:
Dec -90° to -75° → "-90° to -75°"
Dec -75° to -60° → "-75° to -60°"
...
Dec +75° to +90° → "+75° to +90°"
```

**Special handling**:
```cpp
int band = static_cast<int>(std::floor(decDegrees / 15.0)) * 15;
if (band < -90) band = -90;    // South pole boundary
if (band > 75) band = 75;      // Near north pole
int upper = band + 15;
if (upper > 90) upper = 90;    // Clamp north pole
```

**Example**:
```
Input: Dec = -5.391°
-5.391 / 15.0 = -0.359
floor(-0.359) = -1
band = -1 * 15 = -15°
upper = -15 + 15 = 0°
Output: "-15° to 0°"
```

---

## DSO Catalog Matching (Full Tier Only)

### Cone Search Algorithm

**Purpose**: Find Deep-Sky Objects (galaxies, nebulae, clusters) near observation coordinates

**Requirements**:
- RA/Dec from file
- DSO catalogs (NGC.csv, IC.csv, Sharpless, etc.)
- Optional: Focal length + pixel size for Field-of-View calculation

### Step 1: Calculate Search Radius

```cpp
// DSOCatalog.cpp: Cone search radius
// Default: 0.5° (configurable via registry)

// Optional: Use field of view
if (focalLength && pixelSize && imageWidth && imageHeight) {
    double plateScaleX = (206.265 * pixelSize) / focalLength;  // arcsec/pixel
    double plateScaleY = (206.265 * pixelSize) / focalLength;
    
    double fovX = (imageWidth * plateScaleX) / 3600.0;   // degrees
    double fovY = (imageHeight * plateScaleY) / 3600.0;
    
    double fovDiag = std::sqrt(fovX * fovX + fovY * fovY);
    searchRadius = fovDiag / 2.0;  // Half diagonal → covers all edges
}
```

### Step 2: Load Catalogs

```cpp
// ComputedProperties.cpp:99-150
static void EnsureCatalogLoaded() {
    std::call_once(s_catalogOnceFlag, []() {
        auto cat = std::make_shared<DSOCatalog>();
        
        // Load in priority order
        cat->LoadFromCSVFile("NGC.csv");      // Messier, NGC, IC
        cat->AppendFromCSVFile("addendum.csv");     // Recent additions
        cat->AppendFromCSVFile("sharpless.csv");    // Emission nebulae
        
        s_dsoCatalog = cat;
    });
}
```

**Catalog Format** (CSV):
```
id,name,ra,dec,mag,type,size
M1,Crab Nebula,5.575,-27.135,8.4,SNR,6.0
M42,Orion Nebula,83.633,-5.391,4.0,EN,66.0
NGC1976,Orion Nebula,83.633,-5.391,4.0,EN,66.0
IC434,Horsehead Nebula,85.376,-2.277,13.5,EN,1.0
```

### Step 3: Angular Distance Calculation

```cpp
// DSOCatalog.cpp: Great-circle distance
double angularDistance(double ra1, double dec1, 
                       double ra2, double dec2) {
    // Convert to radians
    double raRad1 = ra1 * M_PI / 180.0;
    double decRad1 = dec1 * M_PI / 180.0;
    double raRad2 = ra2 * M_PI / 180.0;
    double decRad2 = dec2 * M_PI / 180.0;
    
    // Haversine formula
    double dRa = raRad2 - raRad1;
    double dDec = decRad2 - decRad1;
    double a = std::sin(dDec / 2) * std::sin(dDec / 2) +
               std::cos(decRad1) * std::cos(decRad2) *
               std::sin(dRa / 2) * std::sin(dRa / 2);
    double c = 2 * std::atan2(std::sqrt(a), std::sqrt(1 - a));
    
    return c * 180.0 / M_PI;  // Result in degrees
}
```

### Step 4: Filter by Radius & Priority

```cpp
// ComputedProperties.cpp:200-250
std::vector<CatalogEntry> matches;

for (const auto& entry : s_dsoCatalog->entries()) {
    double dist = angularDistance(raDeg, decDeg, 
                                  entry.ra, entry.dec);
    if (dist <= searchRadius) {
        matches.push_back(entry);
    }
}

// Sort by priority (Messier > Caldwell > NGC > IC > ...)
std::stable_sort(matches.begin(), matches.end(), 
    [](const auto& a, const auto& b) {
        return GetPriority(a.catalog) > GetPriority(b.catalog);
    });

// Limit results (e.g., top 5 matches)
if (matches.size() > 5) {
    matches.resize(5);
}

// Format as comma-separated list
std::string result = JoinNames(matches);  // "M42, NGC 1976, Orion Nebula"
```

### Step 5: Fuzzy Name Matching

Optional: Match ObjectName against catalog entries:

```cpp
// DSOCatalog.cpp: Fuzzy matching
bool isSameName(const std::string& input, const std::string& catalogName) {
    // Remove common prefixes/spaces
    std::string clean1 = Normalize(input);     // "m 42" → "m42"
    std::string clean2 = Normalize(catalogName); // "M42" → "m42"
    
    if (clean1 == clean2) return true;
    
    // Check substring match (for aliases)
    if (clean1.find(clean2) != std::string::npos) return true;
    if (clean2.find(clean1) != std::string::npos) return true;
    
    return false;
}
```

### Registry Configuration

```reg
HKEY_CURRENT_USER\Software\XISFPropertyHandler
  CatalogPriority = "M,C,NGC,IC,Sh2,B,LBN"  (comma-separated priorities)
  MatchToleranceDeg = "0.5"                  (search radius in degrees)
```

**Default Priority Order**:
- `M` — Messier (brightest/most famous)
- `C` — Caldwell
- `NGC` — New General Catalog
- `IC` — Index Catalog
- `Sh2` — Sharpless (emission nebulae)
- `B` — Barnard (dark nebulae)
- `LBN` — Lynds' Bright Nebula

### Performance

| Stage | Time | Notes |
|-------|------|-------|
| Load NGC.csv | 500-800ms | First call only |
| Angular distance × N entries | 50-200ms | Depends on catalog size |
| Sort + filter | 10-50ms | Usually <100 matches |
| **Total** | **600-1000ms** | Cached after first call |

### Example

```cpp
// Input:
double ra = 83.633, dec = -5.391;      // M42 coordinates
double focalLength = 2100.0;            // mm
double pixelSize = 3.8;                 // microns
uint32_t width = 4096, height = 2748;

// Output:
// Cone search radius: ~0.9° (computed from FOV)
// Matches within radius:
//   M42 (distance: 0°)
//   NGC 1976 (distance: 0°, same object as M42)
//   Orion Nebula (distance: 0°)
// MatchedObjects = "M42, NGC 1976, Orion Nebula"
```

---

## Feature Tier Dependencies

| Property | Free | Standard | Full | Requires |
|----------|------|----------|------|----------|
| RAHour | ✓ | ✓ | ✓ | RA property |
| DecBand | ✓ | ✓ | ✓ | Dec property |
| Constellation | ✗ | ✗ | ✓ | Constellation DB + RA/Dec |
| MatchedObjects | ✗ | ✗ | ✓ | DSO catalogs + RA/Dec |
| StarFWHM | ✓ | ✓ | ✓ | FITS keyword or XISF property |

See [Feature Tiers](feature-tiers.md) for details.

---

## Error Handling & Fallbacks

### Missing RA/Dec

```cpp
// ComputedProperties.cpp:60-70
if (!inputs.hasRA || !inputs.hasDec) {
    // Skip all derived properties
    WritePropertyHandlerTelemetry(TRACE_LEVEL_INFORMATION, XISF_ETW_KEYWORD_PARSE,
        L"ComputedPropertiesSkipped Reason=MissingCoordinates");
    return empty_result;
}
```

### Constellation Lookup Fails

```cpp
// ConstellationDB.cpp:180-195
std::string constellation = FindConstellation(ra, dec);
if (constellation.empty()) {
    // Return "Unknown" or leave empty
    return "";
}
```

### Catalog Load Fails

```cpp
// ComputedProperties.cpp:140-150
if (!LoadCatalogs()) {
    WritePropertyHandlerTelemetry(TRACE_LEVEL_WARNING, XISF_ETW_KEYWORD_CATALOG,
        L"CatalogLoadFailed Path=%s", catalogPath);
    // Continue without catalog matching (MatchedObjects will be empty)
}
```

---

## Caching Strategy

### Computed Properties Cache

```cpp
// ComputedProperties.cpp: Thread-safe singleton
static std::shared_ptr<DSOCatalog> s_dsoCatalog;
static auto s_catalogOnceFlag = std::make_shared<std::once_flag>();

std::call_once(*s_catalogOnceFlag, []() {
    // Load catalogs once per process
    s_dsoCatalog = LoadDSOCatalogs();
});
```

### Per-File Caching

```cpp
// PropertyStore.h: Cache in property handler instance
std::vector<ComputedPropertyEntry> m_computedPropsCache;
bool m_computedPropsComputed = false;

HRESULT GetComputedProperty(...) {
    if (!m_computedPropsComputed) {
        m_computedPropsCache = PopulateComputedProperties(inputs);
        m_computedPropsComputed = true;
    }
    // Return from cache
}
```

---

## Related Documentation

- [Pixel Statistics](pixel-statistics.md) — Median, mean, clipping stats
- [Feature Tiers](feature-tiers.md) — Tier availability matrix
- [Property Metadata](../user-guide/property-metadata.md) — All 65 properties
- [Property Mapping](../reference/property-mapping.md) — Extraction details

---

**Updated**: 2024 | **Handler Version**: See version.json | **Source**: ComputedProperties.cpp, ConstellationDB.cpp, DSOCatalog.cpp
