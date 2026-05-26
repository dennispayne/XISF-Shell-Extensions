# Contributing

## Contribution Guidelines

Thanks for your interest in improving XISF Shell Extensions! This document outlines the workflow for contributing code, reporting bugs, and proposing features.

## Code of Conduct

This project adheres to the [Contributor Covenant Code of Conduct](../../CODE_OF_CONDUCT.md). By participating, you are expected to uphold this code. Please report unacceptable behavior to the project maintainers.

## Getting Started as a Contributor

### Prerequisites

- **Windows 10 build 19041+** (Windows 11 recommended for best compatibility)
- **Visual Studio 2022** or newer with:
  - Desktop development with C++ workload
  - Windows SDK 10.0.26100.0 (or latest)
  - MSVC v143 (or newer) toolset
  - NuGet Package Manager (included)
- **PowerShell 5.1** or **PowerShell 7+** (for build scripts)
- **.NET SDK 8.0+** (for WiX installer builds)
- **Git** (for cloning and submitting changes)

### First Steps

1. **Fork the repository** on GitHub and clone locally:
   ```powershell
   git clone https://github.com/YOUR-USERNAME/XISF-Shell-Extensions.git
   cd XISF-Shell-Extensions
   ```

2. **Open the solution** in Visual Studio:
   ```powershell
   .\Win11-XISF-Shell-Extensions.sln
   ```

3. **Build to verify setup:**
   ```powershell
   msbuild Win11-XISF-Shell-Extensions.sln /p:Configuration=Release /p:Platform=x64 /m
   ```

4. **Run tests** to confirm everything works:
   ```powershell
   vstest.console.exe x64\Release\XISFPropertyHandlerTests.dll
   ```

## Development Environment Setup

### Building the Project

**Visual Studio (recommended for development):**
1. Open `Win11-XISF-Shell-Extensions.sln`
2. Select **Release | x64** from the platform/config dropdown
3. Right-click on the solution and select **Build Solution** (or F7)
4. Artifacts appear in `x64\Release\`

**Command-line builds:**
```powershell
# Full build with package restore
msbuild Win11-XISF-Shell-Extensions.sln `
  /restore `
  /p:Configuration=Release `
  /p:Platform=x64 `
  /m /v:minimal

# Incremental build
msbuild Win11-XISF-Shell-Extensions.sln /p:Configuration=Release /p:Platform=x64 /m
```

### Local Testing & Registration

**Via Settings App (recommended):**
1. Build the solution.
2. Launch `x64\Release\XISFShellExtensionHost.exe` (Settings app).
3. Use toggle buttons to register/unregister handlers.
4. Restart Explorer: **Ctrl+Shift+Esc → Find "Windows Explorer" → Restart**

**Manual Registration (advanced):**
```powershell
# Register Property Handler (admin required)
regsvr32 x64\Release\XISFPropertyHandler.dll

# Unregister
regsvr32 /u x64\Release\XISFPropertyHandler.dll
```

### Debugging a Single Component

**PropertyHandler (F5 debugging in Visual Studio):**
1. Right-click **XISFPropertyHandler** project → **Properties**
2. Debug → Debugger to launch: **Windows Explorer**
3. Set breakpoints in PropertyHandler code
4. F5 to start debugging with Explorer running
5. Navigate to any `.xisf` file in Explorer Details pane to hit breakpoints

**PreviewHandler:**
Same setup; breakpoints trigger when preview pane is opened on `.xisf` file.

**IFilter:**
Harder to debug interactively (runs in `SearchIndexer.exe`). Use ETW tracing instead:
```powershell
# See [Debugging Guide](debugging.md) for ETW setup
```

## Code Standards and Style

### C++ Style

- **C++20** with `/std:c++latest` or `/std:c++20`
- **Warnings:** `/W3 /SDLCheck` with conformance mode on (`/permissive-`)
- **Naming:** `CamelCaseForClasses`, `camelCaseForVariables`, `CONSTANT_NAMES`, `k_prefixForConstants`
- **Header guards:** `#pragma once` (preferred) or `#ifndef` guards
- **Includes:** STL and Windows headers grouped, relative includes after system includes

### Architecture

- **COM Objects:** Implement IUnknown reference counting; prefer `InterlockedIncrement/Decrement` for thread-safe refcounts
- **Initialization:** Use `IInitializeWithStream` for shell handlers (preferred over `IShellExtInit`)
- **No Blocking:** Handler code runs in-process in explorer.exe; never block UI thread
  - Use background threads for expensive operations (histogram, pixel stats)
  - Use async file I/O when possible
- **Memory:** Aim for <1 MB resident per handler instance
- **Error Handling:** Fail fast; log errors via ETW; return empty/sensible defaults rather than E_FAIL
- **Logging:** Use ETW TraceLogging; no file-based logs (see [Debugging Guide](debugging.md))

### New Dependencies

- **Avoid adding new third-party DLL dependencies** — every addition increases download size and maintenance burden
- **Standard Windows SDK allowed** — use Windows Runtime, WinRT, OleAut32, Propsys, etc.
- **STL encouraged** — std::string, std::vector, std::unordered_map, etc. are acceptable
- **Platform-specific APIs only** — no cross-platform abstractions needed

### Documentation

- **Code comments:** Only for non-obvious logic; avoid over-commenting
- **Function headers:** Include brief description and parameter meanings for public functions
- **Commit messages:** Detailed, clear, with "why" not just "what" (see [Commit Message Format](#commit-message-format))
- **README updates:** Reflect significant architectural changes

## Pull Request Process

### Before Opening a PR

1. **Check for existing issues/PRs** — Avoid duplicate work
   ```
   Search: https://github.com/dennispayne/XISF-Shell-Extensions/issues
   ```

2. **Open an issue for non-trivial changes** — Discuss scope and design before coding
   - Bug fixes: Describe reproduction steps
   - Features: Explain use case and proposed API
   - Refactoring: Explain why current code is problematic

3. **Create a feature branch** off `main`:
   ```powershell
   git fetch origin
   git checkout -b feature/my-awesome-feature origin/main
   ```

4. **Make focused commits** — Each commit should be a logical unit
   - Don't mix formatting/refactoring with feature changes
   - Don't include auto-generated files (build artifacts, etc.)

5. **Test locally before pushing:**
   ```powershell
   # Build
   msbuild Win11-XISF-Shell-Extensions.sln /p:Configuration=Release /p:Platform=x64 /m
   
   # Run all unit tests
   vstest.console.exe `
     x64\Release\XISFPropertyHandlerTests.dll `
     x64\Release\XISFPreviewHandlerTests.dll `
     x64\Release\XISFFilterTests.dll
   
   # Manual test: Register handlers via settings app and verify in Explorer
   ```

6. **Update CHANGELOG.md** under `[Unreleased]` section:
   ```markdown
   ## [Unreleased]
   
   ### Added
   - New feature description
   
   ### Fixed
   - Bug description
   ```

### Opening a PR

1. **Push your branch** to your fork:
   ```powershell
   git push origin feature/my-awesome-feature
   ```

2. **Open a PR on GitHub** using the template
   - Link the issue number if applicable: `Fixes #123`
   - Describe what changed and why
   - Include test results screenshot if GUI changes
   - Note any breaking changes

3. **PR checklist:**
   - ✅ All tests pass locally
   - ✅ New tests added for new functionality
   - ✅ Code follows style guide
   - ✅ CHANGELOG.md updated
   - ✅ No unrelated changes

### After Opening

- **Review feedback:** Maintainers will review within a few days
- **Address concerns:** Push additional commits; don't force-push (keeps review history)
- **Re-request review** if changes are made after review comments
- **Squash on merge:** Maintainers will squash into clean history

## Code Review Process

### Expectations

**We look for:**
- ✅ Correctness — Does it fix/add what it claims?
- ✅ Testing — Are there unit tests? Edge cases covered?
- ✅ Performance — Will this slow down Explorer? Handler load time?
- ✅ Compatibility — Does it work on Windows 10 and Windows 11?
- ✅ Security — Any unvalidated input from XISF files?

**We don't enforce:**
- Line length (within reason)
- Exact formatting (auto-formatter could be future improvement)
- Naming conventions if code is already consistent

### Tips for Reviewable PRs

- **Smaller is better** — Aim for <400 lines per PR if possible
- **Separate concerns** — Don't mix parser fixes with UI changes
- **Add comments where non-obvious** — Reviewers appreciate explaining "why"
- **Test edge cases** — Corrupt XISF files, missing properties, etc.

## Commit Message Format

**Format:**
```
[COMPONENT] Short description (50 chars max)

Longer explanation of why this change was needed. Mention any design decisions
or alternative approaches considered. Reference issue numbers with #123.

- Bullet point of significant changes
- Another change

Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>
```

**Components:**
- `[PropertyHandler]` — Metadata extraction, property store, catalog logic
- `[PreviewHandler]` — Image rendering, thumbnails
- `[IFilter]` — Windows Search integration
- `[Host]` — Settings app, COM registration
- `[Build]` — MSBuild, project files, installer
- `[Docs]` — Documentation
- `[Test]` — Unit/integration tests

**Example:**
```
[PropertyHandler] Support custom constellation database

Allow users to provide their own constellation definitions via
%ProgramData%\DennisPayne\XISFShellExtension\catalogs\constellations.csv.
If the file is missing, constellation lookups return empty values.

Fixes #142

- Load constellation CSV if present
- Return empty constellation fields if the catalog is absent or invalid
- Add ConstellationDB::LoadCustom() function
```

## Testing Requirements

- **New code must include tests** — Unit tests in corresponding `*Tests` project
- **Bug fixes should include regression tests** — Prevent re-introduction
- **Performance-critical code:** Profile before and after (use PerformanceTests project)
- **Handlers tested independently** — Mock IStream for isolated testing

**Run tests locally before pushing:**
```powershell
# Run all tests
vstest.console.exe `
  x64\Release\XISFPropertyHandlerTests.dll `
  x64\Release\XISFPreviewHandlerTests.dll `
  x64\Release\XISFFilterTests.dll `
  x64\Release\XISFInstallerTests.dll

# Run specific test
vstest.console.exe x64\Release\XISFPropertyHandlerTests.dll /Tests:TestName
```

## Release Process

Releases are automated via GitHub Actions when a `v*.*.*` Git tag is pushed:

```powershell
# Create release (maintainers only)
git tag v1.2.0
git push origin v1.2.0

# Workflow builds, tests, creates GitHub Release with:
# - MSI installer
# - SHA256SUMS file
# - Build artifacts
```

**Version source:** [version.json](../../version.json)

## Getting Help

- **Questions?** Open a [Discussion](https://github.com/dennispayne/XISF-Shell-Extensions/discussions)
- **Bug report?** [Open an Issue](https://github.com/dennispayne/XISF-Shell-Extensions/issues/new/choose)
- **Security issue?** Email dennispayne (at) hotmail.com or see [SECURITY.md](../../SECURITY.md)

## Useful Resources

- [Architecture Guide](architecture.md) — Component design and data flows
- [Building Guide](building.md) — Full build instructions
- [Debugging Guide](debugging.md) — Local debugging, ETW tracing
- [Testing Guide](testing.md) — Test organization and running tests
- [Property Handler Implementation](property-handler-impl.md) — Details on 65 properties

---

Related: [Architecture](architecture.md), [Building](building.md), [Testing](testing.md)
