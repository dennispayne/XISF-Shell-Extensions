function New-TestXisf {
    <#
    .SYNOPSIS
        Generates a minimal valid XISF file for testing.
    .DESCRIPTION
        Creates a binary XISF file with a well-formed XML header containing
        common FITS keywords used in astrophotography. The file is suitable
        for testing the XISF property handler without requiring real image data.
    .PARAMETER Path
        Full path where the .xisf file will be written.
    .OUTPUTS
        System.String - The full path of the created file.
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string]$Path
    )

    $xml = @'
<?xml version="1.0" encoding="UTF-8"?>
<xisf version="1.0" xmlns="http://www.pixinsight.com/xisf" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:schemaLocation="http://www.pixinsight.com/xisf http://pixinsight.com/xisf/xisf-1.0.xsd">
  <Image geometry="100:100:1" sampleFormat="UInt16" colorSpace="Gray" location="attachment:1024:20000">
    <FITSKeyword name="OBJECT" value="'IC 1396'" comment="Target name" />
    <FITSKeyword name="EXPTIME" value="300.0" comment="Exposure time in seconds" />
    <FITSKeyword name="INSTRUME" value="'ZWO ASI2600MM Pro'" comment="Camera model" />
    <FITSKeyword name="FILTER" value="'Ha'" comment="Filter name" />
    <FITSKeyword name="GAIN" value="100" comment="Camera gain" />
    <FITSKeyword name="OFFSET" value="50" comment="Camera offset" />
    <FITSKeyword name="CCD-TEMP" value="-10.0" comment="Sensor temperature" />
    <FITSKeyword name="SET-TEMP" value="-10.0" comment="Target temperature" />
    <FITSKeyword name="OBJCTRA" value="'21 36 00'" comment="RA J2000" />
    <FITSKeyword name="OBJCTDEC" value="'+57 30 00'" comment="Dec J2000" />
    <FITSKeyword name="XBINNING" value="1" comment="X binning" />
    <FITSKeyword name="YBINNING" value="1" comment="Y binning" />
    <FITSKeyword name="FOCALLEN" value="530.0" comment="Focal length mm" />
    <FITSKeyword name="SITELAT" value="40.0" comment="Site latitude" />
    <FITSKeyword name="SITELONG" value="-75.0" comment="Site longitude" />
    <FITSKeyword name="SITEELEV" value="100.0" comment="Site elevation meters" />
    <FITSKeyword name="DATE-OBS" value="'2024-09-15T02:30:00'" comment="Observation date UTC" />
    <FITSKeyword name="TELESCOP" value="'Takahashi FSQ-106N'" comment="Telescope name" />
    <FITSKeyword name="AIRMASS" value="1.15" comment="Airmass" />
  </Image>
</xisf>
'@

    $xmlBytes = [System.Text.Encoding]::UTF8.GetBytes($xml)
    $headerLength = [uint32]$xmlBytes.Length

    $signature = [System.Text.Encoding]::ASCII.GetBytes('XISF0100')
    $lengthBytes = [System.BitConverter]::GetBytes($headerLength)
    $reserved = [byte[]]::new(4)

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $parentDir = [System.IO.Path]::GetDirectoryName($fullPath)
    if (-not (Test-Path $parentDir)) {
        $null = New-Item -Path $parentDir -ItemType Directory -Force
    }

    $stream = [System.IO.File]::Create($fullPath)
    try {
        $stream.Write($signature, 0, $signature.Length)
        $stream.Write($lengthBytes, 0, $lengthBytes.Length)
        $stream.Write($reserved, 0, $reserved.Length)
        $stream.Write($xmlBytes, 0, $xmlBytes.Length)
    }
    finally {
        $stream.Close()
    }

    return $fullPath
}
