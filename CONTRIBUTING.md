# Contributing to XISF Shell Extension

Thanks for your interest in improving the project! This document outlines the
workflow for contributions.

## Prerequisites

- Windows 10 version 2004 (build 19041) or later — Windows 11 recommended.
- Visual Studio 2022 or newer with:
  - **Desktop development with C++** workload
  - Windows SDK **10.0.26100.0** (or latest)
  - MSVC v143 (or newer) toolset
- PowerShell 5.1 or PowerShell 7+.

## Building

1. Clone the repository.
2. Open `Win11-XISF-Shell-Extensions.sln` in Visual Studio.
3. Select `Release|x64` (or `Debug|x64`) and build the solution.

## Running tests

Tests use the Microsoft C++ Unit Test Framework. Run via Test Explorer in
Visual Studio, or from the command line:

```powershell
vstest.console.exe .\x64\Release\XISFPropertyHandlerTests.dll .\x64\Release\XISFPreviewHandlerTests.dll
```

## Registering handlers for local testing

For local development, launch `XISFShellExtensionHost.exe` from your build
output and use the handler toggle buttons to register/unregister and
enable/disable handlers.

## Building the MSI installer

The installer uses WiX v5 and requires the .NET 8 SDK plus Visual Studio
MSBuild (or Build Tools for Visual Studio) with the C++ workload. Install the
[HeatWave for Visual Studio](https://marketplace.visualstudio.com/items?itemName=FireGiant.FireGiantHeatWaveDev17)
extension to load the `.wixproj` in Solution Explorer with full IntelliSense.

```powershell
msbuild Installer\XISFInstaller\XISFInstaller.wixproj /restore /p:Configuration=Release /p:Platform=x64 /m /v:minimal /nologo
```

The MSI is placed in `Installer\XISFInstaller\bin\Release\`.

## Submitting changes

1. Open an issue first for non-trivial changes so we can discuss scope.
2. Create a feature branch off `main`.
3. Make focused commits with clear messages.
4. Ensure tests pass locally and add new tests when you change behavior.
5. Update `CHANGELOG.md` under `[Unreleased]`.
6. Open a pull request using the template.

## Coding conventions

- C++20, `/W3 /SDLCheck`, conformance mode on.
- Prefer stream-based initialization (`IInitializeWithStream`) for shell
  handlers over file/item initialization.
- Shell handler code runs in-process in `explorer.exe` / `prevhost.exe` —
  keep memory and startup cost minimal, fail fast, and never block.
- No new third-party dependencies without discussion.

## Release process

Releases are produced by the `release.yml` workflow when a `v*.*.*` tag is
pushed. The workflow builds the solution, runs tests, produces an MSI, and
creates a GitHub Release with the MSI and SHA256SUMS.
