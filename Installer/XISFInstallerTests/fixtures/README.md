# XISF Test Fixtures

Drop **real** `.xisf` files into this directory to give the
`HandlerFunctionalTests` suite high-fidelity inputs.

## How fixtures are picked up

`Fixtures.AllFixtures` enumerates every `*.xisf` file in this directory at
test-discovery time. Each fixture becomes its own row in Test Explorer
(via `[DynamicData]`), so a failing thumbnail render against `m42.xisf`
shows up as a distinct, individually-rerunnable test result.

A small **synthetic** XISF is always added to the list as a smoke-test
fallback, so the suite passes even if this directory is empty.

## What makes a good fixture

Aim for **breadth**, not size. A few small files exercising different
shapes are more valuable than one giant monolith:

- A monochrome narrowband frame (Ha / OIII / SII)
- An RGB / OSC stack
- A platesolved file (so `OBJCTRA` / `OBJCTDEC` are present)
- A long-exposure frame (so `EXPTIME` is non-trivial)
- A short flat / dark / bias if you want edge cases

## Size guidance

- Aim for **< 2 MB per file**. Crop / downsize in PixInsight before
  committing if needed (the tests do not validate image content beyond
  "thumbnail is non-null").
- Total fixture footprint should stay under ~10 MB so a fresh clone is
  not painful for non-test contributors.
- Anything larger than ~10 MB belongs in **Git LFS**. To enable, add to
  `.gitattributes` at the repo root:

  ```
  Installer/XISFInstallerTests/fixtures/*.xisf filter=lfs diff=lfs merge=lfs -text
  ```

  …and run `git lfs install` once before adding files.

## What the tests assert per fixture

| Test | Assertion |
|---|---|
| `PropertyHandler_IPropertyStore_ReturnsAtLeastOneProperty` | `IPropertyStore.GetCount() > 0` |
| `PropertyHandler_IPropertyStore_IncludesXisfFmtid` | At least one `PROPERTYKEY.fmtid` matches our XISF group |
| `Shell_GetDetailsOf_ReturnsPopulatedCustomColumns` | At least 5 populated columns + at least one `Astro *` column |
| `ThumbnailProvider_GetImage_ReturnsNonNullBitmap` | `IShellItemImageFactory.GetImage` returns a non-null `HBITMAP` |
| `Filter_LoadIFilter_ReturnsNonNullInstance` | `LoadIFilter` returns a non-null `IFilter` |

## Naming convention

Free-form. Files are referenced by basename in test output, so prefer
short descriptive names: `m42-rgb.xisf`, `ic1396-ha.xisf`,
`flat-ha.xisf`.

## Privacy / PII scrub

Real fixtures collected from your imaging rig embed **personal data**
in the XISF header — primarily:

- `Observation:Location:Latitude` / `Longitude` / `Elevation`
- `SITELAT` / `SITELONG` / `SITEELEV` FITS keywords
- `CAMERAID` (Windows USB hardware-instance-id of the camera)
- `<Property id="PixInsight:ProcessingHistory">` (stacks only — embeds
  full local file paths from the PixInsight pipeline)

Before committing **any new fixture**, run:

```pwsh
pwsh -File Installer/XISFInstallerTests/scrub-fixtures.ps1
```

The scrubber:

1. Zeros every Latitude/Longitude/Elevation value (Property + FITS).
2. Empties `CAMERAID`.
3. Strips the entire `PixInsight:ProcessingHistory` Property and all
   PixInsight-injected `COMMENT` FITSKeyword entries.
4. Removes any embedded `D:/Astro/...`-style local paths.
5. Pads the XML header back to its original byte length so the file's
   binary attachment offsets stay intact (no header-length recompute,
   no payload move).
6. Re-validates: file size unchanged, XML still parses, PII strings
   absent.

Standard FITS keywords (telescope, camera model, filter, exposure,
target RA/Dec, gain, temperature, etc.) are intentionally retained so
the property handler / IFilter tests have meaningful inputs.

