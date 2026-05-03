# Preview Thumbnails Guide

Learn how to use XISF preview and thumbnail features in Windows Explorer. This guide covers viewing previews, understanding how thumbnails work, optimizing performance, and troubleshooting.

## Table of Contents
- [What Are Previews and Thumbnails?](#what-are-previews-and-thumbnails)
- [Enabling Preview Pane](#enabling-preview-pane)
- [Understanding Thumbnails](#understanding-thumbnails)
- [Image Types and Support](#image-types-and-support)
- [Performance Considerations](#performance-considerations)
- [Customization](#customization)
- [Troubleshooting](#troubleshooting)

## What Are Previews and Thumbnails?

The Preview/Thumbnail Handler provides two complementary features:

### Preview Pane
The **Preview pane** appears on the right side of Explorer and shows:
- A rendered view of the XISF image
- A histogram showing pixel brightness distribution
- Image dimensions and file information
- Quick visual inspection without opening the file

### Thumbnails
**Thumbnails** appear when browsing in Icon or Thumbnail view:
- Small preview images representing each file
- Generated once, then cached for instant display
- Allows visual scanning of your image library
- Cached by Windows for fast subsequent access

### How They Differ

| Feature | Preview Pane | Thumbnail |
|---------|---|---|
| **Size** | Large (fills right pane) | Small (icon size) |
| **Update** | Instant on file selection | Generated once, cached |
| **Performance** | May take seconds for large images | Instant after generation |
| **Details Shown** | Image data, histogram, stats | Scaled image only |
| **Use Case** | Detailed inspection | Quick visual scanning |

## Enabling Preview Pane

### Show the Preview Pane

1. **Open Windows Explorer** (`Win+E`)
2. Go to the **View** menu
3. Check **"Preview pane"** (or press **Alt+P**)
   - [Screenshot: View menu with Preview pane option]

The preview pane will appear on the right side of the Explorer window.

### Navigate Through Images

1. Select an XISF file in the main file list
2. The preview pane instantly shows:
   - Rendered image
   - Histogram
   - Image information

3. Use arrow keys to navigate between files
4. Preview updates automatically as you move through files

### Preview Pane Layout

```
[Preview Pane]
┌─────────────────────────────────┐
│   XISF File Name                │
├─────────────────────────────────┤
│                                 │
│      [Rendered Image]           │
│                                 │
├─────────────────────────────────┤
│   [Histogram - R/G/B colors]    │
├─────────────────────────────────┤
│   Width: 2560 px                │
│   Height: 1920 px               │
│   Mean: 1523.4                  │
│   Median: 1425.0                │
└─────────────────────────────────┘
```

### Hide the Preview Pane

1. Go to **View** menu
2. Uncheck **"Preview pane"** (or press **Alt+P**)

The preview pane disappears, giving more space to the file list.

## Understanding Thumbnails

### What Thumbnails Show

When viewing folders in **Icon view** or **Thumbnail view**, Explorer displays small preview images for each XISF file:
- Each thumbnail is a scaled-down version of the image
- Shows what the image looks like at a glance
- Helps you visually identify images without selecting them

[Screenshot: Explorer in Thumbnail view showing XISF images]

### How Thumbnail Generation Works

**First View of a Folder:**
1. You open a folder containing XISF files
2. Windows doesn't have cached thumbnails yet
3. XISF Shell Extensions generates thumbnails on-demand
4. This takes several seconds depending on:
   - Number of XISF files
   - Image complexity and size
   - System performance

**Subsequent Views:**
1. Windows has cached the thumbnails
2. Thumbnails display instantly
3. Cache persists until you:
   - Delete the image file
   - Modify the image file
   - Clear the thumbnail cache

### Thumbnail Cache Location

Thumbnails are cached by Windows in:
```
C:\Users\{YourUsername}\AppData\Local\Microsoft\Windows\Explorer\
```

Windows manages this cache automatically.

### Changing Explorer View

To see thumbnails of XISF files:

**Option 1: Use View Menu**
1. Open Explorer
2. Click **View** menu
3. Select a view:
   - **"Extra large icons"** — Very large thumbnails
   - **"Large icons"** — Large thumbnails
   - **"Medium icons"** — Medium thumbnails
   - **"Small icons"** — Small thumbnails
   - **"List"** — Text list only (no thumbnails)
   - **"Details"** — List with metadata columns
   - **"Tiles"** — Medium thumbnails with filename

**Option 2: Use Keyboard Shortcuts**
- **Ctrl+1** — Icons view
- **Ctrl+2** — List view
- **Ctrl+3** — Details view
- **Ctrl+4** — Tiles view
- **Ctrl+5** — Content view

**Option 3: View Buttons**
Look for view control buttons in the top toolbar:
- [Screenshot showing view buttons]

### Thumbnail Size Adjustment

To control thumbnail size in Icon view:

1. Hold **Ctrl** and scroll your mouse wheel up (larger) or down (smaller)
2. Or use **View** menu → **Zoom** → adjust size

Larger thumbnails use more screen space but make images easier to see.

## Image Types and Support

### Supported XISF Image Formats

The Preview Handler supports XISF files containing:

| Image Type | Support | Notes |
|---|---|---|
| **Grayscale (1-channel)** | ✓ | Displayed as grayscale |
| **RGB (3-channel)** | ✓ | Displayed in color |
| **Multi-channel** | ✓ | First 3 channels used for RGB |
| **8-bit images** | ✓ | Native bit depth |
| **16-bit images** | ✓ | Converted to 8-bit for display |
| **32-bit float images** | ✓ | Converted to 8-bit for display |

### Color Space Handling

**Grayscale Images:**
- Displayed as monochrome
- Histogram shows single brightness distribution
- All three RGB channels show identical data

**Color (RGB) Images:**
- Displayed in full color
- Histogram shows three color channels (Red, Green, Blue)
- Each channel displayed in its color

**Multi-Channel Images:**
- First three channels used for RGB display
- Additional channels ignored in preview
- Full metadata stored in Details pane

### Image Scaling

Preview rendering automatically handles different image sizes:
- **Small images** (< 1000 pixels) — Scaled up for visibility
- **Large images** (> 4000 pixels) — Scaled down to fit pane
- **Aspect ratio** — Always preserved (no distortion)
- **Quality** — High-quality scaling algorithm used

## Performance Considerations

### Thumbnail Generation Performance

**Factors Affecting Speed:**
1. **Number of XISF files** — More files = longer generation
2. **File size** — Larger files take longer to read
3. **Image complexity** — Complex histograms take more CPU
4. **System resources** — Faster CPU/SSD = faster generation
5. **Image dimensions** — 4K+ images take more processing

**Typical Times (Per File):**
- Small XISF (< 10 MB) — 0.5-1 second
- Medium XISF (10-50 MB) — 1-3 seconds
- Large XISF (> 50 MB) — 3-10 seconds

### Optimization Tips

**Tip 1: Use Details View for Large Libraries**
If you have thousands of XISF files:
1. Use **Details view** instead of Thumbnail view
2. This avoids generating thumbnails for all files
3. Preview still renders when you select a file
4. Much faster for browsing large folders

[Screenshot: Details view showing files efficiently]

**Tip 2: Disable Thumbnail Preview Temporarily**
If your system is slow:
1. Open Settings app
2. Toggle **"Preview/Thumbnail Handler"** to OFF
3. Restart Explorer
4. Re-enable when you need to see thumbnails

**Tip 3: Clear Thumbnail Cache**
If thumbnails are out of date or corrupted:
```powershell
Remove-Item -Path "$env:LOCALAPPDATA\Microsoft\Windows\Explorer" `
  -Recurse -Force
```
Then restart Explorer. Thumbnails will regenerate on next view.

**Tip 4: Use Network Locations Carefully**
If your XISF files are on a network drive:
1. Thumbnail generation is slower over the network
2. Use local copies for faster browsing when possible
3. Or use Details view to avoid thumbnail generation

### System Resource Usage

**CPU Usage During Thumbnail Generation:**
- Single-threaded processing (one file at a time)
- Each file uses 50-200% of one CPU core
- Completes in a few seconds per image

**Memory Usage:**
- Minimal; each image processed then discarded
- No significant long-term memory leak

**Disk Usage:**
- Thumbnail cache grows as you browse more folders
- Windows limits cache size automatically
- Cache location: `%LOCALAPPDATA%\Microsoft\Windows\Explorer\`

### Disabling Features for Better Performance

If your system is slow, consider:

**Option 1: Disable Preview Handler**
- Pros: Significantly faster Explorer browsing
- Cons: No thumbnail previews or preview pane
- Settings: Settings app → Preview/Thumbnail toggle OFF

**Option 2: Use Details View**
- Pros: Fast browsing without thumbnails
- Cons: Can't preview images visually
- How: View menu → Details

**Option 3: Limit Indexed Folders**
- Pros: Reduces background indexing CPU usage
- Cons: Search limited to indexed folders
- How: Windows Settings → Search → Indexed locations

## Customization

### Adjusting Preview Quality

The preview rendering uses automatic quality settings:
- **Brightness/Contrast:** Auto-adjusted for visibility
- **Scaling:** High-quality algorithm (not pixelated)
- **Color:** Full color for RGB, grayscale for single-channel

> **Note:** Adjusting quality settings is not currently exposed in the Settings app. The preview rendering is optimized for visibility and uses intelligent defaults.

### Preview Pane Appearance

The preview pane appearance is controlled by Windows Explorer theme:
- **Light theme** — Light background, dark text
- **Dark theme** — Dark background, light text
- Set in Windows Settings → Personalization → Colors

### Thumbnail Size in Explorer

To change thumbnail size globally:
1. Open Explorer
2. **View** menu → **Zoom** → Adjust percentage
3. Or hold **Ctrl** and scroll mouse wheel

Larger thumbnails use more screen space.

### Creating File Associations

To set a default application for opening XISF files:
1. Right-click an XISF file
2. Select **"Open with"** → **"Choose another app"**
3. Select your preferred image viewer
4. Check **"Always use this app"**
5. Click **OK**

The chosen app will open when you double-click XISF files.

## Troubleshooting

### Preview Pane Is Blank or Shows Generic Icon

**Symptom:** Selected XISF file shows a generic document icon instead of preview

**Solutions:**
1. Verify Preview Handler is enabled:
   - Settings app → Check "Preview/Thumbnail Handler" toggle is ON
   - Restart Explorer (Task Manager → Explorer → Restart)

2. Verify the file is readable:
   - Right-click the file → Properties
   - Ensure you have Read permission
   - Try a different XISF file

3. Try enabling Preview pane again:
   - View → Preview pane (toggle off, then on)
   - Restart Explorer

4. Restart your computer if still not working

### Thumbnail Generation Is Very Slow

**Symptom:** Takes many seconds to see thumbnails in a folder

**Solutions:**
1. **Check your system resources:**
   - Open Task Manager (Ctrl+Shift+Esc)
   - Check CPU and disk usage
   - Close other programs using resources

2. **Use Details view instead:**
   - View → Details
   - Faster than waiting for thumbnails

3. **Disable and re-enable the handler:**
   - Settings app → Preview/Thumbnail toggle OFF
   - Restart Explorer
   - Settings app → Preview/Thumbnail toggle ON
   - Restart Explorer

4. **Verify XISF files aren't corrupted:**
   - Try viewing different XISF files
   - If all are slow, it's likely a system resource issue
   - If specific files are slow, the files may be large or complex

### Thumbnail Stays Out of Date

**Symptom:** Thumbnail doesn't update after modifying the file

**Solutions:**
1. **Clear the thumbnail cache:**
   ```powershell
   Remove-Item -Path "$env:LOCALAPPDATA\Microsoft\Windows\Explorer" `
     -Recurse -Force
   ```

2. **Restart Explorer:**
   - Task Manager → Windows Explorer → Restart
   - Or restart your computer

3. **Delete the specific thumbnail:**
   - The cache is stored in a system database
   - Best to clear entire cache (as above)

### Preview Pane Shows Wrong Image or Histogram

**Symptom:** Preview doesn't match the selected file or shows incorrect histogram

**Solutions:**
1. Click on a different file, then back to the original
2. Restart Explorer
3. Verify the XISF file isn't being written to (by another application)
4. Check if the file is corrupted by trying to open it in PixInsight or another tool

### Histogram Is Not Displaying

**Symptom:** Preview shows image but histogram is blank

**Solutions:**
1. The image may lack proper pixel statistics headers
2. This is rarely a problem — most XISF files have histogram data
3. Try another XISF file to confirm
4. If the issue persists, the XISF file may be non-standard

### Thumbnails Disappear After Windows Update

**Symptom:** Thumbnails were working but disappeared after updating Windows

**Solutions:**
1. Reinstall XISF Shell Extensions:
   - Control Panel → Programs and Features → XISF Shell Extensions → Uninstall
   - Restart computer
   - Reinstall using MSI
   - Restart Explorer

2. Or re-register the handler:
   - Settings app → Preview/Thumbnail toggle OFF
   - Restart Explorer
   - Settings app → Preview/Thumbnail toggle ON
   - Restart Explorer

### Some XISF Files Show Thumbnails, Others Don't

**Symptom:** Inconsistent thumbnail display across files

**Solutions:**
1. Check file permissions:
   - Right-click files without thumbnails
   - Properties → Security → Verify you have Read permission

2. Try different file types:
   - Verify files are valid XISF format (.xisf extension)
   - Open one in PixInsight to confirm it's valid

3. Check file size:
   - Very large files (> 500 MB) may not generate thumbnails due to time limits
   - In this case, details view is recommended

4. Clear and regenerate cache:
   ```powershell
   Remove-Item -Path "$env:LOCALAPPDATA\Microsoft\Windows\Explorer" `
     -Recurse -Force
   ```

---

## Next Steps

- **[Handlers Overview](handlers-overview.md)** — Learn about all handlers
- **[Search Indexing](search-indexing.md)** — Make your images searchable
- **[Catalog Management](catalog-management.md)** — Enable object recognition
- **[Getting Started](../getting-started.md)** — Back to basics

## Related Documentation

- **[Pixel Statistics](../features/pixel-statistics.md)** — Understand the numbers in metadata
- **[Computed Properties](../features/computed-properties.md)** — What metadata is calculated
