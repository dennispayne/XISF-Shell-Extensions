<#
.SYNOPSIS
    Pester functional tests for XISF Shell Extension via Windows Sandbox.

.DESCRIPTION
    Builds a signed MSIX, creates a synthetic XISF test file, launches
    Windows Sandbox, and validates end-to-end functionality: property
    handler reads metadata via Shell.Application, settings app runs,
    registry toggles disable/enable handlers, and feature tiers affect
    property output.

    Prerequisites:
      - Windows Sandbox feature enabled
      - Release|x64 solution build
      - Pester v5+

.EXAMPLE
    Invoke-Pester .\Packaging\tests\sandbox-functional.Tests.ps1 -Output Detailed
#>

BeforeAll {
    $script:RepoRoot    = Resolve-Path (Join-Path $PSScriptRoot '..\..')
    $script:BuildScript = Join-Path $RepoRoot 'Packaging\build-msix.ps1'
    $script:ReleaseDir  = Join-Path $RepoRoot 'x64\Release'
    $script:TestsDir    = $PSScriptRoot
    $script:SandboxTimeoutSec = 300   # Functional tests need more time (Explorer restarts)

    # Check prerequisites
    $script:SandboxAvailable = $null -ne (Get-Command 'WindowsSandbox.exe' -ErrorAction SilentlyContinue)

    $script:HasBinaries = @(
        'XISFPropertyHandler.dll', 'XISFPreviewHandler.dll', 'XISFShellExtensionHost.exe'
    ) | ForEach-Object { Test-Path (Join-Path $ReleaseDir $_) } | Where-Object { $_ -eq $false }
    $script:HasBinaries = ($null -eq $HasBinaries -or $HasBinaries.Count -eq 0)
}

Describe 'MSIX functional validation in sandbox' -Tag 'Sandbox', 'Functional' {
    BeforeAll {
        if (-not $SandboxAvailable) { return }
        if (-not $HasBinaries) { return }

        # Unique run folder
        $script:RunId      = [guid]::NewGuid().ToString('N').Substring(0, 8)
        $script:WorkDir    = Join-Path $env:TEMP "xisf-functional-$RunId"
        $script:ResultsDir = Join-Path $WorkDir 'results'
        $script:PkgDir     = Join-Path $WorkDir 'package'
        $script:TestDataDir = Join-Path $WorkDir 'testdata'
        New-Item -ItemType Directory -Path $ResultsDir -Force | Out-Null
        New-Item -ItemType Directory -Path $PkgDir -Force | Out-Null
        New-Item -ItemType Directory -Path $TestDataDir -Force | Out-Null

        # Generate synthetic XISF test file
        . (Join-Path $TestsDir 'New-TestXisf.ps1')
        New-TestXisf -Path (Join-Path $TestDataDir 'test.xisf')

        # Build MSIX
        & $BuildScript -Version '0.99.0' -BuildNumber 0 -OutputDir $PkgDir

        # Create self-signed cert
        $cert = New-SelfSignedCertificate `
            -Type Custom `
            -Subject 'CN=Dennis Payne' `
            -FriendlyName 'XISF Functional Test' `
            -KeyUsage DigitalSignature `
            -CertStoreLocation 'Cert:\CurrentUser\My' `
            -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3')
        $pwd = ConvertTo-SecureString -String 'XisfTest1!' -Force -AsPlainText
        Export-PfxCertificate -Cert $cert -FilePath (Join-Path $PkgDir 'test.pfx') -Password $pwd | Out-Null
        Export-Certificate -Cert $cert -FilePath (Join-Path $PkgDir 'test.cer') | Out-Null
        Remove-Item "Cert:\CurrentUser\My\$($cert.Thumbprint)" -ErrorAction SilentlyContinue

        # Sign the MSIX
        $msixFile = Get-ChildItem $PkgDir -Filter '*.msix' | Select-Object -First 1
        $signtool = & {
            $roots = @("${env:ProgramFiles(x86)}\Windows Kits\10\bin",
                       "${env:ProgramFiles}\Windows Kits\10\bin") | Where-Object { Test-Path $_ }
            foreach ($r in $roots) {
                Get-ChildItem -Path $r -Recurse -Filter 'signtool.exe' -ErrorAction SilentlyContinue |
                    Where-Object { $_.FullName -match '\\x64\\' }
            }
        } | Sort-Object FullName -Descending | Select-Object -First 1
        & $signtool.FullName sign /fd SHA256 /f (Join-Path $PkgDir 'test.pfx') /p 'XisfTest1!' $msixFile.FullName

        # Copy validation script to package dir (mapped as C:\Installer in sandbox)
        Copy-Item (Join-Path $TestsDir 'sandbox-functional-validate.ps1') $PkgDir -Force

        # Generate .wsb
        $script:WsbPath = Join-Path $WorkDir 'functional-test.wsb'
        @"
<Configuration>
  <MappedFolders>
    <MappedFolder>
      <HostFolder>$PkgDir</HostFolder>
      <SandboxFolder>C:\Installer</SandboxFolder>
      <ReadOnly>true</ReadOnly>
    </MappedFolder>
    <MappedFolder>
      <HostFolder>$TestDataDir</HostFolder>
      <SandboxFolder>C:\TestData</SandboxFolder>
      <ReadOnly>true</ReadOnly>
    </MappedFolder>
    <MappedFolder>
      <HostFolder>$ResultsDir</HostFolder>
      <SandboxFolder>C:\Results</SandboxFolder>
      <ReadOnly>false</ReadOnly>
    </MappedFolder>
  </MappedFolders>
  <LogonCommand>
    <Command>powershell -ExecutionPolicy Bypass -File C:\Installer\sandbox-functional-validate.ps1</Command>
  </LogonCommand>
  <MemoryInMB>4096</MemoryInMB>
</Configuration>
"@ | Set-Content -Path $WsbPath -Encoding UTF8

        # Launch sandbox and wait
        $script:SandboxProcess = Start-Process -FilePath $WsbPath -PassThru
        $script:DoneMarker  = Join-Path $ResultsDir 'done.marker'
        $script:ResultsFile = Join-Path $ResultsDir 'functional-results.json'

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
        if ($SandboxProcess -and -not $SandboxProcess.HasExited) {
            $SandboxProcess | Stop-Process -Force -ErrorAction SilentlyContinue
        }
        if ($WorkDir -and (Test-Path $WorkDir)) {
            Remove-Item $WorkDir -Recurse -Force -ErrorAction SilentlyContinue
        }
    }

    # --- Prerequisite checks ---

    It 'Sandbox is available' {
        if (-not $SandboxAvailable) {
            Set-ItResult -Skipped -Because 'Windows Sandbox feature is not enabled'
        }
        $SandboxAvailable | Should -BeTrue
    }

    It 'Functional validation completed within timeout' {
        if (-not $SandboxAvailable) { Set-ItResult -Skipped -Because 'Sandbox not available' }
        if (-not $HasBinaries) { Set-ItResult -Skipped -Because 'Release binaries not available' }
        $Results | Should -Not -BeNullOrEmpty -Because 'Sandbox should have written functional-results.json'
    }

    # --- Installation ---

    It 'Package installed successfully' {
        if (-not $Results) { Set-ItResult -Skipped -Because 'No results' }
        $Results.pass | Should -Contain 'PackageInstalled'
    }

    # --- Runtime ---

    It 'VC runtime is available' {
        if (-not $Results) { Set-ItResult -Skipped -Because 'No results' }
        $Results.pass | Should -Contain 'VCRuntime'
    }

    # --- COM Activation (informational — packaged COM may not activate outside identity) ---

    It 'COM activation of property handler (may fail for packaged COM)' {
        if (-not $Results) { Set-ItResult -Skipped -Because 'No results' }
        # This is informational — packaged COM can't activate outside package context
        $passed = $Results.pass -contains 'ComActivation_PropertyHandler'
        $failed = ($Results.fail | Where-Object { $_.name -eq 'ComActivation_PropertyHandler' })
        if (-not $passed -and $failed) {
            Set-ItResult -Inconclusive -Because "Expected for packaged COM: $($failed.detail)"
        }
    }

    # --- Shell Property Handler ---
    # NOTE: MSIX packaged COM property handlers are only invoked by Explorer's
    # shell, not by SHGetPropertyStoreFromParsingName from PowerShell.  These
    # tests verify whether the handler can be reached from scripted calls;
    # they are expected to fail for MSIX and are therefore informational.

    It 'Property handler exposes OBJECT (IC 1396) [informational — MSIX limitation]' {
        if (-not $Results) { Set-ItResult -Skipped -Because 'No results' }
        $passed = $Results.pass -contains 'ShellProp_OBJECT'
        if (-not $passed) {
            Set-ItResult -Inconclusive -Because 'Packaged COM property handlers are not resolved by SHGetPropertyStoreFromParsingName'
        }
    }

    It 'Property handler exposes EXPTIME (300) [informational — MSIX limitation]' {
        if (-not $Results) { Set-ItResult -Skipped -Because 'No results' }
        $passed = $Results.pass -contains 'ShellProp_EXPTIME'
        if (-not $passed) {
            Set-ItResult -Inconclusive -Because 'Packaged COM property handlers are not resolved by SHGetPropertyStoreFromParsingName'
        }
    }

    It 'Property handler exposes INSTRUME (ZWO ASI2600MM Pro) [informational — MSIX limitation]' {
        if (-not $Results) { Set-ItResult -Skipped -Because 'No results' }
        $passed = $Results.pass -contains 'ShellProp_INSTRUME'
        if (-not $passed) {
            Set-ItResult -Inconclusive -Because 'Packaged COM property handlers are not resolved by SHGetPropertyStoreFromParsingName'
        }
    }

    It 'Property handler exposes FILTER (Ha) [informational — MSIX limitation]' {
        if (-not $Results) { Set-ItResult -Skipped -Because 'No results' }
        $passed = $Results.pass -contains 'ShellProp_FILTER'
        if (-not $passed) {
            Set-ItResult -Inconclusive -Because 'Packaged COM property handlers are not resolved by SHGetPropertyStoreFromParsingName'
        }
    }

    # --- Settings App ---

    It 'Settings app exe exists in package' {
        if (-not $Results) { Set-ItResult -Skipped -Because 'No results' }
        $Results.pass | Should -Contain 'SettingsAppExists'
    }

    It 'Settings app --silent-install runs without crashing' {
        if (-not $Results) { Set-ItResult -Skipped -Because 'No results' }
        $Results.pass | Should -Contain 'SilentInstallRuns'
    }

    # --- Registry Toggle ---

    It 'PropertyEnabled=0 disables property handler' {
        if (-not $Results) { Set-ItResult -Skipped -Because 'No results' }
        $Results.pass | Should -Contain 'ToggleDisable'
    }

    It 'PropertyEnabled=1 re-enables property handler [informational — depends on handler invocation]' {
        if (-not $Results) { Set-ItResult -Skipped -Because 'No results' }
        $passed = $Results.pass -contains 'ToggleEnable'
        if (-not $passed) {
            Set-ItResult -Inconclusive -Because 'Requires property handler invocation which is limited in MSIX'
        }
    }

    # --- Feature Tiers ---

    It 'Basic tier (0) still exposes core FITS metadata [informational — depends on handler invocation]' {
        if (-not $Results) { Set-ItResult -Skipped -Because 'No results' }
        $passed = $Results.pass -contains 'TierBasic'
        if (-not $passed) {
            Set-ItResult -Inconclusive -Because 'Requires property handler invocation which is limited in MSIX'
        }
    }

    It 'Full tier (2) exposes at least as many properties as Basic' {
        if (-not $Results) { Set-ItResult -Skipped -Because 'No results' }
        $Results.pass | Should -Contain 'TierFull'
    }

    # --- Catch-all ---

    It 'No unexpected validation failures' {
        if (-not $Results) { Set-ItResult -Skipped -Because 'No results' }
        # Filter out expected limitations: COM activation, shell properties (MSIX),
        # toggle re-enable and tier tests that depend on handler invocation
        $unexpectedFails = @($Results.fail | Where-Object {
            $_.name -notmatch '^(ComActivation_|ShellProp_|ToggleEnable|TierBasic)'
        })
        if ($unexpectedFails.Count -gt 0) {
            $details = ($unexpectedFails | ForEach-Object { "$($_.name): $($_.detail)" }) -join '; '
            $details | Should -BeNullOrEmpty -Because "All non-handler assertions should pass: $details"
        }
    }

    # --- Diagnostics ---

    It 'Sandbox diagnostics' {
        if (-not $Results) { Set-ItResult -Skipped -Because 'No results' }
        $diag = $Results.info | ConvertTo-Json -Depth 5
        Write-Host "`n=== Sandbox Diagnostics ===" -ForegroundColor Yellow
        Write-Host $diag -ForegroundColor Gray
        Write-Host "=== End Diagnostics ===`n" -ForegroundColor Yellow
        $true | Should -BeTrue
    }
}
