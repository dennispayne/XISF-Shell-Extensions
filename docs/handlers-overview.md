# Win11 XISF Shell Extension Overview

**Note:** This file has been migrated to the new documentation structure. Please see [Handlers Overview (User Guide)](user-guide/handlers-overview.md) for the updated version.

---

This repository packages the shippable shell-extension stages:

- **Property handler** (`IPropertyStore`) for Details pane + search indexing
- **Preview/thumbnail handler** (`IPreviewHandler`, `IThumbnailProvider`) for visual shell integration

## Architecture

```
Windows Explorer
  ├─ Details/Search  ── IPropertyStore ──────┐
  ├─ Preview Pane    ── IPreviewHandler ─────┼─> XISF handlers (COM DLLs)
  └─ Thumbnails      ── IThumbnailProvider ──┘
```

## Components

| Component | Folder | Primary output |
|-----------|--------|----------------|
| Property handler | `PropertyHandler/XISFPropertyHandler` | `XISFPropertyHandler.dll` |
| Property handler tests | `PropertyHandler/XISFPropertyHandlerTests` | Native unit test DLL |
| Preview/thumbnail handler | `PreviewHandler/XISFPreviewHandler` | `XISFPreviewHandler.dll` |
| Preview/thumbnail tests | `PreviewHandler/XISFPreviewHandlerTests` | Native unit test DLL |

## Solution

Use the combined solution:

`Win11-XISF-Shell-Extensions.sln`

## Supporting docs (migrated)

- [Preview Handler Deep Dive](features/preview-handler-deep-dive.md) - Technical details on thumbnail and preview implementation
- [Telemetry & ETW](features/telemetry-etw.md) - Event tracing documentation
- [Handlers Overview (User Guide)](user-guide/handlers-overview.md) - User-facing handler documentation
- [Property Mapping Reference](reference/property-mapping.md) - Complete property mapping reference
