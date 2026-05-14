using System.Xml.Linq;

namespace XISFInstallerTests;

[TestClass]
public class RuntimeLinkageTests
{
    [TestMethod]
    public void ShippingProjects_UseStaticCrtInReleaseX64()
    {
        var repoRoot = FindRepoRoot();
        var projects = new[]
        {
            Path.Combine(repoRoot, "PropertyHandler", "XISFPropertyHandler", "XISFPropertyHandler.vcxproj"),
            Path.Combine(repoRoot, "PreviewHandler", "XISFPreviewHandler", "XISFPreviewHandler.vcxproj"),
            Path.Combine(repoRoot, "Filter", "XISFFilter", "XISFFilter.vcxproj"),
            Path.Combine(repoRoot, "ShellExtensionHost", "ShellExtensionHost", "ShellExtensionHost.vcxproj")
        };

        foreach (var projectPath in projects)
        {
            Assert.IsTrue(File.Exists(projectPath), $"Missing project file: {projectPath}");
            var runtime = GetReleaseX64RuntimeLibrary(projectPath);
            Assert.AreEqual("MultiThreaded", runtime,
                $"Expected static CRT (MultiThreaded) for Release|x64 in {projectPath}, got '{runtime ?? "<null>"}'.");
        }
    }

    private static string? GetReleaseX64RuntimeLibrary(string projectPath)
    {
        var doc = XDocument.Load(projectPath);
        var ns = doc.Root?.Name.Namespace ?? XNamespace.None;

        var releaseGroup = doc.Descendants(ns + "ItemDefinitionGroup")
            .FirstOrDefault(x => (string?)x.Attribute("Condition") is string condition &&
                                 condition.Contains("Release|x64", StringComparison.OrdinalIgnoreCase));

        return releaseGroup?
            .Element(ns + "ClCompile")?
            .Element(ns + "RuntimeLibrary")?
            .Value;
    }

    private static string FindRepoRoot()
    {
        var dir = new DirectoryInfo(AppContext.BaseDirectory);
        while (dir != null)
        {
            if (File.Exists(Path.Combine(dir.FullName, "Win11-XISF-Shell-Extensions.sln")))
                return dir.FullName;
            dir = dir.Parent;
        }

        throw new InvalidOperationException("Could not find repository root");
    }
}
