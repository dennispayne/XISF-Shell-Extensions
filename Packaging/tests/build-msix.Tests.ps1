<#
.SYNOPSIS
    Pester tests for the XISF Shell Extension MSIX packaging script.

.DESCRIPTION
    Validates build-msix.ps1: version handling, binary staging, manifest
    patching, CLSID-to-role mapping, asset packaging, and output file
    generation.

    Prerequisites:
      - Release|x64 solution build (all three binaries in x64\Release)
      - Windows 10 SDK (makeappx.exe)
      - Pester v5+

.EXAMPLE
    Invoke-Pester .\Packaging\tests\build-msix.Tests.ps1 -Output Detailed
#>

BeforeAll {
    $script:RepoRoot   = Resolve-Path (Join-Path $PSScriptRoot '..\..')
    $script:BuildScript = Join-Path $RepoRoot 'Packaging\build-msix.ps1'
    $script:ManifestSrc = Join-Path $RepoRoot 'Packaging\XISFShellExtensions\Package.appxmanifest'
    $script:ReleaseDir  = Join-Path $RepoRoot 'x64\Release'

    # CLSID constants — must match dllmain.cpp in each handler project
    $script:PropertyHandlerClsid  = '7C54FA8B-9D63-4C10-8FBE-1A5A0F9A3B2E'
    $script:ThumbnailProviderClsid = '9C76E8AD-4E85-5F30-B00D-3C7D1AB5F6E0'
    $script:PreviewHandlerClsid   = 'AD87F6CE-5B03-6E41-C11E-4DB2AC06F5F1'

    $script:RequiredBinaries = @(
        'XISFPropertyHandler.dll',
        'XISFPreviewHandler.dll',
        'XISFShellExtensionHost.exe'
    )
}

Describe 'build-msix.ps1 prerequisites' {
    It 'Script file exists' {
        $BuildScript | Should -Exist
    }

    It 'Source manifest exists' {
        $ManifestSrc | Should -Exist
    }

    It 'Release binaries exist' -ForEach @(
        @{ Binary = 'XISFPropertyHandler.dll' },
        @{ Binary = 'XISFPreviewHandler.dll' },
        @{ Binary = 'XISFShellExtensionHost.exe' }
    ) {
        Join-Path $ReleaseDir $Binary | Should -Exist
    }
}

Describe 'Source manifest CLSID-to-role mapping' {
    BeforeAll {
        [xml]$script:Manifest = Get-Content -LiteralPath $ManifestSrc -Raw
        $ns = @{
            uap      = 'http://schemas.microsoft.com/appx/manifest/uap/windows10'
            desktop2 = 'http://schemas.microsoft.com/appx/manifest/desktop/windows10/2'
            com      = 'http://schemas.microsoft.com/appx/manifest/com/windows10'
            default  = 'http://schemas.microsoft.com/appx/manifest/foundation/windows10'
        }
        $script:NsMgr = New-Object System.Xml.XmlNamespaceManager($Manifest.NameTable)
        foreach ($kv in $ns.GetEnumerator()) { $NsMgr.AddNamespace($kv.Key, $kv.Value) }

        $fta = $Manifest.SelectSingleNode('//uap:FileTypeAssociation[@Name="xisf"]', $NsMgr)
        $script:PropClsid  = $fta.SelectSingleNode('desktop2:DesktopPropertyHandler', $NsMgr).Clsid
        $script:PrevClsid  = $fta.SelectSingleNode('desktop2:DesktopPreviewHandler', $NsMgr).Clsid
        $script:ThumbClsid = $fta.SelectSingleNode('desktop2:ThumbnailHandler', $NsMgr).Clsid
    }

    It 'DesktopPropertyHandler references the property handler CLSID' {
        $PropClsid | Should -BeExactly $PropertyHandlerClsid
    }

    It 'DesktopPreviewHandler references the preview handler CLSID (AD87...)' {
        $PrevClsid | Should -BeExactly $PreviewHandlerClsid
    }

    It 'ThumbnailHandler references the thumbnail provider CLSID (9C76...)' {
        $ThumbClsid | Should -BeExactly $ThumbnailProviderClsid
    }

    It 'All three COM classes are declared in ComServer' {
        $classes = $Manifest.SelectNodes('//com:Class', $NsMgr)
        $ids = $classes | ForEach-Object { $_.Id }
        $ids | Should -Contain $PropertyHandlerClsid
        $ids | Should -Contain $ThumbnailProviderClsid
        $ids | Should -Contain $PreviewHandlerClsid
    }

    It 'Preview/Thumbnail classes both reference XISFPreviewHandler.dll' {
        $classes = $Manifest.SelectNodes('//com:Class', $NsMgr)
        ($classes | Where-Object { $_.Id -eq $ThumbnailProviderClsid }).Path |
            Should -BeExactly 'XISFPreviewHandler.dll'
        ($classes | Where-Object { $_.Id -eq $PreviewHandlerClsid }).Path |
            Should -BeExactly 'XISFPreviewHandler.dll'
    }

    It 'Property handler class references XISFPropertyHandler.dll' {
        $classes = $Manifest.SelectNodes('//com:Class', $NsMgr)
        ($classes | Where-Object { $_.Id -eq $PropertyHandlerClsid }).Path |
            Should -BeExactly 'XISFPropertyHandler.dll'
    }
}

Describe 'Version validation' {
    It 'Rejects non-numeric version "<Version>"' -ForEach @(
        @{ Version = 'abc' },
        @{ Version = '1.2' },
        @{ Version = '1.2.3.4.5' }
    ) {
        { & $BuildScript -Version $Version -OutputDir $TestDrive } |
            Should -Throw '*not a 4-part version*'
    }

    It 'Accepts valid version "1.2.3"' {
        # Will fail on binary check if binaries missing, but should not throw version error
        if (-not (Test-Path (Join-Path $ReleaseDir 'XISFPropertyHandler.dll'))) {
            Set-ItResult -Skipped -Because 'Release binaries not available'
        }
        { & $BuildScript -Version '1.2.3' -OutputDir $TestDrive } |
            Should -Not -Throw '*not a 4-part version*'
    }
}

Describe 'Package build output' -Tag 'Integration' {
    BeforeAll {
        $script:OutDir = Join-Path $TestDrive 'msix-out'
        New-Item -ItemType Directory -Path $OutDir -Force | Out-Null

        $hasBinaries = $RequiredBinaries | ForEach-Object {
            Test-Path (Join-Path $ReleaseDir $_)
        }
        if ($hasBinaries -contains $false) {
            $script:SkipBuild = $true
        } else {
            $script:SkipBuild = $false
            & $BuildScript -Version '0.99.0' -BuildNumber 1 -OutputDir $OutDir
        }

        $script:StageDir = Join-Path $RepoRoot 'artifacts\stage\XISFShellExtensions'
    }

    It 'Produces an MSIX file' {
        if ($SkipBuild) { Set-ItResult -Skipped -Because 'Release binaries not available' }
        Join-Path $OutDir 'XISF.ShellExtension_0.99.0.1_x64.msix' | Should -Exist
    }

    It 'Staged directory contains <Binary>' -ForEach @(
        @{ Binary = 'XISFPropertyHandler.dll' },
        @{ Binary = 'XISFPreviewHandler.dll' },
        @{ Binary = 'XISFShellExtensionHost.exe' },
        @{ Binary = 'AppxManifest.xml' }
    ) {
        if ($SkipBuild) { Set-ItResult -Skipped -Because 'Release binaries not available' }
        Join-Path $StageDir $Binary | Should -Exist
    }

    It 'Staged Assets directory contains all required logos' -ForEach @(
        @{ Logo = 'Square44x44Logo.png' },
        @{ Logo = 'Square150x150Logo.png' },
        @{ Logo = 'Wide310x150Logo.png' },
        @{ Logo = 'StoreLogo.png' }
    ) {
        if ($SkipBuild) { Set-ItResult -Skipped -Because 'Release binaries not available' }
        Join-Path $StageDir "Assets\$Logo" | Should -Exist
    }

    It 'Staged manifest has patched version' {
        if ($SkipBuild) { Set-ItResult -Skipped -Because 'Release binaries not available' }
        [xml]$xml = Get-Content (Join-Path $StageDir 'AppxManifest.xml') -Raw
        $xml.Package.Identity.Version | Should -BeExactly '0.99.0.1'
    }

    It 'Staged manifest preserves CLSID-to-role mapping' {
        if ($SkipBuild) { Set-ItResult -Skipped -Because 'Release binaries not available' }
        [xml]$xml = Get-Content (Join-Path $StageDir 'AppxManifest.xml') -Raw
        $ns = New-Object System.Xml.XmlNamespaceManager($xml.NameTable)
        $ns.AddNamespace('uap', 'http://schemas.microsoft.com/appx/manifest/uap/windows10')
        $ns.AddNamespace('desktop2', 'http://schemas.microsoft.com/appx/manifest/desktop/windows10/2')
        $fta = $xml.SelectSingleNode('//uap:FileTypeAssociation[@Name="xisf"]', $ns)
        $fta.SelectSingleNode('desktop2:ThumbnailHandler', $ns).Clsid |
            Should -BeExactly $ThumbnailProviderClsid
        $fta.SelectSingleNode('desktop2:DesktopPreviewHandler', $ns).Clsid |
            Should -BeExactly $PreviewHandlerClsid
    }
}
