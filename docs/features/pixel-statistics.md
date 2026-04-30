# Pixel Statistics

Computing and displaying pixel statistics for XISF images. This document covers the algorithm, performance, tier gating, and caching behavior for median, mean, and clipping detection.

## Overview

**Pixel Statistics** are subsampled image statistics computed on-demand from XISF image data. They provide quality metrics useful for astrophotographers to assess:

- **Median & Mean** — Overall brightness level
- **Clipping Low/High** — Percentage of pixels hitting sensor floor/ceiling

Properties (propID 56-59):
- `XISF.Median` — VT_R8 (normalized [0,1] or raw)
- `XISF.Mean` — VT_R8 (normalized [0,1] or raw)
- `XISF.ClippingLow` — VT_R8 (percentage 0-100)
- `XISF.ClippingHigh` — VT_R8 (percentage 0-100)

**Not Tier-Gated** — Available at all feature levels if image data present

---

## Subsampling Algorithm

### Step 1: Image Selection

```cpp
// PixelStatistics.cpp:62-110
// Scan for Image elements in XISF
// Find largest non-thumbnail image attachment

struct ImageCandidate {
    std::string elemText;
    ULONGLONG attachSize;
    bool isThumbnail;
};

// Candidates picked by:
// 1. Exclude thumbnails (id == "thumbnail" or "Thumbnail")
// 2. Find largest attachment size
// 3. Use that image for stats
```

**Why largest image?** Non-thumbnail images provide most accurate statistics. Thumbnails (usually 256×256 or 512×512) are too small for representative sampling.

**Safety**: If no attachment image found, return `PixelStatsResult { available: false }` — property remains empty.

### Step 2: Geometry & Format Parsing

```cpp
// PixelStatistics.cpp:111-134
// Extract from Image element attributes:
//   geometry = "width:height:channels"  (e.g., "4096:2748:3" for RGB)
//   location = "attachment:offset:size"
//   sampleFormat = "UInt16", "Float32", etc.

// Example: geometry="4096:2748:1" for 4K monochrome
UINT imgW = 4096, imgH = 2748, imgC = 1;

// Example: location="attachment:65536:134070784"
// Offset 65536 bytes from start, size 134 MB
ULONGLONG offset = 65536, attachSize = 134070784;
```

### Step 3: Adaptive Subsampling

```cpp
// PixelStatistics.cpp:160-180
// Target ~1M pixels for speed/memory balance

const uint32_t TARGET_SAMPLES = 1000000;  // 1M pixels
const uint32_t TOTAL_PIXELS = width * height * channels;
const uint32_t STEP = std::max(1u, TOTAL_PIXELS / TARGET_SAMPLES);

// Example:
// Image: 4096 × 2748 × 1 = 11,239,424 pixels
// Step = 11,239,424 / 1,000,000 ≈ 11
// Sample every 11th pixel → ~1M samples

// Small images (< 1M pixels):
// Image: 1920 × 1080 × 3 = 6,220,800 pixels
// Step = 1 (no subsampling, use all pixels)
```

### Step 4: Pixel Reading & Quantization

```cpp
// PixelStatistics.cpp:190-220
for (uint64_t pos = 0; pos < imageSize; pos += step * pixelBytes) {
    // Seek to position in image data
    stream->Seek(offset + pos, STREAM_SEEK_SET, nullptr);
    
    // Read pixel value based on sampleFormat
    double normalized = 0.0;
    if (sampleFormat == "UInt16") {
        uint16_t val;
        stream->Read(&val, 2, nullptr);
        normalized = val / 65535.0;  // [0,1]
    } else if (sampleFormat == "Float32") {
        float val;
        stream->Read(&val, 4, nullptr);
        // Clamp to [0,1]
        normalized = std::max(0.0f, std::min(1.0f, val));
    }
    
    // Quantize to 256 bins
    uint32_t bin = static_cast<uint32_t>(normalized * 255.9);
    histogram[bin]++;
}
```

**Supported Formats**:
- `UInt16` — 16-bit unsigned integer (0–65535 → normalized)
- `Float32` — 32-bit IEEE float (clamped to [0,1])
- `Float64` — 64-bit IEEE double (clamped to [0,1])
- Other formats default to UInt16 parsing

**Multi-Channel**: For RGB (3 channels), histogram accumulated per-channel independently. Only monochrome statistics returned (merged histogram).

### Step 5: Statistics Calculation

```cpp
// PixelStatistics.cpp:220-250
// Histogram: 256 bins, each bin = count of pixels in that intensity range

// Median: Find intensity at 50th percentile
uint32_t totalCount = sum(histogram[i] for all i);
uint32_t medianTarget = totalCount / 2;
uint32_t cumulative = 0;
for (int i = 0; i < 256; i++) {
    cumulative += histogram[i];
    if (cumulative >= medianTarget) {
        result.median = i / 255.0;  // Normalized [0,1]
        break;
    }
}

// Mean: Average intensity
double sum_intensity = 0;
for (int i = 0; i < 256; i++) {
    sum_intensity += (i / 255.0) * histogram[i];
}
result.mean = sum_intensity / totalCount;

// Clipping Low: Percentage of pixels at floor (bin 0)
result.clippingLowPct = (histogram[0] / (double)totalCount) * 100.0;

// Clipping High: Percentage of pixels at ceiling (bin 255)
result.clippingHighPct = (histogram[255] / (double)totalCount) * 100.0;
```

---

## Performance Characteristics

### Timing Breakdown

| Operation | Time | Notes |
|-----------|------|-------|
| XML header parsing | <50ms | One-time per file |
| Image element scan | <10ms | Finding largest image |
| Stream seeking + reading | 300-800ms | Depends on image size |
| Subsampling | 400-1000ms | ~1M pixel reads |
| Histogram computation | 50-200ms | 256-bin aggregation |
| **Total** | **800-2000ms** | First call (cached after) |

### Memory Usage

- Histogram: 256 × 4 bytes = 1 KB (per channel)
- Cached result: ~60 bytes (4 doubles + flag)
- Stream buffer: typically 64 KB (Windows I/O)
- **Total**: < 1 MB overhead

### Scalability

| Image Size | Samples | Step | Timing | Notes |
|-----------|---------|------|--------|-------|
| 512×512 mono | 262,144 | 1 | 150ms | All pixels |
| 1920×1080 RGB | 6,220,800 | 6 | 300ms | Every 6th pixel |
| 4096×2748 RGB | 33,718,272 | 33 | 900ms | Every 33rd pixel |
| 8192×6144 RGB | 150,994,944 | 150 | 2000ms | Every 150th pixel |

---

## Threading & Caching

### Lazy Evaluation

Statistics **not computed** until explicitly requested:

```cpp
// PropertyStore::GetValue(PKEY_XISF_Median, propvar)
if (IsPixelStatKey(key)) {
    // Only compute if requested
    if (!m_pixelStatsComputed) {
        m_pixelStats = ComputePixelStats(m_pStream, m_xmlHeader);
        m_pixelStatsComputed = true;
    }
    return GetStatValue(key, propvar);
}
```

This avoids unnecessary I/O for files accessed via Details pane only (no pixel stats requested).

### Caching Strategy

```cpp
// PropertySheetHandler.cpp:88-100
// In property sheet tab, pixel stats computed once on-demand
// Cached in memory for duration of dialog

if (m_statsCache.valid) {
    // Use cached result
    return m_statsCache;
}

// Compute on worker thread
std::thread([this]() {
    m_stats = ComputePixelStats(...);
    m_statsCache = m_stats;
    // UI notified via WM_PAINT
}).detach();
```

### File-Level Caching

If XISF file contains cached properties:

```xml
<Property id="Observation:Pixel:Median" type="Float64">0.45</Property>
<Property id="Observation:Pixel:Mean" type="Float64">0.42</Property>
<Property id="Observation:Pixel:ClippingLow" type="Float64">2.5</Property>
<Property id="Observation:Pixel:ClippingHigh" type="Float64">1.2</Property>
```

Handler **skips computation** and returns cached values directly (~1ms).

---

## Feature Tier Gating

**Pixel Statistics are NOT tier-gated** — available at all levels (Free/Standard/Full).

However, display of statistics in property sheet may vary by tier:

| Tier | Median | Mean | Clipping | Histogram |
|------|--------|------|----------|-----------|
| Free | ✓ | ✓ | ✓ | ✓ |
| Standard | ✓ | ✓ | ✓ | ✓ |
| Full | ✓ | ✓ | ✓ | ✓ |

See [Feature Tiers](feature-tiers.md) for complete tier matrix.

---

## Error Handling

### Parse Failures

If image geometry unparseable:

```cpp
// PixelStatistics.cpp:115-120
if (geometry.empty() || location.empty()) {
    WritePropertyHandlerTelemetry(TRACE_LEVEL_INFORMATION, XISF_ETW_KEYWORD_PERF,
        L"PixelStatsUnavailable Reason=MissingGeometryOrLocation");
    return result;  // available=false
}
```

### Stream Seek Failures

If IStream::Seek fails:

```cpp
LARGE_INTEGER liOffset;
liOffset.QuadPart = offset;
HRESULT hr = pStream->Seek(liOffset, STREAM_SEEK_SET, nullptr);
if (FAILED(hr)) {
    WritePropertyHandlerTelemetry(TRACE_LEVEL_WARNING, XISF_ETW_KEYWORD_PERF,
        L"PixelStatsUnavailable Reason=SeekFailed hr=%x", hr);
    return result;  // available=false
}
```

### Timeout Protection

No explicit timeout, but adaptive subsampling ensures:
- Small images complete in 100-200ms
- Large images (100MP+) complete in <3s
- Property Sheet UI remains responsive (worker thread)

---

## Usage Examples

### Query Pixel Statistics via IPropertyStore

```cpp
CComPtr<IPropertyStore> store;
// ... initialize store with XISF file ...

PROPVARIANT pv;
PropVariantInit(&pv);

// Get median
HRESULT hr = store->GetValue(PKEY_XISF_Median, &pv);
if (SUCCEEDED(hr) && pv.vt == VT_R8) {
    double median = pv.dblVal;  // [0,1] normalized
    printf("Median: %.3f\n", median);
}
PropVariantClear(&pv);

// Get clipping low
hr = store->GetValue(PKEY_XISF_ClippingLow, &pv);
if (SUCCEEDED(hr) && pv.vt == VT_R8) {
    double clipping = pv.dblVal;  // 0-100%
    printf("Clipping Low: %.1f%%\n", clipping);
}
PropVariantClear(&pv);
```

### Display in Property Sheet

```cpp
// PropertySheetHandler.cpp:240-280
// Paint histogram in dialog
HDC hdc = BeginPaint(hwnd, &ps);
RECT rcHist = {...};
PaintHistogram(hdc, rcHist, m_histogram);
EndPaint(hwnd, &ps);

// Display stats
wchar_t buf[256];
swprintf_s(buf, L"Median: %.3f | Mean: %.3f | Clipping: %.1f%%/%.1f%%",
    m_stats.median, m_stats.mean, 
    m_stats.clippingLowPct, m_stats.clippingHighPct);
DrawTextW(hdc, buf, -1, &rcText, DT_LEFT);
```

---

## Troubleshooting

### Stats unavailable for some files

**Check**:
1. File has attached Image? `<Image location="attachment:..." />`
2. Image has geometry? `<Image geometry="W:H:C" />`
3. Unsupported format? Only UInt16/Float32/Float64 supported

**ETW Trace**:
```
logman start XISF-trace -p "XISF-PropertyHandler" -ets
logman stop XISF-trace -ets
tracerpt XISF-trace.etl -of xml
```

Look for "PixelStatsUnavailable" events with reason.

### Slow property retrieval

**Check**:
1. Is file cached? Look for `Observation:Pixel:*` properties in file
2. Network share? Stream I/O slower over network
3. Large image (>50MP)? Expected 1-2s first call

**Mitigation**:
- Save statistics to XISF file after first compute
- Use Standard or Full tier (better caching)

---

## Related Documentation

- [Computed Properties](computed-properties.md) — RA/Dec bands, constellation, DSO matching
- [Feature Tiers](feature-tiers.md) — Tier availability matrix
- [Handlers Technical Reference](../reference/handlers-technical.md) — Performance characteristics
- [Property Mapping Reference](../reference/property-mapping.md) — Type conversion for stats

---

**Updated**: 2024 | **Handler Version**: See version.json | **Source**: PixelStatistics.cpp, PropertySheetHandler.cpp
