<#
.SYNOPSIS
    Functional validation script that runs INSIDE a Windows Sandbox after
    MSIX installation to test the XISF Shell Extension end-to-end.

.DESCRIPTION
    Installs the MSIX package, then validates: package registration,
    VC runtime availability, COM activation, property handler via
    Shell.Application, settings app, registry toggle, and feature tier.

    Expects:
      - C:\Installer\test.cer         (signing certificate)
      - C:\Installer\XISF.ShellExtension_0.99.0.0_x64.msix
      - C:\TestData\test.xisf         (test file with known metadata)

    Writes:
      - C:\Results\functional-results.json  (atomic via .tmp rename)
      - C:\Results\done.marker

    Calls shutdown /s /t 0 at the very end.
#>

$ErrorActionPreference = 'Continue'

# ---------------------------------------------------------------------------
# Result accumulator
# ---------------------------------------------------------------------------
$results = @{
    pass = [System.Collections.Generic.List[string]]::new()
    fail = [System.Collections.Generic.List[hashtable]]::new()
    info = @{}
}

function Assert {
    param(
        [string]$Name,
        [bool]  $Condition,
        [string]$Detail = ''
    )
    if ($Condition) {
        $results.pass.Add($Name)
    } else {
        $results.fail.Add(@{ name = $Name; detail = $Detail })
    }
}

# Ensure results directory exists
New-Item -ItemType Directory -Path 'C:\Results' -Force -ErrorAction SilentlyContinue | Out-Null

# Constants
$certPath  = 'C:\Installer\test.cer'
$msixPath  = 'C:\Installer\XISF.ShellExtension_0.99.0.0_x64.msix'
$xisfPath  = 'C:\TestData\test.xisf'
$pkgName   = 'DennisPayne.XISF.ShellExtension'
$regBase   = 'HKCU:\Software\DennisPayne\XISF Shell Extension'

$PropertyHandlerClsid   = '7C54FA8B-9D63-4C10-8FBE-1A5A0F9A3B2E'
$ThumbnailProviderClsid = '9C76E8AD-4E85-5F30-B00D-3C7D1AB5F6E0'
$PreviewHandlerClsid    = 'AD87F6CE-5B03-6E41-C11E-4DB2AC06F5F1'

# ---------------------------------------------------------------------------
# Helper: read properties from a .xisf file via the Windows Property Store.
# Uses SHGetPropertyStoreFromParsingName for reliable access to custom props.
# Falls back to Shell.Application.GetDetailsOf for standard columns.
# Returns a hashtable of property-name => value for non-empty values.
# ---------------------------------------------------------------------------
function Get-ShellProperties {
    param([string]$FilePath)

    $props = @{}

    # Method 1: Shell.Application — GetDetailsOf for standard columns, 
    # then ExtendedProperty for our known XISF property names
    try {
        $shell  = New-Object -ComObject Shell.Application
        $folder = $shell.NameSpace((Split-Path $FilePath))
        if ($folder) {
            $item = $folder.ParseName((Split-Path $FilePath -Leaf))
            if ($item) {
                # Standard columns 0-349
                for ($i = 0; $i -lt 350; $i++) {
                    $header = $folder.GetDetailsOf($null, $i)
                    $value  = $folder.GetDetailsOf($item, $i)
                    if ($value -and $value.Length -gt 0) {
                        $props[$header] = $value
                    }
                }

                # Query known Astro.* properties by canonical name
                $astroProps = @(
                    'Astro.Object', 'Astro.ExposureTime', 'Astro.CameraModel',
                    'Astro.Filter', 'Astro.Gain', 'Astro.Offset', 'Astro.SensorTemp',
                    'Astro.RA', 'Astro.Dec', 'Astro.Telescope', 'Astro.FocalLength',
                    'Astro.SiteLat', 'Astro.SiteLong', 'Astro.DateObs', 'Astro.Airmass',
                    'Astro.Binning', 'Astro.Constellation', 'Astro.DataState',
                    'System.Photo.CameraModel', 'System.Photo.ExposureTime',
                    'System.Keywords', 'System.Title'
                )
                foreach ($propName in $astroProps) {
                    try {
                        $val = $item.ExtendedProperty($propName)
                        if ($null -ne $val -and "$val" -ne '') {
                            $props[$propName] = "$val"
                        }
                    } catch {}
                }
            }
        }
    } catch {
        $results.info['ShellPropertyError'] = $_.Exception.Message
    }

    # Method 2: Direct property store access for our known custom properties
    try {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

[StructLayout(LayoutKind.Sequential, Pack = 4)]
public struct PROPERTYKEY {
    public Guid fmtid;
    public uint pid;
}

[ComImport, Guid("886D8EEB-8CF2-4446-8D02-CDBA1DBDCF99"),
 InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
public interface IPropertyStore {
    int GetCount(out uint cProps);
    int GetAt(uint iProp, out PROPERTYKEY pkey);
    int GetValue(ref PROPERTYKEY key, out PropVariant pv);
    int SetValue(ref PROPERTYKEY key, ref PropVariant pv);
    int Commit();
}

[StructLayout(LayoutKind.Explicit, Size = 24)]
public struct PropVariant {
    [FieldOffset(0)] public ushort vt;
    [FieldOffset(8)] public IntPtr pVal;
    [FieldOffset(8)] public long iVal;
    [FieldOffset(8)] public double dVal;

    public string AsString() {
        switch (vt) {
            case 31: return Marshal.PtrToStringUni(pVal);  // VT_LPWSTR
            case 8:  return Marshal.PtrToStringBSTR(pVal); // VT_BSTR
            case 5:  return dVal.ToString();               // VT_R8
            case 4:  return BitConverter.ToSingle(BitConverter.GetBytes((int)iVal), 0).ToString(); // VT_R4
            case 3:  return ((int)iVal).ToString();        // VT_I4
            case 19: return ((uint)iVal).ToString();       // VT_UI4
            case 2:  return ((short)iVal).ToString();      // VT_I2
            case 18: return ((ushort)iVal).ToString();     // VT_UI2
            case 11: return (iVal != 0).ToString();        // VT_BOOL
            case 20: return iVal.ToString();               // VT_I8
            case 21: return ((ulong)iVal).ToString();      // VT_UI8
            case 0:  return null;                          // VT_EMPTY
            case 1:  return null;                          // VT_NULL
            default: return "vt=" + vt;
        }
    }
}

public static class PropertyStoreHelper {
    [DllImport("shell32.dll", CharSet = CharSet.Unicode, PreserveSig = true)]
    public static extern int SHGetPropertyStoreFromParsingName(
        string pszPath,
        IntPtr pbc,
        int flags,  // GPS_DEFAULT=0, GPS_READWRITE=2
        [MarshalAs(UnmanagedType.LPStruct)] Guid riid,
        out IPropertyStore ppv);

    [DllImport("propsys.dll", CharSet = CharSet.Unicode)]
    public static extern int PSGetNameFromPropertyKey(ref PROPERTYKEY propkey,
        [MarshalAs(UnmanagedType.LPWStr)] out string ppszCanonicalName);

    [DllImport("ole32.dll")]
    public static extern int PropVariantClear(ref PropVariant pvar);

    public static System.Collections.Generic.Dictionary<string, string> ReadAll(string path) {
        var dict = new System.Collections.Generic.Dictionary<string, string>();
        var iid  = new Guid("886D8EEB-8CF2-4446-8D02-CDBA1DBDCF99");
        IPropertyStore store;
        int hr = SHGetPropertyStoreFromParsingName(path, IntPtr.Zero, 0, iid, out store);
        if (hr != 0) {
            dict["__SHGetPropStore_HR"] = "0x" + hr.ToString("X8");
            return dict;
        }

        uint count;
        store.GetCount(out count);
        dict["__PropertyCount"] = count.ToString();
        for (uint i = 0; i < count; i++) {
            PROPERTYKEY key;
            store.GetAt(i, out key);
            PropVariant pv = new PropVariant();
            store.GetValue(ref key, out pv);
            string val = pv.AsString();
            string name;
            PSGetNameFromPropertyKey(ref key, out name);
            string keyName = name ?? (key.fmtid.ToString() + ":" + key.pid);
            dict[keyName] = val ?? ("(vt=" + pv.vt + ")");
            PropVariantClear(ref pv);
        }
        return dict;
    }
}
'@ -ErrorAction Stop

        $directProps = [PropertyStoreHelper]::ReadAll($FilePath)
        foreach ($kv in $directProps.GetEnumerator()) {
            if (-not $props.ContainsKey($kv.Key)) {
                $props[$kv.Key] = $kv.Value
            }
        }
        $results.info['DirectPropertyStoreCount'] = $directProps.Count
        $results.info['DirectPropertyStoreKeys'] = ($directProps.Keys | Sort-Object) -join '; '
    } catch {
        $results.info['DirectPropertyStoreError'] = $_.Exception.Message
    }

    return $props
}

# ---------------------------------------------------------------------------
# Helper: restart Explorer so registry changes take effect on COM handlers
# ---------------------------------------------------------------------------
function Restart-Explorer {
    Stop-Process -Name explorer -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 3
    # Explorer auto-restarts in the sandbox; wait for it
    $deadline = (Get-Date).AddSeconds(15)
    while (-not (Get-Process -Name explorer -ErrorAction SilentlyContinue) -and (Get-Date) -lt $deadline) {
        Start-Sleep -Seconds 1
    }
    Start-Sleep -Seconds 2
}

# ===================================================================
# 1. Installation
# ===================================================================
try {
    Import-Certificate -FilePath $certPath -CertStoreLocation Cert:\LocalMachine\TrustedPeople -ErrorAction Stop | Out-Null

    # Install VCLibs framework dependency if the appx is available (required for sideloading)
    $vcLibsAppx = 'C:\Installer\Microsoft.VCLibs.x64.14.00.Desktop.appx'
    if (Test-Path $vcLibsAppx) {
        Add-AppxPackage -Path $vcLibsAppx -ErrorAction Stop
    }

    Add-AppxPackage -Path $msixPath -ErrorAction Stop
} catch {
    $results.info['InstallError'] = $_.Exception.Message
}

try {
    $pkg = Get-AppxPackage -Name $pkgName -ErrorAction SilentlyContinue
    Assert 'PackageInstalled' ($null -ne $pkg) 'Get-AppxPackage returned null'

    if ($pkg) {
        $results.info['PackageFullName'] = $pkg.PackageFullName
        $results.info['InstallLocation'] = $pkg.InstallLocation

        # Register property description schema (MSIX doesn't call DllRegisterServer)
        $propdescPath = Join-Path $pkg.InstallLocation 'xisf.propdesc'
        if (Test-Path $propdescPath) {
            Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class PropSys {
    [DllImport("propsys.dll", CharSet = CharSet.Unicode)]
    public static extern int PSRegisterPropertySchema(string pszPath);
}
'@
            $hr = [PropSys]::PSRegisterPropertySchema($propdescPath)
            $results.info['PropdescRegistered'] = ($hr -eq 0)
            $results.info['PropdescHR']         = "0x{0:X8}" -f $hr
        } else {
            $results.info['PropdescRegistered'] = $false
            $results.info['PropdescPath'] = $propdescPath
        }
    }
} catch {
    Assert 'PackageInstalled' $false $_.Exception.Message
}

# ===================================================================
# 1b. HKCU Shell Metadata Registration (via --register-msix)
# ===================================================================
# MSIX only registers COM classes and file type association. The settings
# app's --register-msix mode writes FullDetails/PreviewDetails/InfoTip,
# KindMap, PerceivedType, Content Type, and registers the propdesc schema
# — all to HKCU (no elevation required).
try {
    if ($pkg) {
        $exePath = Join-Path $pkg.InstallLocation 'XISFShellExtensionHost.exe'
        $proc = Start-Process -FilePath $exePath -ArgumentList '--register-msix' `
                    -Wait -PassThru -NoNewWindow -ErrorAction Stop
        $results.info['RegisterMsixExitCode'] = $proc.ExitCode
        $results.info['HkcuRegistered'] = ($proc.ExitCode -eq 0)

        # Verify key entries were written
        $sfaPath = 'HKCU:\Software\Classes\SystemFileAssociations\.xisf'
        $fd = (Get-ItemProperty -Path $sfaPath -Name 'FullDetails' -ErrorAction SilentlyContinue).FullDetails
        $results.info['FullDetailsWritten'] = ($null -ne $fd -and $fd.Length -gt 20)
    } else {
        $results.info['HkcuRegistered'] = $false
        $results.info['HkcuError'] = 'Package not installed'
    }
} catch {
    $results.info['HkcuRegistered'] = $false
    $results.info['HkcuError'] = $_.Exception.Message
}

# Restart Explorer so the shell picks up the new propdesc schema and handler registrations
Restart-Explorer

# ===================================================================
# 2. VC Runtime Check
# ===================================================================
try {
    $vcInSystem32 = Test-Path "$env:SystemRoot\System32\vcruntime140.dll"
    $vcInPackage  = $false
    if ($pkg) {
        $vcInPackage = Test-Path (Join-Path $pkg.InstallLocation 'vcruntime140.dll')
    }
    # Also check VCLibs framework package (MSIX dependency graph)
    $vcLibsPkg = Get-AppxPackage -Name 'Microsoft.VCLibs.140.00.UWPDesktop' -ErrorAction SilentlyContinue |
        Where-Object { $_.Architecture -eq 'X64' } | Select-Object -First 1
    $vcInFramework = $false
    if ($vcLibsPkg) {
        $vcInFramework = Test-Path (Join-Path $vcLibsPkg.InstallLocation 'vcruntime140.dll')
    }
    Assert 'VCRuntime' ($vcInSystem32 -or $vcInPackage -or $vcInFramework) `
           "vcruntime140.dll not found in System32, package, or VCLibs framework"
    $results.info['VCRuntime_System32']  = $vcInSystem32
    $results.info['VCRuntime_Package']   = $vcInPackage
    $results.info['VCRuntime_Framework'] = $vcInFramework
} catch {
    Assert 'VCRuntime' $false $_.Exception.Message
}

# ===================================================================
# 3. COM Activation Tests
# ===================================================================
foreach ($entry in @(
    @{ Name = 'ComActivation_PropertyHandler';   Clsid = $PropertyHandlerClsid },
    @{ Name = 'ComActivation_ThumbnailProvider'; Clsid = $ThumbnailProviderClsid },
    @{ Name = 'ComActivation_PreviewHandler';    Clsid = $PreviewHandlerClsid }
)) {
    try {
        $type = [Type]::GetTypeFromCLSID([Guid]$entry.Clsid)
        $obj  = [Activator]::CreateInstance($type)
        Assert $entry.Name $true ''
        if ($obj -is [IDisposable]) { $obj.Dispose() }
    } catch {
        # Packaged COM may not activate outside package identity context — record but don't hard-fail
        Assert $entry.Name $false "Expected for packaged COM: $($_.Exception.Message)"
    }
}

# ===================================================================
# 4. Property Handler via Shell (Most Important Test)
# ===================================================================
try {
    # Give Explorer time to pick up the new handler registration
    Start-Sleep -Seconds 3

    $shellProps = Get-ShellProperties -FilePath $xisfPath
    $results.info['ShellPropertyCount'] = $shellProps.Count

    # Store all discovered property values for diagnostics
    $discoveredProps = @{}

    # Look for our known metadata values anywhere in the shell properties
    $knownPatterns = @{
        'OBJECT'   = 'IC 1396'
        'EXPTIME'  = '300'
        'INSTRUME' = 'ZWO ASI2600MM Pro'
        'FILTER'   = 'Ha'
        'TELESCOP' = 'Takahashi FSQ-106N'
    }

    foreach ($kv in $knownPatterns.GetEnumerator()) {
        $found = $false
        foreach ($prop in $shellProps.GetEnumerator()) {
            if ($prop.Value -match [regex]::Escape($kv.Value)) {
                $discoveredProps[$kv.Key] = $prop.Value
                $found = $true
                break
            }
        }
        if (-not $found) {
            # Also try exact match
            foreach ($prop in $shellProps.GetEnumerator()) {
                if ($prop.Value -eq $kv.Value) {
                    $discoveredProps[$kv.Key] = $prop.Value
                    $found = $true
                    break
                }
            }
        }
    }

    $results.info['PropertyValues'] = $discoveredProps

    # Assert that we found at least the core properties
    Assert 'ShellProp_OBJECT'   ($discoveredProps.ContainsKey('OBJECT'))   "IC 1396 not found in shell properties"
    Assert 'ShellProp_EXPTIME'  ($discoveredProps.ContainsKey('EXPTIME'))  "300 not found in shell properties"
    Assert 'ShellProp_INSTRUME' ($discoveredProps.ContainsKey('INSTRUME')) "ZWO ASI2600MM Pro not found in shell properties"
    Assert 'ShellProp_FILTER'   ($discoveredProps.ContainsKey('FILTER'))   "Ha not found in shell properties"
} catch {
    Assert 'ShellProp_OBJECT'   $false "Shell property scan threw: $($_.Exception.Message)"
    Assert 'ShellProp_EXPTIME'  $false "Shell property scan threw: $($_.Exception.Message)"
    Assert 'ShellProp_INSTRUME' $false "Shell property scan threw: $($_.Exception.Message)"
    Assert 'ShellProp_FILTER'   $false "Shell property scan threw: $($_.Exception.Message)"
}

# ===================================================================
# 4b. Real XISF Data Test (if C:\AstroData is mapped)
# ===================================================================
if (Test-Path 'C:\AstroData') {
    try {
        # Find the first .xisf LIGHT frame for testing
        $realFile = Get-ChildItem 'C:\AstroData' -Recurse -Filter '*.xisf' -ErrorAction SilentlyContinue |
            Where-Object { $_.DirectoryName -match '\\LIGHT' } |
            Select-Object -First 1

        if (-not $realFile) {
            # Fall back to any .xisf file
            $realFile = Get-ChildItem 'C:\AstroData' -Recurse -Filter '*.xisf' -ErrorAction SilentlyContinue |
                Select-Object -First 1
        }

        if ($realFile) {
            $results.info['RealDataFile'] = $realFile.FullName
            $results.info['RealDataSize'] = $realFile.Length

            $realProps = Get-ShellProperties -FilePath $realFile.FullName
            $results.info['RealDataPropertyCount'] = $realProps.Count

            # Record a sample of discovered real properties for diagnostics
            $realSample = @{}
            foreach ($prop in $realProps.GetEnumerator()) {
                if ($prop.Key -match 'XISF\.' -or $prop.Key -match 'Astro\.' -or
                    $prop.Key -match 'ObjectName|ExposureTime|CameraModel|Filter|Telescope') {
                    $realSample[$prop.Key] = $prop.Value
                }
            }
            $results.info['RealDataProperties'] = $realSample

            Assert 'RealDataRead' ($realProps.Count -gt 0) 'No properties returned for real XISF file'
        } else {
            $results.info['RealDataFile'] = 'No .xisf files found in C:\AstroData'
        }
    } catch {
        Assert 'RealDataRead' $false "Real data test threw: $($_.Exception.Message)"
    }
} else {
    $results.info['RealDataAvailable'] = $false
}

# ===================================================================
# 5. Settings App Tests
# ===================================================================
try {
    if ($pkg) {
        $exePath = Join-Path $pkg.InstallLocation 'XISFShellExtensionHost.exe'
        Assert 'SettingsAppExists' (Test-Path $exePath) "Not found: $exePath"

        # Run --silent-install; exit 0 = all catalogs OK, exit 1 = some failed (expected in sandbox)
        # Anything else is a crash.
        try {
            $proc = Start-Process -FilePath $exePath -ArgumentList '--silent-install' `
                        -Wait -PassThru -NoNewWindow -ErrorAction Stop
            $exitCode = $proc.ExitCode
            $results.info['SilentInstallExitCode'] = $exitCode
            # 0 or 1 are acceptable (1 = catalog download failure, expected without network)
            Assert 'SilentInstallRuns' ($exitCode -eq 0 -or $exitCode -eq 1) `
                   "Unexpected exit code: $exitCode (crash?)"
        } catch {
            Assert 'SilentInstallRuns' $false "Failed to run: $($_.Exception.Message)"
        }
    } else {
        Assert 'SettingsAppExists' $false 'Package not installed'
        Assert 'SilentInstallRuns' $false 'Package not installed'
    }
} catch {
    Assert 'SettingsAppExists' $false $_.Exception.Message
}

# ===================================================================
# 6. Registry Toggle Test
# ===================================================================
try {
    # Ensure registry key exists
    if (-not (Test-Path $regBase)) {
        New-Item -Path $regBase -Force | Out-Null
    }

    # Disable property handler
    Set-ItemProperty -Path $regBase -Name 'PropertyEnabled' -Value 0 -Type DWord -Force

    # Restart Explorer so the handler picks up the registry change
    Restart-Explorer

    $propsDisabled = Get-ShellProperties -FilePath $xisfPath
    $disabledFound = $false
    foreach ($prop in $propsDisabled.GetEnumerator()) {
        if ($prop.Value -match 'IC 1396') {
            $disabledFound = $true
            break
        }
    }
    Assert 'ToggleDisable' (-not $disabledFound) "IC 1396 still visible after PropertyEnabled=0"

    # Re-enable property handler
    Set-ItemProperty -Path $regBase -Name 'PropertyEnabled' -Value 1 -Type DWord -Force

    Restart-Explorer

    $propsEnabled = Get-ShellProperties -FilePath $xisfPath
    $enabledFound = $false
    foreach ($prop in $propsEnabled.GetEnumerator()) {
        if ($prop.Value -match 'IC 1396') {
            $enabledFound = $true
            break
        }
    }
    Assert 'ToggleEnable' $enabledFound "IC 1396 not visible after PropertyEnabled=1"
} catch {
    Assert 'ToggleDisable' $false "Toggle test threw: $($_.Exception.Message)"
    Assert 'ToggleEnable'  $false "Toggle test threw: $($_.Exception.Message)"
}

# ===================================================================
# 7. Feature Tier Test
# ===================================================================
try {
    if (-not (Test-Path $regBase)) {
        New-Item -Path $regBase -Force | Out-Null
    }

    # Tier 0 = Basic — XML header metadata only (no computed properties)
    Set-ItemProperty -Path $regBase -Name 'FeatureTier' -Value 0 -Type DWord -Force
    Set-ItemProperty -Path $regBase -Name 'PropertyEnabled' -Value 1 -Type DWord -Force

    Restart-Explorer

    $propsBasic = Get-ShellProperties -FilePath $xisfPath

    # At Basic tier, core FITS keywords (OBJECT, EXPTIME) should still appear
    $basicHasCore = $false
    foreach ($prop in $propsBasic.GetEnumerator()) {
        if ($prop.Value -match 'IC 1396' -or $prop.Value -match '300') {
            $basicHasCore = $true
            break
        }
    }
    Assert 'TierBasic' $basicHasCore "Basic tier should still expose core FITS metadata"
    $results.info['TierBasicPropertyCount'] = $propsBasic.Count

    # Tier 2 = Full — includes computed properties (constellation, DSO search, pixel stats)
    Set-ItemProperty -Path $regBase -Name 'FeatureTier' -Value 2 -Type DWord -Force

    Restart-Explorer

    $propsFull = Get-ShellProperties -FilePath $xisfPath
    $results.info['TierFullPropertyCount'] = $propsFull.Count

    # Full tier should have at least as many properties as Basic tier
    Assert 'TierFull' ($propsFull.Count -ge $propsBasic.Count) `
           "Full tier ($($propsFull.Count) props) should have >= Basic tier ($($propsBasic.Count) props)"

    # Clean up — restore to Full tier
    Set-ItemProperty -Path $regBase -Name 'FeatureTier' -Value 2 -Type DWord -Force
} catch {
    Assert 'TierBasic' $false "Tier test threw: $($_.Exception.Message)"
    Assert 'TierFull'  $false "Tier test threw: $($_.Exception.Message)"
}

# ===================================================================
# Write results atomically
# ===================================================================
try {
    $json  = $results | ConvertTo-Json -Depth 5
    $tmp   = 'C:\Results\functional-results.tmp'
    $final = 'C:\Results\functional-results.json'
    Set-Content -Path $tmp -Value $json -Encoding UTF8
    Move-Item -Path $tmp -Destination $final -Force
} catch {
    # Last-resort: write directly
    $results | ConvertTo-Json -Depth 5 | Set-Content -Path 'C:\Results\functional-results.json' -Encoding UTF8
}

# Signal completion
New-Item -Path 'C:\Results\done.marker' -ItemType File -Force | Out-Null

# Shutdown the sandbox unless keep-alive marker is present
if (Test-Path 'C:\Installer\keep-alive.marker') {
    Write-Host "`n=== Sandbox kept alive for inspection ===" -ForegroundColor Cyan
    Write-Host "Results: C:\Results\functional-results.json" -ForegroundColor Cyan
    Write-Host "Test data: C:\TestData\" -ForegroundColor Cyan
    Write-Host "Package: $((Get-AppxPackage -Name 'DennisPayne.XISF.ShellExtension' -ErrorAction SilentlyContinue).InstallLocation)" -ForegroundColor Cyan
    Write-Host "Close this window or the sandbox to end.`n" -ForegroundColor Cyan
} else {
    Start-Sleep -Seconds 2
    shutdown /s /t 0
}
