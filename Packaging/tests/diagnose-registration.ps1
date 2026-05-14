<#
.SYNOPSIS
    Dumps an authoritative XISF Shell Extensions registration report for
    post-MSI-install verification. Intended to be invoked inside a Windows
    Sandbox via wsb exec, but runs anywhere.

.DESCRIPTION
    Writes C:\Trace\diag-<tag>-<timestamp>.txt with:
      - Per-extension ShellEx entries (Preview, Thumbnail) under HKCR\.xisf
      - All five InProcServer32 keys for our CLSIDs
      - .xisf PersistentHandler + IFilter chain
      - Approved Shell Extensions entries scoped to our CLSIDs
      - DLL/EXE layout under Program Files
      - WSearch service state
      - Installed MSI product info

.NOTES
    PowerShell does NOT auto-mount the HKCR: drive (only HKLM: and HKCU:).
    Any script that uses HKCR:\... paths must call New-PSDrive. reg.exe has
    its own alias table so 'reg query HKCR\...' works either way; this trip
    has caused multiple false-alarm "MISSING" diagnostics in the past.

    The Preview handler has TWO different GUIDs that get confused in older
    diags:
      * COM CLSID:            {AD87F6CE-5B03-6E41-C11E-4DB2AC06F5F1}
      * TraceLogging provider {4FD34FD0-08B3-5D9A-8D77-B9D6705D6B75}
    Always use the CLSID for registry checks.
#>

param([string]$Tag = 'baseline')

$ErrorActionPreference = 'Continue'
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$out = "C:\Trace\diag-$Tag-$stamp.txt"
New-Item -ItemType Directory -Path 'C:\Trace' -Force | Out-Null

function Write-Section($title) {
    "" | Out-File $out -Append
    "===== $title =====" | Out-File $out -Append
}

function Get-DefaultValue($path) {
    try { (Get-ItemProperty -Path $path -Name '(default)' -ErrorAction Stop).'(default)' }
    catch { '<MISSING>' }
}

Write-Section "TAG: $Tag    TIME: $stamp"
"Computer: $env:COMPUTERNAME" | Out-File $out -Append
"OS:       $((Get-CimInstance Win32_OperatingSystem).Caption) build $((Get-CimInstance Win32_OperatingSystem).BuildNumber)" | Out-File $out -Append

# PowerShell does not auto-mount the HKCR drive. Add it so HKCR:\... paths work.
if (-not (Get-PSDrive HKCR -ErrorAction SilentlyContinue)) {
    New-PSDrive -Name HKCR -PSProvider Registry -Root HKEY_CLASSES_ROOT -Scope Script | Out-Null
}

Write-Section 'Per-extension shellex'
$keys = @{
  'HKCR:\.xisf'                                                            = '(default ProgID)'
  'HKCR:\.xisf\ShellEx\{8895b1c6-b41f-4c1c-a562-0d564250836f}'              = 'IPreviewHandler'
  'HKCR:\.xisf\ShellEx\{e357fccd-a995-4576-b01f-234630154e96}'              = 'IThumbnailProvider'
  'HKCR:\SystemFileAssociations\.xisf\ShellEx\{8895b1c6-b41f-4c1c-a562-0d564250836f}' = 'IPreviewHandler(SFA)'
  'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\PropertySystem\PropertyHandlers\.xisf' = 'PropertyHandler assoc'
  'HKCR:\CLSID\{7C54FA8B-9D63-4C10-8FBE-1A5A0F9A3B2E}\InProcServer32'       = 'Property handler DLL'
  'HKCR:\CLSID\{A3B7C8D9-E1F2-4A5B-8C6D-7E8F9A0B1C2D}\InProcServer32'       = 'PropertySheet DLL'
  'HKCR:\CLSID\{AD87F6CE-5B03-6E41-C11E-4DB2AC06F5F1}\InProcServer32'       = 'PreviewHandler DLL'
  'HKCR:\CLSID\{9C76E8AD-4E85-5F30-B00D-3C7D1AB5F6E0}\InProcServer32'       = 'ThumbnailProvider DLL'
  'HKCR:\CLSID\{B4E7F2A1-3D8C-4F5E-9A1B-6C2D8E4F7A3B}\InProcServer32'       = 'Filter DLL'
  'HKCR:\CLSID\{C5F8A3B2-4E9D-5067-AB2C-7D3E9F508B4C}\PersistentAddinsRegistered\{89BCB740-6119-101A-BCB7-00DD010655AF}' = 'IFilter persistent-handler'
  'HKCR:\.xisf\PersistentHandler'                                          = '.xisf persistent-handler'
  'HKLM:\SOFTWARE\Microsoft\Windows Search\ContentIndexer\Extensions\.xisf' = 'Search content indexer'
}
foreach ($k in $keys.Keys) {
    $v = Get-DefaultValue $k
    "{0,-95} -> {1}    [{2}]" -f $k, $v, $keys[$k] | Out-File $out -Append
}

Write-Section 'Approved Shell Extensions (XISF CLSIDs)'
try {
    $approvedPath = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Shell Extensions\Approved'
    $props = (Get-ItemProperty $approvedPath -ErrorAction SilentlyContinue).PSObject.Properties |
             Where-Object { $_.Name -match '(?i)7C54FA8B|AD87F6CE|9C76E8AD|B4E7F2A1|A3B7C8D9|C5F8A3B2' }
    if ($props) { $props | ForEach-Object { "{0}  =  {1}" -f $_.Name, $_.Value | Out-File $out -Append } }
    else { '(no XISF entries found)' | Out-File $out -Append }
} catch { "Error reading Approved key: $_" | Out-File $out -Append }

Write-Section 'XISF handler binaries on disk'
$candidatePaths = @(
    'C:\Program Files\XISF Shell Extensions',
    'C:\Program Files (x86)\XISF Shell Extensions'
)
foreach ($p in $candidatePaths) {
    if (Test-Path $p) {
        Get-ChildItem $p -Recurse -Include '*.dll','*.exe','*.propdesc','*.csv' -ErrorAction SilentlyContinue |
            ForEach-Object { "{0,12}  {1:yyyy-MM-dd HH:mm}  {2}" -f $_.Length, $_.LastWriteTime, $_.FullName | Out-File $out -Append }
    }
}

Write-Section 'WSearch service'
try {
    $svc = Get-Service WSearch -ErrorAction Stop
    "WSearch status: $($svc.Status)  startup: $($svc.StartType)" | Out-File $out -Append
    "SetupCompletedSuccessfully = $((Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows Search' -Name SetupCompletedSuccessfully -ErrorAction SilentlyContinue).SetupCompletedSuccessfully)" | Out-File $out -Append
} catch { "WSearch service not found: $_" | Out-File $out -Append }

Write-Section 'Installed MSI products (XISF)'
Get-CimInstance Win32_Product -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -match 'XISF' -or $_.Vendor -match 'Dennis Payne' } |
    Select-Object Name,Version,InstallDate,IdentifyingNumber |
    Format-List | Out-String | Out-File $out -Append

Write-Section 'DONE'
"Wrote $out"
