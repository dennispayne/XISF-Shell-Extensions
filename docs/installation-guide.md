# Installation Guide

XISF Shell Extensions adds powerful Windows Explorer integration for XISF astronomy image files, enabling previews, thumbnails, metadata display, and deep-sky catalog search. This guide walks you through installation and initial setup.

## Table of Contents
- [System Requirements](#system-requirements)
- [Installation (MSI Method)](#installation-msi-method)
- [Installation (Developer Method)](#installation-developer-method)
- [Verification](#verification)
- [Post-Installation Setup](#post-installation-setup)
- [Uninstallation](#uninstallation)
- [Troubleshooting](#troubleshooting)

## System Requirements

### Minimum Requirements
- **Windows 10** version 2004 (build 19041) or later
- **Windows 11** recommended for best performance
- **Administrator privileges** required for installation
- **4 GB RAM** minimum (8 GB recommended for large image libraries)
- **300 MB** free disk space for installation

### Supported Components
- Windows Explorer
- Windows Search (optional, for indexing)
- Compatible with all Windows image viewers and shell utilities

## Installation (MSI Method)

### Recommended for All Users

The MSI (Microsoft Installer) is the easiest and most reliable way to install XISF Shell Extensions.

### Step 1: Download the Installer

1. Navigate to the [XISF Shell Extensions releases page](https://github.com/dennispayne/XISF-Shell-Extensions/releases/latest)
2. Download the file named **`XISF.ShellExtensions_<version>_x64.msi`**
   - Example: `XISF.ShellExtensions_1.0.0_x64.msi`
3. Save the file to a location on your computer (e.g., Downloads folder)

> **Note:** Only 64-bit (x64) installation is currently supported.

### Step 2: Run the Installer

1. **Right-click** the downloaded `.msi` file
2. Select **"Run as administrator"**
   - [Screenshot: "Run as administrator" context menu]
3. Click **"Install"** when the installer window appears
   - [Screenshot: Installer welcome dialog]

### Step 3: Wait for Installation

The installer will:
- Copy shell handler files to `C:\Program Files\XISF Shell Extensions\`
- Register COM components with Windows
- Install the XISF Shell Extension Settings app
- Add items to the Start menu

This typically takes 30-60 seconds.

### Step 4: Restart Windows Explorer

After installation completes:

1. **Press `Ctrl+Shift+Esc`** to open Task Manager
2. Find **"Windows Explorer"** in the process list
3. Click to select it and click **"Restart"**
   - [Screenshot: Task Manager with Explorer selected]
4. Explorer will restart automatically

Alternatively, you can restart your computer to ensure all changes take effect.

## Installation (Developer Method)

For development or when an MSI is unavailable, you can install handlers manually.

### Prerequisites
- Visual Studio 2022 with C++ and Windows SDK workloads
- PowerShell 5.1 or PowerShell 7+
- Administrator privileges

### Steps

1. Clone and build the repository:
   ```powershell
   git clone https://github.com/dennispayne/XISF-Shell-Extensions.git
   cd XISF-Shell-Extensions
   # Open Win11-XISF-Shell-Extensions.sln in Visual Studio
   # Build the solution (Release|x64 configuration)
   ```

2. Register handlers:
   - Navigate to the build output directory (`x64\Release`)
   - Run **`XISFShellExtensionHost.exe`**
   - Click the toggle buttons to register handlers
   - [Screenshot: Settings app toggle buttons]

3. Restart Explorer (as described above)

See the [Contributing Guide](../CONTRIBUTING.md) for detailed build instructions.

## Verification

### Check Installation Success

After restarting Explorer, verify installation by:

1. **Open File Explorer** and navigate to a folder containing XISF files
   - [Screenshot: Explorer with .xisf files]

2. **Select an XISF file** and check:
   - ✓ A **thumbnail preview** appears in the preview pane (right side)
   - ✓ Detailed **metadata** shows in the Details pane at the bottom
   - ✓ File properties display in the info tooltip

3. **Open Settings app**:
   - Press the **Windows key** and search for "**XISF Shell Extension Settings**"
   - Or click **Start → XISF Shell Extension Settings**
   - You should see toggle buttons for each handler
   - [Screenshot: Settings app main window]

### Test Each Handler

| Handler | How to Test | Expected Result |
|---------|------------|-----------------|
| **Property Handler** | Select an XISF file and look at the Details pane | Metadata like "Object Name", "Camera", "Exposure" appears |
| **Preview Handler** | Select an XISF file | Thumbnail and preview image display in preview pane |
| **Search Filter** | Open Windows Search and search for "NGC" or an object name | XISF files with matching metadata appear in results |

## Post-Installation Setup

### Enable Handlers

After installation, all handlers are enabled by default. To verify or change settings:

1. Open **XISF Shell Extension Settings** (Start menu or `XISFShellExtensionHost.exe`)
2. Toggle switches control each handler:
   - **Property Handler** — Details pane metadata
   - **Preview/Thumbnail Handler** — Image previews and thumbnails
   - **Search Filter** — Windows Search integration

> **Tip:** You can disable any handler if you don't need it. Changes take effect on the next file selection in Explorer.

### (Optional) Install Catalogs

To enable deep-sky object recognition and constellation mapping:

1. Open **XISF Shell Extension Settings**
2. Click **"Install / Update from GitHub"** to download NGC, IC, and Sharpless catalogs
   - Requires internet connection
   - Installation takes ~30 seconds
   - [Screenshot: Catalog installation dialog]

For offline installation or air-gapped systems, see [Catalog Management](user-guide/catalog-management.md).

### (Optional) Configure Windows Search Indexing

To make XISF metadata searchable from the Windows Search box:

1. Press **Windows key + I** to open Windows Settings
2. Go to **"Privacy & Security"** → **"Searching Windows"**
3. Verify that your XISF file folders are in the indexed locations
   - [Screenshot: Windows Settings search configuration]

> **Note:** Windows Search indexing is optional. XISF metadata will still display in Explorer Details pane even if search indexing is not enabled.

## Uninstallation

### Using Windows Settings (Recommended)

1. Press **Windows key + I** to open Settings
2. Go to **"Apps"** → **"Installed apps"**
3. Search for "**XISF**"
4. Click the three dots (**⋯**) next to "XISF Shell Extensions"
5. Select **"Uninstall"**
6. Confirm the uninstallation prompt

### Using Control Panel

1. Open **Control Panel**
2. Go to **"Programs and Features"** or **"Programs"** → **"Programs and Features"**
3. Find **"XISF Shell Extensions"** in the list
4. Click **"Uninstall"**
5. Confirm the uninstallation

### Using PowerShell (Advanced)

Find the product code:
```powershell
Get-WmiObject -Class Win32_Product | Select-Object Name, IdentifyingNumber | 
  Where-Object { $_.Name -like "*XISF*" }
```

Then uninstall:
```powershell
msiexec /x {PRODUCT_CODE}
```

Replace `{PRODUCT_CODE}` with the IdentifyingNumber from the first command.

### Restart Explorer

After uninstalling:
1. Open **Task Manager** (`Ctrl+Shift+Esc`)
2. Select **Windows Explorer** and click **Restart**

All XISF Shell Extensions features will be removed from Windows Explorer.

## Troubleshooting

### Handlers Not Appearing After Installation

**Symptom:** Metadata or previews don't show for XISF files

**Solutions:**
1. Restart Windows Explorer (Task Manager → Windows Explorer → Restart)
2. Verify the Settings app shows handlers are enabled
3. Restart your computer entirely
4. Reinstall: Uninstall → Restart → Reinstall using MSI

### "Administrator privileges required" Error

**Symptom:** Installation fails with permission error

**Solutions:**
1. Right-click the MSI and select **"Run as administrator"**
2. Disable any antivirus temporarily during installation
3. Ensure your user account has administrator privileges
4. Run the installer from an admin PowerShell:
   ```powershell
   msiexec /i "C:\path\to\XISF.ShellExtensions_1.0.0_x64.msi"
   ```

### Preview Pane Blank or Slow

**Symptom:** Thumbnails or previews are missing or very slow to appear

**Solutions:**
1. Check that the Preview Handler is enabled in Settings
2. Verify Windows has read access to the XISF file
3. For large or complex XISF files, preview generation may take several seconds — this is normal
4. Disable and re-enable the handler: Settings → Preview/Thumbnail toggle → restart Explorer

### Search Indexing Not Working

**Symptom:** "Windows Search doesn't find my XISF files"

**Solutions:**
1. Verify the Search Filter is enabled in Settings
2. Ensure your XISF folder is indexed:
   - Windows Settings → Privacy & Security → Searching Windows → Indexed locations
3. Rebuild the Windows Search index:
   - Windows Settings → Privacy & Security → Searching Windows → Advanced → Rebuild index
4. Wait a few minutes for the index to rebuild and re-index your files

### Settings App Won't Launch

**Symptom:** "XISF Shell Extension Settings" doesn't open

**Solutions:**
1. Verify installation completed successfully
2. Try launching from the Start menu (search "XISF Shell Extension Settings")
3. Manually launch `XISFShellExtensionHost.exe`:
   ```powershell
   & "C:\Program Files\XISF Shell Extensions\XISFShellExtensionHost.exe"
   ```
4. Reinstall the application if it still fails

### Antivirus or System Protection Warnings

**Symptom:** Antivirus blocks installation or marks as suspicious

**Solutions:**
1. This is normal for unsigned shell extensions; add to antivirus whitelist if desired
2. See [Security](../SECURITY.md) for information on signing and verification
3. Verify the MSI signature:
   ```powershell
   Get-AuthenticodeSignature "C:\path\to\XISF.ShellExtensions_1.0.0_x64.msi"
   ```

## Getting Help

- Check [Getting Started Guide](getting-started.md) for first steps
- See [User Guide](user-guide/handlers-overview.md) for feature details
- Visit the [Troubleshooting Reference](reference/troubleshooting.md) for more help
- Open an issue on [GitHub](https://github.com/dennispayne/XISF-Shell-Extensions/issues)

---

**Next Steps:** [Getting Started](getting-started.md)
