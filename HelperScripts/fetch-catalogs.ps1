<#
.SYNOPSIS
    Downloads the OpenNGC catalog data required to build the XISF Property Handler.
.DESCRIPTION
    Fetches NGC.csv and addendum.csv from the OpenNGC GitHub repository into
    PropertyHandler\XISFPropertyHandler\data\. Skips files that already exist
    unless -Force is specified.
.PARAMETER Force
    Re-download even if files already exist.
.EXAMPLE
    .\fetch-catalogs.ps1
.EXAMPLE
    .\fetch-catalogs.ps1 -Force
#>
[CmdletBinding()]
param(
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

$dataDir = Join-Path $PSScriptRoot '..\PropertyHandler\XISFPropertyHandler\data'
if (-not (Test-Path $dataDir)) { New-Item -ItemType Directory -Path $dataDir | Out-Null }

$baseUrl = 'https://raw.githubusercontent.com/mattiaverga/OpenNGC/master/database_files'

$files = @(
    @{ Name = 'NGC.csv';      Url = "$baseUrl/NGC.csv" }
    @{ Name = 'addendum.csv'; Url = "$baseUrl/addendum.csv" }
)

foreach ($f in $files) {
    $dest = Join-Path $dataDir $f.Name
    if ((Test-Path $dest) -and -not $Force) {
        Write-Host "  OK (cached): $($f.Name)" -ForegroundColor DarkGray
        continue
    }
    Write-Host "  Downloading: $($f.Name) ..." -NoNewline
    Invoke-WebRequest -Uri $f.Url -OutFile $dest -UseBasicParsing
    $size = [math]::Round((Get-Item $dest).Length / 1KB, 1)
    Write-Host " $size KB" -ForegroundColor Green
}

Write-Host "`nCatalog data ready in: $dataDir" -ForegroundColor Cyan
