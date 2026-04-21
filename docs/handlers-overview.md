# Win11 XISF Shell Extension Overview

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

`Win11-XISF-Shell-Extension.sln`

## Supporting docs

- [`preview-handler.md`](preview-handler.md)
- [`telemetry.md`](telemetry.md)
