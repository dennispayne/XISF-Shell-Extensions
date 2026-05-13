<#
.SYNOPSIS
    Regenerates Installer\XISFInstaller\License.rtf from the repo-root LICENSE.

.DESCRIPTION
    Wired into the WiX installer build via XISFInstaller.wixproj's
    RegenerateLicenseRtf target so the EULA shown in the MSI's
    LicenseAgreementDlg always matches the canonical LICENSE file.

    The output is a plain RTF 1 document with the first line bold (treated
    as the title, e.g. "MIT License") and subsequent lines flowed as
    paragraphs. Blank lines in LICENSE become blank paragraphs.

.PARAMETER LicensePath
    Source plain-text license. Defaults to <repo>\LICENSE.

.PARAMETER OutputPath
    Target RTF file. Defaults to <repo>\Installer\XISFInstaller\License.rtf.
#>
[CmdletBinding()]
param(
    [string] $LicensePath,
    [string] $OutputPath
)

$ErrorActionPreference = 'Stop'

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot '..')
if (-not $LicensePath) { $LicensePath = Join-Path $repoRoot 'LICENSE' }
if (-not $OutputPath)  { $OutputPath  = Join-Path $repoRoot 'Installer\XISFInstaller\License.rtf' }

if (-not (Test-Path $LicensePath)) {
    throw "LICENSE not found at $LicensePath"
}

function Convert-ToRtfText {
    param([string] $Text)
    $sb = New-Object System.Text.StringBuilder
    foreach ($ch in $Text.ToCharArray()) {
        switch ($ch) {
            '\' { [void]$sb.Append('\\') }
            '{' { [void]$sb.Append('\{') }
            '}' { [void]$sb.Append('\}') }
            default {
                $code = [int]$ch
                if ($code -lt 0x80) { [void]$sb.Append($ch) }
                else { [void]$sb.AppendFormat('\u{0}?', $code) }
            }
        }
    }
    return $sb.ToString()
}

$raw = (Get-Content $LicensePath -Raw) -replace "`r`n", "`n"
$lines = $raw -split "`n"
# Drop trailing empty line that always follows the final newline.
while ($lines.Count -gt 0 -and [string]::IsNullOrEmpty($lines[-1])) {
    $lines = $lines[0..($lines.Count - 2)]
}

$rtf = New-Object System.Text.StringBuilder
[void]$rtf.AppendLine('{\rtf1\ansi\ansicpg1252\deff0\nouicompat\deflang1033')
[void]$rtf.AppendLine('{\fonttbl{\f0\fnil\fcharset0 Calibri;}}')
[void]$rtf.AppendLine('{\*\generator XISF Shell Extensions installer build (scripts\Convert-LicenseToRtf.ps1)}')
[void]$rtf.AppendLine('\viewkind4\uc1\pard\sa120\sl276\slmult1\f0\fs20')

$first = $true
foreach ($line in $lines) {
    $trim = $line.TrimEnd()
    if ($first) {
        [void]$rtf.Append('\b ')
        [void]$rtf.Append((Convert-ToRtfText $trim))
        [void]$rtf.AppendLine('\b0\par')
        $first = $false
        continue
    }
    if ($trim -eq '') {
        [void]$rtf.AppendLine('\par')
    } else {
        [void]$rtf.Append((Convert-ToRtfText $trim))
        [void]$rtf.AppendLine('\par')
    }
}
[void]$rtf.AppendLine('}')

# Skip the write if content is unchanged so MSBuild incremental build stays clean.
$new = $rtf.ToString()
$writeIt = $true
if (Test-Path $OutputPath) {
    $existing = [System.IO.File]::ReadAllText($OutputPath, [System.Text.Encoding]::UTF8)
    if ($existing -eq $new) { $writeIt = $false }
}

if ($writeIt) {
    [System.IO.File]::WriteAllText($OutputPath, $new, (New-Object System.Text.UTF8Encoding $false))
    Write-Host "Updated $OutputPath ($($new.Length) bytes)"
} else {
    Write-Host "License.rtf is up to date."
}
