using System.Runtime.InteropServices;
using Microsoft.Win32;
using System.Diagnostics;
using System.Security.Principal;

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
    private const string PropertyHandlerClsid = "{7C54FA8B-9D63-4C10-8FBE-1A5A0F9A3B2E}";
    private const string PreviewThumbnailClsid = "{9C76E8AD-4E85-5F30-B00D-3C7D1AB5F6E0}";
    private const string FilterClsid = "{B4E7F2A1-3D8C-4F5E-9A1B-6C2D8E4F7A3B}";
    private const string PersistentHandlerClsid = "{C5F8A3B2-4E9D-5067-AB2C-7D3E9F508B4C}";

    [ClassInitialize]
    public static void FindMsi(TestContext context)
    {
        // MsiDir can be overridden via runsettings
        var msiDir = context.Properties.ContainsKey("MsiDir")
            ? context.Properties["MsiDir"]!.ToString()!
            : Path.Combine(FindRepoRoot(), "Installer", "XISFInstaller", "bin");

        var msiFiles = Directory.GetFiles(msiDir, "*.msi", SearchOption.AllDirectories)
            .Select(path => new FileInfo(path))
            .OrderByDescending(file => file.LastWriteTimeUtc)
            .ToList();

        Assert.IsTrue(msiFiles.Count > 0,
            $"No MSI found under {msiDir}. Build the installer first: msbuild Win11-XISF-Shell-Extensions.sln /t:Rebuild /p:Configuration=Release /p:Platform=x64");

        s_msiPath = msiFiles[0].FullName;
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

    // ── EULA / License Agreement ────────────────────────────────────────

    [TestMethod]
    public void LicenseAgreementDlg_EmbedsRepoLicense()
    {
        var rtf = GetControlText("LicenseAgreementDlg", "LicenseText");
        Assert.IsNotNull(rtf, "LicenseAgreementDlg.LicenseText control is missing.");
        Assert.IsTrue(rtf.StartsWith(@"{\rtf"),
            $"LicenseText is not RTF (got: {rtf.Substring(0, Math.Min(40, rtf.Length))}…).");

        // Sanity-check actual MIT License content, not the WiX placeholder.
        StringAssert.Contains(rtf, "MIT License",
            "EULA does not embed the repo MIT License header.");
        StringAssert.Contains(rtf, "Dennis Payne",
            "EULA does not include the copyright holder.");
        StringAssert.Contains(rtf, "WITHOUT WARRANTY",
            "EULA is missing the MIT warranty disclaimer.");
        Assert.IsFalse(System.Text.RegularExpressions.Regex.IsMatch(rtf, @"(?i)\bplaceholder\b|<<\s*place\b"),
            "EULA still contains WiX placeholder text — wire up WixUILicenseRtf.");
    }

    [TestMethod]
    public void LicenseRtf_StaysInSyncWithRepoLicense()
    {
        var repoRoot = FindRepoRoot();
        var licensePath = Path.Combine(repoRoot, "LICENSE");
        var rtfPath = Path.Combine(repoRoot, "Installer", "XISFInstaller", "License.rtf");

        Assert.IsTrue(File.Exists(licensePath), $"LICENSE not found at {licensePath}.");
        Assert.IsTrue(File.Exists(rtfPath), $"License.rtf not found at {rtfPath}. Run scripts\\Convert-LicenseToRtf.ps1.");

        var license = File.ReadAllText(licensePath);
        var rtf = File.ReadAllText(rtfPath);

        // Every non-empty line in LICENSE must appear verbatim in the RTF
        // (RTF body uses literal text between control words, so substring
        // search is sufficient for plain ASCII MIT text).
        foreach (var line in license.Split('\n'))
        {
            var trimmed = line.TrimEnd('\r').Trim();
            if (string.IsNullOrEmpty(trimmed)) continue;
            StringAssert.Contains(rtf, trimmed,
                $"License.rtf is missing LICENSE line: \"{trimmed}\". Regenerate via scripts\\Convert-LicenseToRtf.ps1.");
        }
    }

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

    [TestMethod]
    public void FileTable_DoesNotContainSharplessSeedCatalog()
        => CollectionAssert.DoesNotContain(GetFileIds(), "SeedSharplessCsv");

    [TestMethod]
    public void FileTable_DoesNotContainConstellationsSeedCatalog()
        => CollectionAssert.DoesNotContain(GetFileIds(), "SeedConstellationsCsv");

    [TestMethod]
    public void CatalogDataDirectoryTree_IsUnderCommonAppData()
    {
        var rows = QueryTable("Directory");
        var byId = rows.ToDictionary(r => r[0], StringComparer.OrdinalIgnoreCase);

        Assert.IsTrue(byId.ContainsKey("DennisPayneDataDir"), "DennisPayneDataDir is missing.");
        Assert.IsTrue(byId.ContainsKey("XisfDataDir"), "XisfDataDir is missing.");
        Assert.IsTrue(byId.ContainsKey("XisfCatalogDir"), "XisfCatalogDir is missing.");

        Assert.AreEqual("CommonAppDataFolder", byId["DennisPayneDataDir"][1]);
        StringAssert.EndsWith(byId["DennisPayneDataDir"][2], "DennisPayne");
        Assert.AreEqual("DennisPayneDataDir", byId["XisfDataDir"][1]);
        StringAssert.EndsWith(byId["XisfDataDir"][2], "XISFShellExtension");
        Assert.AreEqual("XisfDataDir", byId["XisfCatalogDir"][1]);
        StringAssert.EndsWith(byId["XisfCatalogDir"][2], "catalogs");
    }

    [TestMethod]
    public void CatalogDataComponent_TargetsCatalogDirectoryAndPublishesPathRegistry()
    {
        var componentRow = QueryTable("Component")
            .FirstOrDefault(r => string.Equals(r[0], "CatalogDataFolder", StringComparison.Ordinal));
        Assert.IsNotNull(componentRow, "CatalogDataFolder component is missing.");
        Assert.AreEqual("XisfCatalogDir", componentRow![2], "CatalogDataFolder must target XisfCatalogDir.");

        var registryRow = QueryTable("Registry")
            .FirstOrDefault(r =>
                string.Equals(r[5], "CatalogDataFolder", StringComparison.Ordinal) &&
                string.Equals(r[2], @"SOFTWARE\DennisPayne\XISF Shell Extension\Catalogs", StringComparison.Ordinal) &&
                string.Equals(r[3], "Path", StringComparison.Ordinal));

        Assert.IsNotNull(registryRow, "Catalog path registry row for CatalogDataFolder is missing.");
        Assert.AreEqual("[XisfCatalogDir]", registryRow![4]);
    }

    [TestMethod]
    public void CatalogDownload_CustomActionInvokesSettingsHostSilentInstall()
    {
        var customActions = GetCustomActionIds();
        CollectionAssert.Contains(customActions, "CA_SilentCatalogInstall");

        var setterTarget = GetCustomActionTarget("SetCA_SilentCatalogInstall") ?? string.Empty;
        StringAssert.Contains(setterTarget, "XISFShellExtensionHost.exe");
        StringAssert.Contains(setterTarget, "--silent-install");
    }

    [TestMethod]
    public void CatalogDownload_CustomActionRunsAfterSearchPathSetup()
    {
        var rows = QueryTable("InstallExecuteSequence")
            .Where(r =>
                string.Equals(r[0], "CA_AddSearchPath", StringComparison.Ordinal) ||
                string.Equals(r[0], "CA_SilentCatalogInstall", StringComparison.Ordinal))
            .ToDictionary(r => r[0], StringComparer.Ordinal);

        Assert.IsTrue(rows.ContainsKey("CA_SilentCatalogInstall"),
            "CA_SilentCatalogInstall must be scheduled in InstallExecuteSequence.");

        var condition = rows["CA_SilentCatalogInstall"][1];
        StringAssert.Contains(condition, "$SettingsApp=3");
        StringAssert.Contains(condition, "NOT REMOVE=\"ALL\"");

        if (rows.TryGetValue("CA_AddSearchPath", out var addSearchPath))
        {
            Assert.IsTrue(
                int.TryParse(rows["CA_SilentCatalogInstall"][2], out var silentSequence) &&
                int.TryParse(addSearchPath[2], out var addSearchSequence) &&
                silentSequence > addSearchSequence,
                "CA_SilentCatalogInstall must run after CA_AddSearchPath when both are scheduled.");
        }
    }

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

    [TestMethod]
    public void RegistrationSetProperties_DoNotUseShellRedirection()
    {
        var targets = new[]
        {
            GetCustomActionTarget("SetCA_RegisterProperty"),
            GetCustomActionTarget("SetCA_RegisterPreview"),
            GetCustomActionTarget("SetCA_RegisterFilter")
        };

        foreach (var target in targets)
        {
            Assert.IsFalse(string.IsNullOrWhiteSpace(target), "Expected SetCA_Register* target to be present.");
            Assert.IsFalse(target!.Contains(">>", StringComparison.Ordinal),
                $"Custom action target must not use shell redirection with WixQuietExec64: {target}");
            Assert.IsFalse(target.Contains(">", StringComparison.Ordinal),
                $"Custom action target must not use shell redirection with WixQuietExec64: {target}");
        }
    }

    [TestMethod]
    public void RegistrationSetProperties_UseSilentRegsvr32()
    {
        var targets = new[]
        {
            GetCustomActionTarget("SetCA_RegisterProperty"),
            GetCustomActionTarget("SetCA_RegisterPreview"),
            GetCustomActionTarget("SetCA_RegisterFilter")
        };

        foreach (var target in targets)
        {
            Assert.IsFalse(string.IsNullOrWhiteSpace(target), "Expected SetCA_Register* target to be present.");
            StringAssert.Contains(target!, "regsvr32.exe");
            StringAssert.Contains(target, " /s ");
        }
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

    [TestMethod]
    public void SettingsShortcut_HasLaunchSafeConfiguration()
    {
        var row = QueryTable("Shortcut")
            .FirstOrDefault(r => string.Equals(r[0], "SettingsAppShortcut", StringComparison.Ordinal));

        Assert.IsNotNull(row, "SettingsAppShortcut was not found in the MSI Shortcut table.");
        Assert.AreEqual("[INSTALLFOLDER]XISFShellExtensionHost.exe", row![4], "Shortcut target must point to the installed host executable.");
        Assert.IsTrue(string.IsNullOrEmpty(row[5]), "Shortcut arguments should be empty for normal config app launch.");
        Assert.AreEqual("INSTALLFOLDER", row[11], "Shortcut working directory should be INSTALLFOLDER.");
    }

    [TestMethod]
    public void CompleteFeature_InstallsSettingsAppAndShortcut()
    {
        var featureComponents = QueryTable("FeatureComponents");
        var pairs = featureComponents
            .Select(r => (Feature: r[0], Component: r[1]))
            .ToList();

        Assert.IsTrue(pairs.Contains(("Complete", "SettingsApp")),
            "Complete feature must include the settings app component.");
        Assert.IsTrue(pairs.Contains(("Complete", "SettingsShortcut")),
            "Complete feature must include the settings shortcut component.");
    }

    [TestMethod]
    public void HandlerFeatures_AreChildFeaturesEnabledByDefault()
    {
        AssertFeatureParentAndLevel("PropertyHandlerFeature");
        AssertFeatureParentAndLevel("PreviewHandlerFeature");
        AssertFeatureParentAndLevel("FilterHandlerFeature");
    }

    [TestMethod]
    public void HandlerComponents_AreMappedToTheirSelectableFeatures()
    {
        AssertComponentMappedToSingleFeature("PropertyHandlerDll", "PropertyHandlerFeature");
        AssertComponentMappedToSingleFeature("PreviewHandlerDll", "PreviewHandlerFeature");
        AssertComponentMappedToSingleFeature("FilterDll", "FilterHandlerFeature");
    }

    [TestMethod]
    public void SilentDefaultInstall_SelectsAllHandlerFeatures()
    {
        var installLevel = int.TryParse(GetMsiProperty("INSTALLLEVEL"), out var parsedInstallLevel) ? parsedInstallLevel : 1;

        AssertFeatureSelectedAtInstallLevel("PropertyHandlerFeature", installLevel);
        AssertFeatureSelectedAtInstallLevel("PreviewHandlerFeature", installLevel);
        AssertFeatureSelectedAtInstallLevel("FilterHandlerFeature", installLevel);

        var featureConditions = QueryTableIfExists("Condition")
            .Where(r =>
                string.Equals(r[0], "PropertyHandlerFeature", StringComparison.Ordinal) ||
                string.Equals(r[0], "PreviewHandlerFeature", StringComparison.Ordinal) ||
                string.Equals(r[0], "FilterHandlerFeature", StringComparison.Ordinal))
            .ToList();

        Assert.AreEqual(0, featureConditions.Count,
            "Handler features should not have Condition-table overrides that disable default selection.");
    }

    [TestMethod]
    public void InstallSequence_ConditionsGateHandlerActionsByComponentState()
    {
        StringAssert.Contains(GetInstallSequenceCondition("CA_RegisterProperty"), "$PropertyHandlerDll=3");
        StringAssert.Contains(GetInstallSequenceCondition("CA_UnregisterProperty"), "$PropertyHandlerDll=2");
        StringAssert.Contains(GetInstallSequenceCondition("CA_RegisterPreview"), "$PreviewHandlerDll=3");
        StringAssert.Contains(GetInstallSequenceCondition("CA_UnregisterPreview"), "$PreviewHandlerDll=2");
        StringAssert.Contains(GetInstallSequenceCondition("CA_RegisterFilter"), "$FilterDll=3");
        StringAssert.Contains(GetInstallSequenceCondition("CA_UnregisterFilter"), "$FilterDll=2");
    }

    [TestMethod]
    public void InstallSequence_SetPropertyActionsUseComponentStateConditions()
    {
        StringAssert.Contains(GetInstallSequenceCondition("SetCA_RegisterProperty"), "$PropertyHandlerDll=3");
        StringAssert.Contains(GetInstallSequenceCondition("SetCA_UnregisterProperty"), "$PropertyHandlerDll=2");
        StringAssert.Contains(GetInstallSequenceCondition("SetCA_RegisterPreview"), "$PreviewHandlerDll=3");
        StringAssert.Contains(GetInstallSequenceCondition("SetCA_UnregisterPreview"), "$PreviewHandlerDll=2");
        StringAssert.Contains(GetInstallSequenceCondition("SetCA_RegisterFilter"), "$FilterDll=3");
        StringAssert.Contains(GetInstallSequenceCondition("SetCA_UnregisterFilter"), "$FilterDll=2");
    }

    [TestMethod]
    public void RegistrationActions_AreRepairAwareAndNotGloballyConditioned()
    {
        AssertRegistrationConditionShape(
            GetInstallSequenceCondition("CA_RegisterProperty"),
            "$PropertyHandlerDll=3",
            "?PropertyHandlerDll<>3");
        AssertRegistrationConditionShape(
            GetInstallSequenceCondition("CA_RegisterPreview"),
            "$PreviewHandlerDll=3",
            "?PreviewHandlerDll<>3");
        AssertRegistrationConditionShape(
            GetInstallSequenceCondition("CA_RegisterFilter"),
            "$FilterDll=3",
            "?FilterDll<>3");

        AssertRegistrationConditionShape(
            GetInstallSequenceCondition("SetCA_RegisterProperty"),
            "$PropertyHandlerDll=3",
            "?PropertyHandlerDll<>3");
        AssertRegistrationConditionShape(
            GetInstallSequenceCondition("SetCA_RegisterPreview"),
            "$PreviewHandlerDll=3",
            "?PreviewHandlerDll<>3");
        AssertRegistrationConditionShape(
            GetInstallSequenceCondition("SetCA_RegisterFilter"),
            "$FilterDll=3",
            "?FilterDll<>3");
    }

    [TestMethod]
    public void UnregistrationActions_AreFeatureTransitionBased()
    {
        AssertUnregistrationConditionShape(GetInstallSequenceCondition("CA_UnregisterProperty"), "$PropertyHandlerDll=2", "?PropertyHandlerDll=3");
        AssertUnregistrationConditionShape(GetInstallSequenceCondition("CA_UnregisterPreview"), "$PreviewHandlerDll=2", "?PreviewHandlerDll=3");
        AssertUnregistrationConditionShape(GetInstallSequenceCondition("CA_UnregisterFilter"), "$FilterDll=2", "?FilterDll=3");

        AssertUnregistrationConditionShape(GetInstallSequenceCondition("SetCA_UnregisterProperty"), "$PropertyHandlerDll=2", "?PropertyHandlerDll=3");
        AssertUnregistrationConditionShape(GetInstallSequenceCondition("SetCA_UnregisterPreview"), "$PreviewHandlerDll=2", "?PreviewHandlerDll=3");
        AssertUnregistrationConditionShape(GetInstallSequenceCondition("SetCA_UnregisterFilter"), "$FilterDll=2", "?FilterDll=3");
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

    // ── Optional XISF data path / Windows Search integration ────────────

    [TestMethod]
    public void OptionalDataPath_PropertyIsBoundToRegistrySearch()
    {
        var appSearch = QueryTable("AppSearch")
            .FirstOrDefault(r => string.Equals(r[0], "XISF_DATA_PATH", StringComparison.Ordinal));
        Assert.IsNotNull(appSearch, "AppSearch must populate XISF_DATA_PATH from the persisted HKLM value.");

        var signature = appSearch![1];
        var regLocator = QueryTable("RegLocator")
            .FirstOrDefault(r => string.Equals(r[0], signature, StringComparison.Ordinal));
        Assert.IsNotNull(regLocator, $"RegLocator entry '{signature}' is missing.");
        Assert.AreEqual("2", regLocator![1], "Registry search must target HKEY_LOCAL_MACHINE.");
        Assert.AreEqual(@"SOFTWARE\DennisPayne\XISF Shell Extension\MSI", regLocator[2]);
        Assert.AreEqual("DataPath", regLocator[3]);
    }

    [TestMethod]
    public void OptionalDataPath_DialogHasEditControlBoundToProperty()
    {
        var dialog = QueryTable("Dialog")
            .FirstOrDefault(r => string.Equals(r[0], "XISFDataPathDlg", StringComparison.Ordinal));
        Assert.IsNotNull(dialog, "XISFDataPathDlg must be present in the MSI UI.");

        var pathEdit = QueryTable("Control")
            .FirstOrDefault(r =>
                string.Equals(r[0], "XISFDataPathDlg", StringComparison.Ordinal) &&
                string.Equals(r[1], "PathEdit", StringComparison.Ordinal));
        Assert.IsNotNull(pathEdit, "PathEdit control is missing from XISFDataPathDlg.");
        Assert.AreEqual("Edit", pathEdit![2]);
        Assert.AreEqual("XISF_DATA_PATH", pathEdit[8],
            "PathEdit must be bound to the XISF_DATA_PATH property.");
    }

    [TestMethod]
    public void OptionalDataPath_DialogIsInjectedBetweenCustomizeAndVerifyReady()
    {
        var events = QueryTable("ControlEvent");

        bool ForwardFromCustomize(List<string> r) =>
            string.Equals(r[0], "CustomizeDlg", StringComparison.Ordinal) &&
            string.Equals(r[1], "Next", StringComparison.Ordinal) &&
            string.Equals(r[2], "NewDialog", StringComparison.Ordinal) &&
            string.Equals(r[3], "XISFDataPathDlg", StringComparison.Ordinal);

        bool BackFromVerifyReady(List<string> r) =>
            string.Equals(r[0], "VerifyReadyDlg", StringComparison.Ordinal) &&
            string.Equals(r[1], "Back", StringComparison.Ordinal) &&
            string.Equals(r[2], "NewDialog", StringComparison.Ordinal) &&
            string.Equals(r[3], "XISFDataPathDlg", StringComparison.Ordinal);

        Assert.IsTrue(events.Any(ForwardFromCustomize),
            "CustomizeDlg Next must route to XISFDataPathDlg.");
        Assert.IsTrue(events.Any(BackFromVerifyReady),
            "VerifyReadyDlg Back must route to XISFDataPathDlg.");

        // Our override must beat the WixUI_FeatureTree default publishes.
        var forward = events.First(ForwardFromCustomize);
        Assert.IsTrue(int.TryParse(forward[5], out var forwardOrder) && forwardOrder >= 2,
            "Override publish from CustomizeDlg must use Order >= 2 to beat WixUI defaults.");
    }

    [TestMethod]
    public void OptionalDataPath_PersistsValueToHKLMViaComponent()
    {
        var component = QueryTable("Component")
            .FirstOrDefault(r => string.Equals(r[0], "DataPathRegistry", StringComparison.Ordinal));
        Assert.IsNotNull(component, "DataPathRegistry component is missing.");

        var registryRow = QueryTable("Registry")
            .FirstOrDefault(r =>
                string.Equals(r[5], "DataPathRegistry", StringComparison.Ordinal) &&
                string.Equals(r[2], @"SOFTWARE\DennisPayne\XISF Shell Extension\MSI", StringComparison.Ordinal) &&
                string.Equals(r[3], "DataPath", StringComparison.Ordinal));
        Assert.IsNotNull(registryRow, "DataPathRegistry must write XISF_DATA_PATH under the MSI registry key.");
        Assert.AreEqual("[XISF_DATA_PATH]", registryRow![4],
            "DataPathRegistry value must be the formatted XISF_DATA_PATH property.");
    }

    [TestMethod]
    public void OptionalDataPath_HasAddAndRemoveSearchPathCustomActions()
    {
        var actions = GetCustomActionIds();
        CollectionAssert.Contains(actions, "CA_AddSearchPath");
        CollectionAssert.Contains(actions, "CA_RemoveSearchPath");
    }

    [TestMethod]
    public void OptionalDataPath_SearchPathActionsAreDeferredAndIgnoreFailure()
    {
        // msidbCustomActionTypeDll(1) | msidbCustomActionTypeContinue(0x40)
        // | msidbCustomActionTypeInScript(0x400) | msidbCustomActionTypeNoImpersonate(0x800)
        const int expectedType = 0x800 | 0x400 | 0x40 | 0x1; // 3137

        foreach (var actionId in new[] { "CA_AddSearchPath", "CA_RemoveSearchPath" })
        {
            var row = QueryTable("CustomAction")
                .FirstOrDefault(r => string.Equals(r[0], actionId, StringComparison.Ordinal));
            Assert.IsNotNull(row, $"CustomAction '{actionId}' is missing.");
            Assert.AreEqual(expectedType.ToString(), row![1],
                $"{actionId} must be deferred, NoImpersonate, and ignore failures (best-effort indexing).");
        }
    }

    [TestMethod]
    public void OptionalDataPath_SearchPathActionsTargetHostExeCli()
    {
        var addTarget = GetCustomActionTarget("CA_AddSearchPath") ?? string.Empty;
        var removeTarget = GetCustomActionTarget("CA_RemoveSearchPath") ?? string.Empty;

        // SetProperty rewrites the CA's Target at runtime via the Set<ActionId>
        // companion CA — verify the formatted command line on that.
        var addSetter = GetCustomActionTarget("SetCA_AddSearchPath") ?? string.Empty;
        var removeSetter = GetCustomActionTarget("SetCA_RemoveSearchPath") ?? string.Empty;

        StringAssert.Contains(addSetter, "XISFShellExtensionHost.exe");
        StringAssert.Contains(addSetter, "--add-search-path");
        StringAssert.Contains(addSetter, "[XISF_DATA_PATH]");

        StringAssert.Contains(removeSetter, "XISFShellExtensionHost.exe");
        StringAssert.Contains(removeSetter, "--remove-search-path");
        StringAssert.Contains(removeSetter, "[XISF_DATA_PATH]");
    }

    [TestMethod]
    public void OptionalDataPath_SearchPathActionsAreScheduledAroundFilterLifecycle()
    {
        var seq = QueryTable("InstallExecuteSequence");

        int Sequence(string action)
        {
            var row = seq.FirstOrDefault(r => string.Equals(r[0], action, StringComparison.Ordinal));
            Assert.IsNotNull(row, $"InstallExecuteSequence action '{action}' is missing.");
            Assert.IsTrue(int.TryParse(row![2], out var s),
                $"Could not parse sequence number for '{action}'.");
            return s;
        }

        Assert.IsTrue(Sequence("CA_AddSearchPath") > Sequence("CA_StartWSearch"),
            "CA_AddSearchPath must run after Windows Search is back up.");
        Assert.IsTrue(Sequence("CA_RemoveSearchPath") < Sequence("CA_UnregisterFilter"),
            "CA_RemoveSearchPath must run before the filter is unregistered (host EXE still on disk).");

        // Both gated on XISF_DATA_PATH being non-empty so a blank field is a no-op.
        StringAssert.Contains(GetInstallSequenceCondition("CA_AddSearchPath"), "XISF_DATA_PATH");
        StringAssert.Contains(GetInstallSequenceCondition("CA_RemoveSearchPath"), "XISF_DATA_PATH");
    }

    [TestMethod]
    [TestCategory("InstallerIntegration")]
    [DoNotParallelize]
    [Timeout(10 * 60 * 1000)]
    public void Msi_Install_CompletesAndRegistersCoreComClasses()
    {
        if (!OperatingSystem.IsWindows())
            Assert.Inconclusive("Installer integration test only runs on Windows.");

        if (!IsProcessElevated())
            Assert.Inconclusive("Installer integration test requires an elevated process.");

        if (LooksLikeExistingMachineInstall())
            Assert.Inconclusive("Existing machine-level install detected; skipping destructive installer integration test.");

        var installRoot = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),
            $"XISF Shell Extensions Regression {Guid.NewGuid():N}");

        var logDir = Path.GetDirectoryName(s_msiPath)!;
        var installLog = Path.Combine(logDir, $"msi-regression-install-{Guid.NewGuid():N}.log");
        var uninstallLog = Path.Combine(logDir, $"msi-regression-uninstall-{Guid.NewGuid():N}.log");

        var installSucceeded = false;
        string? uninstallFailure = null;

        try
        {
            var installArgs = $"/i \"{s_msiPath}\" /qn /norestart /l*v \"{installLog}\" INSTALLFOLDER=\"{installRoot}\"";
            var installExitCode = RunProcess("msiexec.exe", installArgs, TimeSpan.FromMinutes(5));

            Assert.AreEqual(0, installExitCode,
                $"MSI install failed with exit code {installExitCode}.{Environment.NewLine}Log tail:{Environment.NewLine}{ReadLogTail(installLog)}");

            installSucceeded = true;

            AssertComServerPointsToPath(PropertyHandlerClsid, Path.Combine(installRoot, "XISFPropertyHandler.dll"));
            AssertComServerPointsToPath(PreviewThumbnailClsid, Path.Combine(installRoot, "XISFPreviewHandler.dll"));
            AssertComServerPointsToPath(FilterClsid, Path.Combine(installRoot, "XISFFilter.dll"));
            AssertRegistryDefaultValueEquals(
                @"SOFTWARE\Classes\.xisf\PersistentHandler",
                PersistentHandlerClsid,
                ".xisf PersistentHandler is missing after MSI install.");
        }
        finally
        {
            var uninstallArgs = $"/x \"{s_msiPath}\" /qn /norestart /l*v \"{uninstallLog}\"";
            var uninstallExitCode = RunProcess("msiexec.exe", uninstallArgs, TimeSpan.FromMinutes(5));

            if (installSucceeded && uninstallExitCode != 0)
            {
                uninstallFailure = $"MSI uninstall failed with exit code {uninstallExitCode}.{Environment.NewLine}Log tail:{Environment.NewLine}{ReadLogTail(uninstallLog)}";
            }

            TryDeleteDirectory(installRoot);
        }

        if (!string.IsNullOrWhiteSpace(uninstallFailure))
            Assert.Fail(uninstallFailure);
    }

    // ── Helpers ─────────────────────────────────────────────────────────

    private static List<string> GetFileIds()
        => QueryTable("File").Select(r => r[0]).ToList();

    private static List<string> GetCustomActionIds()
        => QueryTable("CustomAction").Select(r => r[0]).ToList();

    private static List<string> GetInstallSequenceActions()
        => QueryTable("InstallExecuteSequence").Select(r => r[0]).ToList();

    private static string GetInstallSequenceCondition(string actionId)
    {
        var row = QueryTable("InstallExecuteSequence")
            .FirstOrDefault(r => string.Equals(r[0], actionId, StringComparison.Ordinal));
        Assert.IsNotNull(row, $"InstallExecuteSequence action '{actionId}' was not found.");
        return row![1] ?? string.Empty;
    }

    private static string? GetCustomActionTarget(string actionId)
    {
        var installer = CreateInstaller();
        try
        {
            var db = installer.OpenDatabase(s_msiPath, 0);
            var escapedActionId = actionId.Replace("'", "''");
            var view = db.OpenView($"SELECT `Target` FROM `CustomAction` WHERE `Action` = '{escapedActionId}'");
            view.Execute();
            var record = view.Fetch();
            return record?.StringData(1);
        }
        finally
        {
            Marshal.ReleaseComObject(installer);
        }
    }

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

    private static string? GetControlText(string dialog, string control)
    {
        var installer = CreateInstaller();
        try
        {
            var db = installer.OpenDatabase(s_msiPath, 0);
            var view = db.OpenView(
                $"SELECT `Text` FROM `Control` WHERE `Dialog_` = '{dialog}' AND `Control` = '{control}'");
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

    private static List<List<string>> QueryTableIfExists(string table)
    {
        try
        {
            return QueryTable(table);
        }
        catch (COMException)
        {
            return [];
        }
    }

    private static dynamic CreateInstaller()
    {
        var type = Type.GetTypeFromProgID("WindowsInstaller.Installer")
            ?? throw new InvalidOperationException("WindowsInstaller.Installer COM class not found");
        return Activator.CreateInstance(type)
            ?? throw new InvalidOperationException("Failed to create WindowsInstaller.Installer instance");
    }

    private static bool IsProcessElevated()
    {
        using var identity = WindowsIdentity.GetCurrent();
        var principal = new WindowsPrincipal(identity);
        return principal.IsInRole(WindowsBuiltInRole.Administrator);
    }

    private static bool LooksLikeExistingMachineInstall()
    {
        if (ReadRegistryDefaultValue($@"SOFTWARE\Classes\CLSID\{PropertyHandlerClsid}\InProcServer32") is string existing &&
            !string.IsNullOrWhiteSpace(existing))
        {
            return true;
        }

        using var key = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView.Registry64)
            .OpenSubKey(@"SOFTWARE\DennisPayne\XISF Shell Extension\MSI");
        return key?.GetValue("RestartManagerInstalled") != null;
    }

    private static void AssertComServerPointsToPath(string clsid, string expectedPath)
    {
        var actual = ReadRegistryDefaultValue($@"SOFTWARE\Classes\CLSID\{clsid}\InProcServer32");
        Assert.IsFalse(string.IsNullOrWhiteSpace(actual), $"Missing COM registration for {clsid}.");

        var normalizedExpected = NormalizePath(expectedPath);
        var normalizedActual = NormalizePath(actual!);
        Assert.AreEqual(normalizedExpected, normalizedActual,
            true,
            $"COM server path mismatch for {clsid}. Expected '{normalizedExpected}', got '{normalizedActual}'.");
    }

    private static void AssertRegistryDefaultValueEquals(string subKey, string expectedValue, string messageIfMissing)
    {
        var actual = ReadRegistryDefaultValue(subKey);
        Assert.IsFalse(string.IsNullOrWhiteSpace(actual), messageIfMissing);
        Assert.AreEqual(expectedValue, actual, true, $"Registry value mismatch for HKLM\\{subKey}.");
    }

    private static string? ReadRegistryDefaultValue(string subKey)
    {
        using var key = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView.Registry64)
            .OpenSubKey(subKey);
        return key?.GetValue(null) as string;
    }

    private static string NormalizePath(string path)
    {
        var trimmed = path.Trim().Trim('"');
        return Path.GetFullPath(Environment.ExpandEnvironmentVariables(trimmed));
    }

    private static int RunProcess(string fileName, string arguments, TimeSpan timeout)
    {
        using var process = new Process
        {
            StartInfo = new ProcessStartInfo
            {
                FileName = fileName,
                Arguments = arguments,
                UseShellExecute = false,
                CreateNoWindow = true
            }
        };

        process.Start();
        if (!process.WaitForExit((int)timeout.TotalMilliseconds))
        {
            try { process.Kill(entireProcessTree: true); } catch { }
            Assert.Fail($"{fileName} timed out after {timeout.TotalMinutes} minutes. Arguments: {arguments}");
        }

        return process.ExitCode;
    }

    private static string ReadLogTail(string logPath, int maxLines = 80)
    {
        if (!File.Exists(logPath))
            return $"Log file not found: {logPath}";

        var lines = File.ReadAllLines(logPath);
        if (lines.Length <= maxLines)
            return string.Join(Environment.NewLine, lines);

        return string.Join(Environment.NewLine, lines.Skip(lines.Length - maxLines));
    }

    private static void TryDeleteDirectory(string path)
    {
        try
        {
            if (Directory.Exists(path))
                Directory.Delete(path, recursive: true);
        }
        catch
        {
            // Best-effort cleanup; uninstall is authoritative.
        }
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

    private static void AssertFeatureParentAndLevel(string featureId)
    {
        var row = QueryTable("Feature")
            .FirstOrDefault(r => string.Equals(r[0], featureId, StringComparison.Ordinal));

        Assert.IsNotNull(row, $"Feature '{featureId}' was not found in MSI Feature table.");
        Assert.AreEqual("Complete", row![1], $"Feature '{featureId}' must be parented under Complete.");
        Assert.AreEqual("1", row[5], $"Feature '{featureId}' should be enabled by default at Level 1.");
    }

    private static void AssertComponentMappedToSingleFeature(string componentId, string expectedFeatureId)
    {
        var featureIds = QueryTable("FeatureComponents")
            .Where(r => string.Equals(r[1], componentId, StringComparison.Ordinal))
            .Select(r => r[0])
            .Distinct(StringComparer.Ordinal)
            .ToList();

        Assert.AreEqual(1, featureIds.Count, $"Component '{componentId}' should map to exactly one feature.");
        Assert.AreEqual(expectedFeatureId, featureIds[0], $"Component '{componentId}' should map to feature '{expectedFeatureId}'.");
    }

    private static void AssertFeatureSelectedAtInstallLevel(string featureId, int installLevel)
    {
        var row = QueryTable("Feature")
            .FirstOrDefault(r => string.Equals(r[0], featureId, StringComparison.Ordinal));

        Assert.IsNotNull(row, $"Feature '{featureId}' was not found in MSI Feature table.");
        var featureLevel = int.Parse(row![5]);
        Assert.IsTrue(featureLevel <= installLevel,
            $"Feature '{featureId}' should be selected by default for silent install (feature level {featureLevel}, INSTALLLEVEL {installLevel}).");
    }

    private static void AssertRegistrationConditionShape(string condition, string desiredStateFragment, string priorStateFragment)
    {
        StringAssert.Contains(condition, desiredStateFragment);
        StringAssert.Contains(condition, priorStateFragment);
        StringAssert.Contains(condition, "REINSTALL");
        Assert.IsFalse(condition.Contains("NOT REMOVE", StringComparison.OrdinalIgnoreCase),
            $"Condition should be state-based, not globally gated by NOT REMOVE: {condition}");
    }

    private static void AssertUnregistrationConditionShape(string condition, string desiredStateFragment, string priorStateFragment)
    {
        StringAssert.Contains(condition, desiredStateFragment);
        StringAssert.Contains(condition, priorStateFragment);
        Assert.IsFalse(condition.Contains("REMOVE", StringComparison.OrdinalIgnoreCase),
            $"Condition should be feature-transition based, not global REMOVE: {condition}");
    }
}
