using System.Runtime.InteropServices;

namespace XISFInstallerTests;

/// <summary>
/// Validates the XISF Shell Extensions MSI package structure by querying
/// MSI database tables via COM (WindowsInstaller.Installer).
/// No actual install is performed.
/// </summary>
[TestClass]
public class MsiTests
{
    private static string s_msiPath = string.Empty;

    [ClassInitialize]
    public static void FindMsi(TestContext context)
    {
        // MsiDir can be overridden via runsettings
        var msiDir = context.Properties.Contains("MsiDir")
            ? context.Properties["MsiDir"]!.ToString()!
            : Path.Combine(FindRepoRoot(), "Installer", "XISFInstaller", "bin", "Release");

        var msiFiles = Directory.GetFiles(msiDir, "*.msi");
        Assert.IsTrue(msiFiles.Length > 0,
            $"No MSI found in {msiDir}. Build the installer first: dotnet build Installer\\XISFInstaller -c Release");

        s_msiPath = msiFiles[0];
    }

    // ── Package Properties ──────────────────────────────────────────────

    [TestMethod]
    public void ProductName_IsXISFShellExtensions()
        => Assert.AreEqual("XISF Shell Extensions", GetMsiProperty("ProductName"));

    [TestMethod]
    public void Manufacturer_IsDennisPayne()
        => Assert.AreEqual("Dennis Payne", GetMsiProperty("Manufacturer"));

    [TestMethod]
    public void ProductVersion_IsValid3Part()
    {
        var ver = GetMsiProperty("ProductVersion");
        Assert.IsNotNull(ver);
        Assert.IsTrue(System.Text.RegularExpressions.Regex.IsMatch(ver, @"^\d+\.\d+\.\d+$"),
            $"ProductVersion '{ver}' is not a valid 3-part version");
    }

    [TestMethod]
    public void UpgradeCode_IsSet()
        => Assert.IsFalse(string.IsNullOrEmpty(GetMsiProperty("UpgradeCode")));

    // ── File Table ──────────────────────────────────────────────────────

    [TestMethod]
    public void FileTable_ContainsPropertyHandlerDll()
        => CollectionAssert.Contains(GetFileIds(), "XISFPropertyHandler.dll");

    [TestMethod]
    public void FileTable_ContainsPreviewHandlerDll()
        => CollectionAssert.Contains(GetFileIds(), "XISFPreviewHandler.dll");

    [TestMethod]
    public void FileTable_ContainsFilterDll()
        => CollectionAssert.Contains(GetFileIds(), "XISFFilter.dll");

    [TestMethod]
    public void FileTable_ContainsSettingsApp()
        => CollectionAssert.Contains(GetFileIds(), "XISFShellExtensionHost.exe");

    [TestMethod]
    public void FileTable_ContainsPropdesc()
        => CollectionAssert.Contains(GetFileIds(), "xisf.propdesc");

    // ── Custom Actions ──────────────────────────────────────────────────

    [TestMethod]
    public void CustomActions_HasRegistrationForAllHandlers()
    {
        var actions = GetCustomActionIds();
        CollectionAssert.Contains(actions, "CA_RegisterProperty");
        CollectionAssert.Contains(actions, "CA_RegisterPreview");
        CollectionAssert.Contains(actions, "CA_RegisterFilter");
    }

    [TestMethod]
    public void CustomActions_HasUnregistrationForAllHandlers()
    {
        var actions = GetCustomActionIds();
        CollectionAssert.Contains(actions, "CA_UnregisterProperty");
        CollectionAssert.Contains(actions, "CA_UnregisterPreview");
        CollectionAssert.Contains(actions, "CA_UnregisterFilter");
    }

    // ── Shortcut ────────────────────────────────────────────────────────

    [TestMethod]
    public void Shortcut_Exists()
    {
        var rows = QueryTable("Shortcut");
        Assert.IsTrue(rows.Count > 0, "No shortcuts found in MSI");
    }

    [TestMethod]
    public void Shortcut_TargetsSettingsApp()
    {
        var rows = QueryTable("Shortcut");
        // Column index 4 is Target in the Shortcut table
        var targets = rows.Select(r => r[4]).ToList();
        CollectionAssert.Contains(targets, "[INSTALLFOLDER]XISFShellExtensionHost.exe");
    }

    // ── Install Sequence ────────────────────────────────────────────────

    [TestMethod]
    public void InstallSequence_HasRegistrationActions()
    {
        var actions = GetInstallSequenceActions();
        CollectionAssert.Contains(actions, "CA_RegisterProperty");
        CollectionAssert.Contains(actions, "CA_RegisterPreview");
        CollectionAssert.Contains(actions, "CA_RegisterFilter");
    }

    [TestMethod]
    public void InstallSequence_HasUnregistrationActions()
    {
        var actions = GetInstallSequenceActions();
        CollectionAssert.Contains(actions, "CA_UnregisterProperty");
        CollectionAssert.Contains(actions, "CA_UnregisterPreview");
        CollectionAssert.Contains(actions, "CA_UnregisterFilter");
    }

    // ── Helpers ─────────────────────────────────────────────────────────

    private static List<string> GetFileIds()
        => QueryTable("File").Select(r => r[0]).ToList();

    private static List<string> GetCustomActionIds()
        => QueryTable("CustomAction").Select(r => r[0]).ToList();

    private static List<string> GetInstallSequenceActions()
        => QueryTable("InstallExecuteSequence").Select(r => r[0]).ToList();

    private static string? GetMsiProperty(string property)
    {
        var installer = CreateInstaller();
        try
        {
            var db = installer.OpenDatabase(s_msiPath, 0);
            var view = db.OpenView($"SELECT `Value` FROM `Property` WHERE `Property` = '{property}'");
            view.Execute();
            var record = view.Fetch();
            return record?.StringData(1);
        }
        finally
        {
            Marshal.ReleaseComObject(installer);
        }
    }

    private static List<List<string>> QueryTable(string table)
    {
        var installer = CreateInstaller();
        try
        {
            var db = installer.OpenDatabase(s_msiPath, 0);
            var view = db.OpenView($"SELECT * FROM `{table}`");
            view.Execute();

            // Get column count from column info
            var colInfo = view.ColumnInfo(0); // msiColumnInfoNames
            int colCount = 0;
            for (int i = 1; i <= 30; i++)
            {
                try
                {
                    var name = colInfo.StringData(i);
                    if (string.IsNullOrEmpty(name)) break;
                    colCount = i;
                }
                catch { break; }
            }
            Marshal.ReleaseComObject(colInfo);

            var rows = new List<List<string>>();
            while (true)
            {
                var record = view.Fetch();
                if (record == null) break;
                var cols = new List<string>();
                for (int i = 1; i <= colCount; i++)
                    cols.Add(record.StringData(i));
                rows.Add(cols);
                Marshal.ReleaseComObject(record);
            }

            Marshal.ReleaseComObject(view);
            Marshal.ReleaseComObject(db);
            return rows;
        }
        finally
        {
            Marshal.ReleaseComObject(installer);
        }
    }

    private static dynamic CreateInstaller()
    {
        var type = Type.GetTypeFromProgID("WindowsInstaller.Installer")
            ?? throw new InvalidOperationException("WindowsInstaller.Installer COM class not found");
        return Activator.CreateInstance(type)
            ?? throw new InvalidOperationException("Failed to create WindowsInstaller.Installer instance");
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
