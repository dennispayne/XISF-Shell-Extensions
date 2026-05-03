# Feature Tiers

Understanding feature availability levels and tier-gated properties in the XISF Shell Extensions.

## Overview

**Feature Tiers** control which properties and features are available based on installation level, catalog availability, or license. This allows:

- **Free tier**: Core properties (65 properties available)
- **Standard tier**: Enhanced metadata (all 65 properties + computed basics)
- **Full tier**: Complete astrophotography integration (constellation, DSO matching, advanced analytics)

Tier is determined at **handler initialization** based on registry settings and installed catalogs.

---

## Tier Levels

### Tier Free

**Availability**: Unpackaged install or MSIX without optional catalogs

**Properties Available**: All 65 basic properties

| Category | Count | Includes |
|----------|-------|----------|
| Instrument & Camera | 8 | Camera, focal length, gain, binning, etc. |
| Observation Parameters | 6 | Exposure time, filter, image type, date |
| Telescope & Equipment | 11 | Telescope, focuser, rotator, altitude/azimuth |
| Location | 4 | Site latitude/longitude/elevation, local date |
| Celestial Coordinates | 6 | RA, Dec, RA/Dec conversions, airmass |
| Environment | 8 | Temperature, humidity, pressure, sky quality |
| Guiding | 3 | Guide errors, pier side |
| Image Structure | 5 | Width, height, color space, sample format |
| Pixel Statistics | 4 | Median, mean, clipping low/high |
| Data Quality | 1 | Data state |
| **Basic Computed** | 3 | RAHour, DecBand, StarFWHM |

**Features**:
- Details pane property display ✓
- Search indexing ✓
- Property sheet with histogram ✓
- Pixel statistics computation ✓

**Limitations**:
- No constellation resolution
- No DSO catalog matching
- No advanced analytics

### Tier Standard

**Availability**: Same as Free (not currently used for differentiation)

**Additional Features**:
- Performance optimizations
- Caching enhancements
- Constellation DB pre-loaded (if installed)

**Note**: Currently maps to Free tier behavior. Reserved for future feature expansion.

### Tier Full

**Availability**: MSIX with full DSO catalog package or custom installation

**Additional Properties**:

| Property | Type | Requires | Example |
|----------|------|----------|---------|
| **Constellation** (44) | String | IAU boundaries + RA/Dec | "Orion" |
| **MatchedObjects** (45) | String | DSO catalogs + RA/Dec + FOV | "M42, NGC 1976" |

**Catalog Requirements**:
- `NGC.csv` — Messier, NGC, IC objects
- `addendum.csv` — Recent discoveries (optional)
- `sharpless.csv` — Emission nebulae (optional)

Located in: `%LOCALAPPDATA%\XISFShellExtension\catalogs\`

**Features**:
- All Free tier features ✓
- Constellation resolution ✓
- DSO catalog matching ✓
- Advanced search by constellation ✓
- Astrophotography workflow optimization ✓

---

## Feature Matrix

| Feature | Free | Standard | Full |
|---------|------|----------|------|
| **Properties** |
| Basic properties (57) | ✓ | ✓ | ✓ |
| Pixel statistics (4) | ✓ | ✓ | ✓ |
| RAHour, DecBand (2) | ✓ | ✓ | ✓ |
| StarFWHM (1) | ✓ | ✓ | ✓ |
| Constellation | ✗ | ✗ | ✓ |
| MatchedObjects | ✗ | ✗ | ✓ |
| **UI & Display** |
| Details pane | ✓ | ✓ | ✓ |
| Property sheet tab | ✓ | ✓ | ✓ |
| Histogram display | ✓ | ✓ | ✓ |
| **Search** |
| Property indexing | ✓ | ✓ | ✓ |
| Sort/filter | ✓ | ✓ | ✓ |
| Constellation search | ✗ | ✗ | ✓ |
| DSO name search | ✗ | ✗ | ✓ |
| **Performance** |
| Property retrieval | ~1ms | ~1ms | ~1ms |
| Computed properties | 10-50ms | 10-50ms | 10-50ms |
| Constellation lookup | — | — | <1ms (cached) |
| DSO matching | — | — | 10-200ms |

---

## Tier Detection

### Registry-Based Detection

```cpp
// HandlerSettings.h/cpp: Tier detection at initialization
static FeatureTier DetectFeatureTier() {
    // Check registry for explicit tier setting
    wchar_t buf[64] = {};
    DWORD cb = sizeof(buf);
    LONG st = RegGetValueW(HKEY_CURRENT_USER, 
        L"Software\\DennisPayne\\XISF Shell Extension", 
        L"FeatureTier", RRF_RT_REG_SZ, nullptr, buf, &cb);
    
    if (st == ERROR_SUCCESS && buf[0] != L'\0') {
        std::string tier(buf);
        if (tier == "Full") return FeatureTier::Full;
        if (tier == "Standard") return FeatureTier::Standard;
    }
    
    // Auto-detect based on catalog availability
    return DetectTierFromCatalogs();
}

static FeatureTier DetectTierFromCatalogs() {
    PWSTR pszBase = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &pszBase))) {
        return FeatureTier::Free;
    }
    
    std::wstring catalogPath = std::wstring(pszBase) + 
        L"\\XISFShellExtension\\catalogs\\NGC.csv";
    CoTaskMemFree(pszBase);
    
    // If NGC.csv exists → Full tier
    if (PathFileExistsW(catalogPath.c_str())) {
        return FeatureTier::Full;
    }
    
    return FeatureTier::Free;
}
```

### Tier-Based Property Filtering

```cpp
// ComputedProperties.cpp: Populate based on tier
std::vector<ComputedPropertyEntry> PopulateComputedProperties(
    const ComputedPropertyInputs& inputs) {
    
    std::vector<ComputedPropertyEntry> result;
    
    // Always available
    AddRAHour(result, inputs);
    AddDecBand(result, inputs);
    AddStarFWHM(result, inputs);
    
    // Full tier only
    if (inputs.tier == FeatureTier::Full) {
        AddConstellation(result, inputs);
        AddMatchedObjects(result, inputs);
    }
    
    return result;
}
```

---

## Registry Tier Configuration

### Explicit Tier Setting

```reg
HKEY_CURRENT_USER\Software\DennisPayne\XISF Shell Extension
  FeatureTier = "Full"      (REG_SZ: "Free", "Standard", or "Full")
```

Override auto-detection. Useful for:
- Testing Full tier features without installing catalogs
- Restricting to Free tier on restricted networks
- License-based activation

### Catalog Priority

```reg
HKEY_CURRENT_USER\Software\DennisPayne\XISF Shell Extension
  CatalogPriority = "M,C,NGC,IC,Sh2,B,LBN"  (REG_SZ)
```

Controls DSO match ordering (Full tier only):
- `M` — Messier (brightest)
- `C` — Caldwell
- `NGC` — New General Catalog
- `IC` — Index Catalog
- `Sh2` — Sharpless emission nebulae
- `B` — Barnard dark nebulae
- `LBN` — Lynds' Bright Nebula

Default: `"M,C,NGC,IC,Sh2,B,LBN"` (Messier-prioritized)

### Match Tolerance

```reg
HKEY_CURRENT_USER\Software\DennisPayne\XISF Shell Extension
  MatchToleranceDeg = "0.5"  (REG_SZ: degrees)
```

Cone search radius for DSO matching (Full tier only):
- `0.5` — Default (small search, precise matches)
- `1.0` — Larger radius (more matches, potential false positives)
- `0.25` — Strict (only closest match)

Example: RA/Dec within ±0.5° of catalog entry → included in MatchedObjects

---

## Upgrading Tiers

### Free → Full Upgrade Path

#### Option 1: Manual Catalog Installation

1. Download catalog CSV files:
   - NGC.csv (galaxies, nebulae, clusters)
   - addendum.csv (recent discoveries, optional)
   - sharpless.csv (emission nebulae, optional)

2. Create directory:
   ```
   %LOCALAPPDATA%\XISFShellExtension\catalogs\
   ```

3. Copy files to directory

4. Restart Windows Explorer:
   ```powershell
   Stop-Process -Name explorer -Force
   Start-Process explorer
   ```

5. Verify: Check Properties Details pane for "Constellation" property

#### Option 2: MSIX Package with Catalogs

Install the Full-featured MSIX package:
```powershell
Add-AppxPackage -Path XISFShellExtension-Full.msix
```

Includes pre-installed catalogs in app package.

#### Option 3: Registry Override

For testing without catalogs:
```powershell
reg add HKCU\Software\DennisPayne\"XISF Shell Extension" /v FeatureTier /t REG_SZ /d "Full" /f
```

⚠️ **Note**: Properties will be available but matching won't find results without catalogs.

---

## Performance Implications

### Memory

| Tier | Resident Memory | Notes |
|------|-----------------|-------|
| Free | ~2 MB | Base handler + settings |
| Standard | ~2 MB | Same as Free |
| Full | ~10 MB | DSO catalogs (NGC.csv ~3MB, etc.) |

Catalogs loaded once per process on first Constellation/MatchedObjects lookup.

### CPU

| Operation | Free | Full | Notes |
|-----------|------|------|-------|
| Property retrieval | ~1ms | ~1ms | No difference |
| Computed properties | 10-50ms | 10-50ms | No difference |
| Constellation (first) | N/A | 50-100ms | DB load |
| Constellation (cached) | N/A | <1ms | Polygon lookup |
| DSO matching (first) | N/A | 600-1000ms | Catalog load + search |
| DSO matching (cached) | N/A | 10-200ms | Cone search |

### Disk I/O

Free tier: No catalog I/O
Full tier: 
- First access: ~5-10MB read from catalogs
- Subsequent access: Cached in memory (no I/O)

---

## Troubleshooting Tier Detection

### "Constellation" property not appearing

**Check tier**:
```powershell
reg query HKCU\Software\DennisPayne\"XISF Shell Extension"
```

Should show `FeatureTier` or catalog files present at `%LOCALAPPDATA%\XISFShellExtension\catalogs\NGC.csv`

**Fix**:
1. Verify catalogs installed in correct location
2. Restart Explorer: `Stop-Process -Name explorer -Force; Start-Process explorer`
3. Force tier: `reg add HKCU\Software\DennisPayne\"XISF Shell Extension" /v FeatureTier /t REG_SZ /d "Full" /f`

### Slow DSO matching

**Check catalog size**:
```powershell
Get-ChildItem "$env:LOCALAPPDATA\XISFShellExtension\catalogs\*.csv" | Measure-Object -Sum Length
```

Expected: ~4-5 MB total for NGC.csv + addendum.csv

**Optimize**:
1. Use smaller cone search radius: `MatchToleranceDeg = "0.25"`
2. Adjust catalog priority (remove lower-priority sources): `CatalogPriority = "M,NGC,IC"`

---

## Development & Testing

### Tier Activation for Development

```cpp
// Mock tier in unit tests
ComputedPropertyInputs inputs;
inputs.tier = FeatureTier::Full;
inputs.projectionEnabled = true;
inputs.raDeg = 83.633;
inputs.decDeg = -5.391;

auto props = PopulateComputedProperties(inputs);
// Should include Constellation, MatchedObjects
```

### Test Data Files

Sample NGC.csv format:
```
id,name,ra,dec,mag,type,size
M1,Crab Nebula,85.37,-27.14,8.4,SNR,6.0
M42,Orion Nebula,83.63,-5.39,4.0,EN,66.0
NGC1976,Orion Nebula,83.63,-5.39,4.0,EN,66.0
```

---

## Related Documentation

- [Computed Properties](computed-properties.md) — Constellation, DSO algorithms
- [Pixel Statistics](pixel-statistics.md) — All tiers support stats
- [Property Metadata](../user-guide/property-metadata.md) — Tier availability per property
- [Handlers Technical Reference](../reference/handlers-technical.md) — Registry structure

---

**Updated**: 2024 | **Handler Version**: See version.json | **Catalog Data**: OpenNGC Project (CC-BY-SA 4.0)
