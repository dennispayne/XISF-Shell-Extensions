# XISF Shell Extensions

[![CI](https://github.com/dennispayne/XISF-Shell-Extensions/actions/workflows/ci.yml/badge.svg)](https://github.com/dennispayne/XISF-Shell-Extensions/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

Native Windows shell extensions for [XISF](https://pixinsight.com/xisf/) — the
Extensible Image Serialization Format used by PixInsight and other
astrophotography tools. This project adds first-class File Explorer support
for `.xisf` files on Windows 10 / 11, with deep-sky catalog integration,
pixel statistics, and sky coordinate resolution.

## Components

| Component             | Function                                          | Key Features                                              |
| --------------------- | ------------------------------------------------- | --------------------------------------------------------- |
| **PropertyHandler**   | Details pane metadata display                     | Metadata enrichment, sky coordinates, catalog names       |
| **PreviewHandler**    | Preview pane renderer & Explorer thumbnails       | Fast image rendering, inline histogram                    |
| **IFilter**           | Windows Search content indexing for `.xisf` files | Index object names, coordinates, image properties         |

The Property Handler enriches metadata with deep-sky catalog names (NGC / IC / 
Sharpless / etc.) when files carry OBJECT / OBJCTRA / OBJCTDEC headers. Catalog 
data is **not** shipped inside the installer; instead, the bundled **XISF Shell 
Extension** settings app fetches it on demand from a cryptographically-pinned 
[OpenNGC](https://github.com/mattiaverga/OpenNGC) commit (SHA-256-verified), or 
you can import locally offline.

## Install

### MSI installer (recommended)

Download `XISF.ShellExtensions_<ver>_x64.msi` from the [latest release](https://github.com/dennispayne/XISF-Shell-Extensions/releases/latest).
The MSI installs all handlers plus the settings app to
`C:\Program Files\XISF Shell Extensions\`. COM registration is handled
automatically during install (requires admin). After install, **restart
Explorer** (Task Manager → File Explorer → Restart) so the handlers load.

Open **Start → XISF Shell Extension Settings** to toggle individual handlers
or fetch the optional catalogs.

### Uninstall

From **Settings → Apps → Installed apps**, or via **Control Panel → Programs
and Features**, or:

```powershell
msiexec /x {product-code}
```

### Developer install (unpackaged)

For local development without the MSI, build the solution and use the
**XISF Shell Extension** settings app (`XISFShellExtensionHost.exe`) to
register/unregister handlers via the toggle buttons. See the
[Contributing](CONTRIBUTING.md) guide.

## Feature Highlights

### Deep-Sky Catalog Integration
Automatic object name and constellation resolution for frames with OBJECT, 
OBJCTRA, and OBJCTDEC headers. Supports NGC, IC, and Sharpless catalogs via 
OpenNGC. Cone search matches images to known sky objects with configurable 
search radius. Works online or offline — import catalogs locally for air-gapped 
systems.

### Pixel Statistics
Computed statistics directly from XISF image data: mean, median, minimum, 
maximum, and clipping analysis. Displayed in the Details pane — no external 
processing needed.

### Sky Coordinate Resolution
Automatic constellation mapping and RA/Dec band display for frames with 
astrometry. Understand image orientation and coverage at a glance from Explorer 
Details pane.

### Computed Properties
Derived metadata from XISF headers: observation date, filter, camera, telescope, 
binning, exposure time, and more. Pulled directly from file headers — always 
accurate.

### Windows Search Integration
Full-text indexing of image properties, object names, and catalog data. Search 
for "NGC 1234" or "M31" across your entire image library via Windows Search.

## Enabling / disabling handlers at runtime

Open the **XISF Shell Extension Settings** app from Start. The toggle buttons
control each handler independently:
- **Property Handler** — details pane file metadata
- **Preview/Thumbnail Handler** — preview pane and Explorer thumbnails
- **Search Filter** — Windows Search content indexing

Disabling a handler makes Explorer fall back to default behavior; the DLLs
stay installed. Changes apply to new handler activations — restart Explorer
to force a reload.

Under the hood, toggles write to:
`HKCU\Software\DennisPayne\XISF Shell Extension\{PropertyEnabled,PreviewEnabled}` (DWORD, default 1).

## Feature Tiers

**Basic** — Always available without additional setup:
- Standard XISF properties: image dimensions, date created, color space, file size
- Computed metadata from headers: camera, telescope, exposure time, binning
- Fast thumbnail generation in Explorer
- Windows Search indexing of file properties

**Enriched** — Unlocked by installing catalogs:
- Deep-sky catalog names (NGC, IC, Sharpless)
- Constellation mapping for RA/Dec coordinates
- Sky object cone search (find images of M31, NGC 224, etc. together)
- Search Windows for "Orion" and get all frames of objects in that constellation
- Full-text search for object names across your entire image library

Installing catalogs is optional and takes ~30 seconds. See [Catalogs](#catalogs) for details.

## Catalogs

Catalog files enable enriched metadata: deep-sky object names, constellation 
mapping, and full-text search. They live in
`%LOCALAPPDATA%\XISFShellExtension\catalogs\` and are loaded automatically if present 
(`NGC.csv`, `addendum.csv`, `sharpless.csv`). Missing files are simply skipped.

**Why catalogs matter:**
- Enriches your image library with discoverable metadata
- Essential for air-gapped systems (import once, use offline)
- Enables constellation-level search across thousands of frames
- Used by Property Handler and Windows Search IFilter

**Install catalogs:**

1. **Online (pinned + verified).** In the settings app, click
   **Install / Update from GitHub**. The app downloads each file from a
   specific OpenNGC commit SHA over HTTPS (TLS 1.2+, cert validation enforced),
   hashes the stream with SHA-256, and rejects any file whose hash does not
   match the compiled-in pin. The download is written to a temp file and only
   `MoveFileEx`-ed into place after verification.

2. **Offline / air-gapped.** Click **Import from File…**, pick a local
   `NGC.csv` or `addendum.csv`, and choose which pin it should match. The same
   SHA-256 check applies; mismatched files are rejected.

The **Copy Expected Hashes** button puts the pinned commit SHA, URLs, and
expected SHA-256 values on the clipboard so you can cross-verify them
independently on github.com before trusting a build.

## Build from source

See [CONTRIBUTING.md](CONTRIBUTING.md) for prerequisites and the build workflow.

## Repository layout

```
XISF-Shell-Extensions/
├── PropertyHandler/
│   ├── XISFPropertyHandler/         # DLL: details pane + Search
│   └── XISFPropertyHandlerTests/    # MS C++ unit tests
├── PreviewHandler/
│   ├── XISFPreviewHandler/          # DLL: preview pane + thumbnails
│   └── XISFPreviewHandlerTests/
├── Filter/
│   ├── XISFFilter/                  # DLL: Windows Search IFilter
│   └── XISFFilterTests/
├── PerformanceTests/                # Microbenchmarks
├── ShellExtensionHost/              # Settings app + catalog installer
├── Installer/
│   ├── XISFInstaller/               # WiX v5 MSI project
│   └── XISFInstallerTests/          # MSTest MSI validation tests
├── docs/                            # Design docs
├── build/                           # Shared MSBuild props (versioning)
├── version.json                     # SemVer source of truth
└── Win11-XISF-Shell-Extensions.sln
```

## Docs

For comprehensive documentation, see [`docs/index.md`](docs/index.md).

### Quick Links
- [Installation & setup](docs/installation-guide.md)
- [Getting started](docs/getting-started.md)
- [Troubleshooting](docs/reference/troubleshooting.md)
- [Architecture & design](docs/developer-guide/architecture.md)
- [Building from source](docs/developer-guide/building.md)
- [Property metadata reference](docs/user-guide/property-metadata.md)
- [Settings reference](docs/user-guide/settings-reference.md)

## Versioning & release

This project follows [Semantic Versioning](https://semver.org/). The current
version lives in [`version.json`](version.json). CI produces an MSI for
every `v*.*.*` Git tag.

## License

MIT — see [LICENSE](LICENSE).

Third-party data acquired at runtime (not bundled):

- [OpenNGC](https://github.com/mattiaverga/OpenNGC) — CC BY-SA 4.0. Catalog
  files are downloaded from a pinned commit on user request and stored under
  `%LOCALAPPDATA%\XISFShellExtension\catalogs\`.

