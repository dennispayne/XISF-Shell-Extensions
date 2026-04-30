# Architecture

## System Design Overview

XISF Shell Extensions extends Windows Explorer and Windows Search with native support for XISF (Extensible Image Serialization Format) files used in astrophotography. The architecture follows Windows shell extension patterns: three independent COM-based handlers that run in-process within `explorer.exe` and `prevhost.exe`, plus a companion settings app.

## Component Architecture

### Core Handlers (In-Process COM DLLs)

```
┌─────────────────────────────────────────────────────────────┐
│                   Windows File Explorer                      │
├─────────────────────────────────────────────────────────────┤
│  ┌──────────────────┐  ┌──────────────────┐                │
│  │  PropertyHandler │  │  PreviewHandler  │                │
│  │  (IPropertyStore)│  │ (IPreviewHandler)│                │
│  │   IInitialize... │  │  IThumbnail...   │                │
│  └────────┬─────────┘  └────────┬─────────┘                │
│           │                     │                           │
│           └─────────┬───────────┘                           │
│                     │                                       │
│            ┌────────▼─────────┐                            │
│            │   Shared libs    │                            │
│            │   XISFParser     │                            │
│            │   Catalog loader │                            │
│            │   Stats compute  │                            │
│            └──────────────────┘                            │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│                   Windows Search                             │
├─────────────────────────────────────────────────────────────┤
│  ┌──────────────────┐                                       │
│  │   IFilter (DLL)  │  Indexes XISF metadata                │
│  │   (XISFFilter)   │                                       │
│  └────────┬─────────┘                                       │
│           │                                                 │
│  ┌────────▼──────────────────┐                              │
│  │   XISFParser + Catalog    │                              │
│  │   (catalog-enabled search)│                              │
│  └───────────────────────────┘                              │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│          Settings App (XISFShellExtensionHost.exe)          │
├─────────────────────────────────────────────────────────────┤
│  • Handler enable/disable toggles                           │
│  • COM registration/unregistration                          │
│  • Catalog download & verification (SHA-256)               │
│  • Configuration UI                                        │
└─────────────────────────────────────────────────────────────┘
```

## Core Components

### 1. PropertyHandler (XISFPropertyHandler.dll)

**Interfaces:**
- `IPropertyStore` — Provides 65+ FITS/XISF metadata properties to Explorer Details pane
- `IInitializeWithStream` — Receives XISF file stream for parsing
- `IPropertyStoreCapabilities` — Declares which properties are supported (read-only)
- `IShellExtInit` — Legacy property sheet initialization
- `IShellPropSheetExt` — Adds "Astro Details" property sheet tab with computed statistics

**Responsibilities:**
- Parse XISF binary header and extract XML metadata
- Populate Windows property store with 65 metadata properties
- Load deep-sky catalogs and enrich metadata with object names
- Compute pixel statistics (mean, median, min, max, clipping) in background thread
- Render histogram and statistics on property sheet tab

**Key Data Flow:**
```
File Selection in Explorer
         │
         ▼
PropertyHandler receives IStream
         │
         ▼
XISFParser extracts metadata from binary+XML
         │
         ▼
PropertyStore populates basic properties (image geometry, date, etc.)
         │
         ▼
ComputedProperties enriches with catalog data, sky coordinates
         │
         ▼
Details pane displays 65 metadata fields
         │
         ▼
Background thread computes histogram + statistics
```

**Class Architecture:**

- `CXISFPropertyHandler` — COM class implementing IPropertyStore
- `XISFParser` — Parses XISF binary format and XML header (no external XML lib)
- `PropertyStore` — In-memory VARIANT dictionary keyed by PROPERTYKEY
- `ComputedProperties` — Pure function that derives properties from raw metadata
- `DSOCatalog` — In-memory DSO database loaded from CSV files
- `ConstellationDB` — RA/Dec to constellation mapping
- `PixelStatistics` — Streaming min/mean/max/median/clipping computation

### 2. PreviewHandler (XISFPreviewHandler.dll)

**Interfaces:**
- `IPreviewHandler` — Renders image in preview pane
- `IThumbnailProvider` — Generates thumbnails for Explorer view
- `IInitializeWithStream` — Receives XISF stream for decoding

**Responsibilities:**
- Decode XISF image data to RGB/grayscale
- Render preview image in Win32 window
- Generate thumbnails with optional overlay (histogram, object name)
- Cache rendered images for performance

**Design Decision:** Stream-based initialization allows Explorer to pass pre-opened handles without XISF parsing permission issues.

### 3. IFilter (XISFFilter.dll)

**Interfaces:**
- `IFilter` — Windows Search content indexing API
- `IInitializeWithStream` — Receives XISF stream for parsing

**Responsibilities:**
- Parse XISF metadata for Windows Search indexing
- Extract searchable properties: object names, coordinates, image properties
- Index catalog-enriched names if catalogs available
- Enable full-text search queries like "NGC 1234" or "M31"

**Design Decision:** Shares XISF parser with PropertyHandler but runs in `SearchIndexer.exe` process. Keeps indexing overhead minimal by skipping image data decoding.

### 4. Settings App (XISFShellExtensionHost.exe)

**Functions:**
- **Handler Toggles** — Enable/disable each handler via HKCU registry writes
- **Registry Management** — COM registration/unregistration of DLLs
- **Catalog Installer** — Download from pinned OpenNGC commit, verify SHA-256
- **Offline Import** — Import locally-managed catalog files

**Technology:** C++ WinUI 3 (or WPF/WinForms depending on version)

## Shared Utilities

### XISFParser

**Goal:** Extract metadata from XISF binary files without external dependencies (no XML libraries).

**Algorithm:**
1. Read XISF binary signature (8 bytes: `XISF0100`)
2. Read header length (UINT64 little-endian)
3. Seek and read XML header block
4. Parse XML manually with simple regex-style scanning
5. Extract `<FITSKeyword>`, `<Property>`, `<Image>` elements
6. Build O(1) lookup indices (keyword name → value, property id → value)

**Why Manual Parsing?** 
- Reduces binary footprint (no libxml, msxml dependencies)
- Handles malformed input gracefully
- Shell handler code must be fast and lean (runs in explorer.exe)
- Single-file header-only design aids testing and auditing

**Interface:**
```cpp
class XISFParser {
  static ParseResult ParseFile(const std::string& filePath);
  static ParseResult ParseXMLString(const std::string& xmlContent);
};

struct XISFRawMetadata {
  std::string xmlHeader;
  std::vector<FITSKeyword> fitsKeywords;
  std::vector<XISFProperty> properties;
  std::unordered_map<std::string, std::string> imageAttributes;
  uint32_t imageCount;
};
```

### DSOCatalog

**Goal:** Fast cone-search lookup of deep-sky objects by RA/Dec coordinates.

**Data Structure:** In-memory CSV loaded from `%LOCALAPPDATA%\XISFShellExtension\catalogs\NGC.csv` + addendum.

**Algorithm:**
- Load catalog CSV on first property handler use (lazy, thread-safe via `std::call_once`)
- Cache loaded catalog in process-wide singleton
- Cone search with configurable match tolerance (default 0.5°)
- Priority system: prefer catalog prefixes (NGC > IC > Sharpless) based on registry

**Why Separate from Indexer?**
- Catalog data is ~2 MB; keeping it process-local avoids repeated disk I/O
- Call-once pattern ensures single load per explorer.exe session
- Registry-driven priority allows users to prefer certain catalogs

## Property Flow and Design Patterns

### Feature Tiers

Metadata is enriched in tiers based on what's available:

1. **Basic** — Always available
   - File metadata: size, creation date, dimensions
   - FITS headers: OBJECT, EXPOSURE, CAMERA, etc. (as-is from file)
   - Image attributes: color space, bit depth

2. **Enriched** (if catalogs installed)
   - Object names from deep-sky catalogs
   - Constellation names
   - Sky coordinate bands

3. **Computed**
   - Pixel statistics (mean, median, min/max, clipping)
   - RA hour bands (from astrometry)
   - Derived properties (filter type from name, binning from dimensions)

### COM Lifetime

Each handler instance:
1. Receives IStream with XISF file
2. Parses metadata once (cached in member variables)
3. Populates property store on-demand (IPropertyStore::GetCount / GetAt)
4. Spawns background thread for expensive operations (histogram, pixel stats)
5. Destroyed when Explorer selection changes

**Design Principle:** Fail fast, keep resident memory < 1 MB per file, never block UI thread.

### Error Handling

- **Parse Errors** → Log via ETW, return empty/default values to Explorer
- **Missing Properties** → Gracefully omitted (not E_FAIL)
- **Catalog Load Failure** → Silently fall back to Basic tier
- **Corrupt Binary** → Log error, don't crash explorer.exe

See [Telemetry & ETW](../features/telemetry-etw.md) for detailed logging.

## Thread Safety

- **XISFParser** — Stateless, thread-safe (no state mutations)
- **DSOCatalog** — Singleton loaded once via `std::call_once`, read-only thereafter
- **PropertyStore** — Created per-handler-instance, not shared across threads
- **ETW Logging** — Thread-safe via Windows Event Tracing infrastructure

## Performance Constraints

| Component | Budget | Rationale |
|-----------|--------|-----------|
| XISF Parser | <50 ms | Runs on UI thread (Details pane open) |
| Catalog Lookup | <10 ms | Property retrieval must be instant |
| Histogram Rendering | <500 ms | Background thread, user can wait |
| Thumbnail Generation | <200 ms | Called for every file view; cancellable |

## Persistence

- **Catalogs** — `%LOCALAPPDATA%\XISFShellExtension\catalogs\{NGC,addendum,sharpless}.csv`
- **Settings** — `HKCU\Software\DennisPayne\XISF Shell Extension\{PropertyEnabled,PreviewEnabled,CatalogPriority,MatchToleranceDeg}`
- **Handler Registry** — `HKCU\Software\Classes\.xisf\…` (installed via COM registration)

## Security & Isolation

- **No Elevation** — All handlers and settings app run as current user
- **In-Process** — No out-of-proc COM servers (simpler, no IPC overhead)
- **Untrusted Input** — XISF files treated as untrusted; parser validates binary signature, header size limits
- **Catalog Verification** — SHA-256 pinning for GitHub-sourced catalogs; optional offline import with same hash check

## Design Decisions: Why This Architecture?

| Decision | Rationale |
|----------|-----------|
| Three separate handlers | Each has distinct lifetime and permission model; Property Handler runs on every file selection, PreviewHandler only when preview pane visible |
| IInitializeWithStream | Receivers file stream instead of file path; Explorer doesn't grant file-open permission via IShellExtInit alone |
| Manual XISF parser | No external XML library reduces footprint and attack surface; educational for contributors |
| Catalog singleton | Reduces load on Settings app; benefits all three handlers; thread-safe via call_once |
| Background histogram thread | Pixel statistics take 100s ms on large images; don't block UI thread |
| ETW over file logging | Windows-native, zero-overhead when not tracing; supports live debugger inspection |
| Feature tiers | Allows basic functionality offline; enriched tier only if user installs catalogs |

---

Related: [Contributing](contributing.md), [Property Handler Implementation](property-handler-impl.md), [Debugging](debugging.md)
