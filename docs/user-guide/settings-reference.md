# Settings Reference: XISF Shell Extensions

This is the comprehensive user guide for the **Settings Application** (XISFShellExtensionHost.exe). The Settings app is your control center for managing XISF shell handlers, catalogs, and diagnostics.

## Quick Start

1. **Open the Settings App**: Launch `XISFShellExtensionHost.exe` from your Start menu or Applications folder
2. **Enable Handlers**: Toggle property, preview, and search handlers to add XISF support to Windows Explorer
3. **Install Catalogs**: Download or import NGC/IC/Sharpless object catalogs to enable object name resolution
4. **Verify**: Check your XISF files in Explorer — you should now see previews, properties, and search support

---

## Table of Contents

1. [Section 1: Handler Toggles](#section-1-handler-toggles)
2. [Section 2: Catalog Management](#section-2-catalog-management)
3. [Section 3: ETW Tracing](#section-3-etw-tracing)
4. [Section 4: Feature Tier Info](#section-4-feature-tier-info)
5. [Section 5: Troubleshooting](#section-5-troubleshooting)

---

## Section 1: Handler Toggles

### Overview

Three shell handlers extend Windows Explorer's capabilities for XISF files. Each handler can be independently enabled or disabled based on your needs.

> **Registry Location**: `HKEY_CURRENT_USER\Software\DennisPayne\XISF Shell Extension`

### Property Handler

**What It Does**  
The Property Handler populates Explorer's Details pane and Property dialog with XISF metadata. When you right-click an XISF file → Properties, you see fields like:
- Instrument & telescope information
- Observation date & time
- Focal length & resolution
- Object name (when catalogs are installed)
- Computed properties (Right Ascension, Declination, pixel statistics)

**Registry Location**  
- **Key**: `HKEY_CURRENT_USER\Software\DennisPayne\XISF Shell Extension`
- **Value**: `PropertyEnabled` (DWORD, 1=enabled, 0=disabled)

**Effect on Explorer**  
✅ **When enabled**: Details pane shows rich XISF metadata; Properties dialog is fully populated  
❌ **When disabled**: Falls back to generic file properties (size, date modified, etc.)

**When to Disable**  
- XISF properties are not needed for your workflow
- Performance troubleshooting (see [Troubleshooting](#section-5-troubleshooting))
- Testing handler behavior

**How to Toggle**  
1. Open Settings app
2. Look for the **Property Handler** toggle under "Handler Status"
3. Click the toggle button (shows "Enable" or "Disable")
4. Click **Apply** to save
5. If "Restart Explorer" checkbox is checked, Explorer will restart automatically

---

### Preview/Thumbnail Handler

**What It Does**  
Generates live previews in Explorer's preview pane and generates thumbnail icons for XISF files.

**Features**:
- **Preview Pane**: Select an XISF file in Explorer; the preview pane shows the image
- **Thumbnails**: When viewing folders, XISF thumbnails appear in list and icon views
- **Quick Look** (Windows 11+): Press Space on an XISF file for instant preview

**Registry Location**  
- **Key**: `HKEY_CURRENT_USER\Software\DennisPayne\XISF Shell Extension`
- **Value**: `PreviewEnabled` (DWORD, 1=enabled, 0=disabled)

**Effect on Explorer**  
✅ **When enabled**: XISF thumbnails and previews appear instantly  
❌ **When disabled**: Generic document icon appears; preview pane shows "No preview available"

**When to Disable**  
- Performance is impacted (thumbnail generation is CPU/GPU intensive)
- Network storage or slow drives (thumbnails cache locally, but first-access may be slow)
- Troubleshooting handler conflicts

**How to Toggle**  
1. Open Settings app
2. Look for **Preview/Thumbnail Handler** under "Handler Status"
3. Click the toggle button
4. Click **Apply**
5. Optionally restart Explorer via the checkbox

---

### Search Filter Handler

**What It Does**  
Enables Windows Search to index XISF file properties, allowing you to search for files by:
- Telescope or instrument name
- Observation date
- Object name (constellation, NGC/IC/Sharpless catalog number)
- Focal length, sensor resolution, filter

**Registry Location**  
- **Key**: `HKEY_CURRENT_USER\Software\DennisPayne\XISF Shell Extension`
- **Value**: `FilterEnabled` (DWORD, 1=enabled, 0=disabled)

**Effect on Explorer**  
✅ **When enabled**: Windows Search indexes XISF properties; search results include matches on metadata  
❌ **When disabled**: XISF search only matches filename; properties are not indexed

**When to Disable**  
- Search indexing is slowing down disk performance on older/slower drives
- You don't use Windows Search for XISF files
- Troubleshooting storage indexing issues

**How to Toggle**  
1. Open Settings app
2. Look for **Search Filter Handler** under "Handler Status"
3. Click the toggle button
4. Click **Apply**
5. **Note**: After enabling, Windows Search will re-index the system. This may take several minutes depending on disk size and XISF file count.

> **Tip**: After enabling the Search Filter, go to Settings → Search → Indexing Options to verify that your XISF folders are included in the index.

---

### Default State & Registry Keys

All handlers are **enabled by default**. If the registry key does not exist, the handler assumes enabled.

| Handler | Registry Value Name | Default | Type |
|---------|-------------------|---------|------|
| Property | `PropertyEnabled` | 1 (enabled) | DWORD |
| Preview | `PreviewEnabled` | 1 (enabled) | DWORD |
| Filter | `FilterEnabled` | 1 (enabled) | DWORD |

**Viewing Registry Values**  
1. Open Registry Editor (`regedit.exe`)
2. Navigate to: `Computer\HKEY_CURRENT_USER\Software\DennisPayne\XISF Shell Extension`
3. You should see the three `*Enabled` entries

---

### Restarting Explorer

Some changes (especially Preview/Thumbnail handler) take effect immediately, but others require an Explorer restart for full effect.

**Manual Restart**:
1. In Settings app, check **"Restart Explorer on apply"** checkbox
2. Click **Apply**
3. Settings app will:
   - Kill all Explorer processes
   - Write registry changes
   - Restart Explorer automatically

**Manual Method** (if Settings app is unavailable):
1. Right-click taskbar
2. Select "Task Manager"
3. Find "Windows Explorer" in the list
4. Click it and select "Restart"

> ⚠️ **Warning**: Restarting Explorer will close all open file browser windows. Save any work before applying.

---

## Section 2: Catalog Management

### Overview

XISF Shell Extensions uses **object name catalogs** to enhance XISF metadata. Catalogs map image coordinates and properties to known astronomical objects.

**What Catalogs Do**:
- Enable constellation mapping (identify which constellation appears in the image)
- Resolve object names (NGC 3603, Betelgeuse, etc.)
- Enable full-text search across catalog entries
- Support computed property filtering

**Install Location**  
Catalogs are stored in: `%ProgramData%\DennisPayne\XISFShellExtension\catalogs\`

(Windows expands `%ProgramData%` to `C:\ProgramData`)

---

### Available Catalogs

#### NGC.csv (OpenNGC Database)

**Content**: Primary catalog of ~14,000 deep-sky objects (galaxies, nebulae, star clusters)

**What It Enables**:
- NGC and IC designations (NGC 253, IC 342, etc.)
- Object type classification (galaxy, open cluster, planetary nebula, etc.)
- Common names (Andromeda Galaxy, Crab Nebula, etc.)
- Magnitude and size estimates

**File Size**: ~8 MB  
**Format**: CSV (Comma-Separated Values)  
**Source**: [OpenNGC Project](https://github.com/mattiaverga/OpenNGC) on GitHub

#### addendum.csv (OpenNGC Addendum)

**Content**: Supplementary objects not in the main NGC catalog (~150 additional objects)

**What It Enables**:
- Extends NGC coverage with additional Sharpless objects and Caldwell catalog entries
- Object properties and alternate names

**File Size**: ~1 MB  
**Format**: CSV  
**Source**: OpenNGC addendum on GitHub

#### sharpless.csv

**Content**: Sharpless 2 HII-region catalog.

**What It Enables**:
- Additional emission-nebula name matches (Sh2 designations)
- Better object-name coverage for nebula-heavy targets

#### constellations.csv

**Content**: IAU constellation boundary catalog used for constellation resolution.

**What It Enables**:
- Constellation mapping from RA/Dec coordinates
- Expanded keyword/search enrichment with constellation names

---

### Online Installation (Download from Pinned Sources)

#### Step 1: Open Catalog Management

1. Launch Settings app
2. Look for the **"Catalog Management"** section
3. You should see four catalog rows: **"OpenNGC NGC.csv"**, **"OpenNGC addendum.csv"**, **"Sharpless sharpless.csv"**, and **"IAU Constellations constellations.csv"**

#### Step 2: Download and Verify

Click **"Download"** (or **"Update"**) next to the catalog name.

**Behind the Scenes**:
- Settings app downloads each catalog from its pinned source URL
- **SHA-256 cryptographic hash** is computed while downloading
- Hash is compared against pinned hash compiled into the app
- If hash matches: file is saved; if mismatch: file is deleted and error shown
- Process uses TLS (HTTPS) for secure transfer

**Why Hash Verification?**  
The hash ensures:
- File integrity (no corruption during download)
- Authenticity (attacker cannot substitute a different file without detection)
- Pinned hash is cryptographically tied to a specific GitHub commit, making it impossible to serve outdated versions

#### Step 3: Progress and Errors

**During Download**:
- Progress bar shows bytes transferred and estimated time
- Click **"Cancel"** to abort (temp file is deleted)

**Success**:
- Status changes to ✓ Verified
- Icon turns green
- File is ready for use (effects appear within seconds in Explorer)

**Error Conditions** (Cause → Solution):
- **"HTTP connection failed"** → Check internet connectivity
- **"SHA-256 mismatch"** → Download failed or corrupted file; retry or check disk space
- **"File size exceeded"** → Catalog was larger than expected (possible attack); try again
- **"URL not allowed"** → Security policy prevents download from this host (should not occur for built-in catalog sources)

> **Tip**: Save the `.etl` trace files from failed downloads if reporting a bug (see [ETW Tracing](#section-3-etw-tracing)).

---

### Offline Installation (Local File Import)

Use this method when you have a pre-downloaded catalog file or your system lacks internet access.

#### Step 1: Obtain Catalog File

Get a copy of the catalog file (e.g., `NGC.csv`) from:
- Your organization's internal repository
- A portable drive from another system
- Manual download from [OpenNGC on GitHub](https://github.com/mattiaverga/OpenNGC)

> ⚠️ **Important**: Offline import applies **the same SHA-256 verification** as online download. The file must match the pinned hash, ensuring you have the exact known-good version.

#### Step 2: Open File Picker

1. In Settings app, find the catalog you want to import
2. Look for **"Import from File"** or **"Browse..."** button
3. Click it to open the file picker

#### Step 3: Select and Verify

1. Navigate to the catalog file
2. Click **"Open"**
3. Settings app will:
   - Read the file
   - Compute SHA-256 hash
   - Compare against pinned hash
   - If match: copy to `%ProgramData%\DennisPayne\XISFShellExtension\catalogs\` and mark as verified
   - If mismatch: reject and show error

> **Offline Verification**: The same security model applies offline. If your file doesn't match the pinned hash, the import fails to prevent corrupted or outdated data from being used.

---

### Verifying Installation Success

After installing a catalog, verify it's active:

#### Check 1: File Exists

1. Open File Explorer
2. Type in address bar: `%ProgramData%\DennisPayne\XISFShellExtension\catalogs\`
3. You should see:
   - `NGC.csv` (~8 MB)
   - `addendum.csv` (~1 MB)
   - `sharpless.csv`
   - `constellations.csv`

#### Check 2: Settings App Status

In Settings app, the status icon next to each catalog should show:
- ✓ Verified (green) — file exists and hash is correct
- ⚠ Unknown (yellow) — file exists but hasn't been hashed yet (rare)
- ✗ Missing (red) — file not found

#### Check 3: XISF Properties Show Object Names

1. Open an XISF file in Windows Explorer
2. Select the file and view the Details pane (or right-click → Properties)
3. Look for a property like "Object Name" or "Constellation"
4. If the field contains a value (not empty), catalogs are active

---

### Re-downloading / Re-importing Catalogs

If a catalog becomes corrupted or outdated:

1. In Settings app, find the catalog entry
2. Click **"Download"** again (or **"Import from File"**)
3. Settings app will re-download and verify, replacing the old file

**To Remove a Catalog**:
1. Open File Explorer
2. Navigate to `%ProgramData%\DennisPayne\XISFShellExtension\catalogs\`
3. Delete the `.csv` file
4. In Settings app, status will change to ✗ Missing

> **Note**: Removing a catalog disables constellation mapping and object name resolution but doesn't affect other XISF properties.

---

### Features Enabled by Catalogs

| Feature | Requires | Effect |
|---------|----------|--------|
| **Constellation Mapping** | NGC.csv | XISF properties include constellation (e.g., "Orion") |
| **Object Name Resolution** | NGC.csv, addendum.csv | XISF properties include NGC/IC/Sharpless object names |
| **Full-Text Search** | NGC.csv, addendum.csv | Windows Search can find objects by name (e.g., "Andromeda") |
| **Computed Properties** | All handlers active | Right Ascension, Declination, pixel statistics |

---

## Section 3: ETW Tracing

### What Tracing Is For

ETW (Event Tracing for Windows) traces capture detailed diagnostic information about handler operations. Use traces to:

- **Troubleshoot failures**: Why doesn't an XISF file show a thumbnail?
- **Diagnose performance**: Is property extraction slow? Where does time go?
- **Capture detailed logs**: For bug reports to developers
- **Analyze handler behavior**: What properties were extracted? Were catalogs checked?

**Privacy**: Traces collect file metadata only (paths, sizes, timestamps). No file contents are captured.

---

### Starting a Trace

1. In Settings app, find the **"ETW Tracing"** section
2. Click **"Start Trace"**
3. A timer begins counting seconds
4. Reproduce the scenario:
   - Open an XISF file in Explorer
   - View its Details pane
   - Thumbnail hover over file
   - Search for XISF files

5. After reproducing, click **"Stop Trace"**
6. Settings app creates an ETL (ETW Log) file in:
   - `%TEMP%\xisf\xisf-YYYYMMDD-HHMMSS.etl`

---

### Stopping a Trace and Exporting

1. Click **"Stop Trace"** button
2. Trace session stops immediately
3. Settings app shows:
   - **Stop Time** (when trace ended)
   - **Trace File Location** (path to .etl file)

#### Viewing the Trace

After stopping, you can:

**Option A: Open in Windows Performance Analyzer (WPA)**
- WPA is included with Windows Performance Toolkit
- Click **"Open Trace"** button in Settings app (if available)
- WPA opens the .etl file
- Add "Generic Events" table
- Filter by provider name "XISF-PropertyHandler" or "XISF-PreviewHandler"

**Option B: Export to XML**
- Click **"Export to XML"** button
- Settings app runs `tracerpt` to convert ETL → XML
- Resulting file is easier to share and search

**Option C: Analyze with logman**
- Advanced command-line analysis (see below)

---

### Advanced Analysis with logman

For developers or advanced users:

```powershell
# View active trace sessions
logman query -ets

# If you need to manually capture a trace instead of using the Settings UI:
logman create trace XISFTrace -o C:\Temp\xisf.etl -ets
logman update trace XISFTrace -p "{6F6B0C9D-6B76-5A24-BC3D-708314E96F2B}" 0xFFFF 0x04 -ets
# (Property handler GUID)

# Do your scenario …

logman stop XISFTrace -ets

# Parse the trace to CSV
tracerpt C:\Temp\xisf.etl -o C:\Temp\xisf.csv -of CSV
```

---

### Collecting Traces for Bug Reports

When submitting a bug report:

1. **Reproduce the issue** with tracing enabled:
   - Start Trace → Reproduce issue → Stop Trace
2. **Collect the trace file**:
   - Location: `%TEMP%\xisf\xisf-*.etl`
   - Copy to a shared location or cloud storage
3. **Export to XML** (optional, makes file more readable):
   - Click "Export to XML" button
   - Attach both .etl and .xml files

4. **Include in bug report**:
   - Attach trace file(s)
   - Describe: what happened, what you expected, what XISF files you used
   - Include Settings app version (shown in Settings UI)
   - Include Windows version (Settings → System → About)

---

### Privacy & Data Collection

**What Is Collected**:
- Handler lifecycle events (initialize, unload)
- XISF file metadata (path, file size, header info)
- Catalog lookup results
- Timing measurements
- Error codes and failure stages

**What Is NOT Collected**:
- File contents (pixel data, image data, scientific arrays)
- User data outside the XISF files being analyzed
- Any information sent outside your local machine

**Retention**: Trace files are stored locally in `%TEMP%`. Windows cleans up temporary files periodically (usually after 30 days). You can manually delete trace files anytime.

---

## Section 4: Feature Tier Info

### Feature Tiers

The extension has three feature tiers that determine which capabilities are available. Tiers affect:
- Which properties are extracted from XISF headers
- Availability of computed properties (Right Ascension, Declination)
- Search indexing depth

#### Basic Tier (0)

**Default for first-time install**

**What You Get**:
- Property extraction: instrument, telescope, observation date/time
- Basic XISF metadata display
- Thumbnail preview (image only, no annotations)
- File indexing by filename

**No Support For**:
- Constellation mapping
- Object name resolution
- Computed properties (RA/Dec)
- Advanced search

#### Standard Tier (1)

**What You Get**:
- Everything in Basic +
- Constellation mapping (when NGC.csv is installed)
- Object name resolution (when catalogs installed)
- Full-text search on object names

**No Support For**:
- Computed properties (RA/Dec coordinate calculations)

#### Full Tier (2) – **Recommended**

**What You Get**:
- Everything in Standard +
- Computed properties (Right Ascension, Declination)
- Pixel statistics (width, height, color depth)
- All search capabilities
- Full metadata population

**This is the default tier and recommended for all users.**

---

### How to See Current Tier

1. Open Settings app
2. Look for **"Feature Tier"** indicator (usually in advanced section)
3. Shows current tier: "Basic", "Standard", or "Full"

---

### Viewing Tier in Explorer

To see which tier is active, check what properties appear when you select an XISF file:

**Basic Tier** shows:
- Instrument
- Telescope
- Observation date/time

**Standard Tier** shows:
- Above +
- Constellation
- Object Name

**Full Tier** shows:
- Above +
- Right Ascension
- Declination
- Image Width & Height
- Color Depth

---

### How Tier Affects Search

**Basic**: Search matches filename only

**Standard**: Search matches filename + constellation, object name

**Full**: Search matches all fields including coordinates, pixel dimensions

> **Tip**: If your XISF files aren't appearing in Windows Search despite having catalogs installed, check that you're on at least Standard tier.

---

## Section 5: Troubleshooting

### Settings App Won't Open

**Symptom**: Clicking the app icon does nothing, or app crashes immediately

**Diagnosis**:

1. Check if another instance is running:
   ```powershell
   Get-Process XISFShellExtensionHost -ErrorAction SilentlyContinue
   ```

2. If running, kill it:
   ```powershell
   Stop-Process -Name XISFShellExtensionHost -Force
   ```

3. Try opening again

**If Still Won't Open**:

1. Open Registry Editor
2. Navigate to: `HKEY_CURRENT_USER\Software\DennisPayne\XISF Shell Extension`
3. Delete the entire key (this resets to defaults)
4. Try opening Settings app again
5. Re-enable handlers and re-install catalogs if needed

---

### Handlers Don't Appear in Explorer (After Enabling)

**Symptom**: Toggled handlers on in Settings, clicked Apply, but XISF properties/preview still don't show in Explorer

**Common Cause**: Explorer is caching old handler state

**Solution 1: Restart Explorer**

1. In Settings app, check **"Restart Explorer on apply"** checkbox
2. Click Apply
3. This terminates and restarts all Explorer windows

**Solution 2: Manual Registry Restart**

1. Open PowerShell as Administrator
2. Kill Explorer:
   ```powershell
   Stop-Process -Name explorer -Force
   ```
3. Wait 2 seconds
4. Restart Explorer:
   ```powershell
   & "C:\Windows\explorer.exe"
   ```

**Solution 3: Verify Handler Registration**

1. Open Registry Editor
2. Navigate to: `HKEY_CLASSES_ROOT\CLSID\`
3. Look for these CLSIDs:
   - `{7C54FA8B-9D63-4C10-8FBE-1A5A0F9A3B2E}` (Property Handler)
   - `{9C76E8AD-4E85-5F30-B00D-3C7D1AB5F6E0}` (Preview Handler)
   - `{B4E7F2A1-3D8C-4F5E-9A1B-6C2D8E4F7A3B}` (Search Filter)

4. If missing, handlers may not be registered. See "Handlers Still Missing" below.

---

### Handlers Still Don't Appear (Not Registered)

**Symptom**: Handler CLSIDs don't exist in registry, or Settings app shows "Register" button instead of "Enable/Disable"

**Cause**: Handlers DLL files are missing or not registered

**Solution 1: Register via Settings App**

1. Open Settings app
2. For each missing handler, click **"Register"** button
3. If no "Register" button appears, handlers may be corrupted

**Solution 2: Re-Register Handlers (Manual)**

1. Open PowerShell as Administrator
2. Run:
   ```powershell
   cd "C:\Program Files\WindowsApps\DennisPayne.XISFShellExtensions_*\x64\"
   .\XISFPropertyHandler.exe /register
   ```

3. Restart Explorer (as shown above)

---

### Catalog Download Fails

**Symptom**: "Download" button shows error like "HTTP connection failed" or "SHA-256 mismatch"

**Check 1: Internet Connectivity**

1. Open Command Prompt
2. Run: `ping raw.githubusercontent.com`
3. If fails, check your internet connection and firewall

**Check 2: Disk Space**

1. Open File Explorer
2. Right-click `%ProgramData%\DennisPayne\XISFShellExtension\catalogs\`
3. Check available space (need ~10 MB free minimum)
4. If low, free up disk space and retry

**Check 3: Try Again**

1. Click **"Download"** again (network glitches happen)
2. If persistent, try again in a few minutes (GitHub may be having issues)

**Check 4: Offline Import**

If downloads consistently fail:

1. Download catalog manually from [OpenNGC on GitHub](https://github.com/mattiaverga/OpenNGC/tree/36cb178a0f69dba8bfc03a99c10512831edf1c6b/database_files)
2. Copy `NGC.csv` and `addendum.csv` to a USB drive
3. Use "Import from File" in Settings app
4. Offline import applies same SHA-256 verification

---

### ETW Tracing Not Working

**Symptom**: "Start Trace" button does nothing, or trace file is empty after stopping

**Check 1: Administrator Privileges**

ETW requires elevated privileges. Run Settings app as Administrator:

1. Right-click XISFShellExtensionHost.exe
2. Select "Run as Administrator"
3. Try tracing again

**Check 2: Check %TEMP% Directory**

1. Open File Explorer
2. Type in address bar: `%TEMP%\xisf\`
3. If directory doesn't exist or is empty, tracing isn't working

**Solution**:
1. Create directory manually: `mkdir %TEMP%\xisf`
2. Try tracing again
3. If still empty, check Windows Event Log for errors

**Check 3: Verify Logman is Available**

1. Open Command Prompt
2. Run: `logman query -ets`
3. If command not found, ETW tools may not be installed

---

### How to Collect Diagnostic Info

When reporting a bug, collect:

1. **ETW Trace** (see [ETW Tracing](#section-3-etw-tracing)):
   - Start Trace
   - Reproduce issue
   - Stop Trace
   - Save .etl file

2. **Registry Export**:
   - Open Registry Editor
   - Navigate to: `HKEY_CURRENT_USER\Software\DennisPayne\XISF Shell Extension`
   - Right-click → Export
   - Save as `.reg` file

3. **Settings Screenshot**:
   - Open Settings app
   - Take screenshot of all visible sections
   - Include handler toggle states and catalog status

4. **System Info**:
   - Open Settings → System → About
   - Note Windows version, build, and architecture (32-bit or 64-bit)
   - Include in bug report

---

### Support & Bug Reports

**Report Bugs**:

1. Go to [XISF Shell Extensions GitHub Issues](https://github.com/dennispayne/XISF-Shell-Extensions/issues)
2. Click "New Issue"
3. Provide:
   - Clear reproduction steps
   - Expected vs. actual behavior
   - Diagnostic info from above (trace, registry, screenshots)
   - System info (Windows version, architecture)

**Ask Questions**:

1. Check [existing discussions](https://github.com/dennispayne/XISF-Shell-Extensions/discussions)
2. Start a new discussion if not addressed

**Security Vulnerability**:

1. Do NOT open a public issue
2. Email maintainers through [SECURITY.md](../../SECURITY.md)
3. Allow time for response and patch

---

## Additional Resources

- **[Installation Guide](../installation-guide.md)** — How to install XISF Shell Extensions
- **[Handlers Technical Reference](../reference/handlers-technical.md)** — For developers
- **[Catalog Specification](../reference/catalog-spec.md)** — Catalog file format details
- **[Telemetry & ETW](../features/telemetry-etw.md)** — Detailed ETW event reference
- **[Feature Tiers](../features/feature-tiers.md)** — Feature comparison by tier
- **[Getting Started](../getting-started.md)** — Quick orientation guide

---

## Keyboard Shortcuts & Tips

| Action | Shortcut / Method |
|--------|-----------------|
| Open Settings app | Start menu → "XISF Shell Extensions Settings" |
| Open file locations | In Settings, click folder icon next to paths |
| Reset to defaults | Delete registry key: `HKEY_CURRENT_USER\Software\DennisPayne\XISF Shell Extension` |
| View catalog files | Address bar: `%ProgramData%\DennisPayne\XISFShellExtension\catalogs\` |
| View trace files | Address bar: `%TEMP%\xisf\` |
| Check handler registration | Registry: `HKEY_CLASSES_ROOT\CLSID\{7C54FA8B-9D63-4C10-8FBE-1A5A0F9A3B2E}` |

---

## Frequently Asked Questions

**Q: Will enabling handlers slow down Explorer?**

A: Minimal impact. Property extraction is cached; thumbnails are generated once and cached by Explorer. If you notice slowness, try disabling Preview Handler (most CPU-intensive) and re-enable after clearing thumbnail cache.

**Q: Can I use offline catalogs?**

A: Yes. Use "Import from File" to import `.csv` files from USB drives or local folders. Same SHA-256 verification applies.

**Q: What if I only want certain handlers enabled?**

A: Each handler is independent. You can enable Property Handler and Search Filter, but disable Preview. Use the toggle buttons to customize.

**Q: Do I need internet for the app to work?**

A: The Settings app itself doesn't require internet. Catalog *downloading* requires internet, but offline import works without it. Once catalogs are installed, all features work offline.

**Q: How do I uninstall the extension?**

A: Disable all handlers in Settings (or delete registry key manually), then uninstall the app through Settings → Apps → Installed Apps.

**Q: Can I use this on a network drive?**

A: XISF files on network drives work, but first-access may be slow (thumbnail generation, property extraction). Caching mitigates this after first view. SMB1 shares may have compatibility issues; SMB3 (modern shares) recommended.

---

**Settings App Version**: 1.0+  
**Last Updated**: 2025  
**XISF Spec**: [International Virtual Observatory Alliance (IVOA)](https://www.ivoa.net/documents/XISF/)
