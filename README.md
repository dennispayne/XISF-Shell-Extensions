# XISF Shell Extensions

[![CI](https://github.com/dennispayne/XISF-Shell-Extensions/actions/workflows/ci.yml/badge.svg)](https://github.com/dennispayne/XISF-Shell-Extensions/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

Native Windows shell extensions for [XISF](https://pixinsight.com/xisf/) — the
Extensible Image Serialization Format used by PixInsight and other
astrophotography tools. This project adds first-class File Explorer support
for `.xisf` files on Windows 10 / 11:

| Component             | What it does                                                        |
| --------------------- | ------------------------------------------------------------------- |
| `XISFPropertyHandler` | Details pane metadata and Windows Search indexing for `.xisf` files |
| `XISFPreviewHandler`  | Preview pane renderer **and** Explorer thumbnail provider           |

The Property Handler can also enrich metadata with deep-sky catalog names
(NGC / IC / Sharpless / etc.) when files carry OBJECT / OBJCTRA / OBJCTDEC
headers. The catalog data is **not** shipped inside the installer. Instead,
the bundled **XISF Shell Extension** settings app fetches it on demand from
a cryptographically-pinned [OpenNGC](https://github.com/mattiaverga/OpenNGC)
commit (SHA-256-verified), or you can import a local copy offline. See
[Catalogs](#catalogs) below.

## Install

### MSIX (recommended)

Download `XISF.ShellExtension_<ver>_x64.msix` from the [latest release](https://github.com/dennispayne/XISF-Shell-Extensions/releases/latest).
The single package contains both handlers plus the settings app; you choose
at runtime which handlers are active.

**First install (self-signed builds):** the 0.x releases are signed with a
self-signed certificate. Before sideloading, import the accompanying
`XISF-Shell-Extensions.cer` into `Local Machine → Trusted People`:

```powershell
# From an elevated PowerShell prompt
Import-Certificate -FilePath .\XISF-Shell-Extensions.cer `
                   -CertStoreLocation Cert:\LocalMachine\TrustedPeople
```

Then install the `.msix`:

```powershell
Add-AppxPackage .\XISF.ShellExtension_<ver>_x64.msix
```

After install, **restart Explorer** (Task Manager → File Explorer → Restart)
so the handlers load. Open **Start → XISF Shell Extension** to toggle
individual handlers or fetch the optional catalogs.

### Uninstall

From **Settings → Apps → Installed apps**, or:

```powershell
Get-AppxPackage *XISF* | Remove-AppxPackage
```

### Developer install (unpackaged)

For local development without MSIX, see `HelperScripts\Register-XISFHandler.ps1`
and the [Contributing](CONTRIBUTING.md) guide.

## Enabling / disabling handlers at runtime

Open the **XISF Shell Extension** app from Start. The two checkboxes toggle
the Property Handler and Preview/Thumbnail Handler independently. Disabling
a handler makes Explorer fall back to default behavior; the DLLs stay
installed. Changes apply to new handler activations — restart Explorer to
force a reload.

Under the hood, toggles write to:
`HKCU\Software\DennisPayne\XISF Shell Extension\{PropertyEnabled,PreviewEnabled}` (DWORD, default 1).

## Catalogs

The Property Handler looks for catalog files in
`%LOCALAPPDATA%\XISFShellExtension\catalogs\` and loads whatever is present
(`NGC.csv`, `addendum.csv`, `sharpless.csv`). Missing files are simply skipped.

Two ways to install them:

1. **Online (pinned + verified).** In the settings app, click
   **Install / Update from GitHub**. The app downloads each file from a
   specific OpenNGC commit SHA over HTTPS (TLS 1.2+, cert validation
   enforced), hashes the stream with SHA-256, and rejects any file whose
   hash does not match the compiled-in pin. The download is written to a
   temp file and only `MoveFileEx`-ed into place after verification.
2. **Offline / air-gapped.** Click **Import from File…**, pick a local
   `NGC.csv` or `addendum.csv`, and choose which pin it should match. The
   same SHA-256 check applies; mismatched files are rejected.

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
├── PerformanceTests/                # Microbenchmarks
├── ShellExtensionHost/              # Settings app + catalog installer
├── Packaging/
│   ├── XISFShellExtensions/          # MSIX manifest
│   └── Shared/Assets/
├── HelperScripts/                   # Dev scripts (regsvr32, catalogs)
├── docs/                            # Design docs
├── build/                           # Shared MSBuild props (versioning)
├── version.json                     # SemVer source of truth
└── Win11-XISF-Shell-Extensions.sln
```

## Docs

- [`docs/handlers-overview.md`](docs/handlers-overview.md) — architecture and design.
- [`docs/preview-handler.md`](docs/preview-handler.md) — preview/thumbnail rendering.
- [`docs/telemetry.md`](docs/telemetry.md) — local-only ETW tracing.
- [`docs/msix-packaging.md`](docs/msix-packaging.md) — MSIX build, catalog fetcher security model, release process.

## Versioning & release

This project follows [Semantic Versioning](https://semver.org/). The current
version lives in [`version.json`](version.json). CI produces a signed MSIX for
every `v*.*.*` Git tag.

## License

MIT — see [LICENSE](LICENSE).

Third-party data acquired at runtime (not bundled):

- [OpenNGC](https://github.com/mattiaverga/OpenNGC) — CC BY-SA 4.0. Catalog
  files are downloaded from a pinned commit on user request and stored under
  `%LOCALAPPDATA%\XISFShellExtension\catalogs\`.

