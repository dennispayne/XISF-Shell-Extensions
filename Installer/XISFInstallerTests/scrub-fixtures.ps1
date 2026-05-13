# Scrubs PII from XISF fixture headers in-place using length-preserving
# byte substitution (no header-length or attachment-offset recalc needed).
#
# Run: pwsh -File scrub-fixtures.ps1
#
# What we strip:
#   - Observation:Location:{Latitude,Longitude,Elevation} Property values
#   - SITELAT / SITELONG / SITEELEV FITS keyword values
#   - CAMERAID FITS keyword value (Windows USB hardware-instance-id path)
#   - Any embedded D:/Astro/M 42 (or similar) PixInsight history paths
#
# What we keep:
#   - Telescope, camera (model only), filter, exposure, RA/Dec of object,
#     timestamps, FITS keywords describing the target/instrument

[CmdletBinding()]
param([string]$FixturesDir = (Join-Path $PSScriptRoot 'fixtures'))

$ErrorActionPreference = 'Stop'

function Read-XisfHeader {
    param([string]$Path)
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 16) { throw "Too small: $Path" }
    $sig = [System.Text.Encoding]::ASCII.GetString($bytes, 0, 8)
    if ($sig -ne 'XISF0100') { throw "Bad signature in $Path : $sig" }
    $hdrLen = [BitConverter]::ToUInt32($bytes, 8)
    $xmlStart = 16
    $xml = [System.Text.Encoding]::UTF8.GetString($bytes, $xmlStart, $hdrLen)
    return [pscustomobject]@{
        Bytes    = $bytes
        XmlStart = $xmlStart
        HdrLen   = $hdrLen
        Xml      = $xml
    }
}

# Replace a substring with a same-length one, padding with trailing spaces if
# replacement is shorter. Throws if replacement is longer.
function Pad-ToLength {
    param([string]$Replacement, [int]$TargetLen)
    if ($Replacement.Length -gt $TargetLen) {
        throw "Replacement too long: '$Replacement' is $($Replacement.Length), need <= $TargetLen"
    }
    return $Replacement + (' ' * ($TargetLen - $Replacement.Length))
}

# Replace value="<anything>" inside a specific tag, preserving total byte length.
# $TagPattern is a regex matching the surrounding context up to but not including
# the value="..." fragment we want to redact. We require a unique anchor so we
# don't accidentally clobber unrelated value="..." attributes elsewhere.
function Scrub-AttributeValue {
    param(
        [string]$Xml,
        [string]$AnchorRegex,   # e.g. 'name="SITELAT"\s+value=' or 'id="Observation:Location:Latitude"[^/]*?\s+value='
        [string]$NewValue
    )
    $rx = [regex]"(?<anchor>$AnchorRegex)`"(?<val>[^`"]*)`""
    return $rx.Replace($Xml, {
        param($m)
        $oldVal = $m.Groups['val'].Value
        $padded = Pad-ToLength -Replacement $NewValue -TargetLen $oldVal.Length
        return "$($m.Groups['anchor'].Value)`"$padded`""
    })
}

function Write-XisfHeader {
    param([string]$Path, [pscustomobject]$Header, [string]$NewXml)
    $newBytes = [System.Text.Encoding]::UTF8.GetBytes($NewXml)
    if ($newBytes.Length -ne $Header.HdrLen) {
        throw "Header byte length changed (was $($Header.HdrLen), now $($newBytes.Length)). Refusing to write."
    }
    [Array]::Copy($newBytes, 0, $Header.Bytes, $Header.XmlStart, $newBytes.Length)
    [System.IO.File]::WriteAllBytes($Path, $Header.Bytes)
}

function Scrub-File {
    param([string]$Path)
    Write-Host "==> $([System.IO.Path]::GetFileName($Path))" -ForegroundColor Cyan
    $h = Read-XisfHeader -Path $Path
    $origBytes = $h.Bytes.Length
    $xml = $h.Xml
    $origXmlLen = [System.Text.Encoding]::UTF8.GetByteCount($xml)

    # 1) Property elements (Observation:Location:*)
    foreach ($id in @(
        'Observation:Location:Latitude',
        'Observation:Location:Longitude',
        'Observation:Location:Elevation'
    )) {
        $xml = Scrub-AttributeValue -Xml $xml `
            -AnchorRegex ('id="' + [regex]::Escape($id) + '"[^/]*?\s+value=') `
            -NewValue '0'
    }

    # 2) FITS keywords (SITELAT/SITELONG/SITEELEV)
    foreach ($name in @('SITELAT', 'SITELONG', 'SITEELEV')) {
        $xml = Scrub-AttributeValue -Xml $xml `
            -AnchorRegex ('name="' + $name + '"\s+value=') `
            -NewValue '0'
    }

    # 3) CAMERAID (USB hardware instance id)
    $xml = Scrub-AttributeValue -Xml $xml `
        -AnchorRegex 'name="CAMERAID"\s+value=' `
        -NewValue "''"

    # 4) Strip PixInsight processing history wholesale (out of scope for this
    #    repo; carries embedded local paths and is opaque to the shell handler
    #    tests anyway). Two surface areas:
    #      a) <Property id="PixInsight:ProcessingHistory" ...>...</Property>
    #      b) <FITSKeyword name="COMMENT" .../> entries inserted by the
    #         PixInsight pipeline (Image Integration / Background Calibration /
    #         RGB Working Space / Drizzle / etc.)
    $beforeLen = $xml.Length
    $rxHistory = [regex]'(?s)<Property\s+id="PixInsight:ProcessingHistory"[^>]*>.*?</Property>'
    $xml = $rxHistory.Replace($xml, '')
    $rxComment = [regex]'<FITSKeyword\s+name="COMMENT"[^/]*/>'
    $xml = $rxComment.Replace($xml, '')
    if ($xml.Length -lt $beforeLen) {
        Write-Host "    stripped processing history: $($beforeLen - $xml.Length) chars"
    }

    # 5) Length-preserving literal substitution for any embedded local paths
    #    that survived (defense in depth).
    foreach ($literal in @('D:/Astro/M 42', 'D:\Astro\M 42', 'D:/astro/M 42', 'd:/astro/m 42')) {
        if ($xml.Contains($literal)) {
            $padded = Pad-ToLength -Replacement '/redacted' -TargetLen $literal.Length
            $xml = $xml.Replace($literal, $padded)
            Write-Host "    redacted embedded path '$literal'"
        }
    }

    # 6) Pad XML back to original byte length so we don't have to touch the
    #    header-length field or recompute attachment offsets. We pad with a
    #    single XML comment whose body is whatever-many spaces are needed.
    $newXmlLen = [System.Text.Encoding]::UTF8.GetByteCount($xml)
    if ($newXmlLen -lt $origXmlLen) {
        $delta = $origXmlLen - $newXmlLen
        $minComment = '<!---->'.Length        # 7
        if ($delta -lt $minComment) {
            # Pad with raw spaces between elements; XML tolerates that.
            $xml = $xml + (' ' * $delta)
        } else {
            $padBody = ' ' * ($delta - $minComment)
            $xml = $xml + "<!--$padBody-->"
        }
        $newXmlLen = [System.Text.Encoding]::UTF8.GetByteCount($xml)
    }

    Write-XisfHeader -Path $Path -Header $h -NewXml $xml
    $newBytes = [System.IO.File]::ReadAllBytes($Path)
    if ($newBytes.Length -ne $origBytes) {
        throw "File length changed!"
    }

    # Validate XML still parses
    $h2 = Read-XisfHeader -Path $Path
    [void]([xml]$h2.Xml)

    # Validate PII strings are gone
    $remainingPii = @()
    foreach ($needle in @(
        'SITELAT" value="3', 'SITELAT" value="-',
        'SITELONG" value="3', 'SITELONG" value="-',
        'Latitude"[^/]*value="[^0]',
        'Longitude"[^/]*value="[^0]',
        'D:/Astro', 'D:\Astro',
        'PixInsight:ProcessingHistory'
    )) {
        if ($h2.Xml -match $needle) { $remainingPii += $needle }
    }
    if ($remainingPii.Count -gt 0) {
        throw "PII still present matching: $($remainingPii -join ', ')"
    }
    Write-Host "    OK: $origBytes bytes preserved, XML re-parses, PII cleared" -ForegroundColor Green
}

Get-ChildItem -Path $FixturesDir -Filter '*.xisf' | ForEach-Object {
    Scrub-File -Path $_.FullName
}
Write-Host "All fixtures scrubbed." -ForegroundColor Green
