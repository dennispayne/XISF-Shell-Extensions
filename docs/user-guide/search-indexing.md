# Search Indexing Guide

Learn how to search for XISF files using Windows Search. This guide covers how the indexing system works, what metadata is indexed, how to perform effective searches, and troubleshooting.

## Table of Contents
- [Overview](#overview)
- [How Windows Search Indexing Works](#how-windows-search-indexing-works)
- [Enabling Search Indexing](#enabling-search-indexing)
- [What Gets Indexed](#what-gets-indexed)
- [Performing Searches](#performing-searches)
- [Search Query Examples](#search-query-examples)
- [Performance & Tips](#performance--tips)
- [Troubleshooting](#troubleshooting)

## Overview

XISF Shell Extensions includes a **Search Filter** (also called an IFilter) that tells Windows Search how to index XISF file content. This enables you to find XISF files by object name, constellation, camera model, telescope, and other metadata using Windows Search.

### Key Benefits

- **Full-Text Search:** Find images by object name, constellation, equipment, etc.
- **Automatic:** Indexing happens in the background when your computer is idle
- **Fast:** Once indexed, searches return results instantly
- **Offline:** After indexing, search works even without internet
- **Optional:** Can be disabled if you don't use Windows Search

### When Search Is Useful

| Use Case | Example | Result |
|---|---|---|
| **Find by object** | "NGC 1234" | All images of that object |
| **Find by constellation** | "Orion" | All images in Orion constellation |
| **Find by equipment** | "Canon EOS 6D" | All images from that camera |
| **Find by date** | "2024-01-15" | All images from that date |
| **Find by filter** | "Ha" | All images using H-Alpha filter |
| **Combined search** | "NGC 1234 Ha" | Images of NGC 1234 with H-Alpha filter |

## How Windows Search Indexing Works

### What Is Windows Search?

Windows Search is a built-in Windows feature that:
1. Scans your drives in the background
2. Reads file content using specialized filters
3. Builds an index of searchable content
4. Returns instant search results from the index

### The Indexing Process

**Step 1: Folder Scanning**
- Windows Search identifies folders to index (Indexed locations)
- Scans for new or changed files
- Runs automatically when your computer is idle or plugged in

**Step 2: Content Reading**
- For XISF files, Windows Search uses our Search Filter
- The filter reads XISF headers and metadata
- Extracts object names, coordinates, equipment info, etc.

**Step 3: Index Building**
- Extracted metadata is added to the Windows Search index
- Index is stored in a system database
- Updated incrementally as files change

**Step 4: Search**
- When you type a search query, Windows searches the index
- Results appear instantly
- Index is searched, not files (very fast)

### Time Requirements

**Initial Indexing:** First time Windows Search indexes XISF files
- Small library (< 100 files) — 5-10 minutes
- Medium library (100-1000 files) — 30-60 minutes
- Large library (> 1000 files) — Several hours
  - Can run in background without affecting work

**Incremental Updates:** When new files are added
- Runs automatically in background
- Only indexes new/changed files
- Usually completes within minutes

### Performance Impact

**While Indexing:**
- Computer may be slightly slower
- CPU usage around 20-30% for brief periods
- Mostly runs when computer is idle
- Can be disabled temporarily if needed

**After Indexing:**
- Search is very fast (< 1 second)
- No ongoing CPU or disk usage
- Search results are from index (instant)

## Enabling Search Indexing

### Verify Search Filter Is Enabled

The Search Filter is **enabled by default** after installation. To verify:

1. Open **XISF Shell Extension Settings**
   - Start menu → search "XISF Shell Extension Settings"
   - Or: `C:\Program Files\XISF Shell Extensions\XISFShellExtensionHost.exe`

2. Check the **Search Filter** toggle button
   - Should show **ON** (enabled)
   - [Screenshot: Settings app with Search Filter enabled]

3. If OFF, toggle it to ON and restart Explorer

### Enable Indexed Locations

Ensure your XISF folders are in Windows Search's indexed locations:

**Step 1: Open Windows Settings**
1. Press **Windows key + I**
2. Go to **"Privacy & Security"**
3. Click **"Searching Windows"**
   - [Screenshot: Windows Settings search page]

**Step 2: Check Indexed Locations**
1. Look for **"Indexed locations"** section
2. Your XISF folder should be listed (or a parent folder like "Documents" or "C:\")
3. If not listed, add it:
   - Click **"Add"** or **"Add a folder"**
   - Browse to your XISF folder
   - Click **"Add this folder"**
   - [Screenshot: Indexed locations dialog]

**Common Indexed Locations:**
- `C:\Users\{YourUsername}\` (home folder) — Often added by default
- `C:\Users\{YourUsername}\Pictures` — Common image location
- `C:\Users\{YourUsername}\Documents` — Document storage
- `D:\Images` — External drive location

### Wait for Initial Indexing

After enabling:
1. Windows Search starts indexing your XISF files
2. This happens in the background
3. Takes from a few minutes to several hours (depending on library size)
4. You can continue working normally

**To Check Indexing Progress:**
1. Open **Windows Settings** → **Privacy & Security** → **Searching Windows**
2. Look for **"Indexing options"** or **"Advanced"** button
3. Click to see progress percentage

## What Gets Indexed

The Search Filter indexes all XISF metadata including:

### Object Information
- **Object Names:** NGC catalog numbers, IC numbers, Sharpless numbers, common names
  - Examples: "NGC 1234", "M31", "M42", "NGC 7293"
- **Coordinates:** Right Ascension (RA) and Declination (Dec) in multiple formats
  - Examples: "00h 42m 44s", "+41° 16' 09"
- **Constellation:** Computed from RA/Dec
  - Examples: "Orion", "Andromeda", "Cygnus"

### Equipment Information
- **Camera:** Sensor model or camera name
  - Examples: "Canon EOS 6D", "Sony Alpha 7R IV"
- **Telescope:** Optical system used
  - Examples: "Takahashi Epsilon 160", "Astro-Physics 155EDF"
- **Filter:** Optical filter used
  - Examples: "L-Enhance", "Ha", "OIII", "SII"
- **Binning:** Sensor binning factor
  - Examples: "1x1", "2x2"

### Observation Data
- **Date:** Observation date in ISO format
  - Examples: "2024-01-15", "2024-01-15T21:30:00Z"
- **Exposure Time:** Integration duration in seconds
  - Examples: "300", "600"
- **Observation Time:** Date and time stamps

### Image Properties
- **Dimensions:** Image width and height
  - Examples: "2560", "1920", "4096"
- **Color Space:** Image format
  - Examples: "RGB", "Grayscale", "Linear RGB"
- **Pixel Statistics:** Mean, median, min, max pixel values
  - Examples: "1523.4", "1425.0", "0", "65535"

### Additional Properties
- **Filename:** Full XISF filename (always indexed)
- **File Size:** File size in bytes
- **File Path:** Directory path

### What Is NOT Indexed
- Raw pixel data (only summary statistics)
- Image preview images
- Ancillary header data
- Comments (unless stored in metadata headers)

## Performing Searches

### Using Windows Search

**Method 1: Search from Start Menu**
1. Press **Windows key** (or click Start)
2. Type your search query
3. Results appear in search panel
4. Click a result to open the file

[Screenshot: Start menu search showing XISF results]

**Method 2: Search from File Explorer**
1. Open **File Explorer** (`Win+E`)
2. In the search box (top right), type your query
3. Results appear in the current folder and subfolders
4. Click a result to select it

[Screenshot: Explorer search box with results]

**Method 3: Advanced Search**
1. Open **File Explorer**
2. Click the **"…"** menu (or **"Search"** tab)
3. Click **"Advanced options"** or **"More"**
4. Choose search filters:
   - Modified date
   - File type (.xisf)
   - Size
   - Tags

### Basic Search Syntax

| Query | Finds |
|---|---|
| `"NGC 1234"` | Exact phrase "NGC 1234" |
| `NGC` | Files containing "NGC" (any NGC object) |
| `M31` | Files containing "M31" (Andromeda) |
| `Orion` | Files containing "Orion" anywhere |
| `Canon AND EOS` | Files with both "Canon" AND "EOS" |
| `"Canon EOS" OR "Sony Alpha"` | Files with either phrase |
| `-"Inferior"` | Exclude files containing "Inferior" |

## Search Query Examples

### Example 1: Find a Specific Object

**Goal:** Find all images of NGC 1234

**Search Query:**
```
NGC 1234
```

**Result:** All XISF files with "NGC 1234" in metadata

**Tips:**
- You can also search for the Messier name: `M1` instead of `NGC 1952`
- Partial searches work: `NGC 124` matches `NGC 1234`, `NGC 1240`, etc.

### Example 2: Find Objects in a Constellation

**Goal:** Find all images in the Orion constellation

**Search Query:**
```
Orion
```

**Result:** All XISF files with images in Orion

**Tips:**
- Constellation names are computed automatically from RA/Dec
- Works even if the file doesn't have an explicit constellation header
- Partial names work: `Ori` matches `Orion`

### Example 3: Find Images from a Specific Camera

**Goal:** Find all images taken with a Canon EOS 6D Mark II

**Search Query:**
```
"Canon EOS 6D Mark II"
```

**Result:** All images from that camera

**Tips:**
- Use quotes for exact match of camera model
- Partial search also works: `Canon` matches all Canon cameras

### Example 4: Find Images from a Specific Date

**Goal:** Find all images from January 15, 2024

**Search Query:**
```
2024-01-15
```

**Result:** All files with observation date matching January 15, 2024

**Tips:**
- Use ISO date format: YYYY-MM-DD
- You can also search just the year: `2024` for all 2024 images
- Works with both date and date-time stamps

### Example 5: Combine Multiple Criteria

**Goal:** Find images of NGC 1234 using an H-Alpha filter

**Search Query:**
```
NGC 1234 Ha
```

**Result:** Images matching both "NGC 1234" AND "Ha"

**Tips:**
- Space between words means AND
- Use quotes for exact phrases: `"NGC 1234"`
- Can combine many criteria: `NGC 1234 Ha Canon 2024`

### Example 6: Find by Filter

**Goal:** Find all H-Alpha (Ha) filter images

**Search Query:**
```
Ha
```

**Result:** All files with Ha filter metadata

**Tips:**
- Filter names vary: Ha, H-Alpha, HAlpha — search variations
- Works for other filters: OIII, SII, etc.

### Example 7: Find by Telescope

**Goal:** Find all images from a Takahashi telescope

**Search Query:**
```
Takahashi
```

**Result:** All images from Takahashi telescopes

**Tips:**
- Partial search works for brand names
- Can combine with object: `Takahashi NGC 1234`

### Example 8: Exclude Certain Images

**Goal:** Find all NGC images except NGC 1234

**Search Query:**
```
NGC -1234
```

**Result:** All NGC images except those containing "1234"

**Tips:**
- Use minus sign (-) to exclude terms
- Works for excluding files: `-Corona` excludes files with "Corona" in name

## Performance & Tips

### Search Performance

**First Search in a Category:**
- First time searching a newly indexed folder: 1-2 seconds
- Includes reading parts of the index into memory

**Subsequent Searches:**
- Same or similar searches: < 1 second
- Windows caches results

**Large Library Searches:**
- Search for "NGC" across 10,000 files: Still < 1 second
- Index makes searching very fast regardless of library size

### Optimization Tips

**Tip 1: Be Specific**
- More specific queries are faster: `NGC 1234` vs `NGC`
- Exact searches use fewer index entries

**Tip 2: Use Quotes for Exact Phrases**
```
"Canon EOS 6D" — Faster than: Canon EOS 6D
```

**Tip 3: Exclude Large Categories**
If search returns too many results, narrow down:
- `NGC` (too broad) → `NGC 1000` (specific)
- `Orion` (entire constellation) → `Orion Nebula` (specific)

**Tip 4: Search from Explorer, Not Start Menu**
- Start menu searches may search other locations too
- Explorer search is limited to current folder (faster)

**Tip 5: Limit Indexed Folders**
If you have very large indexed areas:
- Remove unnecessary folders from indexed locations
- Add only folders containing XISF files
- Settings → Searching Windows → Indexed locations → Remove unnecessary folders

### Disabling Search During Intensive Work

If indexing is slowing your system:

**Temporarily Pause Indexing:**
1. Open **Windows Settings** → **Privacy & Security** → **Searching Windows**
2. Click **"Advanced"** or **"Indexing options"**
3. Click **"Pause"** button
4. System resources are freed
5. Click **"Resume"** when done

**Permanently Disable for XISF Folder:**
1. Remove that folder from indexed locations (Settings → Searching Windows)
2. Or disable the Search Filter (Settings app → Search Filter toggle OFF)

## Troubleshooting

### Search Isn't Finding XISF Files

**Symptom:** Searching for object names returns no results

**Solutions:**
1. **Verify Search Filter is enabled:**
   - Settings app → Check "Search Filter" toggle is ON
   - Restart Explorer

2. **Check if XISF folder is indexed:**
   - Windows Settings → Privacy & Security → Searching Windows
   - Verify your XISF folder is in "Indexed locations"
   - If not, add it: Click Add → Browse to folder

3. **Wait for initial indexing:**
   - After adding a folder, wait several minutes to hours for indexing
   - Check progress: Settings → Searching Windows → Indexing options

4. **Rebuild the search index:**
   - Windows Settings → Privacy & Security → Searching Windows → Advanced
   - Click **"Rebuild index"**
   - Wait for rebuilding to complete

### Search Works But Returns No Results

**Symptom:** Search query doesn't match any files

**Solutions:**
1. **Verify object name exists:**
   - Open one XISF file in Explorer
   - Check Details pane for actual object name
   - Search for that exact name

2. **Try partial searches:**
   - Search `NGC` instead of `NGC 1234` (if object isn't a standard catalog number)
   - Search `2024` instead of specific date `2024-01-15`

3. **Check for typos:**
   - Object names are case-sensitive in some searches
   - Try: `NGC 1234` (with space), `NGC1234` (without space)

4. **Verify the file is indexed:**
   - Right-click file → Properties
   - Check "Indexed" checkbox is enabled
   - If not checked, the file won't appear in search results

### Search Is Very Slow

**Symptom:** Searches take many seconds or minutes

**Solutions:**
1. **Wait for indexing to complete:**
   - If just added a folder, wait for initial indexing
   - Check Settings → Searching Windows for progress

2. **Reduce indexed locations:**
   - Remove unnecessary folders from indexed locations
   - Reduces index size and search time

3. **Try search from Explorer (not Start menu):**
   - Start menu searches all locations (slower)
   - Explorer search limited to current folder (faster)

4. **Check system resources:**
   - Open Task Manager (Ctrl+Shift+Esc)
   - Check if CPU or disk is at 100%
   - Close other programs if needed

### Catalog Objects Aren't Searchable

**Symptom:** Searching for "NGC 1234" doesn't work even though files have that object

**Solutions:**
1. **Verify catalogs are installed:**
   - Settings app → Catalog management section
   - Check if NGC/IC/Sharpless catalogs are installed
   - If not, click "Install / Update from GitHub"

2. **Rebuild search index after catalog install:**
   - Windows Settings → Privacy & Security → Searching Windows → Advanced
   - Click "Rebuild index"
   - Wait for rebuilding

3. **Check file headers:**
   - Some XISF files may not have OBJECT/OBJCTRA/OBJCTDEC headers
   - These files can't provide object information
   - Check file metadata in PixInsight

### Some XISF Files Don't Appear in Search Results

**Symptom:** Most files are searchable but specific files aren't found

**Solutions:**
1. **Verify the file is indexed:**
   - Right-click file → Properties
   - Check "Indexed" checkbox

2. **Check file permissions:**
   - Verify you have Read permission
   - Right-click → Properties → Security → Edit

3. **Try rebuilding index for that folder:**
   - Right-click folder → Properties
   - Click "Advanced" → Check "Allow indexing"
   - Wait for re-indexing

4. **Verify file isn't locked:**
   - If another application has the file open, indexing may fail
   - Close the file in other applications
   - Wait for re-indexing

### Search Results Have Duplicates or Old Results

**Symptom:** Same file appears multiple times or deleted file still appears

**Solutions:**
1. **Rebuild search index:**
   - Windows Settings → Privacy & Security → Searching Windows → Advanced
   - Click "Rebuild index"
   - Wait for completion

2. **Restart Explorer:**
   - Task Manager → Windows Explorer → Restart

3. **Restart your computer:**
   - Sometimes clears cached search results

### Indexed Locations Don't Include My Folder

**Symptom:** XISF folder isn't in the indexed locations list

**Solutions:**
1. **Add folder to indexed locations:**
   - Windows Settings → Privacy & Security → Searching Windows
   - Click "Add a folder" or "Add"
   - Browse to your XISF folder
   - Click "Add this folder"

2. **Or add parent folder:**
   - Add `C:\` or `C:\Users\{YourUsername}\` instead
   - Automatically indexes all subfolders

3. **Wait for indexing:**
   - After adding, Windows starts indexing in background
   - Initial indexing takes minutes to hours
   - Check progress in Indexing options

---

## Next Steps

- **[Handlers Overview](handlers-overview.md)** — Learn about search filter
- **[Catalog Management](catalog-management.md)** — Install object catalogs
- **[Getting Started](../getting-started.md)** — Back to basics
- **[Settings Reference](settings-reference.md)** — All configuration options

## Related Documentation

- **[Feature Tiers](../features/feature-tiers.md)** — Learn about Basic vs. Enriched features
- **[Computed Properties](../features/computed-properties.md)** — What metadata is indexed
