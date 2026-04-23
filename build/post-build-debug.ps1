<#
.SYNOPSIS
    Post-build step for handler DLL projects (Debug only).
    Ensures Explorer is running after a build that may have killed it.

.DESCRIPTION
    If explorer.exe is not running (because pre-build-debug.ps1 killed it
    to release a DLL lock), this script starts it and waits briefly for the
    shell to initialize before F5 attach.
#>

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (Get-Process -Name explorer -ErrorAction SilentlyContinue) {
    exit 0
}

Write-Host "** Explorer is not running. Starting shell..."
Start-Process explorer.exe

# Wait up to 10 seconds for Explorer to appear.
$timeout = [DateTime]::UtcNow.AddSeconds(10)
while (-not (Get-Process -Name explorer -ErrorAction SilentlyContinue) -and
       ([DateTime]::UtcNow -lt $timeout)) {
    Start-Sleep -Milliseconds 500
}

if (Get-Process -Name explorer -ErrorAction SilentlyContinue) {
    # Brief pause for shell to fully initialize before VS tries to attach.
    Start-Sleep -Seconds 1
    Write-Host "** Explorer is running."
} else {
    Write-Warning "Explorer did not start within timeout."
}

exit 0
