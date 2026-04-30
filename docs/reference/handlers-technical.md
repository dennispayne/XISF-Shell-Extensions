# Handlers Technical Reference

Complete technical specifications for XISF shell extension COM components, including CLSID mappings, registry structure, and interface implementations.

## Overview

The XISF Shell Extensions implement **three COM interfaces** to integrate with Windows Explorer:

| Interface | Purpose | Handler | Output |
|-----------|---------|---------|--------|
| **IPropertyStore** | Property retrieval for Details pane & search | Property Handler | XISFPropertyHandler.dll |
| **IPreviewHandler** | Rich preview rendering in Preview Pane | Preview Handler | XISFPreviewHandler.dll |
| **IThumbnailProvider** | Custom thumbnail generation | Preview Handler | XISFPreviewHandler.dll |

Plus **IShellExtInit** and **IShellPropSheetExt** for property sheet integration.

---

## Property Handler (IPropertyStore)

### CLSID & ProgID

```cpp
// PropertyHandler/XISFPropertyHandler/src/dllmain.cpp:13-19
DEFINE_GUID(CLSID_XISFPropertyHandler,
    0x7C54FA8B, 0x9D63, 0x4C10, 0x8F, 0xBE, 0x1A, 0x5A, 0x0F, 0x9A, 0x3B, 0x2E);

DEFINE_GUID(CLSID_XISFPropertySheet,
    0xA3B7C8D9, 0xE1F2, 0x4A5B, 0x8C, 0x6D, 0x7E, 0x8F, 0x9A, 0x0B, 0x1C, 0x2D);

// ProgID: XISFFile
// File Extension: .xisf
// Content Type: application/xisf
```

### COM Interfaces

```cpp
// PropertyHandler/XISFPropertyHandler/src/PropertyStore.h:1-3
class CXISFPropertyHandler : 
    public IPropertyStore,              // Core property retrieval
    public IInitializeWithStream,       // Stream initialization
    public IPropertyStoreCapabilities   // Capability negotiation
```

| Interface | Method | Purpose |
|-----------|--------|---------|
| **IPropertyStore** | `GetCount()` | Return number of properties |
| | `GetAt(index)` | Retrieve property by index |
| `GetValue(key)` | Retrieve property value by PROPERTYKEY |
| `SetValue(key, value)` | **Not supported** (read-only) |
| `Commit()` | **Not supported** |
| **IInitializeWithStream** | `Initialize(stream, mode)` | Load XISF file from stream |
| **IPropertyStoreCapabilities** | `IsPropertyWritable(key)` | Return FALSE for all properties |

### Registry Structure (Unpackaged)

```reg
HKEY_CLASSES_ROOT
  \.xisf
    (Default) = "XISFFile"
    Content Type = "application/xisf"
    PerceivedType = "image"

  \XISFFile
    (Default) = "XISF Image File"

  \CLSID\{7C54FA8B-9D63-4C10-8FBE-1A5A0F9A3B2E}
    (Default) = "XISF Property Handler"
    DisableProcessIsolation = 1 (DWORD)
    
    \InProcServer32
      (Default) = "C:\Path\To\XISFPropertyHandler.dll"
      ThreadingModel = "Apartment"

HKEY_LOCAL_MACHINE
  \SOFTWARE\Microsoft\Windows\CurrentVersion
    \PropertySystem\PropertyHandlers\.xisf
      (Default) = "{7C54FA8B-9D63-4C10-8FBE-1A5A0F9A3B2E}"

    \Explorer\KindMap
      .xisf = "picture"

    \Shell Extensions\Approved
      {7C54FA8B-9D63-4C10-8FBE-1A5A0F9A3B2E} = "XISF Property Handler"

  \SOFTWARE\Microsoft\Windows Search\CrawlScopeManager\Windows\SystemIndex\Extensions\.xisf
    (Default) = "1" (enable indexing)
```

**Registration Method**: `DllRegisterServer()` in dllmain.cpp:88-122

### Property Descriptor Schema

The `.propdesc` XML schema registers all 65 properties with Windows:

```xml
<!-- PropertyHandler/XISFPropertyHandler/propdesc/xisf.propdesc (excerpt) -->
<propertyDescription name="XISF.ExposureTime" 
    formatID="{7C54FA8B-9D63-4C10-8FBE-1A5A0F9A3B2E}" 
    propID="2">
  <searchInfo inInvertedIndex="false" isColumn="true" />
  <labelInfo label="Astro Exposure Time" />
  <typeInfo type="Double" isInnate="false" isViewable="true" />
  <displayInfo defaultColumnWidth="60" displayType="Number">
    <numberFormat formatAs="General" />
  </displayInfo>
</propertyDescription>
```

All 65 properties follow this pattern with propID 2-65 (propID 1 reserved).

### Handler Initialization Flow

```cpp
// IInitializeWithStream::Initialize(IStream *pstream, DWORD grfMode)
1. IStream pointer stored
2. ReadAll() extracts XISF binary preamble
3. XISFParser::ParseFile() extracts XML header
4. XISFRawMetadata built with FITS keywords & XISF properties
5. PropertyStore cache populated on-demand by GetValue()
```

### Property Extraction Pipeline

```
XISF File Stream
    ↓
[XISFParser]  — Extract binary header + XML metadata
    ↓
XISFRawMetadata {
  xmlHeader: string
  fitsKeywords: []
  properties: []
  imageAttributes: {}
}
    ↓
[PropertyStore::GetValue]
    ↓
    ├─ Check basic properties (FITS keywords → XISF properties)
    ├─ Check computed properties
    ├─ Check pixel statistics (if requested)
    └─ Return PROPVARIANT
```

### Performance Characteristics

| Operation | Timing | Notes |
|-----------|--------|-------|
| Stream initialization | <50ms | XML parsing; no pixel I/O |
| GetValue() - basic prop | <1ms | O(1) lookup in pre-built maps |
| GetValue() - computed | 10-50ms | Constellation, MatchedObjects on first call |
| GetValue() - pixel stats | 500-2000ms | Subsamples ~1M pixels; cached |
| Full property enumeration | ~200ms | All 65 properties |

Properties are **lazily computed** — PixelStatistics only computed if explicitly requested.

---

## Property Sheet Handler (IShellPropSheetExt)

### CLSID

```cpp
// CLSID_XISFPropertySheet = {A3B7C8D9-E1F2-4A5B-8C6D-7E8F9A0B1C2D}
```

### COM Interfaces

```cpp
// PropertyHandler/XISFPropertyHandler/src/PropertySheetHandler.h:59
class CXISFPropertySheetHandler : 
    public IShellExtInit,           // Initialize from shell data object
    public IShellPropSheetExt       // Add property sheet pages
```

| Interface | Method | Purpose |
|-----------|--------|---------|
| **IShellExtInit** | `Initialize(pidl, pdtobj, hkey)` | Load file path & metadata |
| **IShellPropSheetExt** | `AddPages(pfnAddPage, lParam)` | Add "Astro Details" property sheet |
| | `ReplacePage(uPageID, ...)` | **Not supported** |

### Property Sheet Features

The "Astro Details" tab displays:

1. **Histogram** — 256-bin intensity distribution (per-channel for RGB)
2. **Pixel Statistics**
   - Median, Mean, Clipping Low/High
   - Cached in file metadata if available
3. **Metadata Summary**
   - Equipment, observation, location, environment

Tab hosted in a modeless dialog spawned on-demand. Long-running pixel analysis offloaded to worker thread.

### Registry Structure

```reg
HKEY_CLASSES_ROOT
  \XISFFile\shellex
    \PropertySheetHandlers
      \XISFPropertySheet
        (Default) = "{A3B7C8D9-E1F2-4A5B-8C6D-7E8F9A0B1C2D}"

HKEY_LOCAL_MACHINE
  \SOFTWARE\Microsoft\Windows\CurrentVersion\Shell Extensions\Approved
    {A3B7C8D9-E1F2-4A5B-8C6D-7E8F9A0B1C2D} = "XISF Property Sheet"
```

---

## Installation Modes

### 1. Unpackaged (Regsvr32)

```powershell
# Register property handler
regsvr32 XISFPropertyHandler.dll

# Register preview handler (if shipped separately)
regsvr32 XISFPreviewHandler.dll
```

- Registers in HKEY_CLASSES_ROOT and HKEY_LOCAL_MACHINE
- Requires admin privileges
- Permanent across reboots
- DLL cached by Windows — restart Explorer after updates

### 2. MSIX Packaged

```xml
<!-- Packaging/Package.appxmanifest -->
<Extensions>
  <desktop:Extension 
      Category="windows.fileTypeAssociation" 
      Executable="ShellExtensionHost.exe" 
      EntryPoint="XISFShellExtensionHost.App">
    <desktop:FileTypeAssociation Name="xisf">
      <desktop:SupportedFileTypes>
        <FileType>.xisf</FileType>
      </desktop:SupportedFileTypes>
    </desktop:FileTypeAssociation>
  </desktop:Extension>
</Extensions>
```

- Uses a proxy host process (ShellExtensionHost.exe)
- Handlers run in app container with reduced privileges
- Registry changes virtualized to app's package registry hive

---

## Handler Settings Registry

User-configurable settings stored in HKEY_CURRENT_USER:

```reg
HKEY_CURRENT_USER
  \Software\DennisPayne\XISF Shell Extension
    PropertyEnabled = 1 (DWORD, 0=disabled)
    PreviewEnabled = 1 (DWORD, 0=disabled)
    FilterEnabled = 1 (DWORD, 0=disabled search results)
    CatalogPriority = "M,C,NGC,IC,Sh2,B,LBN" (REG_SZ)
    MatchToleranceDeg = "0.5" (REG_SZ, cone search radius)
```

Handlers read these at initialization; changes applied after Explorer restart.

---

## Telemetry & Diagnostics

### ETW Provider

```cpp
// PropertyHandler/XISFPropertyHandler/src/dllmain.cpp:21-22
TRACELOGGING_DEFINE_PROVIDER(g_hPropertyProvider, "XISF-PropertyHandler",
    (0x6f6b0c9d, 0x6b76, 0x5a24, 0xbc, 0x3d, 0x70, 0x83, 0x14, 0xe9, 0x6f, 0x2b));
```

**Provider Name**: `XISF-PropertyHandler`  
**Keywords**:
- `XISF_ETW_KEYWORD_PERF` — Performance metrics (pixel stats timing, etc.)
- `XISF_ETW_KEYWORD_CATALOG` — Catalog loading, DSO matches
- `XISF_ETW_KEYWORD_PARSE` — XML parsing, metadata extraction

Trace level defaults to `TRACE_LEVEL_INFORMATION`. Configurable via registry or WMI.

### Test Hook

```cpp
// External telemetry hook for unit tests
extern "C" typedef void (*XISFPropertyHandlerTelemetryHook)
    (UCHAR level, ULONGLONG keyword, PCWSTR format, ...);
extern "C" XISFPropertyHandlerTelemetryHook g_xisfPropertyHandlerTelemetryHook;
```

Set by PropertyHandlerTests.cpp to intercept telemetry events.

---

## Error Handling

### Parse Failures

When XISF parsing fails:

```cpp
// PropertyHandler/XISFPropertyHandler/src/PropertyStore.cpp:150
if (!parseResult.ok()) {
    PropVariantInit(&pv);
    pv.vt = VT_EMPTY;
    *ppropvar = pv;
    return S_FALSE;  // Property not available
}
```

Returns `S_FALSE` rather than error HRESULT — Windows treats as "property unavailable."

### Stream Errors

If IStream::Read fails midway through pixel statistics:

```cpp
// PixelStatistics.cpp:96-100
WritePropertyHandlerTelemetry(TRACE_LEVEL_INFORMATION, XISF_ETW_KEYWORD_PERF,
    L"PixelStatsUnavailable Reason=NoAttachmentImage");
return result;  // PixelStatsResult with available=false
```

Error logged; property remains empty.

---

## Threading Model

### Apartment Model

```cpp
hr = SetRegSZValue(HKEY_CLASSES_ROOT, szInProc, L"ThreadingModel", L"Apartment");
```

- Handlers run on the calling thread
- Thread-safe property access via `std::mutex` in cached maps
- No parallel property retrieval across threads
- COM apartment ensures serialized access to shell objects

### Pixel Statistics Threading

```cpp
// PropertySheetHandler.cpp:87-90
void ComputeAnalysis() {
    m_analyzing.store(true, std::memory_order_release);
    std::thread(&CXISFPropertySheetHandler::ComputeAnalysis, this).detach();
}
```

Pixel stats computed on dedicated worker thread; UI updated via message pump.

---

## Compatibility

### Windows Versions

| OS | Status | Notes |
|----|--------|-------|
| Windows 10 21H2+ | ✓ Supported | IPropertyStore, IThumbnailProvider available |
| Windows 11 | ✓ Fully supported | Modern Shell Extensions, MSIX packaging |
| Windows Server 2022 | ✓ Supported | Regsvr32 installation only |

### Framework Versions

- **C++**: C++17 (MSVC 2019+)
- **Windows SDK**: 10.0.22621.0 (Windows 11)
- **Compiler**: MSVC v142+

### Antivirus / Security

Handlers run with user privileges (Apartment model). Some security software may require whitelisting:

- `XISFPropertyHandler.dll`
- `XISFPreviewHandler.dll`
- `ShellExtensionHost.exe` (MSIX only)

---

## Reference Implementation

| Component | File | Lines | Purpose |
|-----------|------|-------|---------|
| Property Handler Class | PropertyStore.h | 1-150 | IPropertyStore interface |
| Property Extraction | PropertyStore.cpp | 160-1200 | GetValue() implementation |
| Property Sheet | PropertySheetHandler.h | 1-120 | IShellPropSheetExt |
| Sheet UI | PropertySheetHandler.cpp | 200-800 | Histogram, stats rendering |
| DLL Entry Points | dllmain.cpp | 88-150 | DllRegisterServer, etc. |
| XML Parser | XISFParser.h/cpp | 1-400 | XISF header parsing |

---

## Related Documentation

- [Property Mapping Reference](property-mapping.md) — XISF ↔ Windows mapping pipeline
- [Pixel Statistics](../features/pixel-statistics.md) — Subsampling algorithm & caching
- [Computed Properties](../features/computed-properties.md) — Constellation, DSO catalog
- [Feature Tiers](../features/feature-tiers.md) — Tier-gated property availability
- [Property Metadata](../user-guide/property-metadata.md) — All 65 properties

---

**Updated**: 2024 | **Handler Version**: See version.json
