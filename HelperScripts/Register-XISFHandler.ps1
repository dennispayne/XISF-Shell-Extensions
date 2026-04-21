#Requires -RunAsAdministrator
<#
.SYNOPSIS
    Registers or unregisters XISF shell handler DLLs.
.DESCRIPTION
    Supports the Property Handler and the Preview/Thumbnail Handler.
    For registration, the script performs unregister -> Explorer restart -> register
    to avoid Explorer in-process DLL caching.
.PARAMETER Handler
    One or more handlers to process: Property, Preview.
.PARAMETER Configuration
    Build configuration to use. Defaults to Debug.
.PARAMETER Unregister
    If specified, unregisters the selected handler(s).
.PARAMETER FlushCache
    Flush the Windows thumbnail cache. Only applies when the Preview handler is included.
    Defaults to true.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory, Position = 0)]
    [ValidateSet('Property', 'Preview')]
    [string[]]$Handler,

    [Parameter(Position = 1)]
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',

    [switch]$Unregister,

    [switch]$FlushCache = $true
)

$ErrorActionPreference = 'Stop'

$handlers = @{
    'Property' = @{
        Label   = 'Property Handler'
        DllPath = Join-Path $PSScriptRoot "..\x64\$Configuration\XISFPropertyHandler.dll"
        CLSIDs  = @('{7C54FA8B-9D63-4C10-8FBE-1A5A0F9A3B2E}')
    }
    'Preview' = @{
        Label   = 'Preview / Thumbnail Handler'
        DllPath = Join-Path $PSScriptRoot "..\x64\$Configuration\XISFPreviewHandler.dll"
        CLSIDs  = @('{9C76E8AD-4E85-5F30-B00D-3C7D1AB5F6E0}', '{AD87F6CE-5B03-6E41-C11E-4DB2AC06F5F1}')
    }
}

function Restart-Explorer {
    Write-Host "Restarting Explorer..."
    Stop-Process -Name explorer -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 2
    Start-Process explorer
}

function Remove-Clsids([string[]]$clsids) {
    foreach ($clsid in $clsids) {
        foreach ($root in @("HKLM:\SOFTWARE\Classes\CLSID\$clsid", "Registry::HKEY_CLASSES_ROOT\CLSID\$clsid")) {
            if (Test-Path $root) {
                Remove-Item -Path $root -Recurse -Force -ErrorAction SilentlyContinue
                Write-Host "  Cleaned: $root"
            }
        }
    }
}

$shouldFlushCache = $FlushCache -and ($Handler -contains 'Preview')

foreach ($h in $Handler) {
    $info = $handlers[$h]
    $dllPath = $info.DllPath

    Write-Host '============================================'
    Write-Host " XISF Handler - $($info.Label) [$Configuration]"
    Write-Host '============================================'

    if (-not (Test-Path $dllPath)) {
        Write-Error "DLL not found: $dllPath`nBuild the project first ($Configuration|x64)."
    }

    if ($Unregister) {
        Write-Host "Unregistering: $dllPath"
        & regsvr32 /u /s $dllPath
        Remove-Clsids $info.CLSIDs
        Write-Host "$($info.Label) has been unregistered."
        continue
    }

    Write-Host "Unregistering stale registration (if any): $dllPath"
    & regsvr32 /u /s $dllPath
    Remove-Clsids $info.CLSIDs

    # Explorer caches in-process shell extension DLLs; restart between unregister/register.
    Restart-Explorer

    Write-Host "Registering: $dllPath"
    & regsvr32 /s $dllPath

    foreach ($clsid in $info.CLSIDs) {
        $inproc = "Registry::HKEY_CLASSES_ROOT\CLSID\$clsid\InProcServer32"
        if (Test-Path $inproc) {
            $registered = (Get-ItemProperty $inproc).'(default)'
            Write-Host "  $clsid -> $registered" -ForegroundColor Green
        } else {
            Write-Warning "CLSID $clsid - InProcServer32 not found after registration!"
        }
    }

    Write-Host "$($info.Label) registered."
}

if ($shouldFlushCache) {
    Write-Host "Flushing thumbnail cache..."
    Remove-Item "$env:LOCALAPPDATA\Microsoft\Windows\Explorer\thumbcache_*.db" -Force -ErrorAction SilentlyContinue
    Write-Host "  Thumbnail cache cleared."
}

Write-Host "`nDone!" -ForegroundColor Cyan
