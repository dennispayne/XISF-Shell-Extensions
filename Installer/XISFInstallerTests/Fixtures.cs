using System.Reflection;

namespace XISFInstallerTests;

/// <summary>
/// Discovers XISF fixture files for the
/// <see cref="HandlerFunctionalTests"/> data-driven tests.
///
/// At test-discovery time we enumerate any <c>*.xisf</c> file under
/// <c>Installer/XISFInstallerTests/fixtures/</c> in the repo, plus one
/// synthetic file written by <see cref="TestXisfBuilder"/> to a stable
/// temp path. Each entry produces its own row in Test Explorer via
/// <c>[DynamicData]</c>, so a failure for one file does not mask
/// successes for others.
/// </summary>
public static class Fixtures
{
    public static readonly string SyntheticXisfPath =
        Path.Combine(Path.GetTempPath(), "xisf-functest-synthetic.xisf");

    /// <summary>
    /// Enumerates fixture rows for <c>[DynamicData]</c>:
    /// <c>{ path, displayName }</c>.
    /// </summary>
    public static IEnumerable<object[]> AllFixtures()
    {
        var dir = TryFindFixturesDir();
        if (dir is not null)
        {
            foreach (var file in Directory.EnumerateFiles(dir, "*.xisf", SearchOption.TopDirectoryOnly)
                                          .OrderBy(p => p, StringComparer.OrdinalIgnoreCase))
            {
                yield return new object[] { file, Path.GetFileName(file) };
            }
        }
        yield return new object[] { SyntheticXisfPath, "<synthetic>" };
    }

    /// <summary>
    /// Custom display name for <c>[DynamicData]</c>: shows the fixture
    /// basename instead of a marshalled path string.
    /// </summary>
    public static string GetDisplayName(MethodInfo methodInfo, object[] data)
        => $"{methodInfo.Name}({(string)data[1]})";

    private static string? TryFindFixturesDir()
    {
        var dir = new DirectoryInfo(AppContext.BaseDirectory);
        while (dir is not null)
        {
            var candidate = Path.Combine(
                dir.FullName, "Installer", "XISFInstallerTests", "fixtures");
            if (Directory.Exists(candidate))
                return candidate;
            dir = dir.Parent;
        }
        return null;
    }
}
