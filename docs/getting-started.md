# Getting Started

Welcome to XISF Shell Extensions! This guide walks you through your first experience with the software and shows you how to unlock its features.

## Table of Contents
- [What You Get](#what-you-get)
- [After Installation](#after-installation)
- [Opening the Settings App](#opening-the-settings-app)
- [Your First XISF File](#your-first-xisf-file)
- [Enabling Features](#enabling-features)
- [Common First Tasks](#common-first-tasks)
- [Next Steps](#next-steps)

## What You Get

XISF Shell Extensions provides three core features that integrate seamlessly into Windows Explorer:

### 1. **File Metadata Display** (Property Handler)
When you select an XISF file in Explorer, detailed information automatically displays in the Details pane:
- Image dimensions and file size
- Camera and telescope information
- Exposure time and filter details
- Observation date and time
- Deep-sky object names (NGC, IC, Sharpless catalogs — if catalogs are installed)
- Sky coordinates and constellation mapping

[Screenshot: Explorer Details pane showing metadata]

### 2. **Image Previews and Thumbnails** (Preview/Thumbnail Handler)
- **Thumbnails** appear in Explorer Icon view as you browse folders
- **Preview pane** shows a rendered image of the XISF file when selected
- Built-in histogram for quick image analysis
- Fast rendering — even for large images

[Screenshot: Explorer preview pane with XISF image and histogram]

### 3. **Full-Text Search Integration** (Search Filter)
- Search for XISF files by metadata using Windows Search
- Find images by object name, constellation, camera model, telescope, etc.
- Search box in Explorer: type "**NGC 1234**" to find all frames of that object
- Indexing happens automatically in the background

[Screenshot: Windows Search results for XISF files]

## After Installation

Once you've installed XISF Shell Extensions via the MSI and restarted Explorer, you're ready to use the basic features.

### Immediate Capabilities (No Setup Required)

All three handlers are **enabled by default**. Simply select any XISF file in Explorer:
- ✓ Metadata displays in the Details pane
- ✓ A thumbnail and preview render in the preview pane
- ✓ File properties appear in tooltips and file listings

### What Works Without Catalogs

**Basic features** are always available:
- Standard XISF properties: image size, color space, file size
- Computed metadata: camera model, telescope, exposure time, filter, binning
- Observation date and image statistics
- Fast thumbnail generation in Icon view
- Windows Search indexing of basic file properties

### Optional: Install Catalogs

**Enriched features** require installing optional catalog data:
- Deep-sky object names (NGC, IC, Sharpless catalogs)
- Constellation mapping for RA/Dec coordinates
- Sky object cone search (find all images of the same object together)
- Full-text search for "Orion" to find all constellation frames
- Enhanced search results with object information

> **Tip:** Catalog installation takes ~30 seconds and is completely optional. You can skip this if you're offline or prefer not to use it.

## Opening the Settings App

The **XISF Shell Extension Settings** app is your control center for managing handlers and installing catalogs.

### Launch the Settings App

**Option 1: From Start Menu (Easiest)**
1. Press the **Windows key**
2. Type "**xisf**"
3. Click **"XISF Shell Extension Settings"**

[Screenshot: Windows Start menu search results]

**Option 2: Direct File Launch**
```powershell
C:\Program Files\XISF Shell Extensions\XISFShellExtensionHost.exe
```

**Option 3: Program Files**
1. Open **File Explorer**
2. Navigate to `C:\Program Files\XISF Shell Extensions\`
3. Double-click **`XISFShellExtensionHost.exe`**

### Settings App Main Window

[Screenshot: Settings app with toggle buttons and action buttons]

The main window shows:
- **Toggle buttons** for each handler (left side)
- **Action buttons** for catalog management (right side)
- **Status indicators** showing which handlers are active
- **Handler information** describing what each does

## Your First XISF File

### Viewing an XISF File

1. **Open Windows Explorer** (`Win+E`)
2. **Navigate** to a folder containing XISF files
3. **Select** any `.xisf` file

You should immediately see:
- A **thumbnail preview** in the preview pane (right side) if you're in detailed list view
- **Metadata** in the Details pane showing image properties
- [Screenshot: Explorer with selected XISF file and metadata]

### Changing View to See Thumbnails

To see thumbnails more prominently:
1. In Explorer, click **View** menu (or press `Ctrl+1` through `Ctrl+5`)
2. Select **"Large Icons"** or **"Thumbnails"**
3. Thumbnails of your XISF images will appear

[Screenshot: Explorer in thumbnail view showing XISF images]

### Try the Preview Pane

1. Ensure **View → Preview pane** is enabled (or press `Alt+P`)
2. Select different XISF files one by one
3. Watch the preview pane update with:
   - Rendered image
   - Histogram (colorful graph showing brightness distribution)
   - Image statistics

[Screenshot: Preview pane showing histogram]

## Enabling Features

### Check Handler Status

All handlers should be enabled automatically after installation. To verify:

1. **Open Settings** (Start menu → "XISF Shell Extension Settings")
2. Look at the **toggle buttons** on the left:
   - **Property Handler** toggle (top)
   - **Preview/Thumbnail Handler** toggle (middle)
   - **Search Filter** toggle (bottom)

If a toggle is **OFF**, the corresponding feature is disabled:
- OFF = Explorer uses Windows defaults
- ON = XISF Shell Extensions provides the feature

[Screenshot: Settings app with all toggles ON]

### Turn Handlers On or Off

To **enable** a handler:
1. Click its **toggle button**
2. Restart Explorer for the change to take effect (Task Manager → Windows Explorer → Restart)

To **disable** a handler:
1. Click its toggle to turn it OFF
2. The feature reverts to Windows defaults for the next file you select

> **Tip:** Disabling a handler doesn't uninstall it — it just turns it off temporarily.

### What Each Handler Does

| Handler | Enabled | Disabled |
|---------|---------|----------|
| **Property Handler** | Shows rich metadata in Details pane | Explorer shows basic file info only |
| **Preview/Thumbnail Handler** | Renders XISF images and displays as thumbnails | Windows shows generic file icon |
| **Search Filter** | XISF metadata is indexed and searchable | Search treats XISF files as generic documents |

## Common First Tasks

### Task 1: Set Up Catalog Search (5 minutes)

**Goal:** Enable deep-sky object name recognition

**Steps:**
1. Open **XISF Shell Extension Settings**
2. Click **"Install / Update from GitHub"** button
3. Wait for the download and verification to complete
   - [Screenshot: Catalog installation progress]
4. Close the dialog

**Now you can:**
- See object names in metadata (e.g., "NGC 6543" instead of just coordinates)
- Search for "NGC 1234" in Windows Search
- Browse images by constellation

See [Catalog Management](user-guide/catalog-management.md) for offline installation.

### Task 2: Find XISF Files with Windows Search (2 minutes)

**Goal:** Search your image library

**Steps:**
1. Open **File Explorer** or **Windows Search** (Win+K)
2. In the search box, type your search query:
   - **"NGC 1234"** to find images of a specific object
   - **"Orion"** to find constellation images
   - **"Canon EOS"** to find images from a specific camera
3. Press Enter
4. Browse the results

See [Search Indexing](user-guide/search-indexing.md) for more search tips.

### Task 3: Customize Handler Settings (2 minutes)

**Goal:** Disable a handler if you don't need it

**Steps:**
1. Open **XISF Shell Extension Settings**
2. Toggle **OFF** any handler you don't want
3. Restart Windows Explorer (Task Manager → Windows Explorer → Restart)

**Example:** If you don't use Windows Search, disable the Search Filter to save system resources.

### Task 4: Check Image Metadata (1 minute)

**Goal:** See detailed information about an XISF file

**Steps:**
1. In Explorer, select an XISF file
2. Ensure **View → Details pane** is enabled (Alt+P)
3. Scroll through the Details pane to see:
   - **Dimensions:** Image size in pixels
   - **Object Name:** What was observed (if applicable)
   - **Camera:** Sensor model
   - **Exposure Time:** Integration time in seconds
   - **Filter:** Optical filter used
   - **Telescope:** Optical system
   - **Observation Date:** When the image was taken
   - **Pixel Statistics:** Mean, median, min, max values

[Screenshot: Details pane showing metadata fields]

## Next Steps

### Learn More About Features

- **[Handlers Overview](user-guide/handlers-overview.md)** — Deep dive into what each handler does
- **[Preview & Thumbnails](user-guide/preview-thumbnails.md)** — Optimize image rendering and thumbnail display
- **[Search Indexing](user-guide/search-indexing.md)** — Master Windows Search for XISF files
- **[Catalog Management](user-guide/catalog-management.md)** — Set up offline catalogs and air-gapped systems
- **[Settings Reference](user-guide/settings-reference.md)** — All configuration options explained

### Explore Features

- **[Pixel Statistics](features/pixel-statistics.md)** — Analyze image brightness and contrast
- **[Computed Properties](features/computed-properties.md)** — Learn what metadata is automatically calculated
- **[Constellation Mapping](features/constellation-mapping.md)** — Understand sky coordinate resolution

### Troubleshooting

- **[Troubleshooting Guide](reference/troubleshooting.md)** — Solutions to common issues
- **[FAQ](reference/troubleshooting.md#faq)** — Frequently asked questions

## Tips & Tricks

### Keyboard Shortcuts
- **Win+E** — Open Explorer
- **Win+K** — Open Windows Search
- **Alt+P** — Toggle Details/Preview pane in Explorer
- **Ctrl+1** through **Ctrl+5** — Change Explorer view (icons, list, details, etc.)

### File Association

To open XISF files with a specific viewer by default:
1. Right-click an XISF file in Explorer
2. Select **"Open with"** → **"Choose another app"**
3. Pick your preferred application
4. Check **"Always use this app for .xisf files"**
5. Click **"OK"**

Your chosen viewer will now open by default when you double-click XISF files.

### Performance Tips

- For **large image libraries**, catalog installation may take a few minutes (one-time)
- **Thumbnail rendering** on first view may be slow for very large XISF files — subsequent views are cached
- **Windows Search indexing** happens automatically in the background when your computer is idle
- On **slower systems**, disable the Preview Handler to improve Explorer responsiveness

---

**Continue Reading:**
- [Installation Guide](installation-guide.md) — Detailed install instructions
- [User Guide](user-guide/handlers-overview.md) — Complete feature documentation
