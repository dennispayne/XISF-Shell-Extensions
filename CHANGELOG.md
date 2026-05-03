# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- **Sharpless catalog** (`sharpless.csv`): 313 Sharpless 2 HII regions are now
  installable from the settings app. The file is hosted in this repo's `data/`
  directory, pinned to a specific commit SHA and SHA-256 hash.  Resolves #1.
- **Constellation boundary and name files** (`constellation_boundaries.csv`,
  `constellation_names.csv`): IAU boundary data and full constellation name
  mappings are now downloadable via the settings app. `ConstellationDB` loads
  them at runtime when present and falls back to compiled-in data otherwise.
  Resolves #2.
- Settings dialog shows five installable catalog rows (NGC.csv, addendum.csv,
  sharpless.csv, constellation_boundaries.csv, constellation_names.csv) each
  with independent Install/Update/Remove buttons.
- `CatalogSpec.h` gains `kSharpless`, `kConstellationBoundaries`,
  `kConstellationNames` entries and a `kProjectDataCommit` constant for the
  project-hosted data files.
- `ConstellationDB` gains `LoadBoundariesFromFile` and `LoadNamesFromFile`
  static methods; compiled-in data remains as a fallback.
- `kAllowedUrlPrefixes` (array of two) replaces the single `kAllowedUrlPrefix`
  to allow downloads from both OpenNGC and this project's own repository.

### Security
- URL allow-list expanded to two pinned `raw.githubusercontent.com` paths:
  `mattiaverga/OpenNGC/` and `dennispayne/XISF-Shell-Extensions/`.
  All three new files are pinned to a specific commit SHA and SHA-256 hash;
  mismatches are rejected and the candidate files are deleted.

## [0.1.0] - TBD

### Added
- Initial public release.
- `XISFPropertyHandler` — Windows Details pane and Search metadata provider for `.xisf` files.
- `XISFPreviewHandler` — Windows preview pane and thumbnail provider for `.xisf` files.
- `XISFShellExtensionHost` — settings app (Start → **XISF Shell Extension Settings**):
  - Toggle all three handlers independently at runtime
    via `HKCU\Software\DennisPayne\XISF Shell Extension\{PropertyEnabled,PreviewEnabled,FilterEnabled}`.
    Disabled handlers return `CLASS_E_CLASSNOTAVAILABLE` from `IClassFactory::CreateInstance`
    so Explorer falls back to default behavior.
  - Tri-state toggle buttons: Disable (registered+enabled), Enable (registered+disabled),
    Register (unregistered). Registration queues for Apply with UAC elevation.
  - Install or update OpenNGC catalogs on demand. Downloads are pinned to a specific
    OpenNGC commit SHA, fetched over HTTPS (TLS 1.2+), streamed through SHA-256, and
    atomically moved into place only after the hash matches the compiled-in pin.
  - Offline import of a local `NGC.csv` or `addendum.csv` against the same pin.
  - **Copy Expected Hashes** button for independent verification on github.com.
- `XISFFilter` — Windows Search IFilter for full-text indexing of `.xisf` metadata.
- MSI installer via WiX v5 (`Installer/XISFInstaller/`). Per-machine install to
  Program Files with automatic COM registration via regsvr32 custom actions.

### Changed
- OpenNGC / addendum / Sharpless catalogs are no longer embedded as RCDATA in
  `XISFPropertyHandler.dll`. They are loaded at runtime from
  `%LOCALAPPDATA%\XISFShellExtension\catalogs\`. The Property Handler degrades
  gracefully (no DSO enrichment) when the files are absent, keeping the shipped
  binary small and removing any redistribution of third-party catalog data.

### Security
- Catalog downloads pin an OpenNGC commit SHA and a SHA-256 hash per file;
  mismatches are rejected and the candidate file is deleted.
- Only HTTPS URLs under `https://raw.githubusercontent.com/mattiaverga/OpenNGC/`
  or `https://raw.githubusercontent.com/dennispayne/XISF-Shell-Extensions/` are
  accepted — enforced by a compiled-in allow-list.
- Per-file size caps enforced during streaming to limit DoS from a compromised CDN.
- TLS 1.2 / 1.3 only; no fallback to legacy protocols; full certificate validation.
- Atomic `MoveFileExW(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)` install;
  no partial files left on disk after a failure.
- Offline import uses the same SHA-256 verifier as the online path.

[Unreleased]: https://github.com/dennispayne/XISF-Shell-Extensions/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/dennispayne/XISF-Shell-Extensions/releases/tag/v0.1.0
