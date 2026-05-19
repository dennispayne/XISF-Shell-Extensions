# Building

## Build Instructions

Complete step-by-step instructions for building XISF Shell Extensions from source, including all three handler DLLs, the settings app, and the MSI installer.

## Build Prerequisites

### System Requirements

- **Windows 10 build 19041** or later (Windows 11 recommended)
- **Visual Studio 2022** (community, professional, or enterprise editions)
  - Version 17.0 or newer
  - Includes MSBuild 17.0+
  - Install from [visualstudio.microsoft.com](https://visualstudio.microsoft.com)

### Visual Studio Workloads & Components

**Desktop development with C++** — Install these components:

- MSVC v143 (x64) — C++ 20 compiler
- Windows 10 SDK 10.0.26100.0 (or latest)
- CMake tools for Windows (optional)
- Windows Runtime C++ Template Library (WRL)

**Installation command** (PowerShell as admin):
```powershell
# Headless install (skip GUI)
vs_community.exe --add Microsoft.VisualStudio.Workload.NativeDesktop `
  --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
  --add Microsoft.VisualStudio.Component.Windows10SDK.26100 `
  --includeRecommended --passive
```

### .NET SDK (for WiX Installer)

- **.NET SDK 8.0 or newer**
- Download from [dotnet.microsoft.com](https://dotnet.microsoft.com)
- Required to build `Installer\XISFInstaller\XISFInstaller.wixproj`

**Verify installation:**
```powershell
dotnet --version    # Should show 8.0.x or later
```

### Optional: WiX Toolset

For IDE support when editing the MSI project:

- **HeatWave for Visual Studio** — Install from [FireGiant Marketplace](https://marketplace.visualstudio.com/items?itemName=FireGiant.FireGiantHeatWaveDev17)
- Adds WiX syntax highlighting, IntelliSense, project templates

Without HeatWave, you can still build via command-line `dotnet build` or `msbuild`.

### Development Tools (Optional)

- **PowerShell 7+** (recommended, but 5.1 works)
- **Git for Windows** (for version control)
- **Visual Studio Code** (for editing non-Visual Studio files)

## Environment Setup

### Clone the Repository

```powershell
git clone https://github.com/dennispayne/XISF-Shell-Extensions.git
cd XISF-Shell-Extensions
```

### Restore NuGet Packages

```powershell
# Manual restore (usually automatic)
nuget restore Win11-XISF-Shell-Extensions.sln

# Or via MSBuild
msbuild Win11-XISF-Shell-Extensions.sln /restore
```

### Verify Build Environment

```powershell
# Check MSBuild
msbuild /version

# Check .NET SDK
dotnet --version

# Check Windows SDK path
dir "C:\Program Files (x86)\Windows Kits\10\Include"
```

## Building from Source

### Build via Visual Studio (Recommended for Development)

1. **Open the solution:**
   ```powershell
   .\Win11-XISF-Shell-Extensions.sln
   ```

2. **Select build configuration:**
   - Platform: **x64** (required; x86 not supported)
   - Configuration: **Release** (or Debug for debugging)

3. **Build the solution:**
   - **F7** or **Build → Build Solution**
   - Or right-click solution → **Build Solution**

4. **Verify successful build:**
   - Output window should show "Build succeeded" with 0 errors
   - Artifacts appear in `x64\Release\` or `x64\Debug\`

### Build via Command Line (for CI/Automation)

**Full build with restore:**
```powershell
msbuild Win11-XISF-Shell-Extensions.sln `
  /restore `
  /p:Configuration=Release `
  /p:Platform=x64 `
  /p:RestoreLockedMode=true `
  /m /v:minimal /nologo
```

**Parameters:**
- `/restore` — Restore NuGet packages
- `/p:Configuration=Release` — Use Release configuration (or Debug)
- `/p:Platform=x64` — Target x64 architecture
- `/m` — Parallel build (multi-core)
- `/v:minimal` — Minimal verbosity (faster logging)
- `/nologo` — Suppress MSBuild banner

**Incremental build (faster on subsequent runs):**
```powershell
msbuild Win11-XISF-Shell-Extensions.sln /p:Configuration=Release /p:Platform=x64
```

## Build Targets and Outputs

### Build Artifacts

All binaries are placed in `x64\{Debug,Release}\`:

| Project | Output | Purpose |
|---------|--------|---------|
| **XISFPropertyHandler** | `XISFPropertyHandler.dll` | Details pane + Windows Search property handler |
| **XISFPreviewHandler** | `XISFPreviewHandler.dll` | Preview pane + thumbnail provider |
| **XISFFilter** | `XISFFilter.dll` | Windows Search IFilter (content indexing) |
| **ShellExtensionHost** | `XISFShellExtensionHost.exe` | Settings app (handler toggles, catalog installer) |
| **XISFInstaller** | `XISF.ShellExtensions_*_x64.msi` | Windows installer (`Installer\XISFInstaller\bin\Release\`) |

### Individual Project Builds

**Build a single project** (useful for faster iteration):

```powershell
# Property Handler only
msbuild PropertyHandler\XISFPropertyHandler\XISFPropertyHandler.vcxproj `
  /p:Configuration=Release /p:Platform=x64

# Preview Handler only
msbuild PreviewHandler\XISFPreviewHandler\XISFPreviewHandler.vcxproj `
  /p:Configuration=Release /p:Platform=x64

# Settings app only
msbuild ShellExtensionHost\ShellExtensionHost\ShellExtensionHost.vcxproj `
  /p:Configuration=Release /p:Platform=x64

# Installer (requires Visual Studio MSBuild / Build Tools with C++ workload)
msbuild Installer\XISFInstaller\XISFInstaller.wixproj /restore /p:Configuration=Release /p:Platform=x64 /m /v:minimal /nologo
```

### Build Configurations

| Configuration | Use Case | Optimization | Debugging |
|---------------|----------|--------------|-----------|
| **Debug** | Local development, debugging | None (slow) | Full symbols, no optimization |
| **Release** | Production, shipping | Full optimization | Stripped (PDB in `x64\Release\`) |

## Troubleshooting Builds

### "Windows SDK not found"

**Error:**
```
C:\Program Files\...\Microsoft.Cpp.Default.props(104): error MSB4019: The imported project
"C:\Program Files\...\Microsoft.Cpp.WindowsSDK.props" was not found.
```

**Solution:**
1. Open Visual Studio Installer
2. Modify your Visual Studio installation
3. Under "Desktop development with C++", ensure **Windows 10 SDK (latest)** is checked
4. Click Modify, then reboot

### "VCPKG not found" / Missing nuget packages

**Error:**
```
error : The following packages are missing and need to be restored: ...
```

**Solution:**
```powershell
# Restore NuGet packages
msbuild Win11-XISF-Shell-Extensions.sln /restore /p:RestoreLockedMode=true

# Or use nuget CLI
nuget restore Win11-XISF-Shell-Extensions.sln
```

### ".NET SDK 8.0 not installed"

**When building installer:**
```
error NETSDK1045: The .NET SDK being used is running on an unsupported version of .NET.
```

**Solution:**
```powershell
# Install .NET 8 SDK
winget install dotnet-sdk-8

# Or download from https://dotnet.microsoft.com/download/dotnet/8.0
```

### Linker errors ("unresolved external symbol")

**Cause:** Missing dependencies or wrong platform

**Debug checklist:**
- [ ] Platform is **x64** (not Win32/x86)
- [ ] Windows SDK version matches project settings
- [ ] All projects built (not just main handler)
- [ ] lib files in `x64\Release\` are recent (not stale)

**Clean and rebuild:**
```powershell
# Remove all build artifacts
rm -r x64 -Force

# Full clean rebuild
msbuild Win11-XISF-Shell-Extensions.sln /restore /t:Rebuild `
  /p:Configuration=Release /p:Platform=x64
```

### C++ Standard Version Issues

**Error:** `error C2039: 'byte': is not a member of 'std'`

**Cause:** C++20 not enabled

**Solution:**
1. Right-click project → Properties
2. C/C++ → Language Standard → `/std:c++latest` or `/std:c++20`
3. Rebuild

### "Application was unable to start correctly (0xc000007b)"

**Cause:** Wrong bitness (32-bit DLL in 64-bit explorer or vice versa)

**Solution:**
- Ensure **x64** platform selected (not Win32)
- Rebuild all components

### Build Performance

**Slow builds?** Try:

```powershell
# Parallel build (use N cores)
msbuild Win11-XISF-Shell-Extensions.sln /m:8 /p:Configuration=Release /p:Platform=x64

# Incremental only (skip unchanged projects)
msbuild Win11-XISF-Shell-Extensions.sln /p:Configuration=Release /p:Platform=x64

# Clean before build if stuck
msbuild /t:Clean /p:Configuration=Release /p:Platform=x64
msbuild /p:Configuration=Release /p:Platform=x64
```

## Building the Installer

The WiX v5 installer builds the final MSI package.

### Build MSI via Visual Studio

1. Right-click **XISFInstaller** project → **Build**
2. Output: `Installer\XISFInstaller\bin\Release\XISF.ShellExtensions_*_x64.msi`

### Build MSI via Command Line

```powershell
msbuild Installer\XISFInstaller\XISFInstaller.wixproj `
  /restore `
  /p:Configuration=Release `
  /p:Platform=x64 `
  /m /v:minimal /nologo
```

### Verify MSI

```powershell
# Check if MSI was created
dir Installer\XISFInstaller\bin\Release\*.msi

# Inspect MSI contents (requires Orca tool or 7-Zip)
# Orca download: https://docs.microsoft.com/en-us/windows/win32/msi/orca-exe
```

## Post-Build: Local Testing

### Register Handlers

**Via Settings App (recommended):**
```powershell
# Launch settings app (built into solution)
.\x64\Release\XISFShellExtensionHost.exe
```

Use the toggle buttons to:
- Enable/Disable Property Handler
- Enable/Disable Preview Handler
- Install/Update catalogs

**Via Command Line (admin required):**
```powershell
# Register individually
regsvr32 x64\Release\XISFPropertyHandler.dll
regsvr32 x64\Release\XISFPreviewHandler.dll
regsvr32 x64\Release\XISFFilter.dll

# Unregister
regsvr32 /u x64\Release\XISFPropertyHandler.dll
```

### Restart Explorer

```powershell
# Kill and restart File Explorer
Stop-Process -Name explorer -Force
Start-Process explorer
```

### Test with Sample XISF File

1. Download a sample `.xisf` file or create one with PixInsight
2. Open File Explorer, navigate to the file
3. **View → Details pane** — Should show XISF metadata
4. **View → Preview pane** — Should show image preview
5. **Right-click → Properties → Astro Details** — Custom property sheet tab

### Run Tests

```powershell
# Run all tests
vstest.console.exe `
  x64\Release\XISFPropertyHandlerTests.dll `
  x64\Release\XISFPreviewHandlerTests.dll `
  x64\Release\XISFFilterTests.dll

# Run with coverage
vstest.console.exe `
  x64\Release\XISFPropertyHandlerTests.dll `
  /Settings:CodeCoverage.runsettings `
  /ResultsDirectory:TestResults
```

## Continuous Integration (GitHub Actions)

Builds are automatically tested via `.github\workflows\ci.yml`:

- Runs on **Windows 2025 with VS 2026** (latest runner)
- Builds `Release|x64` configuration
- Runs all unit tests
- Downloads and verifies catalogs
- Runs performance benchmarks

**View CI results:** [Actions tab](https://github.com/dennispayne/XISF-Shell-Extensions/actions)

## Version Management

**Version source:** `version.json` (SemVer format)

**Automatic version substitution:**
- MSBuild reads `version.json` and passes `XISFVersion` to all projects
- Installer includes version in MSI filename
- Binaries contain version info resource

**To release a new version:**
1. Edit `version.json` with new SemVer
2. Commit to main
3. Tag commit: `git tag v1.2.3`
4. Push tag: `git push origin v1.2.3`
5. GitHub Actions automatically builds and releases MSI

---

Related: [Contributing](contributing.md), [Testing](testing.md), [Debugging](debugging.md)
