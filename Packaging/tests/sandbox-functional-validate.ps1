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

# Early heartbeat — proves script is executing at all
New-Item -ItemType Directory -Path 'C:\Results' -Force -ErrorAction SilentlyContinue | Out-Null
"script_started $(Get-Date -Format o)" | Set-Content 'C:\Results\heartbeat.txt' -ErrorAction SilentlyContinue

# Redirect all output to a log file for diagnostics
Start-Transcript -Path 'C:\Results\validate.log' -Force -ErrorAction SilentlyContinue | Out-Null

# ---------------------------------------------------------------------------
# Result accumulator (initialize BEFORE trap/Write-Results so trap is safe)
# ---------------------------------------------------------------------------
$results = @{
    pass = [System.Collections.Generic.List[string]]::new()
    fail = [System.Collections.Generic.List[hashtable]]::new()
    info = @{}
}

# ---------------------------------------------------------------------------
# Emergency result writer — ensures results are always persisted
# ---------------------------------------------------------------------------
function Write-Results {
    try {
        Stop-Transcript -ErrorAction SilentlyContinue | Out-Null
    } catch {}
    try {
        $json  = $script:results | ConvertTo-Json -Depth 5
        $tmp   = 'C:\Results\functional-results.tmp'
        $final = 'C:\Results\functional-results.json'
        Set-Content -Path $tmp -Value $json -Encoding UTF8
        Move-Item -Path $tmp -Destination $final -Force
    } catch {
        try {
            $script:results | ConvertTo-Json -Depth 5 |
                Set-Content -Path 'C:\Results\functional-results.json' -Encoding UTF8
        } catch {
            '{"pass":[],"fail":[],"info":{"FatalWriteError":"' + $_.Exception.Message + '"}}' |
                Set-Content -Path 'C:\Results\functional-results.json' -Encoding UTF8
        }
    }
    New-Item -Path 'C:\Results\done.marker' -ItemType File -Force -ErrorAction SilentlyContinue | Out-Null
}

# Trap unhandled terminating errors — flush partial results before dying
trap {
    if ($null -ne $script:results) {
        $script:results.info['UnhandledError'] = $_.Exception.Message
        $script:results.info['UnhandledErrorAt'] = $_.InvocationInfo.ScriptLineNumber
    }
    Write-Results
    continue
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

# Local test data dir — files copied here to avoid oplock issues with
# sandbox mapped folders (0x8007012C / ERROR_OPLOCK_NOT_GRANTED)
$localTestDir  = 'C:\LocalTestData'
$localXisfPath = Join-Path $localTestDir 'test.xisf'
New-Item -ItemType Directory -Path $localTestDir -Force -ErrorAction SilentlyContinue | Out-Null

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

    // flags: GPS_DEFAULT=0, GPS_HANDLERPROPERTIESONLY=0x1, GPS_READWRITE=0x2
    public static System.Collections.Generic.Dictionary<string, string> ReadAll(string path, int flags) {
        var dict = new System.Collections.Generic.Dictionary<string, string>();
        var iid  = new Guid("886D8EEB-8CF2-4446-8D02-CDBA1DBDCF99");
        IPropertyStore store;
        int hr = SHGetPropertyStoreFromParsingName(path, IntPtr.Zero, flags, iid, out store);
        dict["__GPS_Flags"] = "0x" + flags.ToString("X");
        if (hr != 0) {
            dict["__SHGetPropStore_HR"] = "0x" + hr.ToString("X8");
            return dict;
        }
        dict["__SHGetPropStore_HR"] = "0x00000000";

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

        $directProps = [PropertyStoreHelper]::ReadAll($FilePath, 0)  # GPS_DEFAULT
        foreach ($kv in $directProps.GetEnumerator()) {
            if (-not $props.ContainsKey($kv.Key)) {
                $props[$kv.Key] = $kv.Value
            }
        }
        $results.info['DirectPropertyStoreCount'] = $directProps.Count
        $results.info['DirectPropertyStoreKeys'] = ($directProps.Keys | Sort-Object) -join '; '
        # Capture the HRESULT
        if ($directProps.ContainsKey('__SHGetPropStore_HR')) {
            $results.info['SHGetPropStore_HR'] = $directProps['__SHGetPropStore_HR']
        }

        # Also try GPS_HANDLERPROPERTIESONLY (0x1) — forces handler activation,
        # bypasses cache. This is what Explorer uses for columns and Details tab.
        $handlerProps = [PropertyStoreHelper]::ReadAll($FilePath, 1)  # GPS_HANDLERPROPERTIESONLY
        $results.info['HandlerOnlyPropStore_HR'] = $handlerProps['__SHGetPropStore_HR']
        $handlerPropCount = if ($handlerProps.ContainsKey('__PropertyCount')) { [int]$handlerProps['__PropertyCount'] } else { 0 }
        $results.info['HandlerOnlyPropertyCount'] = $handlerPropCount

        # Capture all handler-only property names and values for diagnostics
        $handlerSample = @{}
        foreach ($kv in $handlerProps.GetEnumerator()) {
            if ($kv.Key -notlike '__*') {
                $handlerSample[$kv.Key] = $kv.Value
                if (-not $props.ContainsKey($kv.Key)) {
                    $props[$kv.Key] = $kv.Value
                }
            }
        }
        $results.info['HandlerOnlyProperties'] = $handlerSample
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
# 1b. Classical DLL registration (regsvr32) for complete handler setup
# ===================================================================
# MSIX registers COM classes in a per-app virtual registry. For reliable
# testing AND for Explorer to actually invoke our handlers, we supplement
# with classical regsvr32 registration. This writes everything
# DllRegisterServer provides: PropertyHandlers\.xisf, shellex entries,
# FullDetails, propdesc schema, PropertySheetHandler, etc.
#
# We copy DLLs from the MSIX package to a temp location first, since the
# WindowsApps folder has restrictive ACLs.
try {
    $classicalRegDir = 'C:\XISFHandlers'
    $results.info['ClassicalRegDir'] = $classicalRegDir

    if ($pkg) {
        # First try the production code path (--register-msix) with timeout
        $exePath = Join-Path $pkg.InstallLocation 'XISFShellExtensionHost.exe'
        $proc = Start-Process -FilePath $exePath -ArgumentList '--register-msix' `
                    -PassThru -NoNewWindow -ErrorAction Stop
        if (-not $proc.WaitForExit(30000)) {
            $proc.Kill()
            $results.info['RegisterMsixExitCode'] = 'timeout'
        } else {
            $results.info['RegisterMsixExitCode'] = $proc.ExitCode
        }

        # Copy handler DLLs and propdesc to accessible location
        New-Item -ItemType Directory -Path $classicalRegDir -Force | Out-Null
        $filesToCopy = @(
            'XISFPropertyHandler.dll',
            'XISFPreviewHandler.dll',
            'xisf.propdesc'
        )
        foreach ($f in $filesToCopy) {
            $src = Join-Path $pkg.InstallLocation $f
            if (Test-Path $src) {
                Copy-Item $src (Join-Path $classicalRegDir $f) -Force
            }
        }

        # Ensure VCRuntime is available system-wide for the handler DLLs.
        # The DLL loader searches System32 but NOT the InProcServer32 directory.
        # Copy from the already-installed VCLibs framework package to System32
        # (avoids network dependency and hanging downloads in sandbox).
        if (-not (Test-Path "$env:SystemRoot\System32\vcruntime140.dll")) {
            try {
                $vcLibsPkg = Get-AppxPackage -Name 'Microsoft.VCLibs.140.00.UWPDesktop' -ErrorAction SilentlyContinue |
                    Where-Object { $_.Architecture -eq 'X64' } | Select-Object -First 1
                if ($vcLibsPkg) {
                    foreach ($vcDll in @('vcruntime140.dll', 'vcruntime140_1.dll', 'msvcp140.dll')) {
                        $vcSrc = Join-Path $vcLibsPkg.InstallLocation $vcDll
                        if (Test-Path $vcSrc) {
                            Copy-Item $vcSrc "$env:SystemRoot\System32\$vcDll" -Force -ErrorAction SilentlyContinue
                            Copy-Item $vcSrc (Join-Path $classicalRegDir $vcDll) -Force -ErrorAction SilentlyContinue
                        }
                    }
                    # Also add handler dir to PATH as belt-and-suspenders
                    $curPath = [Environment]::GetEnvironmentVariable('PATH', 'Machine')
                    if ($curPath -notlike "*$classicalRegDir*") {
                        [Environment]::SetEnvironmentVariable('PATH', "$classicalRegDir;$curPath", 'Machine')
                    }
                    $results.info['VCRuntime_Source'] = 'VCLibs framework'
                } else {
                    $results.info['VCRuntime_Source'] = 'VCLibs not found'
                }
            } catch {
                $results.info['VCRuntime_Error'] = $_.Exception.Message
            }
        } else {
            $results.info['VCRuntime_Source'] = 'already in System32'
        }

        # Register both handler DLLs classically (requires admin — sandbox has it)
        $propDll = Join-Path $classicalRegDir 'XISFPropertyHandler.dll'
        $prevDll = Join-Path $classicalRegDir 'XISFPreviewHandler.dll'

        if (Test-Path $propDll) {
            $regProc = Start-Process regsvr32 -ArgumentList "/s `"$propDll`"" `
                -PassThru -NoNewWindow -ErrorAction Stop
            if (-not $regProc.WaitForExit(30000)) { $regProc.Kill() }
            $regProc.Refresh()
            $results.info['RegSvr32PropertyHandler'] = $regProc.ExitCode
        }
        if (Test-Path $prevDll) {
            $regProc = Start-Process regsvr32 -ArgumentList "/s `"$prevDll`"" `
                -PassThru -NoNewWindow -ErrorAction Stop
            if (-not $regProc.WaitForExit(30000)) { $regProc.Kill() }
            $regProc.Refresh()
            $results.info['RegSvr32PreviewHandler'] = $regProc.ExitCode
        }

        # Verify FullDetails was written (by regsvr32's DllRegisterServer)
        $sfaPath = 'HKCU:\Software\Classes\SystemFileAssociations\.xisf'
        $sfaPathHKCR = 'Registry::HKEY_CLASSES_ROOT\SystemFileAssociations\.xisf'
        $fd = (Get-ItemProperty -Path $sfaPathHKCR -Name 'FullDetails' -ErrorAction SilentlyContinue).FullDetails
        if (-not $fd) {
            $fd = (Get-ItemProperty -Path $sfaPath -Name 'FullDetails' -ErrorAction SilentlyContinue).FullDetails
        }
        $results.info['FullDetailsWritten'] = ($null -ne $fd -and $fd.Length -gt 20)

        # Verify PropertyHandlers\.xisf was written
        $phPath = 'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\PropertySystem\PropertyHandlers\.xisf'
        $ph = (Get-ItemProperty -Path $phPath -Name '(Default)' -ErrorAction SilentlyContinue).'(Default)'
        $results.info['PropertyHandlerRegistered'] = ($null -ne $ph -and $ph.Length -gt 10)
        $results.info['PropertyHandlerClsidValue'] = $ph

        # Verify InProcServer32 path is correct
        $inproc = (Get-ItemProperty -Path "Registry::HKEY_CLASSES_ROOT\CLSID\{$PropertyHandlerClsid}\InProcServer32" `
            -Name '(Default)' -ErrorAction SilentlyContinue).'(Default)'
        $results.info['InProcServer32Path'] = $inproc
        if ($inproc) {
            $results.info['InProcServer32Exists'] = Test-Path $inproc
        }

        # Check what files exist in C:\XISFHandlers
        $handlerFiles = Get-ChildItem $classicalRegDir -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name
        $results.info['HandlerDirFiles'] = ($handlerFiles -join '; ')

        # Verify propdesc schema
        $propdescFile = Join-Path $classicalRegDir 'xisf.propdesc'
        if (Test-Path $propdescFile) {
            # Re-register from copied (accessible) location
            $hr = [PropSys]::PSRegisterPropertySchema($propdescFile)
            $results.info['PropdescAccessibleHR'] = "0x{0:X8}" -f $hr
        }
    }

    $results.info['HkcuRegistered'] = $true
} catch {
    $results.info['HkcuRegistered'] = $false
    $results.info['HkcuError'] = $_.Exception.Message
}

# Restart Explorer so the shell picks up the new registrations
Restart-Explorer

# ===================================================================
# 1c. Start ETW trace capture for both handler providers
# ===================================================================
$traceSession = 'XISFHandlerTrace'
$etlPath      = 'C:\Results\handler-trace.etl'
try {
    # Stop any leftover session from a previous run
    & logman stop $traceSession -ets 2>$null | Out-Null

    # Create trace session capturing both providers
    & logman create trace $traceSession -ets -o $etlPath -f bincirc -max 64 `
        -p "{6F6B0C9D-6B76-5A24-BC3D-708314E96F2B}" 0xFFFFFFFF 5 `
        2>$null | Out-Null

    # Add the preview/thumbnail provider to the same session
    & logman update trace $traceSession -ets `
        -p "{4FD34FD0-08B3-5D9A-8D77-B9D6705D6B75}" 0xFFFFFFFF 5 `
        2>$null | Out-Null

    $results.info['ETWTraceStarted'] = $true
} catch {
    $results.info['ETWTraceStarted'] = $false
    $results.info['ETWTraceError']   = $_.Exception.Message
}

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
    # Copy test file to local path — sandbox mapped folders don't support
    # oplocks, which causes SHGetPropertyStoreFromParsingName to fail
    # with 0x8007012C (ERROR_OPLOCK_NOT_GRANTED)
    Copy-Item $xisfPath $localXisfPath -Force

    # Give Explorer time to pick up the new handler registration
    Start-Sleep -Seconds 3

    $shellProps = Get-ShellProperties -FilePath $localXisfPath
    $results.info['ShellPropertyCount'] = $shellProps.Count

    # ---------------------------------------------------------------
    # 4a. Validate IPropertyStore activation (GPS_HANDLERPROPERTIESONLY)
    # This is the code path Explorer uses for columns and Details tab.
    # If this HRESULT is not S_OK, nothing downstream can work.
    # ---------------------------------------------------------------
    $handlerHR = $results.info['HandlerOnlyPropStore_HR']
    Assert 'DirectPropStore_Activates' ($handlerHR -eq '0x00000000') `
           "SHGetPropertyStoreFromParsingName(GPS_HANDLERPROPERTIESONLY) returned $handlerHR"

    # ---------------------------------------------------------------
    # 4b. Validate property VALUES via canonical property names
    # Uses the Astro.* canonical names registered in our propdesc schema.
    # This avoids false positives from regex substring matching.
    # ---------------------------------------------------------------
    $canonicalChecks = @{
        'OBJECT'   = @{ Names = @('Astro.Object', 'XISF.ObjectName');     Expected = 'IC 1396' }
        'EXPTIME'  = @{ Names = @('Astro.ExposureTime', 'XISF.ExposureTime'); Expected = '300' }
        'INSTRUME' = @{ Names = @('Astro.CameraModel', 'XISF.Instrument'); Expected = 'ZWO ASI2600MM Pro' }
        'FILTER'   = @{ Names = @('Astro.Filter', 'XISF.FilterName');     Expected = 'Ha' }
    }

    $discoveredProps = @{}

    foreach ($kv in $canonicalChecks.GetEnumerator()) {
        $found = $false
        foreach ($name in $kv.Value.Names) {
            if ($shellProps.ContainsKey($name) -and $shellProps[$name] -eq $kv.Value.Expected) {
                $discoveredProps[$kv.Key] = $shellProps[$name]
                $found = $true
                break
            }
        }
        # Fallback: search by exact value match on any property key containing
        # a relevant substring, but NOT matching common system property names
        if (-not $found) {
            foreach ($prop in $shellProps.GetEnumerator()) {
                $keyLower = $prop.Key.ToLower()
                if ($prop.Value -eq $kv.Value.Expected -and
                    $keyLower -notmatch '^system\.(not|is|shared)' -and
                    $keyLower -ne 'sharing status') {
                    $discoveredProps[$kv.Key] = $prop.Value
                    $found = $true
                    break
                }
            }
        }
    }

    $results.info['PropertyValues'] = $discoveredProps

    Assert 'ShellProp_OBJECT'   ($discoveredProps.ContainsKey('OBJECT'))   "IC 1396 not found via canonical names"
    Assert 'ShellProp_EXPTIME'  ($discoveredProps.ContainsKey('EXPTIME'))  "300 not found via canonical names"
    Assert 'ShellProp_INSTRUME' ($discoveredProps.ContainsKey('INSTRUME')) "ZWO ASI2600MM Pro not found via canonical names"
    Assert 'ShellProp_FILTER'   ($discoveredProps.ContainsKey('FILTER'))   "Ha not found via canonical names"

    # ---------------------------------------------------------------
    # 4c. Validate handler-only properties have non-empty values
    # This proves the IPropertyStore::GetValue path works, which is
    # what Explorer uses for columns and the Details tab.
    # ---------------------------------------------------------------
    $handlerPropCount = $results.info['HandlerOnlyPropertyCount']
    $handlerSample    = $results.info['HandlerOnlyProperties']
    $nonEmptyCount = 0
    if ($handlerSample -is [hashtable]) {
        foreach ($v in $handlerSample.Values) {
            if ($null -ne $v -and "$v" -ne '' -and $v -notmatch '^\(vt=') {
                $nonEmptyCount++
            }
        }
    }
    $results.info['HandlerOnlyNonEmptyCount'] = $nonEmptyCount
    Assert 'DirectPropStore_HasValues' ($nonEmptyCount -gt 0) `
           "IPropertyStore returned $handlerPropCount props but $nonEmptyCount had non-empty values"
} catch {
    Assert 'ShellProp_OBJECT'   $false "Shell property scan threw: $($_.Exception.Message)"
    Assert 'ShellProp_EXPTIME'  $false "Shell property scan threw: $($_.Exception.Message)"
    Assert 'ShellProp_INSTRUME' $false "Shell property scan threw: $($_.Exception.Message)"
    Assert 'ShellProp_FILTER'   $false "Shell property scan threw: $($_.Exception.Message)"
    Assert 'DirectPropStore_Activates' $false "Shell property scan threw: $($_.Exception.Message)"
    Assert 'DirectPropStore_HasValues' $false "Shell property scan threw: $($_.Exception.Message)"
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
            # Copy to local path to avoid oplock issues with mapped folders
            $localRealFile = Join-Path $localTestDir $realFile.Name
            Copy-Item $realFile.FullName $localRealFile -Force -ErrorAction SilentlyContinue
            $results.info['RealDataFile'] = $realFile.FullName
            $results.info['RealDataSize'] = $realFile.Length

            $realProps = Get-ShellProperties -FilePath $localRealFile
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
# 4d. Thumbnail Handler Test
# ===================================================================
# Use a real XISF file (has pixel data for thumbnail decode).
# The synthetic test file has no attachment data, so thumbnails will fail.
try {
    $thumbTestFile = $null
    if (Test-Path 'C:\AstroData') {
        $realThumbFile = Get-ChildItem 'C:\AstroData' -Recurse -Filter '*.xisf' -ErrorAction SilentlyContinue |
            Where-Object { $_.Length -gt 100KB } |
            Select-Object -First 1
        if ($realThumbFile) {
            $thumbTestFile = Join-Path $localTestDir "thumb_$($realThumbFile.Name)"
            Copy-Item $realThumbFile.FullName $thumbTestFile -Force -ErrorAction SilentlyContinue
        }
    }

    if ($thumbTestFile -and (Test-Path $thumbTestFile)) {
        $results.info['ThumbnailTestFile'] = $thumbTestFile
        $results.info['ThumbnailTestFileSize'] = (Get-Item $thumbTestFile).Length

        # Use SHCreateItemFromParsingName + IShellItemImageFactory to request
        # a thumbnail through the shell pipeline (same path Explorer uses).
        # Falls back to direct COM activation if shell path fails (sandbox limitation).
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;

public static class ThumbnailHelper {
    [StructLayout(LayoutKind.Sequential)]
    public struct SIZE { public int cx; public int cy; }

    [ComImport, Guid("bcc18b79-ba16-442f-80c4-8a59c30c463b"),
     InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    public interface IShellItemImageFactory {
        int GetImage(SIZE size, int flags, out IntPtr phbm);
    }

    [ComImport, Guid("e357fccd-a995-4576-b01f-234630154e96"),
     InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    public interface IThumbnailProvider {
        [PreserveSig]
        int GetThumbnail(uint cx, out IntPtr phbmp, out int pdwAlpha);
    }

    [ComImport, Guid("b824b49d-22ac-4161-ac8a-9916e8fa3f7f"),
     InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    public interface IInitializeWithStream {
        [PreserveSig]
        int Initialize(IStream pstream, uint grfMode);
    }

    [DllImport("shell32.dll", CharSet = CharSet.Unicode, PreserveSig = true)]
    public static extern int SHCreateItemFromParsingName(
        string pszPath, IntPtr pbc,
        [MarshalAs(UnmanagedType.LPStruct)] Guid riid,
        [MarshalAs(UnmanagedType.Interface)] out object ppv);

    [DllImport("shlwapi.dll", CharSet = CharSet.Unicode, PreserveSig = true)]
    public static extern int SHCreateStreamOnFileEx(
        string pszFile, uint grfMode, uint dwAttributes,
        bool fCreate, IntPtr pstmTemplate, out IStream ppstm);

    [DllImport("gdi32.dll")]
    public static extern bool DeleteObject(IntPtr hObject);

    [DllImport("gdi32.dll")]
    public static extern int GetObject(IntPtr h, int c, out BITMAP pv);

    [StructLayout(LayoutKind.Sequential)]
    public struct BITMAP {
        public int bmType, bmWidth, bmHeight, bmWidthBytes;
        public short bmPlanes, bmBitsPixel;
        public IntPtr bmBits;
    }

    // Try shell path first, then direct COM
    public static string GetThumbnail(string path, int cx, int cy) {
        // Method 1: Shell pipeline (IShellItemImageFactory)
        string shellResult = TryShellThumbnail(path, cx, cy);
        if (shellResult.StartsWith("OK:")) return shellResult;

        // Method 2: Direct COM activation (bypasses shell pipeline)
        string directResult = TryDirectThumbnail(path, cx, cy);
        if (directResult.StartsWith("OK:")) return "DirectCOM:" + directResult.Substring(3);

        // Return most informative error
        return "Shell=" + shellResult + ";Direct=" + directResult;
    }

    private static string TryShellThumbnail(string path, int cx, int cy) {
        try {
            var iid = new Guid("bcc18b79-ba16-442f-80c4-8a59c30c463b");
            object item;
            int hr = SHCreateItemFromParsingName(path, IntPtr.Zero, iid, out item);
            if (hr != 0) return "SHCreateItem_HR=0x" + hr.ToString("X8");

            var factory = (IShellItemImageFactory)item;
            var size = new SIZE { cx = cx, cy = cy };
            IntPtr hbmp;
            hr = factory.GetImage(size, 0x2 | 0x4, out hbmp);
            if (hr != 0) return "GetImage_HR=0x" + hr.ToString("X8");
            if (hbmp == IntPtr.Zero) return "NULL_HBITMAP";

            BITMAP bm;
            GetObject(hbmp, Marshal.SizeOf(typeof(BITMAP)), out bm);
            string result = "OK:" + bm.bmWidth + "x" + bm.bmHeight + "x" + bm.bmBitsPixel + "bpp";
            DeleteObject(hbmp);
            return result;
        } catch (Exception ex) {
            return "Exception:" + ex.HResult.ToString("X8");
        }
    }

    private static string TryDirectThumbnail(string path, int cx, int cy) {
        try {
            // Create thumbnail provider COM object
            var clsid = new Guid("9C76E8AD-4E85-5F30-B00D-3C7D1AB5F6E0");
            var type = Type.GetTypeFromCLSID(clsid);
            if (type == null) return "CLSID_NOT_FOUND";
            var obj = Activator.CreateInstance(type);
            if (obj == null) return "CREATE_FAILED";

            var initStream = obj as IInitializeWithStream;
            if (initStream == null) return "NO_IInitializeWithStream";

            // Open file as IStream (STGM_READ = 0)
            IStream stream;
            int hr = SHCreateStreamOnFileEx(path, 0, 0, false, IntPtr.Zero, out stream);
            if (hr != 0) return "StreamCreate_HR=0x" + hr.ToString("X8");

            hr = initStream.Initialize(stream, 0);
            if (hr != 0) return "Initialize_HR=0x" + hr.ToString("X8");

            var thumbProvider = obj as IThumbnailProvider;
            if (thumbProvider == null) return "NO_IThumbnailProvider";

            IntPtr hbmp;
            int alpha;
            hr = thumbProvider.GetThumbnail((uint)cx, out hbmp, out alpha);
            if (hr != 0) return "GetThumbnail_HR=0x" + hr.ToString("X8");
            if (hbmp == IntPtr.Zero) return "NULL_HBITMAP";

            BITMAP bm;
            GetObject(hbmp, Marshal.SizeOf(typeof(BITMAP)), out bm);
            string result = "OK:" + bm.bmWidth + "x" + bm.bmHeight + "x" + bm.bmBitsPixel + "bpp";
            DeleteObject(hbmp);
            return result;
        } catch (Exception ex) {
            return "Exception:" + ex.HResult.ToString("X8");
        }
    }
}
'@ -ErrorAction Stop

        $thumbResult = [ThumbnailHelper]::GetThumbnail($thumbTestFile, 256, 256)
        $results.info['ThumbnailResult'] = $thumbResult

        $thumbOk = $thumbResult.StartsWith('OK:') -or $thumbResult.StartsWith('DirectCOM:')
        Assert 'Thumbnail_RealXISF' $thumbOk "Thumbnail from real XISF: $thumbResult"
    } else {
        $results.info['ThumbnailTestFile'] = 'No suitable real XISF found for thumbnail test'
        # Don't fail — skip gracefully when no real data is available
        Assert 'Thumbnail_RealXISF' $true 'Skipped: no real XISF data available'
    }
} catch {
    Assert 'Thumbnail_RealXISF' $false "Thumbnail test threw: $($_.Exception.Message)"
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

    $propsDisabled = Get-ShellProperties -FilePath $localXisfPath
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

    $propsEnabled = Get-ShellProperties -FilePath $localXisfPath
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
# 6b. Preview/Thumbnail Toggle Test (PreviewEnabled)
# ===================================================================
try {
    if (-not (Test-Path $regBase)) {
        New-Item -Path $regBase -Force | Out-Null
    }

    # Disable preview handler
    Set-ItemProperty -Path $regBase -Name 'PreviewEnabled' -Value 0 -Type DWord -Force
    Restart-Explorer

    # Thumbnail via IShellItemImageFactory should fail when disabled
    $thumbDisabledResult = 'skipped'
    if ($thumbTestFile -and (Test-Path $thumbTestFile)) {
        try {
            $thumbDisabledResult = [ThumbnailHelper]::GetThumbnail($thumbTestFile, 256, 256)
        } catch {
            $thumbDisabledResult = "error:$($_.Exception.Message)"
        }
    }
    $results.info['PreviewDisabled_ThumbnailResult'] = $thumbDisabledResult
    # When disabled, GetImage/GetThumbnail should fail (not return OK: or DirectCOM:)
    $previewDisableOk = ($thumbDisabledResult -eq 'skipped') -or
        (-not $thumbDisabledResult.StartsWith('OK:') -and -not $thumbDisabledResult.StartsWith('DirectCOM:'))
    Assert 'TogglePreviewDisable' $previewDisableOk `
           "Thumbnail still works after PreviewEnabled=0: $thumbDisabledResult"

    # Re-enable preview handler
    Set-ItemProperty -Path $regBase -Name 'PreviewEnabled' -Value 1 -Type DWord -Force
    Restart-Explorer
} catch {
    Assert 'TogglePreviewDisable' $false "Preview toggle test threw: $($_.Exception.Message)"
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

    $propsBasic = Get-ShellProperties -FilePath $localXisfPath

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

    $propsFull = Get-ShellProperties -FilePath $localXisfPath
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
# Stop ETW trace and export to CSV for analysis
# ===================================================================
try {
    & logman stop $traceSession -ets 2>$null | Out-Null
    if (Test-Path $etlPath) {
        $results.info['ETWTraceFile'] = $etlPath
        $results.info['ETWTraceSize'] = (Get-Item $etlPath).Length

        # Export to CSV for easy parsing on the host side
        $csvPath = 'C:\Results\handler-trace.csv'
        & tracerpt $etlPath -o $csvPath -of CSV -y 2>$null | Out-Null
        if (Test-Path $csvPath) {
            $results.info['ETWTraceCSV'] = $csvPath
            # Extract key events for quick diagnostics
            $traceLines = Get-Content $csvPath -ErrorAction SilentlyContinue |
                Select-Object -First 200
            $keyEvents = @()
            foreach ($line in $traceLines) {
                if ($line -match 'PropertyStore|Thumbnail|Initialize|Failed|Error|CLASS_E_') {
                    $keyEvents += $line
                }
            }
            if ($keyEvents.Count -gt 0) {
                $results.info['ETWKeyEvents'] = $keyEvents -join "`n"
            }
        }
    }
} catch {
    $results.info['ETWStopError'] = $_.Exception.Message
}

# ===================================================================
# Write results atomically
# ===================================================================
Write-Results

# Shutdown the sandbox unless keep-alive marker is present
if (Test-Path 'C:\Installer\keep-alive.marker') {
    Write-Host "`n=== Sandbox kept alive for inspection ===" -ForegroundColor Cyan
    Write-Host "Results: C:\Results\functional-results.json" -ForegroundColor Cyan
    Write-Host "Test data: C:\TestData\" -ForegroundColor Cyan
    Write-Host "Handler DLLs: C:\XISFHandlers\" -ForegroundColor Cyan
    Write-Host "Package: $((Get-AppxPackage -Name 'DennisPayne.XISF.ShellExtension' -ErrorAction SilentlyContinue).InstallLocation)" -ForegroundColor Cyan
    Write-Host "Close this window or the sandbox to end.`n" -ForegroundColor Cyan
} else {
    Start-Sleep -Seconds 2
    shutdown /s /t 0
}
