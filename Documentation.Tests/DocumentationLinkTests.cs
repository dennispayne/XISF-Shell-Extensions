namespace Documentation.Tests;

/// <summary>
/// Documentation validation tests to ensure:
/// - No broken internal links (dead references to .md files)
/// - Proper markdown conventions and formatting
/// - All files referenced in index.md exist
/// - Consistent heading hierarchy
/// </summary>
[TestClass]
public class DocumentationLinkTests
{
    private static readonly string DocsDir = Path.Combine(
        Path.GetDirectoryName(typeof(DocumentationLinkTests).Assembly.Location)!,
        "..", "..", "..", "..",
        "docs"
    );

    private static readonly string RepoRoot = Path.Combine(DocsDir, "..");

    [TestInitialize]
    public void ValidateDocsDirectory()
    {
        Assert.IsTrue(
            Directory.Exists(DocsDir),
            $"Documentation directory not found at: {DocsDir}"
        );
    }

    [TestMethod]
    [Description("Verify all markdown files exist (no dead links to local files)")]
    public void TestAllLocalLinksExist()
    {
        var brokenLinks = new List<(string source, string link)>();

        foreach (var mdFile in Directory.EnumerateFiles(DocsDir, "*.md", SearchOption.AllDirectories))
        {
            var relativeSource = Path.GetRelativePath(DocsDir, mdFile);
            var content = File.ReadAllText(mdFile);

            // Find markdown links: [text](path)
            var linkPattern = @"\[([^\]]+)\]\(([^)]+)\)";
            var matches = System.Text.RegularExpressions.Regex.Matches(content, linkPattern);

            foreach (System.Text.RegularExpressions.Match match in matches)
            {
                var linkTarget = match.Groups[2].Value;

                // Skip external URLs (http, https, mailto)
                if (linkTarget.StartsWith("http://") || linkTarget.StartsWith("https://") || 
                    linkTarget.StartsWith("mailto:"))
                    continue;

                // Extract file path (remove anchor)
                var filePath = linkTarget.Split('#')[0].Trim();

                if (string.IsNullOrEmpty(filePath))
                    continue;

                // Resolve relative path from the source file's directory
                var sourceDir = Path.GetDirectoryName(mdFile)!;
                var resolvedPath = Path.GetFullPath(Path.Combine(sourceDir, filePath));
                var normalizedPath = Path.GetFullPath(resolvedPath);

                // Check if the target file exists
                if (!File.Exists(normalizedPath))
                {
                    brokenLinks.Add((relativeSource, linkTarget));
                }
            }
        }

        // Report all broken links
        if (brokenLinks.Count > 0)
        {
            var report = new System.Text.StringBuilder();
            report.AppendLine($"Found {brokenLinks.Count} broken link(s):");
            foreach (var (source, link) in brokenLinks)
            {
                report.AppendLine($"  - In {source}: {link}");
            }
            Assert.Fail(report.ToString());
        }
    }

    [TestMethod]
    [Description("Verify README.md links to existing documentation files")]
    public void TestReadmeLinksExist()
    {
        var readmePath = Path.Combine(RepoRoot, "README.md");
        Assert.IsTrue(File.Exists(readmePath), "README.md not found");

        var content = File.ReadAllText(readmePath);
        var brokenLinks = new List<string>();

        // Find markdown links to docs
        var linkPattern = @"\[([^\]]+)\]\(docs/([^)]+)\)";
        var matches = System.Text.RegularExpressions.Regex.Matches(content, linkPattern);

        foreach (System.Text.RegularExpressions.Match match in matches)
        {
            var docPath = match.Groups[2].Value;
            var fullPath = Path.Combine(RepoRoot, "docs", docPath);

            if (!File.Exists(fullPath))
            {
                brokenLinks.Add(docPath);
            }
        }

        if (brokenLinks.Count > 0)
        {
            Assert.Fail($"README.md contains {brokenLinks.Count} broken link(s):\n" +
                        string.Join("\n  - ", brokenLinks));
        }
    }

    [TestMethod]
    [Description("Verify all markdown files have valid header hierarchy (no skipped levels beyond 1)")]
    public void TestHeadingHierarchy()
    {
        var issues = new List<(string file, string issue)>();

        foreach (var mdFile in Directory.EnumerateFiles(DocsDir, "*.md", SearchOption.AllDirectories))
        {
            var relativeSource = Path.GetRelativePath(DocsDir, mdFile);
            var content = File.ReadAllText(mdFile);
            var lines = content.Split('\n');

            int? previousLevel = null;
            for (int i = 0; i < lines.Length; i++)
            {
                var line = lines[i];
                if (!line.StartsWith('#'))
                    continue;

                // Count leading # characters
                int level = 0;
                foreach (var ch in line)
                {
                    if (ch == '#') level++;
                    else break;
                }

                // Check for major level skips (more than 1 level jump in middle of document)
                // Exception: H1 → H3 is allowed (document with intro → sections pattern)
                if (previousLevel != null && previousLevel > 1 && level > previousLevel + 1)
                {
                    issues.Add((relativeSource, $"Line {i + 1}: Skipped heading level from H{previousLevel} to H{level}"));
                }

                previousLevel = level;
            }
        }

        if (issues.Count > 0)
        {
            var report = new System.Text.StringBuilder();
            report.AppendLine($"Found {issues.Count} heading hierarchy issue(s):");
            foreach (var (file, issue) in issues)
            {
                report.AppendLine($"  - {file}: {issue}");
            }
            Assert.Fail(report.ToString());
        }
    }

    [TestMethod]
    [Description("Verify all markdown files exist and are readable")]
    public void TestMarkdownFilesReadable()
    {
        var mdFiles = Directory.EnumerateFiles(DocsDir, "*.md", SearchOption.AllDirectories).ToList();
        Assert.IsTrue(mdFiles.Count > 0, "No markdown files found in docs directory");

        var errors = new List<(string file, string error)>();

        foreach (var file in mdFiles)
        {
            try
            {
                var content = File.ReadAllText(file);
                Assert.IsFalse(string.IsNullOrWhiteSpace(content), $"File {file} is empty");
            }
            catch (Exception ex)
            {
                errors.Add((Path.GetRelativePath(DocsDir, file), ex.Message));
            }
        }

        if (errors.Count > 0)
        {
            var report = new System.Text.StringBuilder();
            report.AppendLine($"Found {errors.Count} file reading error(s):");
            foreach (var (file, error) in errors)
            {
                report.AppendLine($"  - {file}: {error}");
            }
            Assert.Fail(report.ToString());
        }
    }

    [TestMethod]
    [Description("Verify all markdown links use proper relative paths (no absolute Windows paths)")]
    public void TestNoAbsolutePathsInLinks()
    {
        var issues = new List<(string file, string link)>();

        foreach (var mdFile in Directory.EnumerateFiles(DocsDir, "*.md", SearchOption.AllDirectories))
        {
            var relativeSource = Path.GetRelativePath(DocsDir, mdFile);
            var content = File.ReadAllText(mdFile);

            // Find markdown links: [text](path)
            var linkPattern = @"\[([^\]]+)\]\(([^)]+)\)";
            var matches = System.Text.RegularExpressions.Regex.Matches(content, linkPattern);

            foreach (System.Text.RegularExpressions.Match match in matches)
            {
                var linkTarget = match.Groups[2].Value;

                // Skip external URLs
                if (linkTarget.StartsWith("http://") || linkTarget.StartsWith("https://") || 
                    linkTarget.StartsWith("mailto:"))
                    continue;

                // Check for Windows absolute paths (contains :\)
                if (linkTarget.Contains(":\\") || (linkTarget.StartsWith("C:") || linkTarget.StartsWith("D:")))
                {
                    issues.Add((relativeSource, linkTarget));
                }
            }
        }

        if (issues.Count > 0)
        {
            var report = new System.Text.StringBuilder();
            report.AppendLine($"Found {issues.Count} absolute path link(s):");
            foreach (var (file, link) in issues)
            {
                report.AppendLine($"  - In {file}: {link}");
            }
            Assert.Fail(report.ToString());
        }
    }
}
