# Property Handler Implementation

## Technical Implementation Guide

Detailed guide to implementing property handlers for XISF files. Covers the 65+ metadata properties extracted from XISF files, feature tiers, deep-sky catalog integration, and extending with new properties.

## Property Handler Fundamentals

### What is a Property Handler?

A **property handler** is a Windows shell extension (COM DLL) that extracts metadata from file types and displays it in Explorer's Details pane. For XISF files, the property handler:

1. **Parses** the XISF binary format and XML header
2. **Extracts** FITS keywords and XISF properties
3. **Enriches** metadata using deep-sky catalogs
4. **Populates** the Windows property store
5. **Displays** in Explorer Details pane and search results

### Key Interfaces

```cpp
// COM interfaces implemented by CXISFPropertyHandler
interface IPropertyStore {
    HRESULT GetCount(DWORD* pcProps);           // Total property count
    HRESULT GetAt(DWORD iProp, PROPERTYKEY* pkey);
    HRESULT GetValue(REFPROPERTYKEY key, PROPVARIANT* pv);
    HRESULT SetValue(REFPROPERTYKEY key, REFPROPVARIANT propvar);
};

interface IInitializeWithStream {
    HRESULT Initialize(IStream* pstream, DWORD grfMode);
};
```

**Lifetime:**
1. Windows Explorer selects `.xisf` file
2. Explorer creates IPropertyStore instance
3. Explorer calls `Initialize(IStream*)` with file stream
4. Handler parses metadata in background
5. Explorer calls `GetCount()`, then `GetAt()` for each property
6. Handler destroyed when user deselects file

## Implementation Requirements

### XISF Binary Format Parser

**Responsibility:** Extract metadata from XISF binary format without external libraries.

**Algorithm (Phase 1: Binary Extraction):**

```
File: sample.xisf
├─ Bytes 0-7:     "XISF0100" (binary signature)
├─ Bytes 8-15:    headerLength (UINT64 LE)
├─ Bytes 16-...:  XML header block (headerLength bytes)
└─ Rest:          Image pixel data (not parsed by property handler)

XML Header (simplified):
<xisf xmlns="..." version="1.0">
  <Image
    geometry="1920:1080:3"
    sampleFormat="Float32"
    colorSpace="LinearRGB"
    ...
  >
    <FITSKeyword name="OBJECT" value="NGC1234" comment=""/>
    <FITSKeyword name="EXPOSURE" value="120" comment="exposure in sec"/>
    <Property id="Instrument:Camera:Name" type="String" value="ZWO ASI2600MM"/>
  </Image>
</xisf>
```

**Phase 1 Output:** `XISFRawMetadata` struct

```cpp
struct XISFRawMetadata {
    std::string xmlHeader;                              // Full XML text
    std::vector<FITSKeyword> fitsKeywords;             // FITS cards
    std::vector<XISFProperty> properties;              // XISF typed properties
    std::unordered_map<std::string, std::string> imageAttributes;
    uint32_t imageCount;
};
```

**Implementation:** `PropertyHandler/XISFPropertyHandler/src/XISFParser.{h,cpp}`

### Property Population Pipeline

```
Phase 1: Parse XISF Binary
  │
  ├─ Read signature + header size
  ├─ Extract XML block
  ├─ Parse FITSKeywords
  ├─ Parse XISF Properties
  └─ Build O(1) lookup indices
  │
  ▼
Phase 2: Basic Property Population
  │
  ├─ System.Image.Dimensions  ← from imageAttributes["geometry"]
  ├─ System.Image.ColorSpace  ← from imageAttributes["colorSpace"]
  ├─ System.DateModified      ← from FITS: DATEOBS
  ├─ System.Photo.FNumber     ← from XISF: Instrument:Telescope:FocalRatio
  └─ ... 40+ more basic properties
  │
  ▼
Phase 3: Computed Properties (Enrichment)
  │
  ├─ Load DSO Catalog (if available)
  ├─ RA/Dec → Object Name (cone search)
  ├─ RA → RA Hour Band
  ├─ Dec → Dec Latitude Band
  ├─ Compute RA/Dec Bands
  └─ Build searchable text index
  │
  ▼
Phase 4: Background Computation (Thread)
  │
  ├─ Histogram (pixel distribution)
  ├─ Pixel Statistics (mean, median, min, max, clipping)
  └─ Store in cached XISF namespace
```

## The 65 Core Properties

### Property Organization

Properties are organized by category:

```
System.* (Windows standard properties)
├─ System.Image.Dimensions          1920x1080
├─ System.Image.BitDepth            32
├─ System.Image.ColorSpace          LinearRGB
├─ System.ItemDate                  FITS DATEOBS
├─ System.DateModified              File modification time
├─ System.Photo.FNumber             f/6.5
├─ System.Photo.ExposureTime        120 sec
├─ System.Photo.ISOSpeed            400

XISF.* (XISF-specific properties)
├─ XISF.Object                      NGC 1234
├─ XISF.ObjectRA                    12:34:56.1
├─ XISF.ObjectDec                   +45:23:10
├─ XISF.SkyConstellation            Orion
├─ XISF.SkyRAHour                   12h
├─ XISF.SkyDecBand                  +45° to +60°
├─ XISF.Telescope                   Celestron C14
├─ XISF.Camera                      ZWO ASI2600MM
├─ XISF.Filter                      Luminance
├─ XISF.ImageType                   Light
├─ XISF.Observation.RA              12:34:56.10 (obs location)
├─ XISF.Observation.Dec             +45:23:10.0
├─ XISF.Observation.Alt             +75° (altitude above horizon)
├─ XISF.Observation.Az              220° (azimuth)
├─ XISF.Binning                     1x1
├─ XISF.PixelStatistics.Mean        1024.5
├─ XISF.PixelStatistics.Median      1012
├─ XISF.PixelStatistics.Min         0
├─ XISF.PixelStatistics.Max         65535
├─ XISF.PixelStatistics.ClipLow     0.002
├─ XISF.PixelStatistics.ClipHigh    0.001
└─ ... 40+ more
```

### Complete Property Listing

**Basic Properties (Always Available):**

| Property | Type | Source | Example |
|----------|------|--------|---------|
| System.Image.Dimensions | String | Image @geometry | 1920x1080x3 |
| System.Image.BitDepth | UInt32 | sampleFormat | 32 |
| System.Image.ColorSpace | String | colorSpace | LinearRGB |
| System.ItemDate | DateTime | FITS DATEOBS | 2024-01-15T22:30:00 |
| System.Size | UInt64 | File size | 15728640 |
| System.Photo.FNumber | Double | FITS FOCUSRATIO or Instrument:Telescope:FocalRatio | 6.5 |
| System.Photo.ExposureTime | Double | FITS EXPOSURE | 120 |
| System.Photo.ISOSpeed | UInt32 | FITS GAIN | 400 |
| XISF.ImageType | String | FITS IMAGETYP | Light |
| XISF.Telescope | String | Instrument:Telescope:Name | Celestron C14 |
| XISF.TelescopeFocalLength | Double | Instrument:Telescope:FocalLength | 3910 |
| XISF.Camera | String | Instrument:Camera:Name | ZWO ASI2600MM |
| XISF.CameraPixelSize | Double | Instrument:Camera:PixelSize | 0.53 |
| XISF.Filter | String | Instrument:Filter:Name | Luminance |
| XISF.Binning | String | Computed from dimensions + sensor size | 1x1 |
| XISF.ObservationDate | String | FITS DATEOBS | 2024-01-15 |
| XISF.ObservationTime | String | FITS DATEOBS | 22:30:00 UTC |

**Enriched Properties (Catalog-dependent):**

| Property | Type | Source | Example | Tier |
|----------|------|--------|---------|------|
| XISF.Object | String | FITS OBJECT + catalog lookup | NGC 1234 | Basic |
| XISF.ObjectRA | String | FITS OBJCTRA or Observation:RA | 12:34:56.1 | Basic |
| XISF.ObjectDec | String | FITS OBJCTDEC or Observation:Dec | +45:23:10 | Basic |
| XISF.SkyConstellation | String | Dec → constellation database | Orion | Enriched |
| XISF.SkyRAHour | String | RA → hour band | 12h | Enriched |
| XISF.SkyDecBand | String | Dec → latitude band | +45° to +60° | Enriched |
| XISF.ObjectCatalogType | String | NGC/IC/Sharpless prefix | NGC | Enriched |
| XISF.ObjectMagnitude | Double | From catalog | 8.2 | Enriched |

**Pixel Statistics (Computed):**

| Property | Type | Computation | Example |
|----------|------|-------------|---------|
| XISF.PixelStatistics.Mean | Double | Sum(pixels) / count | 1024.5 |
| XISF.PixelStatistics.Median | Double | Histogram: 50th percentile | 1012 |
| XISF.PixelStatistics.Min | Double | Min(all pixels) | 0 |
| XISF.PixelStatistics.Max | Double | Max(all pixels) | 65535 |
| XISF.PixelStatistics.ClipLow | Double | Pixels at 0 / total | 0.002 (0.2%) |
| XISF.PixelStatistics.ClipHigh | Double | Pixels at max / total | 0.001 (0.1%) |

### Property Definitions

Property definitions are registered in the Windows registry via `.propdesc` XML file:

**File:** `PropertyHandler/XISFPropertyHandler/propdesc/xisf.propdesc`

```xml
<?xml version="1.0" encoding="utf-8"?>
<propertyDescriptionList>
  <propertyDescription
    name="XISF.Object"
    formatID="{F6A7B8C9-D0E1-2345-F123-456789012345}"
    propID="2"
    type="String"
    isInnate="false"
    isQueryable="true"
    isColumn="true"
    labelName="Object Name"
    invitationText="Deep-sky object designation"
    hideLabel="false"
  />
  <!-- ... 60+ more properties ... -->
</propertyDescriptionList>
```

**Property Attributes:**
- `formatID` — Unique GUID for this property namespace
- `propID` — Index within namespace (1–65)
- `type` — Data type (String, Int32, UInt32, Double, DateTime, Boolean)
- `isQueryable` — Searchable via Windows Search
- `isColumn` — Visible as Details pane column

## Feature Tiers

### Tier System

Properties are grouped by feature tier based on data availability:

#### Tier 1: Basic (Always Available)

**No dependencies.** Extracted directly from XISF file metadata.

- Image dimensions, bit depth, color space
- File properties (size, date, owner)
- FITS header values (as-is)
- XISF Property elements (as-is)
- Telescope/camera names (from headers)

**Availability:** 100% (no catalog required)

#### Tier 2: Enriched (Catalog-dependent)

**Requires:** Installed DSO catalog files

- Object names from catalog (NGC, IC, Sharpless)
- Constellation names (computed from RA/Dec)
- Sky coordinate bands (RA hour, Dec latitude)
- Magnitude and object type from catalog
- Searchable text index built from catalog data

**Implementation:** `ComputedProperties::PopulateComputedProperties()`

```cpp
// Pseudo-code for catalog enrichment
if (catalogLoaded && hasRA && hasDec) {
    auto result = catalog.ConeSearch(raRad, decRad, toleranceDeg);
    if (result.found) {
        AddObjectNameProperty(result.name);
        AddConstellationProperty(ConstellationDB::LookupConstellation(decDeg));
        AddRAHourProperty(raRad);
        AddDecBandProperty(decDeg);
    }
}
```

**Availability:** Only if user installs catalogs via Settings app

#### Tier 3: Computed (Expensive Calculations)

**Requires:** Background thread for pixel analysis

- Pixel statistics (mean, median, min, max, clipping)
- Histogram display
- Cached in XISF namespace for future access

**Implementation:** `PixelStatistics::Compute()` runs in background thread

```cpp
// Launched from PropertyStore initialization
std::thread([this]() {
    auto stats = PixelStatistics::Compute(m_stream);
    m_stats = stats;  // Cached
    m_computed.store(true, std::memory_order_release);
}).detach();
```

**Availability:** Depends on XISF image data accessibility; skipped if parsing fails

## Catalog Integration Algorithm

### Deep-Sky Object Lookup

**Goal:** Match RA/Dec coordinates to catalog entries (DSOs = Deep-Sky Objects)

**Algorithm:**

```
Input: RA (radians), Dec (radians), Tolerance (radians), Catalog
Output: Matched DSO names or empty list

1. Convert RA/Dec to decimal degrees
   ra_deg = ra_rad * 180 / π
   dec_deg = dec_rad * 180 / π

2. Iterate catalog entries:
   for each entry in catalog.entries:
       - Compute angular distance to entry:
         distance = arccos(
           sin(dec_deg) * sin(entry.dec) +
           cos(dec_deg) * cos(entry.dec) * cos(ra_deg - entry.ra)
         )
       - If distance < tolerance:
           matches.push_back(entry)

3. Sort matches by distance (closest first)

4. Apply priority filter:
   - User-defined catalog priority: NGC > IC > Sharpless
   - Return highest-priority match

5. If multiple matches at same distance:
   - Return first by catalog order
   - Log ambiguity to ETW for debugging
```

**Parameters:**

| Parameter | Default | Tunable | Purpose |
|-----------|---------|---------|---------|
| Tolerance | 0.5° | Yes (registry) | Maximum angular distance for match |
| Priority | NGC, IC, Sh2, B, LBN, M, C | Yes (registry) | Catalog preference order |
| Timeout | 100 ms | No | Abort search if exceeds timeout |

### Catalog Data Structure

**CSV Format** (NGC.csv):

```csv
Name,Type,RA,Dec,Mag,Size,Surface Brightness,Hubble,Constellation
NGC0001,G,00:07:15.84,+27:42:29.1,12.30,1.57x1.07,13.23,SA(s)b,Pegasus
NGC0002,G,00:07:28.83,+27:40:24.8,12.56,1.89x1.05,13.33,S0,Pegasus
```

**Loaded into:** `std::vector<DSOEntry>` with O(1) lookup via spatial index (if enabled)

**Fallback:** Linear scan (O(n) but fast enough for <10,000 entries)

### Catalog Load Sequence

```cpp
// PropertyStore.cpp: First property lookup triggers catalog load
std::call_once(s_catalogOnceFlag, []() {
    auto catalog = std::make_shared<DSOCatalog>();
    
    // 1. Read catalog priority from registry
    auto priority = ReadRegistryPriority();  // default: NGC, IC, Sharpless
    
    // 2. Try to load from ProgramData (resolved path, not literal "%ProgramData%")
    auto ngcPath = xisf::paths::CatalogFile(L"NGC.csv");
    if (catalog.LoadFromCSVFile(WideToUtf8(ngcPath))) {
        catalog.AppendFromCSVFile("addendum.csv");
        catalog.AppendFromCSVFile("sharpless.csv");
    }
    
    // 3. On success, store in process-wide singleton
    s_dsoCatalog = catalog;
    
    // 4. Log load status to ETW
    EventWriteString(L"CatalogLoaded: Count=%u, Priority=%ls", 
        catalog.Count(), priority.c_str());
});
```

**Thread Safety:** Entire sequence protected by `std::call_once` — only one thread initializes catalog, others wait.

## Extensibility: Adding New Properties

### Step 1: Define Property in Registry

**File:** `xisf.propdesc` (XML)

```xml
<propertyDescription
  name="XISF.MyNewProperty"
  formatID="{F6A7B8C9-D0E1-2345-F123-456789012345}"
  propID="50"
  type="String"
  labelName="My Custom Property"
/>
```

**Generate new formatID:**
```powershell
[System.Guid]::NewGuid()  # e.g., F6A7B8C9-D0E1-2345-F123-456789012345
```

### Step 2: Declare Property Key

**File:** `PropertyStore.h`

```cpp
// Define PROPERTYKEY matching propdesc
DEFINE_PROPERTYKEY(PKEY_XISFMyNewProperty, 
  {0xF6A7B8C9, 0xD0E1, 0x2345, {0xF1, 0x23, 0x45, 0x67, 0x89, 0x01, 0x23, 0x45}},
  50);  // propID must match XML
```

### Step 3: Extract Data

**Option A: Basic extraction from FITS headers**

```cpp
// PropertyStore::PopulateProperties()
auto value = m_metadata.getFITSValue("MYNEWKW");
if (!value.empty()) {
    PROPVARIANT pv;
    pv.vt = VT_LPWSTR;
    pv.pwszVal = NarrowToWide(value);
    m_propertyStore->SetValue(PKEY_XISFMyNewProperty, pv);
}
```

**Option B: Computed property**

```cpp
// ComputedProperties.cpp
auto entries = PopulateComputedProperties(inputs);
ComputedPropertyEntry entry;
entry.key = PKEY_XISFMyNewProperty;
entry.type = ComputedPropertyEntry::Type::String;
entry.stringValue = /* computed value */;
entries.push_back(entry);
```

### Step 4: Add Unit Test

**File:** `XISFPropertyHandlerTests.cpp`

```cpp
TEST_METHOD(TestMyNewProperty) {
    // Arrange
    MockStream stream(createXISFWithKeyword("MYNEWKW", "TestValue"));
    CXISFPropertyHandler handler;
    PROPVARIANT pv;
    
    // Act
    handler.Initialize(nullptr, &stream, nullptr);
    HRESULT hr = handler.GetValue(PKEY_XISFMyNewProperty, &pv);
    
    // Assert
    Assert::IsTrue(SUCCEEDED(hr));
    Assert::AreEqual(VT_LPWSTR, pv.vt);
    Assert::AreEqual(L"TestValue", pv.pwszVal);
}
```

### Step 5: Register Handlers

```powershell
# Settings app or manual registration
regsvr32 x64\Release\XISFPropertyHandler.dll
```

## Handler Lifecycle

### Initialization

```cpp
HRESULT CXISFPropertyHandler::Initialize(
    PCIDLIST_ABSOLUTE pidlFolder,
    IDataObject* pdtobj,
    HKEY hkeyProgID)
{
    // 1. Receive IStream or IDataObject
    // 2. Extract file path
    // 3. Parse XISF binary + XML header
    // 4. Build property store in memory
    // 5. Start background thread for expensive computations
    // 6. Return S_OK (or fail gracefully)
    m_initialized = true;
    return S_OK;
}
```

### Property Query

```cpp
HRESULT CXISFPropertyHandler::GetCount(DWORD* pcProps)
{
    if (!m_initialized) return E_FAIL;
    *pcProps = m_propertyStore->GetCount();  // 65
    return S_OK;
}

HRESULT CXISFPropertyHandler::GetAt(DWORD iProp, PROPERTYKEY* pkey)
{
    if (!m_initialized) return E_FAIL;
    *pkey = m_propertyStore->GetKeyAt(iProp);
    return S_OK;
}

HRESULT CXISFPropertyHandler::GetValue(REFPROPERTYKEY key, PROPVARIANT* ppropvar)
{
    if (!m_initialized) return E_FAIL;
    return m_propertyStore->GetValue(key, ppropvar);
}
```

### Cleanup

```cpp
CXISFPropertyHandler::~CXISFPropertyHandler()
{
    // 1. Join background threads (if any)
    // 2. Release IStream
    // 3. Destroy property store
    // 4. Unregister from DLL reference count
}
```

## Performance Optimization

### Parsing Performance

| Phase | Budget | Technique |
|-------|--------|-----------|
| Binary read + signature check | <1 ms | Minimal file I/O |
| XML extraction | <10 ms | Single linear scan |
| XML parsing | <20 ms | Regex-style tokenization (no libxml) |
| Property population | <20 ms | Hash table lookups |
| **Total** | **<50 ms** | Inline background thread start |

### Caching Strategies

**In-process cache:**
- Catalog singleton (loaded once per explorer.exe session)
- Constellation database (hardcoded lookup table)

**File-based cache:**
- Pixel statistics cached in XISF namespace (optional future enhancement)

**Memory budget:**
- Parser output: <100 KB per file
- Property store: <50 KB per file
- Catalog singleton: ~2 MB per explorer.exe process

---

Related: [Architecture](architecture.md), [Building](building.md), [Testing](testing.md)
