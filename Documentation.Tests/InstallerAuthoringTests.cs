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

        var package = File.ReadAllText(packagePath);

        StringAssert.Contains(package, "Id=\"PathLabel\" Type=\"Text\"");
        StringAssert.Contains(package, "Text=\"Data path:\"");
        Assert.IsFalse(package.Contains("Text=\"Data &amp;path:\"", StringComparison.Ordinal),
            "The data path label should not define an access-key ampersand.");
    }
}
