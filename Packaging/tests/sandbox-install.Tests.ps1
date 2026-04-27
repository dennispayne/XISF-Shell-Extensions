<#
.SYNOPSIS
    Sandbox-based MSIX install validation for the XISF Shell Extension.

.DESCRIPTION
    Launches Windows Sandbox with a signed MSIX, installs the package,
    and validates: package registration, COM CLSID activation, .xisf
    file type association, and handler exe presence.

    Results are written to a shared folder and read back by the host.

    Prerequisites:
      - Windows Sandbox feature enabled
      - Release|x64 solution build
      - Pester v5+
      - Run from repo root

    Set $env:XISF_KEEP_SANDBOX = 1 before running to leave the sandbox
    open for manual inspection after tests complete.

.EXAMPLE
    Invoke-Pester .\Packaging\tests\sandbox-install.Tests.ps1 -Output Detailed
#>

BeforeAll {
    $script:RepoRoot    = Resolve-Path (Join-Path $PSScriptRoot '..\..')
    $script:BuildScript = Join-Path $RepoRoot 'Packaging\build-msix.ps1'
    $script:ReleaseDir  = Join-Path $RepoRoot 'x64\Release'
    $script:TestsDir    = $PSScriptRoot
    $script:SandboxTimeoutSec = 180
    $script:KeepSandbox = $env:XISF_KEEP_SANDBOX -eq '1'

    # Check prerequisites
    $script:SandboxAvailable = $false
    if (Get-Command 'WindowsSandbox.exe' -ErrorAction SilentlyContinue) {
        $script:SandboxAvailable = $true
    }

    $script:HasBinaries = @(
        'XISFPropertyHandler.dll', 'XISFPreviewHandler.dll', 'XISFShellExtensionHost.exe'
    ) | ForEach-Object { Test-Path (Join-Path $ReleaseDir $_) } | Where-Object { $_ -eq $false }
    $script:HasBinaries = ($null -eq $HasBinaries -or $HasBinaries.Count -eq 0)

    # CLSID constants
    $script:PropertyHandlerClsid   = '7C54FA8B-9D63-4C10-8FBE-1A5A0F9A3B2E'
    $script:ThumbnailProviderClsid = '9C76E8AD-4E85-5F30-B00D-3C7D1AB5F6E0'
    $script:PreviewHandlerClsid    = 'AD87F6CE-5B03-6E41-C11E-4DB2AC06F5F1'
}

Describe 'MSIX sandbox install validation' -Tag 'Sandbox', 'Integration' {
    BeforeAll {
        if (-not $SandboxAvailable) { return }
        if (-not $HasBinaries) { return }

        # Unique run folder
        $script:RunId      = [guid]::NewGuid().ToString('N').Substring(0, 8)
        $script:WorkDir    = Join-Path $env:TEMP "xisf-sandbox-$RunId"
        $script:ResultsDir = Join-Path $WorkDir 'results'
        $script:PkgDir     = Join-Path $WorkDir 'package'
        New-Item -ItemType Directory -Path $ResultsDir -Force | Out-Null
        New-Item -ItemType Directory -Path $PkgDir -Force | Out-Null

        # Build MSIX
        & $BuildScript -Version '0.99.0' -BuildNumber 0 -OutputDir $PkgDir

        # Create self-signed cert for sandbox install
        $script:CertPath = Join-Path $PkgDir 'test.pfx'
        $script:CerPath  = Join-Path $PkgDir 'test.cer'
        $cert = New-SelfSignedCertificate `
            -Type Custom `
            -Subject 'CN=Dennis Payne' `
            -FriendlyName 'XISF Test Signing' `
            -KeyUsage DigitalSignature `
            -CertStoreLocation 'Cert:\CurrentUser\My' `
            -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3')
        $pwd = ConvertTo-SecureString -String 'XisfTest1!' -Force -AsPlainText
        Export-PfxCertificate -Cert $cert -FilePath $CertPath -Password $pwd | Out-Null
        Export-Certificate -Cert $cert -FilePath $CerPath | Out-Null
        # Clean up from host cert store
        Remove-Item "Cert:\CurrentUser\My\$($cert.Thumbprint)" -ErrorAction SilentlyContinue

        # Sign the MSIX
        $msixFile = Get-ChildItem $PkgDir -Filter '*.msix' | Select-Object -First 1
        $script:MsixPath = $msixFile.FullName
        $signtool = & {
            $roots = @("${env:ProgramFiles(x86)}\Windows Kits\10\bin",
                       "${env:ProgramFiles}\Windows Kits\10\bin") | Where-Object { Test-Path $_ }
            foreach ($r in $roots) {
                Get-ChildItem -Path $r -Recurse -Filter 'signtool.exe' -ErrorAction SilentlyContinue |
                    Where-Object { $_.FullName -match '\\x64\\' }
            }
        } | Sort-Object FullName -Descending | Select-Object -First 1
        & $signtool.FullName sign /fd SHA256 /f $CertPath /p 'XisfTest1!' $MsixPath

        # Write keep-alive marker if requested
        if ($KeepSandbox) {
            New-Item -Path (Join-Path $PkgDir 'keep-alive.marker') -ItemType File -Force | Out-Null
        }

        # Write the sandbox validation script
        $script:ValidationScript = Join-Path $PkgDir 'validate.ps1'
        @'
$ErrorActionPreference = 'Stop'
$results = @{ pass = @(); fail = @(); info = @{} }

function Assert($name, $condition, $detail) {
    if ($condition) { $results.pass += $name }
    else { $results.fail += @{ name = $name; detail = $detail } }
}

try {
    # Trust cert and install
    Import-Certificate -FilePath 'C:\Package\test.cer' -CertStoreLocation Cert:\LocalMachine\TrustedPeople | Out-Null

    # Install VCLibs framework dependency if the appx is available
    $vcLibsAppx = 'C:\Package\Microsoft.VCLibs.x64.14.00.Desktop.appx'
    if (Test-Path $vcLibsAppx) {
        Add-AppxPackage -Path $vcLibsAppx -ErrorAction Stop
    }

    Add-AppxPackage -Path 'C:\Package\XISF.ShellExtension_0.99.0.0_x64.msix'

    # 1. Package registered
    $pkg = Get-AppxPackage -Name 'DennisPayne.XISF.ShellExtension' -ErrorAction SilentlyContinue
    Assert 'PackageRegistered' ($null -ne $pkg) 'Get-AppxPackage returned null'
    if ($pkg) {
        $results.info['PackageFullName'] = $pkg.PackageFullName
        $results.info['InstallLocation'] = $pkg.InstallLocation
    }

    # 2. Exe exists in install location
    if ($pkg) {
        $exePath = Join-Path $pkg.InstallLocation 'XISFShellExtensionHost.exe'
        Assert 'ExeExists' (Test-Path $exePath) "Not found: $exePath"
    }

    # 3. DLLs exist
    if ($pkg) {
        $propDll = Join-Path $pkg.InstallLocation 'XISFPropertyHandler.dll'
        $prevDll = Join-Path $pkg.InstallLocation 'XISFPreviewHandler.dll'
        Assert 'PropertyHandlerDllExists' (Test-Path $propDll) "Not found: $propDll"
        Assert 'PreviewHandlerDllExists' (Test-Path $prevDll) "Not found: $prevDll"
    }

    # 4. .xisf file type association
    $ftaKey = 'HKCU:\Software\Classes\.xisf'
    $hasFta = Test-Path $ftaKey
    Assert 'XisfFileTypeRegistered' $hasFta "Registry key not found: $ftaKey"

    # 5. Packaged COM validation via installed manifest
    #    MSIX COM servers don't register in HKCR\CLSID; verify via the
    #    installed package manifest instead.
    if ($pkg) {
        $instManifest = Join-Path $pkg.InstallLocation 'AppxManifest.xml'
        if (Test-Path $instManifest) {
            [xml]$mx = Get-Content $instManifest -Raw
            $nsMgr = New-Object System.Xml.XmlNamespaceManager($mx.NameTable)
            $nsMgr.AddNamespace('com', 'http://schemas.microsoft.com/appx/manifest/com/windows10')
            $nsMgr.AddNamespace('desktop2', 'http://schemas.microsoft.com/appx/manifest/desktop/windows10/2')
            $nsMgr.AddNamespace('uap', 'http://schemas.microsoft.com/appx/manifest/uap/windows10')

            $comClasses = $mx.SelectNodes('//com:Class', $nsMgr)
            $classIds = $comClasses | ForEach-Object { $_.Id }
            Assert 'ComClsid_PropertyHandler'  ($classIds -contains '7C54FA8B-9D63-4C10-8FBE-1A5A0F9A3B2E') "Property CLSID not in manifest"
            Assert 'ComClsid_ThumbnailProvider' ($classIds -contains '9C76E8AD-4E85-5F30-B00D-3C7D1AB5F6E0') "Thumbnail CLSID not in manifest"
            Assert 'ComClsid_PreviewHandler'   ($classIds -contains 'AD87F6CE-5B03-6E41-C11E-4DB2AC06F5F1') "Preview CLSID not in manifest"

            # Verify CLSID-to-role mapping is correct
            $fta = $mx.SelectSingleNode('//uap:FileTypeAssociation[@Name="xisf"]', $nsMgr)
            $thumbClsid = $fta.SelectSingleNode('desktop2:ThumbnailHandler', $nsMgr).Clsid
            $prevClsid  = $fta.SelectSingleNode('desktop2:DesktopPreviewHandler', $nsMgr).Clsid
            $propClsid  = $fta.SelectSingleNode('desktop2:DesktopPropertyHandler', $nsMgr).Clsid
            Assert 'ClsidMapping_Thumbnail' ($thumbClsid -eq '9C76E8AD-4E85-5F30-B00D-3C7D1AB5F6E0') "ThumbnailHandler mapped to wrong CLSID: $thumbClsid"
            Assert 'ClsidMapping_Preview'   ($prevClsid  -eq 'AD87F6CE-5B03-6E41-C11E-4DB2AC06F5F1') "PreviewHandler mapped to wrong CLSID: $prevClsid"
            Assert 'ClsidMapping_Property'  ($propClsid  -eq '7C54FA8B-9D63-4C10-8FBE-1A5A0F9A3B2E') "PropertyHandler mapped to wrong CLSID: $propClsid"
        } else {
            Assert 'ComClsid_PropertyHandler'   $false "Installed manifest not found"
            Assert 'ComClsid_ThumbnailProvider'  $false "Installed manifest not found"
            Assert 'ComClsid_PreviewHandler'    $false "Installed manifest not found"
        }
    }

} catch {
    $results.fail += @{ name = 'UnhandledException'; detail = $_.Exception.Message }
}

# Write results atomically
$json = $results | ConvertTo-Json -Depth 4
$tmp = 'C:\Results\results.tmp'
$final = 'C:\Results\results.json'
Set-Content -Path $tmp -Value $json -Encoding UTF8
Move-Item -Path $tmp -Destination $final -Force

# Signal done and conditionally shutdown
New-Item -Path 'C:\Results\done.marker' -ItemType File -Force | Out-Null
if (Test-Path 'C:\Package\keep-alive.marker') {
    Write-Host "Sandbox kept alive for inspection. Close manually." -ForegroundColor Cyan
} else {
    Start-Sleep 2
    shutdown /s /t 0
}
'@ | Set-Content -Path $ValidationScript -Encoding UTF8

        # Generate .wsb file
        $script:WsbPath = Join-Path $WorkDir 'test.wsb'
        @"
<Configuration>
  <MappedFolders>
    <MappedFolder>
      <HostFolder>$PkgDir</HostFolder>
      <SandboxFolder>C:\Package</SandboxFolder>
      <ReadOnly>true</ReadOnly>
    </MappedFolder>
    <MappedFolder>
      <HostFolder>$ResultsDir</HostFolder>
      <SandboxFolder>C:\Results</SandboxFolder>
      <ReadOnly>false</ReadOnly>
    </MappedFolder>
  </MappedFolders>
  <LogonCommand>
    <Command>powershell -ExecutionPolicy Bypass -File C:\Package\validate.ps1</Command>
  </LogonCommand>
  <MemoryInMB>4096</MemoryInMB>
</Configuration>
"@ | Set-Content -Path $WsbPath -Encoding UTF8

        # Launch sandbox and wait for results
        $script:SandboxProcess = Start-Process -FilePath $WsbPath -PassThru
        $script:ResultsFile = Join-Path $ResultsDir 'results.json'
        $script:DoneMarker  = Join-Path $ResultsDir 'done.marker'

        $deadline = (Get-Date).AddSeconds($SandboxTimeoutSec)
        while (-not (Test-Path $DoneMarker) -and (Get-Date) -lt $deadline) {
            Start-Sleep -Seconds 5
        }

        if (Test-Path $ResultsFile) {
            $script:Results = Get-Content $ResultsFile -Raw | ConvertFrom-Json
        } else {
            $script:Results = $null
        }
    }

    AfterAll {
        if (-not $KeepSandbox) {
            if ($SandboxProcess -and -not $SandboxProcess.HasExited) {
                $SandboxProcess | Stop-Process -Force -ErrorAction SilentlyContinue
            }
            if ($WorkDir -and (Test-Path $WorkDir)) {
                Remove-Item $WorkDir -Recurse -Force -ErrorAction SilentlyContinue
            }
        } else {
            Write-Host "`nXISF_KEEP_SANDBOX=1: Sandbox left running for inspection." -ForegroundColor Cyan
            Write-Host "Work dir: $WorkDir" -ForegroundColor Cyan
            Write-Host "Close the sandbox window manually when done.`n" -ForegroundColor Cyan
        }
    }

    It 'Sandbox is available' {
        if (-not $SandboxAvailable) {
            Set-ItResult -Skipped -Because 'Windows Sandbox feature is not enabled'
        }
        $SandboxAvailable | Should -BeTrue
    }

    It 'Sandbox validation completed within timeout' {
        if (-not $SandboxAvailable) { Set-ItResult -Skipped -Because 'Windows Sandbox not available' }
        if (-not $HasBinaries) { Set-ItResult -Skipped -Because 'Release binaries not available' }
        $Results | Should -Not -BeNullOrEmpty -Because 'Sandbox should have written results.json'
    }

    It 'Package is registered via Get-AppxPackage' {
        if (-not $Results) { Set-ItResult -Skipped -Because 'No sandbox results' }
        $Results.pass | Should -Contain 'PackageRegistered'
    }

    It 'Settings exe exists in install location' {
        if (-not $Results) { Set-ItResult -Skipped -Because 'No sandbox results' }
        $Results.pass | Should -Contain 'ExeExists'
    }

    It 'Property handler DLL exists in install location' {
        if (-not $Results) { Set-ItResult -Skipped -Because 'No sandbox results' }
        $Results.pass | Should -Contain 'PropertyHandlerDllExists'
    }

    It 'Preview handler DLL exists in install location' {
        if (-not $Results) { Set-ItResult -Skipped -Because 'No sandbox results' }
        $Results.pass | Should -Contain 'PreviewHandlerDllExists'
    }

    It '.xisf file type is registered' {
        if (-not $Results) { Set-ItResult -Skipped -Because 'No sandbox results' }
        $Results.pass | Should -Contain 'XisfFileTypeRegistered'
    }

    It 'Property handler COM CLSID is registered' {
        if (-not $Results) { Set-ItResult -Skipped -Because 'No sandbox results' }
        $Results.pass | Should -Contain 'ComClsid_PropertyHandler'
    }

    It 'Thumbnail provider COM CLSID is registered' {
        if (-not $Results) { Set-ItResult -Skipped -Because 'No sandbox results' }
        $Results.pass | Should -Contain 'ComClsid_ThumbnailProvider'
    }

    It 'Preview handler COM CLSID is registered' {
        if (-not $Results) { Set-ItResult -Skipped -Because 'No sandbox results' }
        $Results.pass | Should -Contain 'ComClsid_PreviewHandler'
    }

    It 'Thumbnail CLSID maps to ThumbnailHandler role' {
        if (-not $Results) { Set-ItResult -Skipped -Because 'No sandbox results' }
        $Results.pass | Should -Contain 'ClsidMapping_Thumbnail'
    }

    It 'Preview CLSID maps to DesktopPreviewHandler role' {
        if (-not $Results) { Set-ItResult -Skipped -Because 'No sandbox results' }
        $Results.pass | Should -Contain 'ClsidMapping_Preview'
    }

    It 'Property CLSID maps to DesktopPropertyHandler role' {
        if (-not $Results) { Set-ItResult -Skipped -Because 'No sandbox results' }
        $Results.pass | Should -Contain 'ClsidMapping_Property'
    }

    It 'No validation failures' {
        if (-not $Results) { Set-ItResult -Skipped -Because 'No sandbox results' }
        if ($Results.fail.Count -gt 0) {
            $details = ($Results.fail | ForEach-Object {
                if ($_ -is [string]) { $_ } else { "$($_.name): $($_.detail)" }
            }) -join '; '
            $details | Should -BeNullOrEmpty -Because "All assertions should pass: $details"
        }
    }
}
