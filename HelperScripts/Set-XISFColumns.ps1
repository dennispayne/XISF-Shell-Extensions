<#
.SYNOPSIS
    Configures a folder's Explorer view to show the most useful XISF columns.
.DESCRIPTION
    Creates or updates the desktop.ini in the target folder to set Details view
    with the most important XISF property columns pre-selected and ordered.
    Requires the XISF Property Handler to be registered.
.PARAMETER FolderPath
    Path to the folder containing .xisf files.
.PARAMETER Reset
    Remove the custom view and revert to default Explorer settings.
.EXAMPLE
    .\Set-XISFColumns.ps1 -FolderPath 'D:\Astro\M 31\2024-08-15\LIGHT'
.EXAMPLE
    .\Set-XISFColumns.ps1 'D:\Astro\M 31\2024-08-15\LIGHT'
.EXAMPLE
    .\Set-XISFColumns.ps1 -FolderPath 'D:\Astro' -Reset
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory, Position = 0)]
    [string]$FolderPath,

    [switch]$Reset
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $FolderPath -PathType Container)) {
    Write-Error "Folder not found: $FolderPath"
}

$FolderPath = (Resolve-Path $FolderPath).Path

# The bag key stores per-folder view settings in the registry.
# We use the Shell Bags approach: set the folder type hint via desktop.ini
# and let Explorer pick up the column layout from the FullDetails registry.
#
# For a more direct approach, we write a BagMRU entry. However, the simplest
# reliable method is to configure the folder as a "Pictures" type (which
# triggers Details view) and set the FolderType.

$desktopIni = Join-Path $FolderPath 'desktop.ini'

if ($Reset) {
    if (Test-Path $desktopIni) {
        attrib -s -h $desktopIni 2>$null
        Remove-Item $desktopIni -Force
        attrib -r $FolderPath 2>$null
        Write-Host "Reset: Removed custom view from $FolderPath" -ForegroundColor Yellow
    } else {
        Write-Host "No custom view to reset in $FolderPath" -ForegroundColor DarkGray
    }
    return
}

# Column definitions — property canonical names in display order.
# These match the PKEYs registered by DllRegisterServer in dllmain.cpp.
$columns = @(
    'System.ItemNameDisplay'
    'System.Size'
    'XISF.ObjectName'
    'XISF.ExposureTime'
    'XISF.FilterName'
    'XISF.ImageType'
    'XISF.Gain'
    'XISF.SensorTemperature'
    'XISF.CameraModel'
    'XISF.Telescope'
    'XISF.FocalLength'
    'XISF.FNumber'
    'XISF.Binning'
    'XISF.DateObserved'
    'XISF.RAHour'
    'XISF.DecBand'
    'XISF.Constellation'
    'XISF.MatchedObjects'
    'XISF.Airmass'
    'XISF.PierSide'
    'XISF.Software'
)

$columnList = 'prop:' + ($columns -join ';')

# Write desktop.ini to hint Explorer about the folder type and view mode.
$iniContent = @"
[.ShellClassInfo]
FolderType=Generic
[ExtShellFolderViews]
{BE098140-A513-11D0-A3A4-00C04FD706EC}={BE098140-A513-11D0-A3A4-00C04FD706EC}
[{BE098140-A513-11D0-A3A4-00C04FD706EC}]
Mode=4
LogicalViewMode=1
ColumnList=$columnList
"@

# Remove existing desktop.ini attributes if present
if (Test-Path $desktopIni) {
    attrib -s -h $desktopIni 2>$null
}

Set-Content -Path $desktopIni -Value $iniContent -Encoding Unicode -Force

# Set system + hidden on desktop.ini (required by Explorer)
attrib +s +h $desktopIni 2>$null
# Set read-only on the folder (tells Explorer to read desktop.ini)
attrib +r $FolderPath 2>$null

Write-Host '============================================' -ForegroundColor Cyan
Write-Host ' XISF Column View Configured' -ForegroundColor Cyan
Write-Host '============================================' -ForegroundColor Cyan
Write-Host ''
Write-Host "Folder: $FolderPath"
Write-Host "View:   Details with $($columns.Count) XISF columns"
Write-Host ''
Write-Host 'Columns enabled:' -ForegroundColor Green
foreach ($col in $columns) {
    $displayName = $col -replace '^(System\.|XISF\.)', ''
    Write-Host "  - $displayName"
}
Write-Host ''
Write-Host 'Open the folder in Explorer to see the columns.' -ForegroundColor Cyan
Write-Host 'If columns are not visible, right-click a column header -> More...' -ForegroundColor DarkGray
Write-Host 'and select the XISF properties from the list.' -ForegroundColor DarkGray
Write-Host ''
Write-Host 'Note: Explorer caches folder views aggressively. If the view does' -ForegroundColor DarkGray
Write-Host 'not update, close all Explorer windows and reopen the folder.' -ForegroundColor DarkGray
