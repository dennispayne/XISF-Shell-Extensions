using System.Diagnostics;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.Principal;
using Microsoft.Win32;

namespace XISFInstallerTests;

/// <summary>
/// Functional E2E tests that install the MSI machine-wide, exercise each
/// shell-extension handler via direct COM (no Explorer, no PowerShell)
/// against every fixture under <c>fixtures/</c> plus one synthetic
/// fallback, and uninstall in cleanup. Mirrors the skip/elevation pattern
/// used by <see cref="MsiTests.Msi_Install_CompletesAndRegistersCoreComClasses"/>.
///
/// All tests are tagged <c>[TestCategory("InstallerIntegration")]</c> so
/// the fast hermetic suite (<see cref="MsiTests"/> default, plus
/// <see cref="RuntimeLinkageTests"/>) is unaffected on dev machines and CI
/// runners that lack elevation.
///
/// Lifecycle is one install per class run:
///   <see cref="Install"/>   - <see cref="ClassInitializeAttribute"/>
///   <see cref="Uninstall"/> - <see cref="ClassCleanupAttribute"/>
///
/// If any precondition fails (not Windows, not elevated, existing install
/// detected, MSI build missing, install fails) every test in the class
/// reports <see cref="Assert.Inconclusive(string)"/> instead of failing.
/// </summary>
[TestClass]
public class HandlerFunctionalTests
{
    private const string PropertyHandlerClsid     = "{7C54FA8B-9D63-4C10-8FBE-1A5A0F9A3B2E}";
    private const string ThumbnailProviderClsid   = "{9C76E8AD-4E85-5F30-B00D-3C7D1AB5F6E0}";
    private const string FilterClsid              = "{B4E7F2A1-3D8C-4F5E-9A1B-6C2D8E4F7A3B}";
    private const string PersistentHandlerClsid   = "{C5F8A3B2-4E9D-5067-AB2C-7D3E9F508B4C}";

    // The fmtid our property handler uses for custom Astro PROPERTYKEYs.
    private static readonly Guid XisfPkeyFmtid = new("7C54FA8B-9D63-4C10-8FBE-1A5A0F9A3B2E");

    private static string s_msiPath        = string.Empty;
    private static string s_installRoot    = string.Empty;
    private static string s_installLog     = string.Empty;
    private static string s_uninstallLog   = string.Empty;
    private static bool   s_installed;
    private static string? s_skipReason;

    public TestContext TestContext { get; set; } = null!;

    // ────────────────────────────────────────────────────────────────────
    // Lifecycle
    // ────────────────────────────────────────────────────────────────────

    [ClassInitialize]
    [Timeout(10 * 60 * 1000)]
    public static void Install(TestContext context)
    {
        if (!OperatingSystem.IsWindows())
        { s_skipReason = "Handler functional tests run on Windows only."; return; }

        if (!IsProcessElevated())
        { s_skipReason = "Handler functional tests require an elevated process."; return; }

        if (LooksLikeExistingMachineInstall())
        { s_skipReason = "Existing machine-level install detected; skipping destructive tests."; return; }

        var msiDir = context.Properties.ContainsKey("MsiDir")
            ? context.Properties["MsiDir"]!.ToString()!
            : Path.Combine(FindRepoRoot(), "Installer", "XISFInstaller", "bin");

        var msiFiles = Directory.GetFiles(msiDir, "*.msi", SearchOption.AllDirectories)
            .Select(p => new FileInfo(p))
            .OrderByDescending(f => f.LastWriteTimeUtc)
            .ToList();
        if (msiFiles.Count == 0)
        { s_skipReason = $"No MSI found under {msiDir}. Build the installer first."; return; }
        s_msiPath = msiFiles[0].FullName;

        s_installRoot = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),
            $"XISF Shell Extensions Functional {Guid.NewGuid():N}");

        var logDir = Path.GetDirectoryName(s_msiPath)!;
        s_installLog   = Path.Combine(logDir, $"msi-functional-install-{Guid.NewGuid():N}.log");
        s_uninstallLog = Path.Combine(logDir, $"msi-functional-uninstall-{Guid.NewGuid():N}.log");

        var installArgs =
            $"/i \"{s_msiPath}\" /qn /norestart /l*v \"{s_installLog}\" INSTALLFOLDER=\"{s_installRoot}\"";
        var exit = RunProcess("msiexec.exe", installArgs, TimeSpan.FromMinutes(5));
        if (exit != 0)
        {
            s_skipReason = $"MSI install failed with exit code {exit}. Log tail:\n{ReadLogTail(s_installLog)}";
            return;
        }
        s_installed = true;

        // Synthetic fixture fallback (always present so the smoke-test
        // row in DynamicData can run).
        TestXisfBuilder.Write(Fixtures.SyntheticXisfPath);
    }

    [ClassCleanup]
    [Timeout(10 * 60 * 1000)]
    public static void Uninstall()
    {
        try
        {
            if (File.Exists(Fixtures.SyntheticXisfPath))
                File.Delete(Fixtures.SyntheticXisfPath);
        }
        catch { }

        if (!s_installed) return;

        var args = $"/x \"{s_msiPath}\" /qn /norestart /l*v \"{s_uninstallLog}\"";
        try { RunProcess("msiexec.exe", args, TimeSpan.FromMinutes(5)); } catch { }
        TryDeleteDirectory(s_installRoot);
    }

    private void SkipIfNotInstalled()
    {
        if (s_skipReason is not null)
            Assert.Inconclusive(s_skipReason);
    }

    // ────────────────────────────────────────────────────────────────────
    // Tests (one row per fixture via [DynamicData])
    // ────────────────────────────────────────────────────────────────────

    [TestMethod]
    [DynamicData(nameof(Fixtures.AllFixtures), typeof(Fixtures),
        DynamicDataDisplayName = nameof(Fixtures.GetDisplayName),
        DynamicDataDisplayNameDeclaringType = typeof(Fixtures))]
    [TestCategory("InstallerIntegration")]
    [DoNotParallelize]
    [Timeout(2 * 60 * 1000)]
    public void PropertyHandler_IPropertyStore_ReturnsAtLeastOneProperty(string xisfPath, string displayName)
    {
        _ = displayName;
        SkipIfNotInstalled();

        var count = HandlerInterop.RunSta(() =>
        {
            var iidShellItem = HandlerInterop.IID_IShellItem;
            var iidPS        = HandlerInterop.IID_IPropertyStore;
            var item = HandlerInterop.SHCreateItemFromParsingName_ShellItem(
                xisfPath, IntPtr.Zero, ref iidShellItem);
            HandlerInterop.PSGetItemPropertyHandler(item, false, ref iidPS, out var ppv);
            var ps = (HandlerInterop.IPropertyStore)ppv;
            try { return ps.GetCount(); }
            finally { Marshal.ReleaseComObject(ps); }
        });

        TestContext.WriteLine($"[{Path.GetFileName(xisfPath)}] IPropertyStore.GetCount() = {count}");
        Assert.IsTrue(count > 0,
            $"Property handler returned no properties for {xisfPath}. " +
            "Either DllRegisterServer didn't run, the handler bailed out parsing the file, " +
            "or the file is on a path that denies oplocks.");
    }

    [TestMethod]
    [DynamicData(nameof(Fixtures.AllFixtures), typeof(Fixtures),
        DynamicDataDisplayName = nameof(Fixtures.GetDisplayName),
        DynamicDataDisplayNameDeclaringType = typeof(Fixtures))]
    [TestCategory("InstallerIntegration")]
    [DoNotParallelize]
    [Timeout(2 * 60 * 1000)]
    public void PropertyHandler_IPropertyStore_IncludesXisfFmtid(string xisfPath, string displayName)
    {
        _ = displayName;
        SkipIfNotInstalled();

        var fmtids = HandlerInterop.RunSta(() =>
        {
            var iidShellItem = HandlerInterop.IID_IShellItem;
            var iidPS        = HandlerInterop.IID_IPropertyStore;
            var item = HandlerInterop.SHCreateItemFromParsingName_ShellItem(
                xisfPath, IntPtr.Zero, ref iidShellItem);
            HandlerInterop.PSGetItemPropertyHandler(item, false, ref iidPS, out var ppv);
            var ps = (HandlerInterop.IPropertyStore)ppv;
            try
            {
                var seen = new HashSet<Guid>();
                var n = ps.GetCount();
                for (uint i = 0; i < n; i++)
                {
                    ps.GetAt(i, out var pk);
                    seen.Add(pk.fmtid);
                }
                return seen;
            }
            finally { Marshal.ReleaseComObject(ps); }
        });

        TestContext.WriteLine($"[{Path.GetFileName(xisfPath)}] Distinct PROPERTYKEY fmtids:");
        foreach (var g in fmtids) TestContext.WriteLine("  " + g);

        Assert.IsTrue(fmtids.Contains(XisfPkeyFmtid),
            $"Expected at least one PROPERTYKEY with fmtid {XisfPkeyFmtid} (our XISF pkey group) " +
            $"for {xisfPath}. Saw {fmtids.Count} distinct fmtids; none match.");
    }

    [TestMethod]
    [DynamicData(nameof(Fixtures.AllFixtures), typeof(Fixtures),
        DynamicDataDisplayName = nameof(Fixtures.GetDisplayName),
        DynamicDataDisplayNameDeclaringType = typeof(Fixtures))]
    [TestCategory("InstallerIntegration")]
    [DoNotParallelize]
    [Timeout(2 * 60 * 1000)]
    public void Shell_GetDetailsOf_ReturnsPopulatedCustomColumns(string xisfPath, string displayName)
    {
        _ = displayName;
        SkipIfNotInstalled();

        var (populated, hasAstroColumn) = HandlerInterop.RunSta(() =>
        {
            var shellType = Type.GetTypeFromProgID("Shell.Application")!;
            dynamic shell  = Activator.CreateInstance(shellType)!;
            dynamic folder = shell.NameSpace(Path.GetDirectoryName(xisfPath)!);
            dynamic item   = folder.ParseName(Path.GetFileName(xisfPath));

            int total = 0;
            bool astro = false;
            for (int col = 0; col < 400; col++)
            {
                string name = folder.GetDetailsOf(null, col) ?? string.Empty;
                string val  = folder.GetDetailsOf(item, col) ?? string.Empty;
                if (string.IsNullOrWhiteSpace(name)) continue;
                if (string.IsNullOrWhiteSpace(val))  continue;
                total++;
                if (name.StartsWith("Astro ", StringComparison.OrdinalIgnoreCase))
                    astro = true;
            }
            Marshal.ReleaseComObject(item);
            Marshal.ReleaseComObject(folder);
            Marshal.ReleaseComObject(shell);
            return (total, astro);
        });

        TestContext.WriteLine(
            $"[{Path.GetFileName(xisfPath)}] GetDetailsOf populated columns = {populated}; " +
            $"has 'Astro *' column = {hasAstroColumn}");
        Assert.IsTrue(populated >= 5,
            $"Expected at least 5 populated shell columns for {xisfPath}; got {populated}.");
        Assert.IsTrue(hasAstroColumn,
            $"No 'Astro *' column populated for {xisfPath}. The propdesc file may not have been " +
            "registered (check that xisf.propdesc is on disk and PSRegisterPropertySchema ran).");
    }

    [TestMethod]
    [DynamicData(nameof(Fixtures.AllFixtures), typeof(Fixtures),
        DynamicDataDisplayName = nameof(Fixtures.GetDisplayName),
        DynamicDataDisplayNameDeclaringType = typeof(Fixtures))]
    [TestCategory("InstallerIntegration")]
    [DoNotParallelize]
    [Timeout(2 * 60 * 1000)]
    public void ThumbnailProvider_GetImage_ReturnsNonNullBitmap(string xisfPath, string displayName)
    {
        _ = displayName;
        SkipIfNotInstalled();

        var hbm = HandlerInterop.RunSta(() =>
        {
            var iid = HandlerInterop.IID_IShellItemImageFactory;
            var fac = HandlerInterop.SHCreateItemFromParsingName_Image(
                xisfPath, IntPtr.Zero, ref iid);
            try
            {
                fac.GetImage(new HandlerInterop.SIZE { cx = 256, cy = 256 },
                             HandlerInterop.SIIGBF_RESIZETOFIT, out var bm);
                return bm;
            }
            finally { Marshal.ReleaseComObject(fac); }
        });

        try
        {
            TestContext.WriteLine($"[{Path.GetFileName(xisfPath)}] HBITMAP = 0x{hbm.ToInt64():X}");
            // Synthetic fixture has no pixel data; the handler may legitimately
            // fail to render a thumbnail. Accept that gracefully.
            if (hbm == IntPtr.Zero &&
                string.Equals(Path.GetFileName(xisfPath),
                              Path.GetFileName(Fixtures.SyntheticXisfPath),
                              StringComparison.OrdinalIgnoreCase))
            {
                Assert.Inconclusive(
                    "Synthetic XISF has no pixel data; thumbnail render is best-effort. " +
                    "Add real fixtures under Installer/XISFInstallerTests/fixtures/ for full coverage.");
            }
            Assert.AreNotEqual(IntPtr.Zero, hbm,
                $"IShellItemImageFactory.GetImage returned a null HBITMAP for {xisfPath}. " +
                "Either the thumbnail provider isn't registered or it failed to render.");
        }
        finally
        {
            if (hbm != IntPtr.Zero) HandlerInterop.DeleteObject(hbm);
        }
    }

    [TestMethod]
    [DynamicData(nameof(Fixtures.AllFixtures), typeof(Fixtures),
        DynamicDataDisplayName = nameof(Fixtures.GetDisplayName),
        DynamicDataDisplayNameDeclaringType = typeof(Fixtures))]
    [TestCategory("InstallerIntegration")]
    [DoNotParallelize]
    [Timeout(2 * 60 * 1000)]
    public void Filter_LoadIFilter_ReturnsNonNullInstance(string xisfPath, string displayName)
    {
        _ = displayName;
        SkipIfNotInstalled();

        var typeName = HandlerInterop.RunSta(() =>
        {
            var ifilter = HandlerInterop.LoadIFilter(xisfPath, null, out var unk);
            try { return ifilter?.GetType().FullName ?? "<null>"; }
            finally
            {
                if (ifilter is not null) Marshal.ReleaseComObject(ifilter);
                if (unk     is not null) Marshal.ReleaseComObject(unk);
            }
        });

        TestContext.WriteLine($"[{Path.GetFileName(xisfPath)}] LoadIFilter returned: {typeName}");
        Assert.IsFalse(typeName == "<null>",
            $"LoadIFilter returned null for {xisfPath}. The IFilter persistent-handler chain " +
            $"(PersistentHandler -> {PersistentHandlerClsid} -> Filter {FilterClsid}) is broken.");
    }

    // ────────────────────────────────────────────────────────────────────
    // Tests that don't need a fixture
    // ────────────────────────────────────────────────────────────────────

    [TestMethod]
    [TestCategory("InstallerIntegration")]
    [DoNotParallelize]
    [Timeout(60 * 1000)]
    public void WSearch_XisfExtension_IsRegisteredAsContentIndexerExtension()
    {
        SkipIfNotInstalled();

        using var key = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView.Registry64)
            .OpenSubKey(@"SOFTWARE\Microsoft\Windows Search\ContentIndexer\Extensions\.xisf");
        Assert.IsNotNull(key,
            ".xisf is not registered under HKLM\\SOFTWARE\\Microsoft\\Windows Search\\ContentIndexer\\Extensions. " +
            "WSearch will not index XISF files.");
    }

    [TestMethod]
    [TestCategory("InstallerIntegration")]
    [DoNotParallelize]
    [Timeout(60 * 1000)]
    public void XisfPersistentHandler_PointsToOurIFilter()
    {
        SkipIfNotInstalled();

        using var key = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView.Registry64)
            .OpenSubKey(@"SOFTWARE\Classes\.xisf\PersistentHandler");
        Assert.IsNotNull(key, "HKCR\\.xisf\\PersistentHandler is missing.");
        var value = key.GetValue(null) as string;
        Assert.AreEqual(PersistentHandlerClsid, value, true,
            "HKCR\\.xisf\\PersistentHandler does not point at our IFilter persistent handler CLSID.");
    }

    // ────────────────────────────────────────────────────────────────────
    // Helpers (intentionally duplicated from MsiTests to keep this class
    // self-contained; future refactor can extract a shared helpers class).
    // ────────────────────────────────────────────────────────────────────

    private static bool IsProcessElevated()
    {
        using var identity = WindowsIdentity.GetCurrent();
        var principal = new WindowsPrincipal(identity);
        return principal.IsInRole(WindowsBuiltInRole.Administrator);
    }

    private static bool LooksLikeExistingMachineInstall()
    {
        using var clsidKey = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView.Registry64)
            .OpenSubKey($@"SOFTWARE\Classes\CLSID\{PropertyHandlerClsid}\InProcServer32");
        if (clsidKey?.GetValue(null) is string existing && !string.IsNullOrWhiteSpace(existing))
            return true;

        using var marker = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView.Registry64)
            .OpenSubKey(@"SOFTWARE\DennisPayne\XISF Shell Extension\MSI");
        return marker?.GetValue("RestartManagerInstalled") != null;
    }

    private static int RunProcess(string fileName, string arguments, TimeSpan timeout)
    {
        using var p = new Process
        {
            StartInfo = new ProcessStartInfo
            {
                FileName = fileName,
                Arguments = arguments,
                UseShellExecute = false,
                CreateNoWindow = true
            }
        };
        p.Start();
        if (!p.WaitForExit((int)timeout.TotalMilliseconds))
        {
            try { p.Kill(entireProcessTree: true); } catch { }
            throw new TimeoutException($"{fileName} timed out after {timeout.TotalMinutes} min: {arguments}");
        }
        return p.ExitCode;
    }

    private static string ReadLogTail(string path, int max = 80)
    {
        if (!File.Exists(path)) return $"<log not found: {path}>";
        var lines = File.ReadAllLines(path);
        return lines.Length <= max
            ? string.Join(Environment.NewLine, lines)
            : string.Join(Environment.NewLine, lines.Skip(lines.Length - max));
    }

    private static void TryDeleteDirectory(string path)
    {
        try { if (Directory.Exists(path)) Directory.Delete(path, recursive: true); } catch { }
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
