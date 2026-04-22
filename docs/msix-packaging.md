# MSIX Packaging

The repository ships a single MSIX per release:

| Package identity                  | Output name                          | Contents                                                                |
| --------------------------------- | ------------------------------------ | ----------------------------------------------------------------------- |
| `DennisPayne.XISF.ShellExtension` | `XISF.ShellExtension_<ver>_x64.msix` | Property DLL + Preview/Thumbnail DLL + `XISFShellExtensionHost.exe` settings app |

Users decide **at runtime** which handler(s) are active by toggling two
checkboxes in the settings app. Nothing about that choice is baked into the
package, so there is no separate "Property-only" or "Preview-only" SKU.

## Architecture

- Shell extensions are DLL-only COM servers. MSIX requires at least one
  `<Application>`, which is `XISFShellExtensionHost.exe` — a real Win32
  settings UI, not a stub. It launches from Start as **XISF Shell Extension**.
- Handler CLSIDs are registered via **Packaged COM** (`com:ComServer` +
  `com:SurrogateServer`) — no `regsvr32`, no admin.
- File-type associations and handler bindings use
  `uap:FileTypeAssociation` together with:
  - `desktop2:DesktopPropertyHandler`
  - `desktop2:DesktopPreviewHandler`
  - `desktop2:ThumbnailHandler`
- Minimum OS: **Windows 10, version 2004 (build 19041)**. MaxVersionTested is
  `10.0.26100.0` (Windows 11 24H2).
- Architecture: x64 only.

## Runtime handler toggle

Each handler's `IClassFactory::CreateInstance` reads a DWORD from
`HKCU\Software\DennisPayne\XISF Shell Extension`:

| Value name        | Gated handler(s)                              |
| ----------------- | --------------------------------------------- |
| `PropertyEnabled` | Property Handler                              |
| `PreviewEnabled`  | Preview Handler **and** Thumbnail Provider    |

When the value is `0`, `CreateInstance` returns `CLASS_E_CLASSNOTAVAILABLE`.
Explorer interprets this as "this CLSID is gone" and silently falls back to
its default behavior. Default is enabled (`1`). Changes take effect on the
next Explorer activation of the handler — restart Explorer to force a reload.

## Catalog acquisition & security model

Catalog files (`NGC.csv`, `addendum.csv`, `sharpless.csv`) are **not** shipped
inside the MSIX. They are installed on-demand by the settings app into
`%LOCALAPPDATA%\XISFShellExtension\catalogs\`. The Property Handler reads
from that directory; when a file is missing, DSO enrichment is simply
skipped.

The installer enforces every one of the following:

| Control                   | Where                                   | What it prevents                             |
| ------------------------- | --------------------------------------- | -------------------------------------------- |
| Commit-SHA URL pinning    | `CatalogSpec.h` `kOpenNGCCommit`        | Silent upstream content changes              |
| SHA-256 hash pinning      | `CatalogSpec.h` per-source              | Compromised CDN / upstream account           |
| URL host allow-list       | `kAllowedUrlPrefix` (compiled-in)       | Redirect / misconfig attacks                 |
| HTTPS-only + TLS 1.2/1.3  | `WinHttpSetOption`                      | Downgrade / MITM                             |
| Full cert validation      | (default, no ignore flags set)          | Invalid / self-signed server certs           |
| Per-file size cap         | `kNGC.maxBytes`, `kAddendum.maxBytes`   | DoS via oversized payload                    |
| Streaming hash            | `Sha256Hasher` over WinHTTP buffer      | Ever touching unverified bytes at rest       |
| Atomic install            | `MoveFileExW(REPLACE + WRITE_THROUGH)`  | Partial / torn files on crash                |
| Temp cleanup on mismatch  | `DeleteFileW` in failure paths          | Leaving rejected bytes on disk               |
| Offline import verified   | `InstallFromLocalFileVerified` (same hasher) | Trusting a file just because it was local |
| Per-user cache dir        | `%LOCALAPPDATA%` with inherited ACL     | Other users of the machine overwriting data  |
| Timing-safe hash compare  | `HexEquals` constant-time loop          | Oracle attacks (defense-in-depth only)       |

To **rotate to a newer OpenNGC snapshot**:

1. Pick a commit SHA on https://github.com/mattiaverga/OpenNGC.
2. Download each file at that commit, compute its SHA-256.
3. Update `kOpenNGCCommit`, `kOpenNGCCommitDate`, each `CatalogSource.url` and
   `expectedSha256` in `ShellExtensionHost/src/CatalogSpec.h`.
4. Add a `### Changed` entry to `CHANGELOG.md` documenting the rotation and
   the rationale (e.g. "add newly-discovered objects").
5. Bump `version.json` and release.

**Do not add mutable refs (branches, tags) to the allow-list.** Commit SHAs
are the only thing that provides the integrity guarantee.

## Versioning

The single source of truth is [`version.json`](../version.json) at the repo
root. MSIX requires a 4-part version, so CI composes it as:

```
<MAJOR>.<MINOR>.<PATCH>.<github.run_number>
```

For local builds the fourth component defaults to `0`. The version flows
through [`build/version.props`](../build/version.props) into both RC
resources (`FileVersion` / `ProductVersion`) and the MSIX Identity `Version`.

## Signing

MSIX packages **must** be signed by a cert whose Subject exactly matches the
manifest's `<Identity Publisher="..." />` attribute, which is hardcoded to:

```
CN=Dennis Payne
```

The release workflow supports two modes:

1. **Production cert (preferred)** — set these repository secrets:
   - `SIGNING_PFX_BASE64` — base64-encoded `.pfx`
   - `SIGNING_PFX_PASSWORD` — plaintext password
2. **Self-signed fallback** — if `SIGNING_PFX_BASE64` is unset, the workflow
   generates an ephemeral self-signed cert, signs with it, and attaches the
   `.cer` to the GitHub Release so users can trust it before sideloading.

Users installing self-signed builds must first import the `.cer` into
`Local Machine → Trusted People` — see the README for the exact command.

## Building locally

From a Developer PowerShell (or any PowerShell with the Windows 10/11 SDK on
PATH):

```powershell
# 1. Build the solution in Release|x64
msbuild Win11-XISF-Shell-Extensions.sln `
    /p:Configuration=Release /p:Platform=x64 `
    /p:XISFVersion=0.1.0 /p:XISFBuildNumber=0 /m

# 2. Produce unsigned MSIX in .\artifacts\
.\Packaging\build-msix.ps1 -Version 0.1.0 -BuildNumber 0

# 3. (Optional) sign with your own .pfx
.\Packaging\build-msix.ps1 -Version 0.1.0 -BuildNumber 0 `
    -CertificatePath .\my-cert.pfx `
    -CertificatePassword (Read-Host -AsSecureString)
```

## Installing the result

```powershell
# Trust the publisher certificate (self-signed builds only)
Import-Certificate -FilePath .\artifacts\XISF-Shell-Extensions.cer `
                   -CertStoreLocation Cert:\LocalMachine\TrustedPeople

Add-AppxPackage .\artifacts\XISF.ShellExtension_0.1.0.0_x64.msix

# Restart Explorer so the handlers load
Stop-Process -Name explorer -Force
```

Then launch **XISF Shell Extension** from Start to toggle handlers and
install catalogs.

## Release process

1. Update [`CHANGELOG.md`](../CHANGELOG.md) — move `[Unreleased]` entries
   under a new `[X.Y.Z]` section.
2. Bump `version.json` to `X.Y.Z`.
3. Commit and push to `main`.
4. Tag: `git tag vX.Y.Z && git push --tags`.
5. The `release.yml` workflow builds, signs, and creates the GitHub Release
   with `.msix`, `.cer` (if self-signed), and `SHA256SUMS.txt`.

## Files and layout

```
Packaging/
├── Shared/
│   └── Assets/                      # Square44/150, Wide310x150, StoreLogo PNGs
├── XISFShellExtensions/
│   └── Package.appxmanifest
└── build-msix.ps1                   # Staging + makeappx + signtool driver

ShellExtensionHost/src/
├── ShellExtensionHost.cpp           # Dialog proc + button handlers
├── ShellExtensionHost.rc            # VERSIONINFO + DIALOG resource
├── ShellExtensionHost.exe.manifest  # DPI-aware, common-controls v6
├── HostResources.h                  # Resource IDs
├── CatalogSpec.h                    # Pinned commit / URLs / SHA-256 / size caps
├── CatalogInstaller.{h,cpp}         # WinHTTP downloader + local-file import
├── Sha256.{h,cpp}                   # BCrypt SHA-256 wrapper
├── HostSettings.{h,cpp}             # HKCU handler-toggle read/write
└── Paths.{h,cpp}                    # %LOCALAPPDATA%\XISFShellExtension paths
```

The `build-msix.ps1` script patches the manifest's `<Identity Version="..." Publisher="..." />`
at pack time, so the committed file always says `0.1.0.0` / `CN=Dennis Payne`
as a fallback.

