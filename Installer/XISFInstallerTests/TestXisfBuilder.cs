using System.Text;

namespace XISFInstallerTests;

/// <summary>
/// Pure-C# port of <c>Installer/XISFInstallerTests/New-TestXisf.ps1</c>.
/// Produces a minimal valid XISF file with a header-only stream containing
/// FITS keywords. Sufficient for exercising the property handler and IFilter;
/// the thumbnail provider may degrade since no pixel data is supplied.
/// </summary>
internal static class TestXisfBuilder
{
    public static void Write(
        string path,
        string objectName = "IC 1396",
        string telescope = "Takahashi FSQ-106N",
        string instrument = "ZWO ASI2600MM Pro",
        string filter = "Ha")
    {
        var xml = $"""
<?xml version="1.0" encoding="UTF-8"?>
<xisf version="1.0" xmlns="http://www.pixinsight.com/xisf" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:schemaLocation="http://www.pixinsight.com/xisf http://pixinsight.com/xisf/xisf-1.0.xsd">
  <Image geometry="100:100:1" sampleFormat="UInt16" colorSpace="Gray" location="attachment:1024:20000">
    <FITSKeyword name="OBJECT" value="'{objectName}'" comment="Target name" />
    <FITSKeyword name="EXPTIME" value="300.0" comment="Exposure time in seconds" />
    <FITSKeyword name="INSTRUME" value="'{instrument}'" comment="Camera model" />
    <FITSKeyword name="FILTER" value="'{filter}'" comment="Filter name" />
    <FITSKeyword name="GAIN" value="100" comment="Camera gain" />
    <FITSKeyword name="OFFSET" value="50" comment="Camera offset" />
    <FITSKeyword name="CCD-TEMP" value="-10.0" comment="Sensor temperature" />
    <FITSKeyword name="OBJCTRA" value="'21 36 00'" comment="RA J2000" />
    <FITSKeyword name="OBJCTDEC" value="'+57 30 00'" comment="Dec J2000" />
    <FITSKeyword name="XBINNING" value="1" comment="X binning" />
    <FITSKeyword name="YBINNING" value="1" comment="Y binning" />
    <FITSKeyword name="FOCALLEN" value="530.0" comment="Focal length mm" />
    <FITSKeyword name="DATE-OBS" value="'2024-09-15T02:30:00'" comment="Observation date UTC" />
    <FITSKeyword name="TELESCOP" value="'{telescope}'" comment="Telescope name" />
    <FITSKeyword name="AIRMASS" value="1.15" comment="Airmass" />
  </Image>
</xisf>
""";
        var xmlBytes = Encoding.UTF8.GetBytes(xml);

        var dir = Path.GetDirectoryName(Path.GetFullPath(path));
        if (!string.IsNullOrEmpty(dir))
            Directory.CreateDirectory(dir);

        using var fs = File.Create(path);
        fs.Write(Encoding.ASCII.GetBytes("XISF0100"));      // 8 bytes signature
        fs.Write(BitConverter.GetBytes((uint)xmlBytes.Length)); // 4 bytes header length (LE)
        fs.Write(new byte[4]);                               // 4 bytes reserved
        fs.Write(xmlBytes);                                  // XML header
    }
}
