using System.Xml.Linq;

namespace Documentation.Tests;

[TestClass]
public class PropdescSearchInfoTests
{
    private static readonly string RepoRoot = Path.Combine(
        Path.GetDirectoryName(typeof(PropdescSearchInfoTests).Assembly.Location)!,
        "..", "..", "..", ".."
    );

    [TestMethod]
    [Description("Verify key equipment-name properties are enabled for full-text indexing")]
    public void TestEquipmentNamePropertiesAreInvertedIndexed()
    {
        var propdescPath = Path.Combine(
            RepoRoot,
            "PropertyHandler",
            "XISFPropertyHandler",
            "propdesc",
            "xisf.propdesc"
        );
        Assert.IsTrue(File.Exists(propdescPath), $"xisf.propdesc not found at: {propdescPath}");

        var document = XDocument.Load(propdescPath);
        XNamespace ns = "http://schemas.microsoft.com/windows/2006/propertydescription";

        foreach (var propertyName in new[] { "XISF.FocuserName", "XISF.RotatorName", "XISF.FilterWheel" })
        {
            var propertyDescription = document
                .Descendants(ns + "propertyDescription")
                .SingleOrDefault(element => string.Equals((string?)element.Attribute("name"), propertyName, StringComparison.Ordinal));

            Assert.IsNotNull(propertyDescription, $"Property '{propertyName}' not found in xisf.propdesc.");

            var searchInfo = propertyDescription!.Element(ns + "searchInfo");
            Assert.IsNotNull(searchInfo, $"searchInfo missing for property '{propertyName}'.");

            Assert.AreEqual(
                "true",
                (string?)searchInfo!.Attribute("inInvertedIndex"),
                $"Property '{propertyName}' must be indexed in the inverted index for full-text search."
            );
        }
    }
}
