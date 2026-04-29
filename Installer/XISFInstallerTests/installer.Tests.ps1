<#
.SYNOPSIS
    Pester tests validating the XISF Shell Extensions MSI package structure.

.DESCRIPTION
    These tests verify the MSI contains the expected files, properties,
    custom actions, and directory structure without requiring an actual install.
    Uses the WiX SDK's dark.exe (decompiler) or direct MSI table queries.
#>

BeforeAll {
    $repoRoot   = Resolve-Path (Join-Path $PSScriptRoot '..\..')
    $msiDir     = Join-Path $repoRoot 'Installer\XISFInstaller\bin\Release'
    $msiPattern = Join-Path $msiDir '*.msi'

    # Find the built MSI
    $msi = Get-ChildItem $msiPattern -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $msi) {
        throw "No MSI found at $msiPattern. Build the installer first: dotnet build Installer\XISFInstaller -c Release"
    }

    $script:MsiPath = $msi.FullName

    # Helper: query MSI database tables using COM
    function Get-MsiTableRows {
        param([string]$MsiPath, [string]$Table)
        $installer = New-Object -ComObject WindowsInstaller.Installer
        $database  = $installer.OpenDatabase($MsiPath, 0) # msiOpenDatabaseModeReadOnly
        $view      = $database.OpenView("SELECT * FROM ``$Table``")
        [void]$view.Execute()
        $rows = @()

        # Get column count from column info record
        $colInfo = $view.ColumnInfo(0) # msiColumnInfoNames
        $colCount = 0
        if ($colInfo) {
            for ($c = 1; $c -le 20; $c++) {
                try {
                    $n = $colInfo.StringData($c)
                    if ([string]::IsNullOrEmpty($n)) { break }
                    $colCount = $c
                } catch { break }
            }
            [System.Runtime.InteropServices.Marshal]::ReleaseComObject($colInfo) | Out-Null
        }
        if ($colCount -eq 0) { $colCount = 10 }

        while ($true) {
            $record = $view.Fetch()
            if (-not $record) { break }
            $cols = @()
            for ($i = 1; $i -le $colCount; $i++) {
                try {
                    $val = $record.StringData($i)
                    $cols += $val
                } catch { break }
            }
            $rows += ,@($cols)
            [System.Runtime.InteropServices.Marshal]::ReleaseComObject($record) | Out-Null
        }
        [System.Runtime.InteropServices.Marshal]::ReleaseComObject($view)     | Out-Null
        [System.Runtime.InteropServices.Marshal]::ReleaseComObject($database)  | Out-Null
        [System.Runtime.InteropServices.Marshal]::ReleaseComObject($installer) | Out-Null
        return $rows
    }

    # Helper: get a single MSI property
    function Get-MsiProperty {
        param([string]$MsiPath, [string]$Property)
        $installer = New-Object -ComObject WindowsInstaller.Installer
        $database  = $installer.OpenDatabase($MsiPath, 0)
        $view      = $database.OpenView("SELECT `Value` FROM `Property` WHERE `Property` = '$Property'")
        [void]$view.Execute()
        $record = $view.Fetch()
        $value = if ($record) { $record.StringData(1) } else { $null }
        if ($record) { [System.Runtime.InteropServices.Marshal]::ReleaseComObject($record) | Out-Null }
        [System.Runtime.InteropServices.Marshal]::ReleaseComObject($view)     | Out-Null
        [System.Runtime.InteropServices.Marshal]::ReleaseComObject($database)  | Out-Null
        [System.Runtime.InteropServices.Marshal]::ReleaseComObject($installer) | Out-Null
        return $value
    }
}

Describe 'MSI Package Properties' {

    It 'ProductName is XISF Shell Extensions' {
        Get-MsiProperty -MsiPath $script:MsiPath -Property 'ProductName' |
            Should -Be 'XISF Shell Extensions'
    }

    It 'Manufacturer is Dennis Payne' {
        Get-MsiProperty -MsiPath $script:MsiPath -Property 'Manufacturer' |
            Should -Be 'Dennis Payne'
    }

    It 'ProductVersion is a valid 3-part version' {
        $ver = Get-MsiProperty -MsiPath $script:MsiPath -Property 'ProductVersion'
        $ver | Should -Match '^\d+\.\d+\.\d+$'
    }

    It 'UpgradeCode is set for major upgrades' {
        Get-MsiProperty -MsiPath $script:MsiPath -Property 'UpgradeCode' |
            Should -Not -BeNullOrEmpty
    }
}

Describe 'MSI File Table' {

    BeforeAll {
        $script:Files = (Get-MsiTableRows -MsiPath $script:MsiPath -Table 'File') |
            ForEach-Object { $_[0] }
    }

    It 'Contains XISFPropertyHandler.dll' {
        $script:Files | Should -Contain 'XISFPropertyHandler.dll'
    }

    It 'Contains XISFPreviewHandler.dll' {
        $script:Files | Should -Contain 'XISFPreviewHandler.dll'
    }

    It 'Contains XISFFilter.dll' {
        $script:Files | Should -Contain 'XISFFilter.dll'
    }

    It 'Contains XISFShellExtensionHost.exe' {
        $script:Files | Should -Contain 'XISFShellExtensionHost.exe'
    }

    It 'Contains xisf.propdesc' {
        $script:Files | Should -Contain 'xisf.propdesc'
    }
}

Describe 'MSI Custom Actions' {

    BeforeAll {
        $script:CustomActions = (Get-MsiTableRows -MsiPath $script:MsiPath -Table 'CustomAction') |
            ForEach-Object { $_[0] }
    }

    It 'Has registration custom actions for all three handlers' {
        $script:CustomActions | Should -Contain 'CA_RegisterProperty'
        $script:CustomActions | Should -Contain 'CA_RegisterPreview'
        $script:CustomActions | Should -Contain 'CA_RegisterFilter'
    }

    It 'Has unregistration custom actions for all three handlers' {
        $script:CustomActions | Should -Contain 'CA_UnregisterProperty'
        $script:CustomActions | Should -Contain 'CA_UnregisterPreview'
        $script:CustomActions | Should -Contain 'CA_UnregisterFilter'
    }
}

Describe 'MSI Shortcut' {

    BeforeAll {
        $script:Shortcuts = Get-MsiTableRows -MsiPath $script:MsiPath -Table 'Shortcut'
    }

    It 'Creates a Start Menu shortcut' {
        $script:Shortcuts.Count | Should -BeGreaterThan 0
    }

    It 'Shortcut targets XISFShellExtensionHost.exe' {
        # Column 5 (index 4) is the Target column in the Shortcut table
        # Single-row result may unwrap to flat array
        $first = if ($script:Shortcuts[0] -is [array]) { $script:Shortcuts[0] } else { $script:Shortcuts }
        $first[4] | Should -Be '[INSTALLFOLDER]XISFShellExtensionHost.exe'
    }
}

Describe 'MSI Install Sequence' {

    BeforeAll {
        $script:ExecSeq = Get-MsiTableRows -MsiPath $script:MsiPath -Table 'InstallExecuteSequence'
        $script:ActionNames = $script:ExecSeq | ForEach-Object { $_[0] }
    }

    It 'Registration actions are scheduled' {
        $script:ActionNames | Should -Contain 'CA_RegisterProperty'
        $script:ActionNames | Should -Contain 'CA_RegisterPreview'
        $script:ActionNames | Should -Contain 'CA_RegisterFilter'
    }

    It 'Unregistration actions are scheduled' {
        $script:ActionNames | Should -Contain 'CA_UnregisterProperty'
        $script:ActionNames | Should -Contain 'CA_UnregisterPreview'
        $script:ActionNames | Should -Contain 'CA_UnregisterFilter'
    }
}
