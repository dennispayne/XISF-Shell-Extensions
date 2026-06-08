using System.Xml.Linq;

namespace Documentation.Tests;

[TestClass]
public class InstallerAuthoringTests
{
    private static readonly string RepoRoot = Path.GetFullPath(Path.Combine(
        Path.GetDirectoryName(typeof(InstallerAuthoringTests).Assembly.Location)!,
        "..", "..", "..", ".."));

    [TestMethod]
    public void DataPathLabel_UsesPlainTextWithoutAmpersand()
    {
        var packagePath = Path.Combine(RepoRoot, "Installer", "XISFInstaller", "Package.wxs");
        Assert.IsTrue(File.Exists(packagePath), $"Installer authoring file not found at: {packagePath}");

        var document = XDocument.Load(packagePath);
        XNamespace ns = "http://wixtoolset.org/schemas/v4/wxs";

        var dialog = document
            .Descendants(ns + "Dialog")
            .SingleOrDefault(element => string.Equals((string?)element.Attribute("Id"), "XISFDataPathDlg", StringComparison.Ordinal));
        Assert.IsNotNull(dialog, "Dialog 'XISFDataPathDlg' not found in Package.wxs.");

        var pathLabel = dialog!
            .Elements(ns + "Control")
            .SingleOrDefault(element => string.Equals((string?)element.Attribute("Id"), "PathLabel", StringComparison.Ordinal));
        Assert.IsNotNull(pathLabel, "Control 'PathLabel' not found in dialog 'XISFDataPathDlg'.");

        Assert.AreEqual(
            "Data path:",
            (string?)pathLabel!.Attribute("Text"),
            "The data path label should be plain text without an access-key ampersand."
        );
    }
}
