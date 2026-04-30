# Catalog Management

Learn how to install and manage deep-sky catalogs. Catalogs enable object name recognition, constellation mapping, and full-text search for XISF files. This guide covers online installation, offline import, security verification, and air-gapped setups.

## Table of Contents
- [Understanding Catalogs](#understanding-catalogs)
- [Catalog Types](#catalog-types)
- [Feature Tiers](#feature-tiers)
- [Online Installation](#online-installation)
- [Offline Installation](#offline-installation)
- [Air-Gapped Setup](#air-gapped-setup)
- [Managing Catalogs](#managing-catalogs)
- [Security & Verification](#security--verification)
- [Troubleshooting](#troubleshooting)

## Understanding Catalogs

### What Are Catalogs?

**Catalogs** are databases of deep-sky objects (galaxies, nebulae, star clusters, etc.) that enable the property handler to recognize what's in your XISF images. When you have object coordinates in the XISF header (OBJECT, OBJCTRA, OBJCTDEC), catalogs allow the handler to:

- **Identify the object:** "You imaged NGC 1234, which is the Crab Nebula"
- **Map the constellation:** "This is in the Andromeda constellation"
- **Enable search:** "Find all my images of the Andromeda constellation"
- **Enrich metadata:** "Show the common name and catalog entry"

### Why Catalogs Matter

**Without Catalogs (Basic Features):**
- Metadata shows raw coordinates: "RA: 00h 42m 44s, Dec: +41° 16' 09""
- No object names shown
- Search by filename only

**With Catalogs (Enriched Features):**
- Metadata shows: "Object Name: M31 (Andromeda Galaxy)"
- Constellation automatically computed: "Andromeda"
- Search for "Andromeda" finds all related images
- Full-text search by object name, constellation, catalog number

### Catalog Data Is Optional

- **Not shipped in installer** — Keeps MSI small (< 20 MB)
- **Downloaded on demand** — Installed when you choose
- **Completely optional** — Works without them
- **Can be offline** — Import locally for air-gapped systems

## Catalog Types

### NGC Catalog

**What It Contains:**
- NGC (New General Catalog) catalog numbers
- Originally published in 1888, updated regularly
- Contains ~13,000 astronomical objects

**What Objects:**
- Galaxies (M31 = NGC 224)
- Nebulae
- Star clusters
- Planetary nebulae
- Other deep-sky objects

**Format:** CSV (comma-separated values)

**File Name:** `NGC.csv`

**Example Entries:**
```
ID,Name,Constellation
224,Andromeda Galaxy,Andromeda
6543,Cat's Eye Nebula,Draco
1976,Orion Nebula,Orion
```

### Addendum Catalog

**What It Contains:**
- Additional objects and corrections
- Updates and improvements to NGC catalog
- Common names and alternate designations
- Modern designations and updates

**File Name:** `addendum.csv`

**Complements:** Works with NGC.csv for complete data

### Sharpless Catalog

**What It Contains:**
- Sharpless (Sh) catalog numbers
- ~312 emission nebulae (ionized gas clouds)
- Originally compiled by Stewart Sharpless

**What Objects:**
- Emission nebulae (H-alpha emitting gas)
- Particularly useful for narrowband imaging
- Often imaged with Ha, OIII, SII filters

**Format:** CSV

**File Name:** `sharpless.csv`

**Example Entries:**
```
Sharpless,Name,Constellation
37,Thor's Helmet,Cepheus
57,Pelican Nebula,Cygnus
262,Cocoon Nebula,Cygnus
```

## Feature Tiers

### Basic Features (Always Available)

No catalog installation required:
- Standard XISF properties (size, color space)
- Computed properties (camera, telescope, exposure)
- Fast thumbnail generation
- Basic Windows Search indexing
- Raw coordinate display

### Enriched Features (Requires Catalogs)

Unlock by installing catalogs:
- **Object recognition:** "NGC 1234" instead of raw coordinates
- **Common names:** "Crab Nebula" instead of just "NGC 1952"
- **Constellation mapping:** Automatically computed from RA/Dec
- **Cone search:** Find images of the same object within search radius
- **Full-text search:** "Orion" finds all Orion constellation images
- **Enhanced metadata:** Catalog entry details in Details pane

### Checking Your Feature Tier

To see which features you have:
1. Open XISF Shell Extension Settings
2. Look for **"Catalog Status"** section
3. Shows:
   - Basic features (always listed)
   - Installed catalogs (NGC, Addendum, Sharpless)
   - Enriched features available

## Online Installation

### Recommended: Installation from GitHub

The easiest way to install catalogs is directly from the GitHub repository.

### Security Model

Before you install, understand the security approach:

**Cryptographic Verification:**
- Catalog files are verified by SHA-256 hash
- Hash is compiled into the application binary
- Downloaded file must match exact hash
- Mismatch = file is rejected and not used

**TLS Encryption:**
- Download occurs over HTTPS (TLS 1.2+)
- Certificate validation enforced
- Cannot be MITM attacked

**Pinning:**
- Hash is "pinned" to a specific GitHub commit
- Only that exact commit version is accepted
- Updates require application code change

**Safe Even If GitHub Is Compromised:**
- If GitHub serves incorrect file → SHA-256 mismatch → rejected
- Attacker would need to change our binary (impossible without source access)

### Step-by-Step Installation

**Step 1: Open Settings App**
1. Press **Windows key** and search for "XISF Shell Extension Settings"
2. Or launch: `C:\Program Files\XISF Shell Extensions\XISFShellExtensionHost.exe`
3. [Screenshot: Settings app main window]

**Step 2: Navigate to Catalog Section**
1. Look for **"Catalog Management"** section (usually on right side)
2. Shows current catalog status

**Step 3: Start Installation**
1. Click **"Install / Update from GitHub"** button
2. [Screenshot: Catalog installation dialog]
3. Dialog appears showing:
   - List of catalogs to install
   - Expected SHA-256 hashes
   - GitHub commit SHA
   - URLs

**Step 4: Review Verification Information**
The dialog shows:
- **OpenNGC Commit:** Specific GitHub commit SHA (hash of the code commit)
- **Download URLs:** Where files will be downloaded from
- **Expected Hashes:** SHA-256 values to verify

Optional: Verify independently on github.com:
1. Visit [OpenNGC Repository](https://github.com/mattiaverga/OpenNGC)
2. Go to specified commit SHA
3. Compare hashes with those shown in dialog

**Step 5: Confirm and Install**
1. Click **"Install"** button
2. Application begins download:
   - Connects to GitHub over HTTPS
   - Downloads NGC.csv
   - Downloads addendum.csv
   - Downloads sharpless.csv (if applicable)
   - Verifies each file's SHA-256 hash
   - Stores in `%LOCALAPPDATA%\XISFShellExtension\catalogs\`

3. Progress dialog shows:
   - Download progress (%)
   - Current file
   - Status (downloading, verifying, etc.)

**Step 6: Wait for Completion**
- Installation typically takes 30-60 seconds
- Depends on internet speed
- Offline catalogs are immediately available to handlers

**Step 7: Verify Installation**
1. Click on an XISF file with object metadata
2. Check Details pane for object name
3. Example: "Object Name: M31 (Andromeda Galaxy)"
4. If visible, installation was successful

### Internet Requirements

- **Internet Speed:** No specific requirement; even slow connections work (download is small: ~5 MB total)
- **TLS 1.2+:** Required for secure connection
- **Firewall:** Must allow HTTPS (port 443) to github.com
- **Proxy:** Works with standard HTTP proxy

### Checking for Updates

To update catalogs to a newer version:

1. Open Settings app
2. Click **"Install / Update from GitHub"** again
3. If new version available:
   - Shows updated hashes
   - Downloads newest version
   - Replaces existing files

## Offline Installation

### When to Use Offline Installation

Offline installation is for:
- **Air-gapped systems** — No internet access
- **Network restrictions** — Cannot connect to GitHub
- **Private networks** — GitHub not accessible
- **Offline imaging** — Pre-prepare before field session
- **Multiple systems** — Download once, install everywhere

### Getting Catalog Files

You need to obtain the CSV files:
- **OpenNGC Repository:** https://github.com/mattiaverga/OpenNGC
- **Clone repository:**
  ```powershell
  git clone https://github.com/mattiaverga/OpenNGC.git
  cd OpenNGC
  ```
- **Files needed:**
  - `catalog.csv` or `NGC.csv`
  - `catalog_addendum.csv` or `addendum.csv`
  - `sharpless.csv`

### Step-by-Step Offline Installation

**Step 1: Obtain Catalog Files**
1. On connected computer, download from OpenNGC repository
2. Copy files to USB drive or network share
3. Transfer to air-gapped system

**Step 2: Open Settings App**
1. On air-gapped system, launch XISF Shell Extension Settings
2. Navigate to **"Catalog Management"** section

**Step 3: Import Catalogs**
1. Click **"Import from File"** button
2. [Screenshot: File import dialog]

**Step 4: Select Catalog File**
1. Dialog shows file browser
2. Navigate to NGC.csv location
3. Select file and click **"Open"**
4. [Screenshot: File browser]

**Step 5: Choose Verification Pin**
A dialog appears asking:
```
"Which commit SHA should this file match?"
```

You must provide the **commit SHA** of the OpenNGC version you're using:
- Look in OpenNGC repository for `.git/HEAD` or latest commit
- Ask for the specific commit SHA
- Or ask for the "Download Expected Hashes"

**Step 6: Verification**
Application verifies:
1. SHA-256 hash is computed
2. Compared to pinned hash
3. If mismatch → error dialog, file rejected
4. If match → file installed

**Step 7: Repeat for Other Catalogs**
1. Repeat steps 3-6 for:
   - addendum.csv
   - sharpless.csv

**Step 8: Verify Installation**
1. Select XISF file with object metadata
2. Check Details pane for object name
3. Should show enriched data

### Getting the Correct Commit SHA

To find the correct commit SHA:

**Option 1: From OpenNGC Repository**
1. Visit https://github.com/mattiaverga/OpenNGC
2. Click **"Commits"** (on main page)
3. Find the commit you used
4. Copy the 7-character (or full 40-character) SHA

**Option 2: From .git Directory**
```powershell
cd path\to\OpenNGC
git rev-parse HEAD
```
Returns the full commit SHA

**Option 3: Ask the Application**
If you're unsure:
1. Open Settings app
2. Click **"Copy Expected Hashes"** button
3. Clipboard shows:
   - Current pinned commit SHA
   - Expected hashes
   - URLs
4. Use the pinned SHA if you're unsure

## Air-Gapped Setup

### Scenario: Offline Astrophotography System

You have a dedicated imaging computer with no internet access. To set up:

**Step 1: On Connected Computer**
1. Download OpenNGC catalog files
2. Copy catalog files to USB drive
3. Note the commit SHA from the repository

**Step 2: Transfer to Air-Gapped System**
1. Insert USB drive
2. Copy files to a temp location on imaging computer
3. Run XISF Shell Extension Settings

**Step 3: Import Catalogs**
1. Click **"Import from File"**
2. Select NGC.csv from USB location
3. Enter commit SHA
4. Repeat for addendum.csv and sharpless.csv

**Step 4: Verify Setup**
1. In XISF Shell Extension Settings → Catalog Status
2. Shows: "NGC catalog installed (13,000+ objects)"
3. Now full search and metadata works offline

### Benefits

- **No internet required** — Catalogs work completely offline
- **Fast search** — No network latency
- **Reliable** — Works even if internet unavailable
- **Private** — Catalog files stay on your system

### Performance Considerations

- **First startup:** Catalog loading into memory (~1 second)
- **Subsequent searches:** < 100 milliseconds
- **Memory usage:** Catalogs use ~50-100 MB RAM (one-time)
- **Disk usage:** ~10 MB for all catalogs

## Managing Catalogs

### Viewing Installed Catalogs

To check what's installed:

1. Open XISF Shell Extension Settings
2. Look for **"Installed Catalogs"** section
3. Shows:
   - NGC catalog: "13,543 objects" (example count)
   - Addendum catalog: "42 additions and corrections"
   - Sharpless catalog: "312 nebulae"

### Storage Location

Catalogs are stored in:
```
C:\Users\{YourUsername}\AppData\Local\XISFShellExtension\catalogs\
```

Files:
- `NGC.csv` — Main catalog
- `addendum.csv` — Corrections and additions
- `sharpless.csv` — Sharpless nebulae

### Removing Catalogs

To remove catalogs and revert to Basic features:

**Option 1: Via Settings App**
1. If a **"Remove Catalogs"** or **"Clear Catalogs"** button exists, click it
2. If not, manually delete files (below)

**Option 2: Manual Deletion**
1. Open File Explorer
2. Navigate to: `%LOCALAPPDATA%\XISFShellExtension\catalogs\`
3. Delete CSV files you want to remove
4. Restart Explorer

**Result:**
- Object names no longer recognized
- Revert to Basic features
- Raw coordinates displayed instead

### Updating Catalogs

To get the latest catalog data:

1. Open XISF Shell Extension Settings
2. Click **"Install / Update from GitHub"**
3. If newer version available:
   - Shows updated commit SHA
   - Downloads latest files
   - Replaces existing catalogs
4. If already up-to-date:
   - Shows "Catalogs already current"

### Catalog File Sizes

| Catalog | Size | Objects |
|---------|------|---------|
| NGC.csv | ~2 MB | 13,543 |
| addendum.csv | < 1 MB | Updates/additions |
| sharpless.csv | < 1 MB | 312 nebulae |
| **Total** | **~5 MB** | **~14,000 objects** |

## Security & Verification

### Security Model Overview

Catalog installation uses multiple layers of security:

**Layer 1: HTTPS Encryption**
- All downloads over TLS 1.2+
- Prevents network snooping
- Certificate validation enforced

**Layer 2: Cryptographic Hash Verification**
- SHA-256 hash of each file computed after download
- Compared to pinned hash in application
- Mismatch = file rejected

**Layer 3: Pinning**
- Hash is "compiled in" — cannot be tampered with without recompiling
- Specific to a particular GitHub commit SHA
- Prevents bait-and-switch attacks

**Layer 4: Atomic Installation**
- Downloaded to temporary file
- Only moved into place after verification
- If power failure during download, temp file abandoned

### Verifying Hashes

To manually verify catalog hashes:

**Step 1: Get Expected Hashes**
1. Open Settings app
2. Click **"Copy Expected Hashes"** button
3. Clipboard contains:
   ```
   OpenNGC Commit: a1b2c3d4e5f6...
   
   Expected SHA-256 Hashes:
   NGC.csv: a1b2c3d4e5f6...
   addendum.csv: f6e5d4c3b2a1...
   sharpless.csv: e5d4c3b2a1f6...
   ```

**Step 2: Compute Your File Hash**
On Windows:
```powershell
# Compute hash of downloaded file
$hash = Get-FileHash -Path "C:\path\to\NGC.csv" -Algorithm SHA256
$hash.Hash
```

On Linux/Mac:
```bash
sha256sum NGC.csv
```

**Step 3: Compare Hashes**
- Your computed hash should exactly match expected hash
- If different → file may be corrupted or tampered with
- Do not use mismatched file

### GitHub Verification

To verify the pinned commit on GitHub:

1. Visit https://github.com/mattiaverga/OpenNGC
2. Go to **"Commits"** section
3. Search for the commit SHA shown in Settings app
4. Verify the commit exists and matches the date you expect
5. Download files from that commit

### When to Distrust a Download

**Warning Signs:**
- Hash mismatch after download
- File size significantly different (e.g., 50 MB instead of 5 MB)
- Settings app shows "verification failed"
- Downloaded file opens but contains garbage data

**Action:**
- Do not use the file
- Delete the file
- Try downloading again
- If problem persists, contact support

## Troubleshooting

### Catalogs Won't Install from GitHub

**Symptom:** "Install from GitHub" button doesn't work or shows error

**Solutions:**
1. **Check internet connection:**
   - Can you browse github.com in web browser?
   - Try from same computer

2. **Check firewall:**
   ```powershell
   # Test connection to GitHub
   Test-NetConnection -ComputerName github.com -Port 443
   ```
   - Should show "TcpTestSucceeded: True"

3. **Check proxy settings:**
   - If behind corporate proxy, ensure proxy is configured
   - Settings → Network & internet → Proxy

4. **Verify TLS 1.2 support:**
   - Windows 10/11 should support TLS 1.2 by default
   - Very old systems may not support it

5. **Restart Settings app:**
   - Close Settings app
   - Reopen and try again

6. **Check antivirus:**
   - Temporarily disable to test if it's blocking
   - Whitelist if needed

### Hash Mismatch During Installation

**Symptom:** Error message: "SHA-256 hash mismatch, file rejected"

**Solutions:**
1. **File may be corrupted:**
   - Try downloading again
   - Check internet connection during download

2. **Settings app may be outdated:**
   - Reinstall XISF Shell Extensions via MSI
   - This ensures pinned hashes are current

3. **GitHub may be serving different version:**
   - Rare, but possible if GitHub updated repository
   - Try again in a few minutes
   - Or download from previous commit

4. **Manual verification:**
   ```powershell
   $expected = "a1b2c3d4e5f6..."  # From Settings app
   $actual = (Get-FileHash -Path "NGC.csv" -Algorithm SHA256).Hash
   if ($expected -eq $actual) {
       Write-Host "Hashes match!"
   } else {
       Write-Host "Hashes don't match!"
   }
   ```

### Object Names Still Not Showing After Installation

**Symptom:** Catalogs installed but metadata still shows raw coordinates

**Solutions:**
1. **Verify catalogs are installed:**
   - Settings app → Catalog Status section
   - Should show "NGC catalog installed"
   - If not, try installing again

2. **Restart Explorer:**
   - Task Manager → Windows Explorer → Restart
   - Or restart computer

3. **Check XISF file has object data:**
   - Not all XISF files have OBJECT/OBJCTRA/OBJCTDEC headers
   - Only files with these headers can be matched to catalog
   - Try a different XISF file you know has object data

4. **Check catalog matches object:**
   - NGC catalog only has NGC and Messier objects
   - If object is Sharpless, Sharpless catalog must also be installed

### Can't Find Imported Catalog Files

**Symptom:** "Import from File" can't locate the CSV files

**Solutions:**
1. **Files must have .csv extension:**
   - File must be named exactly: `NGC.csv`, `addendum.csv`, or `sharpless.csv`
   - Not: `NGC.txt`, `NGC`, etc.

2. **Verify file location:**
   ```powershell
   # Check if file exists
   Test-Path "C:\path\to\NGC.csv"
   ```

3. **Check file permissions:**
   - Ensure you have Read permission
   - Right-click file → Properties → Security

4. **Copy files to easier location:**
   - Try copying to Desktop or Documents
   - Then import from there

### Catalogs Deleted or Missing

**Symptom:** Previously installed catalogs are gone

**Solutions:**
1. **Check installation location:**
   ```powershell
   $path = "$env:LOCALAPPDATA\XISFShellExtension\catalogs"
   Get-ChildItem $path
   ```
   - Should show NGC.csv, addendum.csv, sharpless.csv

2. **Reinstall from GitHub:**
   - Settings app → Install / Update from GitHub
   - Catalogs will be re-downloaded

3. **Check antivirus:**
   - Antivirus may have quarantined files
   - Check antivirus logs
   - Restore files or add to whitelist

4. **Check for accidental deletion:**
   - If you manually deleted files, reinstall:
     - Settings app → Install / Update from GitHub

### Settings App Shows Catalog Installation But Features Not Working

**Symptom:** Catalogs show as installed but object recognition doesn't work

**Solutions:**
1. **Restart Explorer:**
   - Handlers may not have reloaded catalogs
   - Task Manager → Windows Explorer → Restart

2. **Verify file integrity:**
   - Manually verify SHA-256 hashes (see above)
   - If hash mismatch, delete and reinstall

3. **Check file size:**
   ```powershell
   # Catalog files should be approximately:
   # NGC.csv: ~2-3 MB
   # addendum.csv: ~500 KB
   # sharpless.csv: ~50 KB
   Get-Item "$env:LOCALAPPDATA\XISFShellExtension\catalogs\*.csv" | ForEach-Object {
       Write-Host "$($_.Name): $($_.Length) bytes"
   }
   ```
   - If files are very small (< 10 KB), they may be corrupted

4. **Try manual import:**
   - Download CSV files to USB
   - Use Settings → Import from File
   - Manually verify

---

## Next Steps

- **[Getting Started](../getting-started.md)** — Back to basics
- **[Search Indexing](search-indexing.md)** — Use catalogs to search
- **[Handlers Overview](handlers-overview.md)** — Learn how catalogs are used
- **[Installation Guide](../installation-guide.md)** — Installation help

## Related Documentation

- **[Feature Tiers](../features/feature-tiers.md)** — Basic vs. Enriched features
- **[Security](../SECURITY.md)** — Overall security model
- **[OpenNGC Repository](https://github.com/mattiaverga/OpenNGC)** — Catalog source
