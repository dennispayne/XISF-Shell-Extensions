<#
.SYNOPSIS
    Builds, signs, and optionally validates the XISF Shell Extension MSIX package.

.DESCRIPTION
    Produces a single MSIX containing both handlers (Property + Preview/Thumbnail)
    and the XISFShellExtensionHost.exe settings application. Individual handlers
    are enabled/disabled at runtime via HKCU flags written by the settings app.

    Steps:
      1. Stages binaries + assets into artifacts\stage\.
      2. Patches AppxManifest Identity Version/Publisher in-memory.
      3. Calls makeappx.exe pack (strict validation; no /nv).
      4. Optionally calls signtool.exe sign if a certificate is supplied.

    Assumes Release|x64 binaries have already been built to x64\Release\.

.PARAMETER Version
    Semver string, e.g. "0.1.0". A trailing ".<build>" component is appended.

.PARAMETER BuildNumber
    Fourth version component (build metadata). Defaults to 0.

.PARAMETER OutputDir
    Directory for the generated .msix. Default: artifacts\

.PARAMETER CertificatePath
    Optional path to a .pfx for signtool. If omitted, the MSIX is produced
    unsigned and must be signed separately before install.

.PARAMETER CertificatePassword
    Optional SecureString password for the PFX.

.PARAMETER Publisher
    Identity Publisher string (must match cert Subject exactly). Default: CN=Dennis Payne.

.EXAMPLE
    .\Packaging\build-msix.ps1 -Version 0.1.0 -BuildNumber 42

.EXAMPLE
    .\Packaging\build-msix.ps1 -Version 0.1.0 -CertificatePath .\self.pfx -CertificatePassword (Read-Host -AsSecureString)
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$Version,
    [int]$BuildNumber = 0,
    [string]$OutputDir,
    [string]$CertificatePath,
    [System.Security.SecureString]$CertificatePassword,
    [string]$Publisher = 'CN=Dennis Payne',
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot     = Resolve-Path (Join-Path $PSScriptRoot '..')
$releaseDir   = Join-Path $repoRoot "x64\$Configuration"
$sharedAssets = Join-Path $PSScriptRoot 'Shared\Assets'
$manifestSrc  = Join-Path $PSScriptRoot 'XISFShellExtension\Package.appxmanifest'
if (-not $OutputDir) { $OutputDir = Join-Path $repoRoot 'artifacts' }
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null

$fullVersion = "$Version.$BuildNumber"
if ($fullVersion -notmatch '^\d+\.\d+\.\d+\.\d+$') {
    throw "Version '$fullVersion' is not a 4-part version."
}

function Find-SdkTool([string]$name) {
    $roots = @(
        "${env:ProgramFiles(x86)}\Windows Kits\10\bin",
        "${env:ProgramFiles}\Windows Kits\10\bin"
    ) | Where-Object { Test-Path $_ }
    $candidates = foreach ($r in $roots) {
        Get-ChildItem -Path $r -Recurse -Filter $name -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match '\\x64\\' }
    }
    $picked = $candidates | Sort-Object FullName -Descending | Select-Object -First 1
    if (-not $picked) { throw "Could not locate $name in Windows 10 SDK." }
    return $picked.FullName
}

$makeappx = Find-SdkTool 'makeappx.exe'
$signtool = Find-SdkTool 'signtool.exe'
Write-Host "makeappx: $makeappx"
Write-Host "signtool: $signtool"

$binaries = @('XISFPropertyHandler.dll','XISFPreviewHandler.dll','XISFShellExtensionHost.exe')
foreach ($f in $binaries) {
    $p = Join-Path $releaseDir $f
    if (-not (Test-Path $p)) {
        throw "Missing build output: $p. Build the solution ($Configuration|x64) first."
    }
}

$outName = 'XISF.ShellExtension'
Write-Host "`n=== Building $outName`_${fullVersion}_x64.msix ===" -ForegroundColor Cyan

$stage = Join-Path $repoRoot 'artifacts\stage\XISFShellExtension'
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Path $stage -Force | Out-Null

foreach ($bin in $binaries) {
    Copy-Item -Path (Join-Path $releaseDir $bin) -Destination $stage -Force
}

$stageAssets = Join-Path $stage 'Assets'
New-Item -ItemType Directory -Path $stageAssets -Force | Out-Null
Copy-Item -Path (Join-Path $sharedAssets '*') -Destination $stageAssets -Force

$xml = [xml](Get-Content -LiteralPath $manifestSrc -Raw)
$xml.Package.Identity.Version   = $fullVersion
$xml.Package.Identity.Publisher = $Publisher
$dstManifest = Join-Path $stage 'AppxManifest.xml'
$xml.Save($dstManifest)

$outMsix = Join-Path $OutputDir "$outName`_${fullVersion}_x64.msix"
& $makeappx pack /d $stage /p $outMsix /o
if ($LASTEXITCODE -ne 0) { throw "makeappx failed (exit $LASTEXITCODE)" }

if ($CertificatePath) {
    if (-not (Test-Path $CertificatePath)) { throw "Certificate not found: $CertificatePath" }
    $args = @('sign','/fd','SHA256','/f', $CertificatePath)
    if ($CertificatePassword) {
        $bstr = [System.Runtime.InteropServices.Marshal]::SecureStringToBSTR($CertificatePassword)
        try {
            $plain = [System.Runtime.InteropServices.Marshal]::PtrToStringBSTR($bstr)
            $args += @('/p', $plain)
        } finally {
            [System.Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr)
        }
    }
    $args += $outMsix
    & $signtool @args
    if ($LASTEXITCODE -ne 0) { throw "signtool failed (exit $LASTEXITCODE)" }
    Write-Host "Signed: $outMsix" -ForegroundColor Green
} else {
    Write-Warning "Produced UNSIGNED package: $outMsix"
}

Write-Host "OK: $outMsix" -ForegroundColor Green
