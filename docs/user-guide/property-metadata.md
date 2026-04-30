# XISF Property Metadata Reference

Complete reference of all 65 XISF shell extension properties, organized by category with descriptions, types, and example values.

## Overview

The XISF Shell Extensions expose astrophotography-specific metadata as Windows properties through the **IPropertyStore** interface. Properties are extracted from the XISF binary header (XML metadata block) and computed on-demand based on XISF Properties and FITS Keywords.

**Property ID Prefix**: All XISF properties share the common formatID `{7C54FA8B-9D63-4C10-8FBE-1A5A0F9A3B2E}`

## Property Categories

### 1. Instrument & Camera (8 properties: propID 3, 5, 9-10, 13, 19-21)

| Property | Type | XISF Source | Example | Notes |
|----------|------|-------------|---------|-------|
| **CameraModel** (3) | String | `Instrument:Camera:Model` FITS `INSTRUME` | "ZWO ASI6200MM Pro" | Camera equipment identifier |
| **FNumber** (5) | Double | `Instrument:Telescope:FNumber` FITS `APERTURE` | 2.8 | Focal ratio f/N |
| **Gain** (9) | UInt32 | `Instrument:Sensor:Gain` FITS `EGAIN` | 200 | ADU/e⁻ sensitivity |
| **Offset** (10) | UInt32 | `Instrument:Sensor:Offset` FITS `BIAS` | 50 | Bias/offset level in ADU |
| **Binning** (13) | String | `Instrument:Sensor:Binning` FITS `XBINNING:YBINNING` | "1x1" or "2x2" | Pixel binning mode |
| **PixelSize** (19) | Double | `Instrument:Sensor:PixelSize` FITS `PIXSIZE1` | 3.8 | Microns per pixel |
| **ReadoutMode** (20) | String | `Instrument:Sensor:ReadoutMode` FITS `READMODE` | "High Gain" | Camera readout configuration |
| **BayerPattern** (21) | String | Image colorSpace attribute | "BGGR" or "RGGB" | CFA demosaic pattern or "Monochrome" |

**Source Files**: PropertyStore.cpp:190-260, xisf.propdesc propID 3,5,9-10,13,19-21

### 2. Observation Parameters (6 properties: propID 2, 6-8, 14-15)

| Property | Type | XISF Source | Example | Notes |
|----------|------|-------------|---------|-------|
| **ExposureTime** (2) | Double | `Instrument:ExposureTime` FITS `EXPTIME` | 60.0 | Seconds |
| **ObjectName** (6) | String | `Observation:Subject:ReferenceObject:Name` FITS `OBJECT` | "M42" or "Orion Nebula" | Target name (aliases resolved if catalog available) |
| **FilterName** (7) | String | `Instrument:FilterWheel:Filter:Name` FITS `FILTER` | "Ha 6nm" | Emission or broadband filter |
| **ImageType** (8) | String | `Observation:ImageType` FITS `IMAGETYP` | "Light" or "Dark" | Frame classification (Light/Dark/Bias/Flat) |
| **DateObserved** (14) | DateTime | `Observation:Time:Start` FITS `DATE-OBS` | "2024-03-15T22:30:00" | UTC observation start |
| **Software** (15) | String | `Observation:Software:Name` FITS `SWCREATE` | "N.I.N.A." | Acquisition software identifier |

**Source Files**: PropertyStore.cpp:260-330

### 3. Telescope & Equipment (11 properties: propID 12, 25-26, 31-37)

| Property | Type | XISF Source | Example | Notes |
|----------|------|-------------|---------|-------|
| **Telescope** (12) | String | `Instrument:Telescope:Description` FITS `TELESCOP` | "Celestron EdgeHD 9.25" | Telescope model |
| **Altitude** (25) | Double | `Observation:Altitude` FITS `ALT` | 45.5 | Degrees above horizon |
| **Azimuth** (26) | Double | `Observation:Azimuth` FITS `AZ` | 180.0 | Degrees east of north |
| **Rotation** (31) | Double | `Observation:CelestialReferenceSystem:Rotation` | 45.0 | Degrees field rotation |
| **FocuserName** (32) | String | `Instrument:Focuser:Name` | "Moonlite Focuser" | Focuser device name |
| **FocuserPosition** (33) | UInt32 | `Instrument:Focuser:Position` | 2500 | Focus position steps or ADU |
| **FocuserTemp** (34) | Double | `Instrument:Focuser:Temperature` | 18.5 | °C focuser sensor temperature |
| **RotatorName** (35) | String | `Instrument:Rotator:Name` | "Pegasus Falcon" | Rotator device name |
| **RotatorAngle** (36) | Double | `Instrument:Rotator:Angle` | 180.0 | Degrees field angle |
| **FilterWheel** (37) | String | `Instrument:FilterWheel:Name` FITS `FILTERW` | "FW7X" | Filter wheel device |
| **FocalLength** (4) | Double | `Instrument:Telescope:FocalLength` FITS `FOCALLEN` | 2100.0 | Millimeters |

**Source Files**: PropertyStore.cpp:330-420

### 4. Observation Location (4 properties: propID 22-24, 41)

| Property | Type | XISF Source | Example | Notes |
|----------|------|-------------|---------|-------|
| **SiteLatitude** (22) | Double | `Observation:Location:Latitude` FITS `SITELAT` | 40.7128 | Decimal degrees (North positive) |
| **SiteLongitude** (23) | Double | `Observation:Location:Longitude` FITS `SITELONG` | -74.0060 | Decimal degrees (West negative) |
| **SiteElevation** (24) | Double | `Observation:Location:Elevation` FITS `SITEELEV` | 10.0 | Meters above sea level |
| **DateLocal** (41) | DateTime | Computed from DateObserved + timezone | "2024-03-15T18:30:00-04:00" | Observation time in local timezone |

**Source Files**: PropertyStore.cpp:420-480, ComputedProperties.cpp:380-420

### 5. Celestial Coordinates (6 properties: propID 16-18, 25, 29-30)

| Property | Type | XISF Source | Example | Notes |
|----------|------|-------------|---------|-------|
| **RA** (16) | Double | `Observation:CelestialReferenceSystem:RA` FITS `RA` | 83.633 | Decimal degrees (0-360) |
| **Dec** (17) | Double | `Observation:CelestialReferenceSystem:Dec` FITS `DEC` | -5.391 | Decimal degrees (-90 to +90) |
| **SetTemp** (18) | Double | `Instrument:Sensor:TemperatureSetpoint` FITS `SET-TEMP` | -10.0 | °C target sensor temperature |
| **Airmass** (27) | Double | `Observation:Airmass` FITS `AIRMASS` | 1.5 | Atmospheric extinction factor |
| **ObjectRA** (29) | String | Converted from RA degree value | "05h34m32.0s" | Hour-Minute-Second format |
| **ObjectDec** (30) | String | Converted from Dec degree value | "-05°23m24.0"" | Degree-Minute-Second format |

**Source Files**: PropertyStore.cpp:480-560, ComputedProperties.cpp:30-60

### 6. Environment & Weather (8 properties: propID 38-40, 47-52)

| Property | Type | XISF Source | Example | Notes |
|----------|------|-------------|---------|-------|
| **DewPoint** (38) | Double | `Observation:Environment:DewPoint` FITS `DEWPOINT` | 5.2 | °C |
| **Humidity** (39) | Double | `Observation:Environment:Humidity` FITS `HUMIDITY` | 65.0 | Percentage (0-100) |
| **AmbientTemp** (40) | Double | `Observation:Environment:AmbientTemperature` FITS `AMBTEMP` | 12.5 | °C ambient air temperature |
| **SkyQuality** (47) | Double | `Observation:Environment:SkyQuality` FITS `SQM` | 20.85 | Magnitudes per arcsec² |
| **SkyBrightness** (48) | Double | `Observation:Environment:Brightness` | 0.15 | Derived from SQM if available |
| **CloudCover** (49) | Double | `Observation:Environment:CloudCover` FITS `CLOUDCVR` | 0.0 | Percentage (0-100) |
| **Pressure** (50) | Double | `Observation:Environment:Pressure` FITS `PRESSURE` | 1013.25 | hPa (hectopascals) |
| **SkyTemp** (51) | Double | `Observation:Environment:SkyTemperature` FITS `SKYTEMP` | -25.0 | °C measured by IR sensor |

**Source Files**: PropertyStore.cpp:560-620

### 7. Guiding (2 properties: propID 53-54)

| Property | Type | XISF Source | Example | Notes |
|----------|------|-------------|---------|-------|
| **GuideRA** (53) | Double | `Observation:CelestialReferenceSystem:RAPierSide` or guiding log | 0.25 | Arcseconds guide error |
| **GuideDec** (54) | Double | `Observation:CelestialReferenceSystem:DecPierSide` or guiding log | -0.18 | Arcseconds guide error |
| **PierSide** (28) | String | `Observation:CelestialReferenceSystem:PierSide` FITS `PIERSIDE` | "East" or "West" | Mount pier side at exposure |

**Source Files**: PropertyStore.cpp:620-660

### 8. Image Structure (5 properties: propID 60-65)

| Property | Type | XISF Source | Example | Notes |
|----------|------|-------------|---------|-------|
| **ColorSpace** (60) | String | Image element colorSpace attribute | "RGB" or "Gray" | Color space model |
| **SampleFormat** (61) | String | Image element sampleFormat attribute | "UInt16" or "Float32" | Pixel data type |
| **ImageWidth** (62) | UInt32 | Image element geometry attribute | 4096 | Pixels (first dimension) |
| **ImageHeight** (63) | UInt32 | Image element geometry attribute | 2748 | Pixels (second dimension) |
| **ChannelCount** (64) | UInt32 | Image element geometry channels | 1 or 3 | 1=Monochrome, 3=RGB |
| **ImageCount** (65) | UInt32 | Count of `<Image>` elements in XISF | 2 | Number of attached images |

**Source Files**: PropertyStore.cpp:660-720, XISFParser.cpp:100-180

### 9. Pixel Statistics (4 properties: propID 56-59)

| Property | Type | Source | Example | Notes |
|----------|------|--------|---------|-------|
| **Median** (56) | Double | Subsampled pixel analysis | 0.45 | Normalized [0,1] or raw value |
| **Mean** (57) | Double | Subsampled pixel analysis | 0.42 | Normalized [0,1] or raw value |
| **ClippingLow** (58) | Double | Subsampled pixel analysis | 2.5 | Percentage of pixels at floor |
| **ClippingHigh** (59) | Double | Subsampled pixel analysis | 1.2 | Percentage of pixels at ceiling |

Computed by sampling ~1M pixels from the largest non-thumbnail image. See [Pixel Statistics](../features/pixel-statistics.md) for algorithm details.

**Source Files**: PixelStatistics.cpp, PropertySheetHandler.cpp

### 10. Data Quality (1 property: propID 55)

| Property | Type | XISF Source | Example | Notes |
|----------|------|-------------|---------|-------|
| **DataState** (55) | String | `Observation:DataState` or detected state | "Original" or "Processed" | Indicates if data is raw or calibrated |

**Source Files**: PropertyStore.cpp:720-740

### 11. Computed/Derived (4 properties: propID 42-45, 46)

| Property | Type | Source | Example | Notes |
|----------|------|--------|---------|-------|
| **RAHour** (42) | String | Computed from RA | "05h" | RA divided into 24-hour zones |
| **DecBand** (43) | String | Computed from Dec | "+0° to +15°" | Dec divided into 15° bands |
| **Constellation** (44) | String | RA/Dec lookup in IAU catalog | "Orion" | 88 official constellation names |
| **MatchedObjects** (45) | String | Cone search in DSO catalogs | "M42, NGC 1976" | Comma-separated DSO matches |
| **StarFWHM** (46) | Double | `Observation:StarFWHM` FITS `FWHM` | 2.1 | Full-width half-maximum in arcseconds |

Computed properties are **tier-gated**: Constellation and MatchedObjects require the Full feature tier and populated DSO catalogs.

**Source Files**: ComputedProperties.cpp:30-200

## Property Type Mapping

| Type | C++ | COM | Byte Size | Example |
|------|-----|-----|-----------|---------|
| String | `std::string` | `VT_LPWSTR` | Variable | "M42 Nebula" |
| Double | `double` | `VT_R8` | 8 | 83.633 |
| UInt32 | `uint32_t` | `VT_UI4` | 4 | 4096 |
| DateTime | ISO 8601 string→`FILETIME` | `VT_FILETIME` | 8 | "2024-03-15T22:30:00" |
| StringList | `std::vector<std::string>` | `VT_VECTOR \| VT_LPWSTR` | Variable | ["M42", "NGC 1976"] |

## NULL Handling

Properties with no value in the XISF header are **not added** to the property store. This prevents clutter in Details pane and search indexing:

```cpp
// Example from PropertyStore.cpp
if (metadata.getPropertyValue("Instrument:Sensor:Gain").empty()) {
    // Skip adding Gain property
    return;
}
```

Search queries naturally exclude missing properties.

## FITS Keyword Priority

When extracting metadata, the handler checks in this order:

1. **XISF Properties** (typed, preferred)
   - Example: `<Property id="Instrument:ExposureTime" type="Float64">60.0</Property>`
2. **FITS Keywords** (legacy, converted to XISF type)
   - Example: `EXPTIME = 60.0 / Exposure time (seconds)`

XISF Properties take precedence to avoid type ambiguity.

## Registry Location

Properties are registered in the Windows property system via:

```
HKEY_CLASSES_ROOT\.xisf
  → Default: XISFFile

HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\PropertySystem\PropertyHandlers\.xisf
  → Default: {7C54FA8B-9D63-4C10-8FBE-1A5A0F9A3B2E}
```

The `.propdesc` schema is embedded in `XISFPropertyHandler.dll` and automatically registered on installation.

## Related Documentation

- [Handlers Technical Reference](../reference/handlers-technical.md) — COM interfaces, CLSID mappings
- [Property Mapping Reference](../reference/property-mapping.md) — XISF ↔ Windows mapping pipeline
- [Feature Tiers](../features/feature-tiers.md) — Tier-gated property availability
- [Computed Properties](../features/computed-properties.md) — Derivation algorithms

---

**Updated**: 2024 | **Handler Version**: See version.json | **XISF Spec**: [pixinsight.com/xisf](https://pixinsight.com/xisf/)
