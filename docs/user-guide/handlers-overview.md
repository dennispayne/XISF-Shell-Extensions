# Handlers Overview

XISF Shell Extensions adds three specialized handlers to Windows that provide deep integration with XISF astronomy image files. This guide explains what each handler does, how to use them, and how to manage them.

## Table of Contents
- [What Are Handlers?](#what-are-handlers)
- [The Three Handlers](#the-three-handlers)
- [Handler Capabilities](#handler-capabilities)
- [Using Handlers](#using-handlers)
- [Enable/Disable Effects](#enabledisable-effects)
- [Registry Locations](#registry-locations)
- [Troubleshooting](#troubleshooting)

## What Are Handlers?

Handlers are specialized Windows components that extend Explorer's ability to work with specific file types. XISF Shell Extensions registers three handlers with Windows to provide rich metadata display, image previews, and search indexing for `.xisf` files.

### How Handlers Work

When you select a file in Windows Explorer:
1. Explorer queries registered handlers for that file type
2. Our handlers intercept the request for `.xisf` files
3. The handler reads the XISF file and extracts the relevant data
4. The data is displayed/indexed based on handler type
5. Explorer uses the handler's output to show previews, metadata, or enable search

### When Handlers Are Invoked

- **Property Handler:** When you select a file and the Details pane is visible
- **Preview Handler:** When you select a file and the Preview pane is visible, or when Explorer generates thumbnails
- **Search Filter:** Continuously in the background when Windows Search is indexing your drive

## The Three Handlers

### 1. Property Handler — Metadata Display

**What It Does:**
The Property Handler reads XISF file headers and displays detailed metadata in the Windows Explorer Details pane.

**When You See It:**
- Select an XISF file in Explorer
- Look at the Details pane (right side) or the Details tab at the bottom
- Metadata appears instantly

**What It Shows:**
- **File Properties:** Size, dimensions (width × height), color space
- **Image Properties:** Mean, median, minimum, and maximum pixel values
- **Observation Data:** Object name, observation date/time, RA/Dec coordinates
- **Equipment:** Camera model, telescope, filter, binning
- **Integration:** Exposure time, number of exposures combined
- **Computed Metadata:** Constellation name (if astrometric headers present), deep-sky catalog names (NGC, IC, Sharpless)

**Example Metadata:**
```
Dimensions:        2560 × 1920
Object Name:       M31 (Andromeda Galaxy)
Right Ascension:   00h 42m 44s
Declination:       +41° 16' 09"
Camera:            Canon EOS 6D Mark II
Telescope:         Takahashi Epsilon 160
Exposure Time:     300 s
Filter:            L-Enhance
Observation Date:  2024-01-15 21:30:00 UTC
Mean Pixel Value:  1523.4
Median Pixel Value: 1425.0
Min Pixel:         0
Max Pixel:         65535
```

**Registry Location:**
```
HKCU\Software\DennisPayne\XISF Shell Extension\PropertyEnabled (DWORD, 1=ON, 0=OFF)
```

### 2. Preview/Thumbnail Handler — Image Rendering

**What It Does:**
The Preview Handler renders XISF image data and displays it in the Explorer preview pane. It also generates thumbnails for Explorer's Icon and Thumbnail views.

**When You See It:**
- Select an XISF file in Icon or Thumbnail view → see thumbnail preview
- Select an XISF file with Preview pane visible → see full preview + histogram

**What It Shows:**
- **Rendered Image:** Actual pixel data from the XISF file, auto-scaled to fit
- **Histogram:** Visual distribution of pixel brightness (colors represent R/G/B channels)
- **File Info:** Quick reference showing image dimensions and file size
- **Statistics:** Basic pixel range (min/max) in histogram tooltip

**How Thumbnails Work:**
1. First time you open a folder with XISF files → thumbnails are generated (may take seconds)
2. Thumbnails are cached by Windows → subsequent views are instant
3. Clearing the thumbnail cache forces regeneration on next view

**Image Processing:**
- Auto-scales to 8-bit display (16-bit and 32-bit XISF images are converted)
- Applies smart brightness/contrast stretching for visibility
- Preserves image aspect ratio
- Works with single-channel (grayscale) and multi-channel (RGB) images

**Registry Location:**
```
HKCU\Software\DennisPayne\XISF Shell Extension\PreviewEnabled (DWORD, 1=ON, 0=OFF)
```

### 3. Search Filter — Windows Search Integration

**What It Does:**
The Search Filter (IFilter) indexes XISF file content so Windows Search can find files by metadata.

**When You See It:**
- Use Windows Search (Win+K) or Explorer search box
- Type an object name, camera model, or other metadata
- XISF files matching your search appear in results

**What It Indexes:**
- **Object Names:** NGC catalog numbers, IC catalog numbers, Sharpless catalog numbers, common names (M31, M42, etc.)
- **Coordinates:** RA and Dec in multiple formats
- **Constellation Names:** Automatically computed from RA/Dec
- **Equipment:** Camera model, telescope, filter
- **Date/Time:** Observation date and ISO format timestamps
- **Statistics:** Pixel mean, median, min, max values
- **File Properties:** Dimensions, color space, file size

**Example Searches:**
| Search Query | Finds |
|---|---|
| "NGC 1234" | All images of NGC 1234 (cone search) |
| "M31" | All images of the Andromeda Galaxy |
| "Orion" | All images in the Orion constellation |
| "Canon EOS" | All images taken with Canon EOS camera |
| "Takahashi" | All images taken with Takahashi telescope |
| "2024-01-15" | All images from January 15, 2024 |
| "L-Enhance" | All images using L-Enhance filter |

**How Indexing Works:**
1. Windows Search scans your XISF files in the background
2. Our IFilter reads XISF headers and extracts metadata
3. Metadata is added to the Windows Search index
4. First indexing may take several minutes for large libraries
5. Subsequent updates are incremental (only new/changed files)

**Registry Location:**
```
HKCU\Software\DennisPayne\XISF Shell Extension\SearchFilterEnabled (DWORD, 1=ON, 0=OFF)
```

## Handler Capabilities

### Capability Matrix

| Capability | Property Handler | Preview Handler | Search Filter |
|---|---|---|---|
| **Displays metadata** | ✓ | ✗ | ✗ |
| **Shows thumbnails** | ✗ | ✓ | ✗ |
| **Renders preview image** | ✗ | ✓ | ✗ |
| **Indexes content** | ✗ | ✗ | ✓ |
| **Works offline** | ✓ | ✓ | ✓ (if already indexed) |
| **Requires catalogs** | Optional | ✗ | Optional |
| **Supports 16-bit XISF** | ✓ | ✓ | ✓ |
| **Supports RGB images** | ✓ | ✓ | ✓ |
| **Supports grayscale images** | ✓ | ✓ | ✓ |

### Feature Tier Requirements

**Basic Features** (Always Available):
- Standard file properties (size, dimensions, color space)
- Computed properties from headers (camera, telescope, exposure)
- Simple thumbnail generation
- Basic metadata indexing

**Enriched Features** (Requires Catalog Installation):
- Deep-sky object names (NGC, IC, Sharpless)
- Constellation mapping
- Cone search (find images of the same object together)
- Enhanced search results with object information

All three handlers work with both features tiers.

## Using Handlers

### Open the Settings App

The **XISF Shell Extension Settings** app controls all handlers:

1. **Start Menu:** Search for "XISF Shell Extension Settings"
2. **Direct Launch:** `C:\Program Files\XISF Shell Extensions\XISFShellExtensionHost.exe`
3. **Program Files:** Navigate to `C:\Program Files\XISF Shell Extensions\` and double-click `XISFShellExtensionHost.exe`

[Screenshot: Settings app with three handler toggles]

### View Handler Status

The main Settings window displays:
- **Toggle Button** for each handler (left side)
- **Status Indicator** showing ON (enabled) or OFF (disabled)
- **Description** of what each handler does
- **Action Buttons** for catalog management

### Enable a Handler

1. Open **Settings app**
2. Click the **toggle button** next to the handler
3. Button should show **ON** state
4. **Restart Explorer** for the change to take effect:
   - Task Manager → Windows Explorer → Restart
   - Or restart your computer

### Disable a Handler

1. Open **Settings app**
2. Click the **toggle button** next to the handler
3. Button should show **OFF** state
4. **Restart Explorer** for the change to take effect

Disabling a handler does **not** uninstall it. The DLLs remain installed on disk, but Explorer will use Windows defaults for that function.

### Reset All Handlers to Default

1. Open **Settings app**
2. Click **"Reset to Defaults"** button (if present)
3. All handlers are set to **ON** state
4. Restart Explorer

## Enable/Disable Effects

### What Happens When You Disable a Handler

| Handler | Disabled | Effect |
|---------|----------|--------|
| **Property Handler** | Metadata → Basic file info only | Details pane shows generic properties (size, date modified, attributes) |
| **Preview Handler** | Preview/Thumbnails → Generic file icon | Explorer shows generic document icon; no preview renders |
| **Search Filter** | Search → Disabled | XISF files still appear in search by filename, but metadata is not searchable |

### Disabling a Handler to Improve Performance

If your system is slow, try disabling handlers:

1. **Preview Handler** uses the most CPU (generates thumbnails)
   - Disable for faster Explorer browsing
   - Re-enable only when you need to see previews

2. **Search Filter** uses CPU during indexing
   - Disable if you don't use Windows Search for XISF files
   - Saves system resources during idle indexing

3. **Property Handler** is lightweight
   - Rarely needs to be disabled

### Temporary Disabling

To temporarily disable a handler without uninstalling:
1. Open Settings app
2. Toggle **OFF**
3. Restart Explorer
4. Use your XISF files normally (with reduced functionality)
5. To re-enable, toggle **ON** and restart Explorer again

## Registry Locations

### Where Settings Are Stored

All handler settings are stored in the Windows Registry under:
```
HKCU\Software\DennisPayne\XISF Shell Extension\
```

### Registry Keys

| Key | Type | Default | Meaning |
|-----|------|---------|---------|
| `PropertyEnabled` | DWORD | 1 | Property Handler enabled (1=ON, 0=OFF) |
| `PreviewEnabled` | DWORD | 1 | Preview/Thumbnail Handler enabled (1=ON, 0=OFF) |
| `SearchFilterEnabled` | DWORD | 1 | Search Filter enabled (1=ON, 0=OFF) |

### Viewing Registry Settings

1. Open **Registry Editor** (`Win+R`, type `regedit`)
2. Navigate to `HKEY_CURRENT_USER\Software\DennisPayne\XISF Shell Extension`
3. View or modify DWORD values:
   - **1** = enabled
   - **0** = disabled

### Modifying via PowerShell

View current settings:
```powershell
Get-Item -Path "HKCU:\Software\DennisPayne\XISF Shell Extension"
```

Enable Property Handler:
```powershell
Set-ItemProperty -Path "HKCU:\Software\DennisPayne\XISF Shell Extension" `
  -Name "PropertyEnabled" -Value 1 -Type DWord
```

Disable Preview Handler:
```powershell
Set-ItemProperty -Path "HKCU:\Software\DennisPayne\XISF Shell Extension" `
  -Name "PreviewEnabled" -Value 0 -Type DWord
```

After modifying the registry, restart Explorer for changes to take effect.

### Handler Component Registration

Each handler is also registered as a COM component under:
```
HKCU\Software\Classes\CLSID\{GUID}
```

These registry entries are managed by the installer and shouldn't be modified manually.

## Troubleshooting

### Handlers Not Working After Installation

**Symptom:** Metadata doesn't show or previews don't render

**Solutions:**
1. Verify installation completed: Open Settings app and check toggle states
2. Restart Windows Explorer (Task Manager → Windows Explorer → Restart)
3. Restart your computer
4. Check Registry: Verify `PropertyEnabled` and `PreviewEnabled` are set to 1:
   ```powershell
   Get-Item -Path "HKCU:\Software\DennisPayne\XISF Shell Extension"
   ```

### Handlers Stop Working After Windows Update

**Symptom:** Features were working but now don't appear

**Solutions:**
1. Reinstall XISF Shell Extensions:
   - Control Panel → Programs → Programs and Features → XISF Shell Extensions → Uninstall
   - Restart your computer
   - Reinstall using the MSI
   - Restart Explorer

2. Alternatively, re-register handlers:
   - Open Settings app
   - Toggle each handler OFF, then ON
   - Restart Explorer

### Metadata Appears But Thumbnails Don't

**Symptom:** Details pane shows metadata but preview pane is blank

**Solutions:**
1. Verify Preview Handler is enabled: Settings app → check toggle
2. Enable Preview pane: Explorer → View → Preview pane
3. Try right-clicking a file → Open with → Preview
4. Clear thumbnail cache:
   ```powershell
   Remove-Item -Path "$env:LOCALAPPDATA\Microsoft\Windows\Explorer" -Recurse -Force
   ```
5. Restart Explorer and try again

### Search Isn't Finding XISF Files

**Symptom:** Windows Search doesn't return XISF files with metadata

**Solutions:**
1. Verify Search Filter is enabled: Settings app → check toggle
2. Check your XISF folder is in indexed locations:
   - Windows Settings → Privacy & Security → Searching Windows → Indexed locations
3. Rebuild the search index:
   - Windows Settings → Privacy & Security → Searching Windows → Advanced → Rebuild index
4. Wait for indexing to complete (check indexing progress in settings)

### Settings App Won't Launch

**Symptom:** Clicking "XISF Shell Extension Settings" does nothing

**Solutions:**
1. Verify installation: Try reinstalling the MSI
2. Launch manually:
   ```powershell
   & "C:\Program Files\XISF Shell Extensions\XISFShellExtensionHost.exe"
   ```
3. Check antivirus: Add the program to antivirus whitelist if it's being blocked

---

## Next Steps

- **[Installation Guide](../installation-guide.md)** — Set up XISF Shell Extensions
- **[Getting Started](../getting-started.md)** — First-time user walkthrough
- **[Preview & Thumbnails](preview-thumbnails.md)** — Optimize image rendering
- **[Search Indexing](search-indexing.md)** — Master metadata search
- **[Catalog Management](catalog-management.md)** — Install deep-sky catalogs
- **[Technical Reference](../reference/handlers-technical.md)** — Developer documentation
