# XISF Shell Extensions

[![CI](https://github.com/dennispayne/XISF-Shell-Extensions/actions/workflows/ci.yml/badge.svg)](https://github.com/dennispayne/XISF-Shell-Extensions/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

Native Windows shell extensions for [XISF](https://pixinsight.com/xisf/) — the
Extensible Image Serialization Format used by PixInsight and other
astrophotography tools. This project adds first-class File Explorer support
for `.xisf` files on Windows 10 / 11:

| Component             | What it does                                                        |
| --------------------- | ------------------------------------------------------------------- |
| `XISFPropertyHandler` | Details pane metadata for `.xisf` files                             |
| `XISFPreviewHandler`  | Preview pane renderer **and** Explorer thumbnail provider           |
| `XISFFilter`          | Windows Search content indexing (IFilter) for `.xisf` files         |

The Property Handler can also enrich metadata with deep-sky catalog names
(NGC / IC / Sharpless / etc.) when files carry OBJECT / OBJCTRA / OBJCTDEC
headers. The catalog data is **not** shipped inside the installer. Instead,
the bundled **XISF Shell Extension** settings app fetches it on demand from
a cryptographically-pinned [OpenNGC](https://github.com/mattiaverga/OpenNGC)
commit (SHA-256-verified), or you can import a local copy offline. See
[Catalogs](#catalogs) below.

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
`HKCU\Software\DennisPayne\XISF Shell Extension\{PropertyEnabled,PreviewEnabled,FilterEnabled}` (DWORD, default 1).

## Catalogs

The Property Handler looks for catalog files in
`%ProgramData%\DennisPayne\XISFShellExtension\catalogs\` and loads whatever is present
(`NGC.csv`, `addendum.csv`, `sharpless.csv`, `constellations.csv`). Missing
files are simply skipped (constellation lookups fall back to compiled-in data
when the CSV files are absent).

Two ways to install them:

1. **Online (install-time + on-demand).** The MSI creates the shared catalog
   directory but does **not** ship catalog seed files. Instead, the installer
   runs the settings host in `--silent-install` mode to download current
   catalogs after install, and the Settings app lets you refresh them later via
   the catalog list's **Update Catalogs** action. Downloads use HTTPS (TLS
   1.2+, cert validation enforced), hash while streaming, and only replace the
   destination after validation/transformation completes.
   - `NGC.csv`, `addendum.csv` — from OpenNGC (mattiaverga/OpenNGC)
   - `sharpless.csv`, `constellations.csv` — generated at download time from
     authoritative VizieR catalogs
2. **Offline / custom import.** Click **Import from File…** and pick any CSV.
   The file is copied into the machine-wide catalog directory by filename
   (including additional catalogs beyond the built-in set). Imported rows
   display source hash as `N/A`.

The **Copy Expected Hashes** button puts all pinned commit SHAs, URLs, and
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

- [`docs/handlers-overview.md`](docs/handlers-overview.md) — architecture and design.
- [`docs/preview-handler.md`](docs/preview-handler.md) — preview/thumbnail rendering.
- [`docs/telemetry.md`](docs/telemetry.md) — local-only ETW tracing.

## Versioning & release

This project follows [Semantic Versioning](https://semver.org/). The current
version lives in [`version.json`](version.json). CI produces an MSI for
every `v*.*.*` Git tag.

## License

MIT — see [LICENSE](LICENSE).

Third-party data acquired at runtime (not bundled):

- [OpenNGC](https://github.com/mattiaverga/OpenNGC) — CC BY-SA 4.0. Catalog
  files are downloaded from a pinned commit on user request and stored under
  `%ProgramData%\DennisPayne\XISFShellExtension\catalogs\`.
