<#
.SYNOPSIS
    Pre-build step for handler DLL projects (Debug only).
    Releases file locks held by Explorer or other shell hosts so the linker
    can overwrite the DLL.

.DESCRIPTION
    Checks whether the target DLL is locked. If so, kills explorer.exe,
    prevhost.exe (preview handler host), and SearchProtocolHost.exe (indexer)
    to release the lock, then waits for the file to become writable.

    If the DLL is not locked (or does not yet exist), exits immediately
    with no side effects.

.PARAMETER DllPath
    Full path to the output DLL ($(TargetPath) from MSBuild).
#>
param(
    [Parameter(Mandatory)]
    [string]$DllPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Nothing to release if the file doesn't exist yet (first build).
if (-not (Test-Path $DllPath)) {
    exit 0
}

function Test-FileLocked([string]$Path) {
    try {
        [System.IO.File]::Open($Path, 'Open', 'ReadWrite', 'None').Close()
        return $false
    }
    catch {
        return $true
    }
}

if (-not (Test-FileLocked $DllPath)) {
    exit 0
}

Write-Host "** DLL is locked: $(Split-Path $DllPath -Leaf)"
Write-Host "** Stopping shell host processes to release lock..."

# Kill all known hosts that load handler DLLs in-process.
$hosts = @('explorer', 'prevhost', 'SearchProtocolHost')
foreach ($name in $hosts) {
    Get-Process -Name $name -ErrorAction SilentlyContinue |
        ForEach-Object {
            Write-Host "   Stopping $($_.Name) (PID $($_.Id))"
            Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue
        }
}

# Wait for the file lock to be released (up to 10 seconds).
$timeout = [DateTime]::UtcNow.AddSeconds(10)
while ((Test-FileLocked $DllPath) -and ([DateTime]::UtcNow -lt $timeout)) {
    Start-Sleep -Milliseconds 250
}

if (Test-FileLocked $DllPath) {
    Write-Warning "DLL is still locked after timeout. Build may fail."
    exit 1
}

Write-Host "** Lock released. Build proceeding."
exit 0
