using System.Xml.Linq;

namespace Documentation.Tests;

[TestClass]
public class BuildVersionPropsTests
{
    private static readonly string RepoRoot = Path.Combine(
        Path.GetDirectoryName(typeof(BuildVersionPropsTests).Assembly.Location)!,
        "..", "..", "..", ".."
    );

    [TestMethod]
    [Description("Verify native ClCompile version macros include XISF_VERSION_TEXT for C++ callers")]
    public void TestClCompileVersionDefinitionsIncludeVersionText()
    {
        var propsPath = Path.Combine(RepoRoot, "build", "version.props");
        Assert.IsTrue(File.Exists(propsPath), $"version.props not found at: {propsPath}");

        var document = XDocument.Load(propsPath);
        var clCompileDefinitions = document
            .Descendants("ClCompile")
            .Elements("PreprocessorDefinitions")
            .Select(element => element.Value)
            .ToList();

        Assert.AreEqual(1, clCompileDefinitions.Count, "Expected exactly one ClCompile preprocessor definition block.");

        StringAssert.Contains(
            clCompileDefinitions[0],
            "XISF_VERSION_TEXT=$(XISFFullVersion)",
            "ClCompile definitions must set XISF_VERSION_TEXT so C++ sources do not fall back to 0.1.0.0."
        );
    }
}
