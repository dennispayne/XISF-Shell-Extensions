// ShellExtensionHostTests.cpp - Host EXE: integrity + settings + paths tests
// Uses Microsoft Native Unit Test Framework (CppUnitTestFramework)
//
// Three test perspectives:
//   1. Principal developer  - API contracts for Sha256, Paths, HostSettings
//   2. Security engineer    - pin format integrity, allow-list invariants,
//                             hash-mismatch refusal, constant-time compare
//   3. Windows sysadmin     - robustness on missing/garbage input, no crashes
//
#include "CppUnitTest.h"

#include <windows.h>
#include <shlwapi.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "Sha256.h"
#include "Paths.h"
#include "HostSettings.h"
#include "CatalogSpec.h"
#include "CatalogInstaller.h"
#include "HandlerDllPath.h"
#include "UpdaterSpec.h"
#include "UpdaterInternals.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace
{
    // Writes `bytes` to a freshly-created temp file, returns its path.
    std::wstring WriteTempFile(const std::vector<std::uint8_t>& bytes)
    {
        wchar_t tmpDir[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, tmpDir);
        wchar_t tmpFile[MAX_PATH] = {};
        GetTempFileNameW(tmpDir, L"xht", 0, tmpFile);

        HANDLE h = CreateFileW(tmpFile, GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        Assert::IsTrue(h != INVALID_HANDLE_VALUE, L"temp create");
        DWORD written = 0;
        if (!bytes.empty())
        {
            WriteFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
        }
        CloseHandle(h);
        return tmpFile;
    }

    // Case-insensitive compare of narrow strings, ASCII only.
    bool IEqualsAscii(std::wstring_view a, std::wstring_view b)
    {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
        {
            wchar_t ca = a[i], cb = b[i];
            if (ca >= L'A' && ca <= L'Z') ca = static_cast<wchar_t>(ca + 32);
            if (cb >= L'A' && cb <= L'Z') cb = static_cast<wchar_t>(cb + 32);
            if (ca != cb) return false;
        }
        return true;
    }
}

// ===========================================================================
// Sha256 - hashing + hex helpers
// ===========================================================================
namespace ShellExtensionHostTests_Sha256
{
    TEST_CLASS(Sha256Tests)
    {
    public:
        TEST_METHOD(HashEmptyInput_MatchesKnownDigest)
        {
            // Well-known SHA-256 of empty input.
            constexpr wchar_t kEmpty[] =
                L"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

            xisf::Sha256Hasher h;
            Assert::IsTrue(SUCCEEDED(h.Init()));
            std::array<std::uint8_t, 32> d{};
            Assert::IsTrue(SUCCEEDED(h.Finalize(d)));
            Assert::IsTrue(IEqualsAscii(xisf::ToHexLower(d), kEmpty));
        }

        TEST_METHOD(HashAbc_MatchesKnownDigest)
        {
            // SHA-256("abc")
            constexpr wchar_t kAbc[] =
                L"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";

            xisf::Sha256Hasher h;
            Assert::IsTrue(SUCCEEDED(h.Init()));
            Assert::IsTrue(SUCCEEDED(h.Update("abc", 3)));
            std::array<std::uint8_t, 32> d{};
            Assert::IsTrue(SUCCEEDED(h.Finalize(d)));
            Assert::IsTrue(IEqualsAscii(xisf::ToHexLower(d), kAbc));
        }

        TEST_METHOD(ChunkedUpdate_EqualsSingleUpdate)
        {
            const char* msg = "The quick brown fox jumps over the lazy dog";
            const size_t n  = std::strlen(msg);

            std::array<std::uint8_t, 32> oneShot{};
            {
                xisf::Sha256Hasher h;
                Assert::IsTrue(SUCCEEDED(h.Init()));
                Assert::IsTrue(SUCCEEDED(h.Update(msg, n)));
                Assert::IsTrue(SUCCEEDED(h.Finalize(oneShot)));
            }
            std::array<std::uint8_t, 32> chunked{};
            {
                xisf::Sha256Hasher h;
                Assert::IsTrue(SUCCEEDED(h.Init()));
                for (size_t i = 0; i < n; ++i)
                    Assert::IsTrue(SUCCEEDED(h.Update(msg + i, 1)));
                Assert::IsTrue(SUCCEEDED(h.Finalize(chunked)));
            }
            Assert::IsTrue(oneShot == chunked);
        }

        TEST_METHOD(ToHexLower_ProducesLowercase64Chars)
        {
            std::array<std::uint8_t, 32> d{};
            for (size_t i = 0; i < d.size(); ++i) d[i] = static_cast<std::uint8_t>(i);
            auto hex = xisf::ToHexLower(d);
            Assert::AreEqual<size_t>(64, hex.size());
            for (wchar_t c : hex)
            {
                bool isLowerHex = (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f');
                Assert::IsTrue(isLowerHex, L"non-lowercase-hex char emitted");
            }
        }

        TEST_METHOD(HexEquals_CaseInsensitive_MatchesOnlyFullString)
        {
            Assert::IsTrue(xisf::HexEquals(L"ABCDEF01", L"abcdef01"));
            Assert::IsTrue(xisf::HexEquals(L"", L""));
            Assert::IsFalse(xisf::HexEquals(L"abcd", L"abce"));
            Assert::IsFalse(xisf::HexEquals(L"abcd", L"abcde")); // length mismatch
        }

        TEST_METHOD(HashFile_MatchesStreamingHash_OfSameBytes)
        {
            std::vector<std::uint8_t> bytes(17 * 1024);
            for (size_t i = 0; i < bytes.size(); ++i)
                bytes[i] = static_cast<std::uint8_t>(i * 31u);

            auto path = WriteTempFile(bytes);

            std::array<std::uint8_t, 32> streamed{};
            {
                xisf::Sha256Hasher h;
                Assert::IsTrue(SUCCEEDED(h.Init()));
                Assert::IsTrue(SUCCEEDED(h.Update(bytes.data(), bytes.size())));
                Assert::IsTrue(SUCCEEDED(h.Finalize(streamed)));
            }

            std::array<std::uint8_t, 32> fromFile{};
            std::uint64_t size = 0;
            Assert::IsTrue(SUCCEEDED(xisf::HashFile(path.c_str(), fromFile, size)));
            Assert::AreEqual<std::uint64_t>(bytes.size(), size);
            Assert::IsTrue(streamed == fromFile);

            DeleteFileW(path.c_str());
        }

        TEST_METHOD(HashFile_MissingPath_ReturnsFailure)
        {
            std::array<std::uint8_t, 32> d{};
            std::uint64_t sz = 0;
            HRESULT hr = xisf::HashFile(L"C:\\this-path-does-not-exist-xisf-123.bin", d, sz);
            Assert::IsTrue(FAILED(hr));
        }

        TEST_METHOD(Init_CalledTwice_ReturnsUnexpected)
        {
            // Guard: Init() must reject re-initialization without intervening
            // Finalize, otherwise the previously-allocated CNG handles leak.
            xisf::Sha256Hasher h;
            Assert::IsTrue(SUCCEEDED(h.Init()));
            HRESULT hr = h.Init();
            Assert::AreEqual<HRESULT>(E_UNEXPECTED, hr,
                L"Second Init() before Finalize must return E_UNEXPECTED");
        }

        TEST_METHOD(Update_BeforeInit_ReturnsUnexpected)
        {
            // Calling Update before Init must not crash and must report misuse.
            xisf::Sha256Hasher h;
            const char* msg = "abc";
            HRESULT hr = h.Update(msg, 3);
            Assert::AreEqual<HRESULT>(E_UNEXPECTED, hr,
                L"Update() before Init() must return E_UNEXPECTED");
        }

        TEST_METHOD(Update_OversizeLength_ReturnsInvalidArg)
        {
            // BCryptHashData takes ULONG; the wrapper rejects len > INT32_MAX
            // before invoking the OS API. We never dereference the pointer
            // because the size check comes first, so nullptr is safe.
            xisf::Sha256Hasher h;
            Assert::IsTrue(SUCCEEDED(h.Init()));
            HRESULT hr = h.Update(nullptr, static_cast<std::size_t>(0x80000000ull));
            Assert::AreEqual<HRESULT>(E_INVALIDARG, hr,
                L"Update() with len > 0x7FFFFFFF must return E_INVALIDARG");
        }
    };
}

// ===========================================================================
// Paths - ProgramData catalog resolution + HKCU key constant
// ===========================================================================
namespace ShellExtensionHostTests_Paths
{
    TEST_CLASS(PathsTests)
    {
    public:
        TEST_METHOD(AppDataRoot_IsNonEmptyAndExists)
        {
            auto root = xisf::paths::AppDataRoot();
            Assert::IsFalse(root.empty());
            DWORD attrs = GetFileAttributesW(root.c_str());
            Assert::AreNotEqual(INVALID_FILE_ATTRIBUTES, attrs);
            Assert::IsTrue((attrs & FILE_ATTRIBUTE_DIRECTORY) != 0);
        }

        TEST_METHOD(CatalogDir_IsUnderAppDataRoot)
        {
            auto root = xisf::paths::AppDataRoot();
            auto dir  = xisf::paths::CatalogDir();
            Assert::IsFalse(dir.empty());
            Assert::IsTrue(dir.size() > root.size(), L"catalog dir must extend root");
            Assert::IsTrue(dir.compare(0, root.size(), root) == 0);
            Assert::IsTrue(root.find(L"ProgramData") != std::wstring::npos,
                L"catalog root should be machine-wide ProgramData");

            DWORD attrs = GetFileAttributesW(dir.c_str());
            Assert::AreNotEqual(INVALID_FILE_ATTRIBUTES, attrs);
            Assert::IsTrue((attrs & FILE_ATTRIBUTE_DIRECTORY) != 0);
        }

        TEST_METHOD(CatalogFile_AppendsFileName)
        {
            auto dir  = xisf::paths::CatalogDir();
            auto file = xisf::paths::CatalogFile(L"NGC.csv");
            Assert::IsTrue(file.size() > dir.size());
            Assert::IsTrue(file.compare(0, dir.size(), dir) == 0);
            Assert::IsTrue(file.find(L"NGC.csv") != std::wstring::npos);
        }

        TEST_METHOD(HkcuSettingsKey_MatchesDocumentedLocation)
        {
            Assert::AreEqual(
                std::wstring(L"Software\\DennisPayne\\XISF Shell Extension"),
                std::wstring(xisf::paths::kHkcuSettingsKey));
        }
    };
}

// ===========================================================================
// HostSettings - HKCU round-trip + default-true semantics
// ===========================================================================
namespace ShellExtensionHostTests_HostSettings
{
    // RAII: snapshot + restore both toggle values so tests don't disturb the
    // developer's real HKCU state.
    class HkcuGuard
    {
    public:
        HkcuGuard()
        {
            m_prevProp    = ReadDword(L"PropertyEnabled",  m_hadProp);
            m_prevPreview = ReadDword(L"PreviewEnabled",   m_hadPreview);
            m_prevFilter  = ReadDword(L"FilterEnabled",    m_hadFilter);
            m_prevTier    = ReadDword(L"FeatureTier",      m_hadTier);
        }
        ~HkcuGuard()
        {
            if (m_hadProp)    WriteDword(L"PropertyEnabled", m_prevProp);
            else              DeleteValue(L"PropertyEnabled");
            if (m_hadPreview) WriteDword(L"PreviewEnabled",  m_prevPreview);
            else              DeleteValue(L"PreviewEnabled");
            if (m_hadFilter)  WriteDword(L"FilterEnabled",   m_prevFilter);
            else              DeleteValue(L"FilterEnabled");
            if (m_hadTier)    WriteDword(L"FeatureTier",     m_prevTier);
            else              DeleteValue(L"FeatureTier");
        }

        static DWORD ReadDword(const wchar_t* name, bool& present)
        {
            HKEY hk{};
            DWORD val = 0, type = 0, cb = sizeof(val);
            present = false;
            if (RegOpenKeyExW(HKEY_CURRENT_USER, xisf::paths::kHkcuSettingsKey,
                              0, KEY_READ, &hk) == ERROR_SUCCESS)
            {
                if (RegQueryValueExW(hk, name, nullptr, &type,
                                     reinterpret_cast<BYTE*>(&val), &cb) == ERROR_SUCCESS
                    && type == REG_DWORD)
                {
                    present = true;
                }
                RegCloseKey(hk);
            }
            return val;
        }

        static void WriteDword(const wchar_t* name, DWORD val)
        {
            HKEY hk{};
            if (RegCreateKeyExW(HKEY_CURRENT_USER, xisf::paths::kHkcuSettingsKey,
                                0, nullptr, 0, KEY_WRITE, nullptr, &hk, nullptr) == ERROR_SUCCESS)
            {
                RegSetValueExW(hk, name, 0, REG_DWORD,
                               reinterpret_cast<const BYTE*>(&val), sizeof(val));
                RegCloseKey(hk);
            }
        }

        static void DeleteValue(const wchar_t* name)
        {
            HKEY hk{};
            if (RegOpenKeyExW(HKEY_CURRENT_USER, xisf::paths::kHkcuSettingsKey,
                              0, KEY_WRITE, &hk) == ERROR_SUCCESS)
            {
                RegDeleteValueW(hk, name);
                RegCloseKey(hk);
            }
        }

    private:
        bool  m_hadProp = false, m_hadPreview = false, m_hadFilter = false, m_hadTier = false;
        DWORD m_prevProp = 0, m_prevPreview = 0, m_prevFilter = 0, m_prevTier = 0;
    };

    TEST_CLASS(HostSettingsTests)
    {
    public:
        TEST_METHOD(Default_WhenValueAbsent_IsTrue)
        {
            HkcuGuard g;
            HkcuGuard::DeleteValue(L"PropertyEnabled");
            HkcuGuard::DeleteValue(L"PreviewEnabled");

            Assert::IsTrue(xisf::hostsettings::IsPropertyEnabled());
            Assert::IsTrue(xisf::hostsettings::IsPreviewEnabled());
        }

        TEST_METHOD(SetFalse_IsPersistedAndRead)
        {
            HkcuGuard g;
            xisf::hostsettings::SetPropertyEnabled(false);
            xisf::hostsettings::SetPreviewEnabled(false);
            Assert::IsFalse(xisf::hostsettings::IsPropertyEnabled());
            Assert::IsFalse(xisf::hostsettings::IsPreviewEnabled());
        }

        TEST_METHOD(SetTrue_AfterFalse_RestoresEnabled)
        {
            HkcuGuard g;
            xisf::hostsettings::SetPropertyEnabled(false);
            xisf::hostsettings::SetPropertyEnabled(true);
            Assert::IsTrue(xisf::hostsettings::IsPropertyEnabled());
        }

        TEST_METHOD(PropertyAndPreviewToggles_AreIndependent)
        {
            HkcuGuard g;
            xisf::hostsettings::SetPropertyEnabled(true);
            xisf::hostsettings::SetPreviewEnabled(false);
            Assert::IsTrue(xisf::hostsettings::IsPropertyEnabled());
            Assert::IsFalse(xisf::hostsettings::IsPreviewEnabled());
        }

        TEST_METHOD(PropertyToggle_DefaultAbsent_IsEnabled)
        {
            HkcuGuard g;
            HkcuGuard::DeleteValue(L"PropertyEnabled");
            Assert::IsTrue(xisf::hostsettings::IsPropertyEnabled(),
                L"Property handler activation must default to enabled when key is absent");
        }

        TEST_METHOD(PropertyToggle_DisabledValue_IsDisabled)
        {
            HkcuGuard g;
            HkcuGuard::WriteDword(L"PropertyEnabled", 0);
            Assert::IsFalse(xisf::hostsettings::IsPropertyEnabled(),
                L"Property handler activation must be disabled when PropertyEnabled=0");
        }

        TEST_METHOD(PropertyToggle_EnabledValue_IsEnabled)
        {
            HkcuGuard g;
            HkcuGuard::WriteDword(L"PropertyEnabled", 1);
            Assert::IsTrue(xisf::hostsettings::IsPropertyEnabled(),
                L"Property handler activation must be enabled when PropertyEnabled=1");
        }

        TEST_METHOD(PreviewToggle_DefaultAbsent_IsEnabled)
        {
            HkcuGuard g;
            HkcuGuard::DeleteValue(L"PreviewEnabled");
            Assert::IsTrue(xisf::hostsettings::IsPreviewEnabled(),
                L"Preview handler activation must default to enabled when key is absent");
        }

        TEST_METHOD(PreviewToggle_DisabledValue_IsDisabled)
        {
            HkcuGuard g;
            HkcuGuard::WriteDword(L"PreviewEnabled", 0);
            Assert::IsFalse(xisf::hostsettings::IsPreviewEnabled(),
                L"Preview handler activation must be disabled when PreviewEnabled=0");
        }

        TEST_METHOD(PreviewToggle_EnabledValue_IsEnabled)
        {
            HkcuGuard g;
            HkcuGuard::WriteDword(L"PreviewEnabled", 1);
            Assert::IsTrue(xisf::hostsettings::IsPreviewEnabled(),
                L"Preview handler activation must be enabled when PreviewEnabled=1");
        }

        TEST_METHOD(FilterToggle_DefaultAbsent_IsEnabled)
        {
            // Filter activation default mirrors property/preview: opt-out, not opt-in.
            HkcuGuard g;
            HkcuGuard::DeleteValue(L"FilterEnabled");
            Assert::IsTrue(xisf::hostsettings::IsFilterEnabled(),
                L"Filter handler activation must default to enabled when key is absent");
        }

        TEST_METHOD(FilterToggle_SetFalse_IsPersistedAndRead)
        {
            HkcuGuard g;
            xisf::hostsettings::SetFilterEnabled(false);
            Assert::IsFalse(xisf::hostsettings::IsFilterEnabled(),
                L"SetFilterEnabled(false) must round-trip through HKCU");
        }

        TEST_METHOD(FilterToggle_SetTrue_AfterFalse_RestoresEnabled)
        {
            HkcuGuard g;
            xisf::hostsettings::SetFilterEnabled(false);
            xisf::hostsettings::SetFilterEnabled(true);
            Assert::IsTrue(xisf::hostsettings::IsFilterEnabled(),
                L"Re-enabling after disable must be observable on next read");
        }

        TEST_METHOD(GetFeatureTier_DefaultAbsent_IsFull)
        {
            // Documented default: when the value is missing the tier is Full,
            // i.e. the user gets the complete experience until they opt down.
            HkcuGuard g;
            HkcuGuard::DeleteValue(L"FeatureTier");
            Assert::IsTrue(xisf::hostsettings::GetFeatureTier()
                           == xisf::hostsettings::FeatureTier::Full,
                L"GetFeatureTier() must default to Full when value is absent");
        }

        TEST_METHOD(GetFeatureTier_OutOfRangeValue_ClampsToFull)
        {
            // Garbage in the registry (e.g. from a future build that defines
            // additional tiers, or hand-edited keys) must not produce a bogus
            // enum value -- the reader clamps to Full.
            HkcuGuard g;
            HkcuGuard::WriteDword(L"FeatureTier", 999);
            Assert::IsTrue(xisf::hostsettings::GetFeatureTier()
                           == xisf::hostsettings::FeatureTier::Full,
                L"Out-of-range FeatureTier values must clamp to Full");
        }
    };
}

// ===========================================================================
// CatalogSpec - invariants of the compiled-in pin data
// ===========================================================================
namespace ShellExtensionHostTests_CatalogSpec
{
    using namespace xisf::catalogspec;

    TEST_CLASS(CatalogSpecTests)
    {
    public:
        TEST_METHOD(CommitSha_IsFullLength40Hex)
        {
            Assert::AreEqual<size_t>(40, kOpenNGCCommit.size());
            for (wchar_t c : kOpenNGCCommit)
            {
                bool ok = (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f');
                Assert::IsTrue(ok, L"commit SHA must be lowercase hex");
            }
        }

        TEST_METHOD(AllCatalogs_HaveValidPinData)
        {
            for (auto* src : kAllCatalogs)
            {
                Assert::IsNotNull(src);
                Assert::IsFalse(src->displayName.empty());
                Assert::IsFalse(src->fileName.empty());
                Assert::IsFalse(src->url.empty());
                Assert::IsFalse(src->sourceUrl.empty());
                Assert::AreEqual<size_t>(64, src->expectedSha256.size(),
                                         L"SHA-256 pin must be 64 hex chars");
                for (wchar_t c : src->expectedSha256)
                {
                    bool ok = (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f');
                    Assert::IsTrue(ok, L"pin must be lowercase hex");
                }
                Assert::IsFalse(src->sourceHashDisplay.empty());
                Assert::IsTrue(src->maxBytes > 0);
                Assert::IsTrue(src->maxBytes <= 64ull * 1024ull * 1024ull,
                               L"max size cap should be reasonable (<=64MB)");
            }
        }

        TEST_METHOD(SourceHashDisplay_IsEitherPinnedHashOrNA)
        {
            for (auto* src : kAllCatalogs)
            {
                Assert::IsFalse(src->sourceHashDisplay.empty());
                if (src->sourceHashDisplay == kSourceHashNA)
                    continue;

                Assert::AreEqual<size_t>(64, src->sourceHashDisplay.size(),
                                         L"source hash display must be 64 hex chars when pinned");
                for (wchar_t c : src->sourceHashDisplay)
                {
                    bool ok = (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f');
                    Assert::IsTrue(ok, L"sourceHashDisplay pin must be lowercase hex");
                }
            }
        }

        TEST_METHOD(AllCatalogUrls_StartWithAllowedPrefix)
        {
            for (auto* src : kAllCatalogs)
            {
                bool matched = false;
                for (const auto& prefix : kAllowedUrlPrefixes)
                {
                    if (src->url.size() >= prefix.size() &&
                        src->url.compare(0, prefix.size(), prefix) == 0)
                    {
                        matched = true;
                        break;
                    }
                }
                Assert::IsTrue(matched, L"url must start with one of the allowed prefixes");
            }
        }

        TEST_METHOD(AllCatalogSourceUrls_AreHttps)
        {
            constexpr std::wstring_view https = L"https://";
            for (auto* src : kAllCatalogs)
            {
                Assert::IsTrue(src->sourceUrl.size() >= https.size());
                Assert::IsTrue(src->sourceUrl.compare(0, https.size(), https) == 0,
                               L"sourceUrl shown in UI must be HTTPS");
            }
        }

        TEST_METHOD(SharplessAndConstellations_AreNotUserRepoHosted)
        {
            Assert::IsTrue(kSharpless.url.find(L"dennispayne/XISF-Shell-Extensions") == std::wstring_view::npos);
            Assert::IsTrue(kConstellations.url.find(L"dennispayne/XISF-Shell-Extensions") == std::wstring_view::npos);
            Assert::IsTrue(kSharpless.sourceUrl.find(L"cdsarc.cds.unistra.fr") != std::wstring_view::npos);
            Assert::IsTrue(kConstellations.sourceUrl.find(L"cdsarc.cds.unistra.fr") != std::wstring_view::npos);
            Assert::AreEqual(std::wstring(L"N/A"), std::wstring(kSharpless.sourceHashDisplay));
            Assert::AreEqual(std::wstring(L"N/A"), std::wstring(kConstellations.sourceHashDisplay));
        }

        TEST_METHOD(AllowedPrefixes_AreHttps)
        {
            constexpr std::wstring_view https = L"https://";
            for (const auto& prefix : kAllowedUrlPrefixes)
            {
                Assert::IsTrue(prefix.compare(0, https.size(), https) == 0,
                               L"every allowed prefix must use HTTPS");
            }
        }

        TEST_METHOD(CatalogFileNames_AreUnique)
        {
            for (size_t i = 0; i < kAllCatalogs.size(); ++i)
                for (size_t j = i + 1; j < kAllCatalogs.size(); ++j)
                    Assert::IsFalse(kAllCatalogs[i]->fileName == kAllCatalogs[j]->fileName,
                                    L"catalog file names must be unique");
        }

        TEST_METHOD(HostProject_DoesNotBundleSeedCatalogFiles)
        {
            const auto projectPath =
                std::filesystem::path(__FILE__).parent_path() /
                L"..\\ShellExtensionHost\\ShellExtensionHost.vcxproj";
            std::ifstream in(projectPath, std::ios::binary);
            Assert::IsTrue(in.is_open(), L"failed to open ShellExtensionHost.vcxproj");

            const std::string text((std::istreambuf_iterator<char>(in)),
                                   std::istreambuf_iterator<char>());
            Assert::IsFalse(text.empty(), L"host project file unexpectedly empty");
            Assert::IsTrue(text.find("..\\..\\data\\sharpless.csv") == std::string::npos);
            Assert::IsTrue(text.find("..\\..\\data\\constellations.csv") == std::string::npos);
        }
    };
}

// ===========================================================================
// CatalogInstaller - verified download/import flows + Probe
// ===========================================================================
namespace ShellExtensionHostTests_CatalogInstaller
{
    using namespace xisf::catalogspec;
    using namespace xisf::installer;

    namespace
    {
        // Build a temporary CatalogSource whose expected hash matches `bytes`.
        // The destination file goes into the real CatalogDir but with a
        // uniquely-named file so concurrent tests and the user's real
        // catalogs are never overwritten.
        struct SyntheticCatalog
        {
            std::wstring  displayName;
            std::wstring  fileName;
            std::wstring  url;   // value unused by local-file install but kept sane
            std::wstring  hash;
            std::wstring  sourceUrl;
            std::wstring  sourceHashDisplay;
            CatalogSource src;
        };

        SyntheticCatalog MakeSynthetic(const std::vector<std::uint8_t>& bytes,
                                       std::uint64_t maxBytes = 4ull * 1024 * 1024)
        {
            SyntheticCatalog sc;
            wchar_t unique[64]{};
            std::swprintf(unique, 64, L"xisf-test-%llu-%lu.bin",
                          static_cast<unsigned long long>(GetTickCount64()),
                          GetCurrentThreadId());
            sc.fileName = unique;
            sc.url      = L"https://raw.githubusercontent.com/mattiaverga/OpenNGC/dummy/not-used.bin";

            std::array<std::uint8_t, 32> d{};
            xisf::Sha256Hasher h;
            h.Init();
            h.Update(bytes.data(), bytes.size());
            h.Finalize(d);
            sc.hash = xisf::ToHexLower(d);
            sc.displayName = L"synthetic";
            sc.sourceUrl = L"https://example.invalid/synthetic";
            sc.sourceHashDisplay = sc.hash;

            sc.src.displayName    = sc.displayName;
            sc.src.fileName       = sc.fileName;
            sc.src.url            = sc.url;
            sc.src.sourceUrl      = sc.sourceUrl;
            sc.src.expectedSha256 = sc.hash;
            sc.src.sourceHashDisplay = sc.sourceHashDisplay;
            sc.src.maxBytes       = maxBytes;
            return sc;
        }

        SyntheticCatalog MakePinnedDownloadSource(const CatalogSource& base, const wchar_t* stem, const wchar_t* urlOverride = nullptr)
        {
            SyntheticCatalog sc;
            wchar_t unique[96]{};
            std::swprintf(unique, 96, L"%ls-%llu-%lu.csv",
                          stem,
                          static_cast<unsigned long long>(GetTickCount64()),
                          GetCurrentThreadId());
            sc.displayName = std::wstring(base.displayName);
            sc.fileName = unique;
            sc.url = urlOverride ? std::wstring(urlOverride) : std::wstring(base.url);
            sc.hash = std::wstring(base.expectedSha256);
            sc.sourceUrl = std::wstring(base.sourceUrl);
            sc.sourceHashDisplay = std::wstring(base.sourceHashDisplay);

            sc.src.displayName = sc.displayName;
            sc.src.fileName = sc.fileName;
            sc.src.url = sc.url;
            sc.src.sourceUrl = sc.sourceUrl;
            sc.src.expectedSha256 = sc.hash;
            sc.src.sourceHashDisplay = sc.sourceHashDisplay;
            sc.src.maxBytes = base.maxBytes;
            return sc;
        }

        void CleanupInstalled(const SyntheticCatalog& sc)
        {
            auto path = xisf::paths::CatalogFile(sc.fileName.c_str());
            DeleteFileW(path.c_str());
        }
    }

    TEST_CLASS(CatalogInstallerTests)
    {
    public:
        TEST_METHOD(InstallFromLocalFile_HashMatches_Succeeds)
        {
            std::vector<std::uint8_t> bytes(2048);
            for (size_t i = 0; i < bytes.size(); ++i)
                bytes[i] = static_cast<std::uint8_t>(i & 0xff);

            auto sc   = MakeSynthetic(bytes);
            auto src  = WriteTempFile(bytes);

            Report r = InstallFromLocalFileVerified(sc.src, src.c_str(), nullptr, nullptr);
            Assert::IsTrue(r.result == Result::Ok,
                           L"valid local file must install successfully");
            Assert::AreEqual<std::uint64_t>(bytes.size(), r.bytesTransferred);
            Assert::IsTrue(IEqualsAscii(r.computedHash, sc.hash));

            // File must now exist in catalog dir.
            auto installed = xisf::paths::CatalogFile(sc.fileName.c_str());
            Assert::AreNotEqual(INVALID_FILE_ATTRIBUTES,
                                GetFileAttributesW(installed.c_str()));

            DeleteFileW(src.c_str());
            CleanupInstalled(sc);
        }

        TEST_METHOD(InstallFromPinnedUrl_DownloadsPinnedCatalogEndToEnd)
        {
            auto sc = MakePinnedDownloadSource(kAddendum, L"xisf-download");
            CleanupInstalled(sc);

            Report r = InstallFromPinnedUrl(sc.src, nullptr, nullptr);
            Assert::IsTrue(r.result == Result::Ok,
                           (std::wstring(L"downloaded catalog should install successfully: ") + r.errorDetail).c_str());

            Presence p = Probe(sc.src);
            Assert::IsTrue(p.state == PresenceState::PresentVerified,
                           L"downloaded catalog should probe as verified");
            Assert::IsTrue(IEqualsAscii(p.computedHash, sc.hash));

            CleanupInstalled(sc);
        }

        TEST_METHOD(InstallFromPinnedUrl_NetworkFailureDoesNotInstallCatalog)
        {
            const std::wstring missingAddendumUrl =
                std::wstring(L"https://raw.githubusercontent.com/mattiaverga/OpenNGC/") +
                std::wstring(kOpenNGCCommit) +
                L"/database_files/does-not-exist.csv";
            auto sc = MakePinnedDownloadSource(
                kAddendum,
                L"xisf-download-missing",
                missingAddendumUrl.c_str());
            CleanupInstalled(sc);

            Report r = InstallFromPinnedUrl(sc.src, nullptr, nullptr);
            Assert::IsTrue(
                r.result == Result::HttpBadStatus ||
                r.result == Result::HttpConnectFailed ||
                r.result == Result::HttpRequestFailed,
                (std::wstring(L"missing remote catalog should report an HTTP/network failure, got ") + std::to_wstring(static_cast<int>(r.result))).c_str());
            if (r.result == Result::HttpBadStatus) {
                Assert::AreEqual(404u, r.httpStatus);
            }

            auto dest = xisf::paths::CatalogFile(sc.fileName.c_str());
            Assert::AreEqual<DWORD>(INVALID_FILE_ATTRIBUTES, GetFileAttributesW(dest.c_str()));
        }

        TEST_METHOD(InstallFromLocalFile_HashMismatch_Rejected_NoInstall)
        {
            std::vector<std::uint8_t> bytes(1024, 0xAB);
            auto sc = MakeSynthetic(bytes);

            // Tamper: source file differs by one byte.
            std::vector<std::uint8_t> tampered = bytes;
            tampered[0] ^= 0x01;
            auto src = WriteTempFile(tampered);

            Report r = InstallFromLocalFileVerified(sc.src, src.c_str(), nullptr, nullptr);
            Assert::IsTrue(r.result == Result::HashMismatch,
                           L"tampered file must be rejected");

            // Destination must not exist.
            auto dest = xisf::paths::CatalogFile(sc.fileName.c_str());
            Assert::AreEqual<DWORD>(INVALID_FILE_ATTRIBUTES, GetFileAttributesW(dest.c_str()));

            DeleteFileW(src.c_str());
        }

        TEST_METHOD(InstallFromLocalFile_ExceedsCap_Rejected)
        {
            std::vector<std::uint8_t> bytes(4096, 0x77);
            auto sc = MakeSynthetic(bytes, /*maxBytes=*/1024); // cap below actual size
            auto src = WriteTempFile(bytes);

            Report r = InstallFromLocalFileVerified(sc.src, src.c_str(), nullptr, nullptr);
            Assert::IsTrue(r.result == Result::SizeExceeded,
                           L"oversize file must be rejected by size cap");

            DeleteFileW(src.c_str());
        }

        TEST_METHOD(InstallFromLocalFile_MissingSource_ReportsOpenFailure)
        {
            std::vector<std::uint8_t> bytes(16, 0x11);
            auto sc = MakeSynthetic(bytes);

            Report r = InstallFromLocalFileVerified(
                sc.src,
                L"C:\\this-file-does-not-exist-xisf-test-987.bin",
                nullptr, nullptr);
            Assert::IsTrue(r.result == Result::SourceOpenFailed);
        }

        TEST_METHOD(InstallFromLocalFileUnverified_CopiesWithoutPinCheck)
        {
            const std::string openNgc =
                "Name;Type;RA;Dec;Const;MajAx;MinAx;PosAng;B-Mag;V-Mag;J-Mag;H-Mag;K-Mag;SurfBr;Hubble;Pax;Pm-RA;Pm-Dec;RadVel;Redshift;Cstar-U;Cstar-B;Cstar-V;M;NGC;IC;CstarNames;Identifiers;Common names;NED notes;OpenNGC notes\n"
                "Sh2-1;HII;17:20:50.0;-34:42:00;Sco;90;90;;;;;;;;;;;;;;;;;;;;;;;Sh2-1;;sample\n";
            std::vector<std::uint8_t> bytes(openNgc.begin(), openNgc.end());
            auto src = WriteTempFile(bytes);

            wchar_t unique[64]{};
            std::swprintf(unique, 64, L"xisf-import-%llu.csv",
                          static_cast<unsigned long long>(GetTickCount64()));

            Report r = InstallFromLocalFileUnverified(unique, src.c_str(),
                                                      4ull * 1024ull * 1024ull,
                                                      nullptr, nullptr);
            Assert::IsTrue(r.result == Result::Ok);
            Assert::AreEqual<std::uint64_t>(bytes.size(), r.bytesTransferred);
            Assert::AreEqual<size_t>(64, r.computedHash.size());

            auto installed = xisf::paths::CatalogFile(unique);
            Assert::AreNotEqual(INVALID_FILE_ATTRIBUTES, GetFileAttributesW(installed.c_str()));

            DeleteFileW(src.c_str());
            DeleteFileW(installed.c_str());
        }

        TEST_METHOD(InstallFromLocalFileUnverified_RejectsInvalidTargetName)
        {
            std::vector<std::uint8_t> bytes(64, 0x42);
            auto src = WriteTempFile(bytes);

            Report r = InstallFromLocalFileUnverified(L"..\\evil.csv", src.c_str(),
                                                      1024ull * 1024ull,
                                                      nullptr, nullptr);
            Assert::IsTrue(r.result == Result::SourceOpenFailed);

            DeleteFileW(src.c_str());
        }

        TEST_METHOD(InstallFromLocalFileUnverified_RejectsNonOpenNgcCsv)
        {
            const std::string malformed = "not;a;valid;openngc\nrow;without;required;columns\n";
            std::vector<std::uint8_t> bytes(malformed.begin(), malformed.end());
            auto src = WriteTempFile(bytes);

            Report r = InstallFromLocalFileUnverified(L"user-import.csv", src.c_str(),
                                                      1024ull * 1024ull,
                                                      nullptr, nullptr);
            Assert::IsTrue(r.result == Result::InvalidContent);
            DeleteFileW(src.c_str());
        }

        TEST_METHOD(InstallFromLocalFileUnverified_RejectsMalformedConstellationsCsv)
        {
            const std::string malformed = "B,not-a-number,10,80,UMi\n";
            std::vector<std::uint8_t> bytes(malformed.begin(), malformed.end());
            auto src = WriteTempFile(bytes);

            Report r = InstallFromLocalFileUnverified(L"constellations.csv", src.c_str(),
                                                      1024ull * 1024ull,
                                                      nullptr, nullptr);
            Assert::IsTrue(r.result == Result::InvalidContent);
            DeleteFileW(src.c_str());
        }

        TEST_METHOD(Probe_MissingFile_ReturnsMissing)
        {
            std::vector<std::uint8_t> bytes(8, 0x22);
            auto sc = MakeSynthetic(bytes);
            // Don't install; probe for absence.
            CleanupInstalled(sc);

            Presence p = Probe(sc.src);
            Assert::IsTrue(p.state == PresenceState::Missing);
        }

        TEST_METHOD(Probe_AfterSuccessfulInstall_ReturnsVerified)
        {
            std::vector<std::uint8_t> bytes(512);
            for (size_t i = 0; i < bytes.size(); ++i)
                bytes[i] = static_cast<std::uint8_t>(i);
            auto sc  = MakeSynthetic(bytes);
            auto src = WriteTempFile(bytes);

            Report r = InstallFromLocalFileVerified(sc.src, src.c_str(), nullptr, nullptr);
            Assert::IsTrue(r.result == Result::Ok);

            Presence p = Probe(sc.src);
            Assert::IsTrue(p.state == PresenceState::PresentVerified,
                           L"freshly installed file must probe as verified");
            Assert::AreEqual<std::uint64_t>(bytes.size(), p.sizeBytes);
            Assert::IsTrue(IEqualsAscii(p.computedHash, sc.hash));

            DeleteFileW(src.c_str());
            CleanupInstalled(sc);
        }

        TEST_METHOD(Probe_FileWithWrongHash_ReportsMismatch)
        {
            std::vector<std::uint8_t> bytes(256, 0x33);
            auto sc = MakeSynthetic(bytes);

            // Hand-place a different-content file at the catalog path.
            auto dest = xisf::paths::CatalogFile(sc.fileName.c_str());
            HANDLE h = CreateFileW(dest.c_str(), GENERIC_WRITE, 0, nullptr,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            Assert::IsTrue(h != INVALID_HANDLE_VALUE);
            std::vector<std::uint8_t> other(256, 0x44);
            DWORD w = 0;
            WriteFile(h, other.data(), static_cast<DWORD>(other.size()), &w, nullptr);
            CloseHandle(h);

            Presence p = Probe(sc.src);
            Assert::IsTrue(p.state == PresenceState::PresentMismatch);

            DeleteFileW(dest.c_str());
        }
    };
}

// ===========================================================================
// HandlerDllPath - resolve handler binaries across output layouts
// ===========================================================================
namespace ShellExtensionHostTests_HandlerDllPath
{
    namespace fs = std::filesystem;

    class TempRootGuard
    {
    public:
        TempRootGuard()
        {
            wchar_t tmpDir[MAX_PATH] = {};
            GetTempPathW(MAX_PATH, tmpDir);

            wchar_t tmpName[MAX_PATH] = {};
            GetTempFileNameW(tmpDir, L"xhp", 0, tmpName);
            DeleteFileW(tmpName);

            m_root = tmpName;
            fs::create_directories(m_root);
        }

        ~TempRootGuard()
        {
            std::error_code ec;
            fs::remove_all(m_root, ec);
        }

        std::wstring Root() const { return m_root.wstring(); }

    private:
        fs::path m_root;
    };

    static void WriteEmptyFile(const fs::path& p)
    {
        fs::create_directories(p.parent_path());
        HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        Assert::IsTrue(h != INVALID_HANDLE_VALUE);
        CloseHandle(h);
    }

    TEST_CLASS(HandlerDllPathTests)
    {
    public:
        TEST_METHOD(ResolvePropertyPrefersSolutionLevelOutput)
        {
            TempRootGuard tmp;
            fs::path root(tmp.Root());
            auto expected = root / L"x64" / L"Debug" / L"XISFPropertyHandler.dll";
            WriteEmptyFile(expected);

            auto resolved = xisf::hostpaths::ResolveHandlerDllPath(root.wstring(), true, L"Debug");
            Assert::AreEqual(expected.wstring(), resolved);
        }

        TEST_METHOD(ResolvePreviewFallsBackToProjectLevelOutput)
        {
            TempRootGuard tmp;
            fs::path root(tmp.Root());
            auto expected = root / L"PreviewHandler" / L"XISFPreviewHandler" / L"x64" / L"Release" / L"XISFPreviewHandler.dll";
            WriteEmptyFile(expected);

            auto resolved = xisf::hostpaths::ResolveHandlerDllPath(root.wstring(), false, L"Release");
            Assert::AreEqual(expected.wstring(), resolved);
        }

        TEST_METHOD(ResolveFilterHandler_PrefersSolutionLevelOutput)
        {
            // Mirror of ResolvePropertyPrefersSolutionLevelOutput but for the
            // Filter handler -- ensures the Filter branch of the candidate list
            // (introduced when HandlerType::Filter was added) returns the
            // solution-level x64\<cfg>\XISFFilter.dll first.
            TempRootGuard tmp;
            fs::path root(tmp.Root());
            auto expected = root / L"x64" / L"Debug" / L"XISFFilter.dll";
            WriteEmptyFile(expected);

            auto resolved = xisf::hostpaths::ResolveHandlerDllPath(
                root.wstring(),
                xisf::hostpaths::HandlerType::Filter,
                L"Debug");
            Assert::AreEqual(expected.wstring(), resolved);
        }

        TEST_METHOD(ResolveHandler_EmptyConfiguration_ReturnsEmpty)
        {
            // The resolver bails out early if the caller supplies an empty
            // configuration string (or null) -- we should get an empty
            // path rather than e.g. \"x64\\\\XISFPropertyHandler.dll\" with a
            // blank middle segment.
            TempRootGuard tmp;
            auto withEmpty = xisf::hostpaths::ResolveHandlerDllPath(
                tmp.Root(),
                xisf::hostpaths::HandlerType::Property,
                L"");
            Assert::IsTrue(withEmpty.empty(),
                L"Empty configuration must yield an empty resolved path");

            auto withNull = xisf::hostpaths::ResolveHandlerDllPath(
                tmp.Root(),
                xisf::hostpaths::HandlerType::Property,
                nullptr);
            Assert::IsTrue(withNull.empty(),
                L"Null configuration must yield an empty resolved path");
        }
    };
}

// ────────────────────────────────────────────────────────────────────────────
// UpdaterInternals tests — pure logic, no network access
// ────────────────────────────────────────────────────────────────────────────
namespace ShellExtensionHostTests_Updater
{
    using namespace xisf::updater::internals;
    using namespace Microsoft::VisualStudio::CppUnitTestFramework;

    TEST_CLASS(UpdaterSemVerTests)
    {
    public:
        TEST_METHOD(ParseSemVer_ThreePart_Correct)
        {
            SemVer v;
            Assert::IsTrue(ParseSemVer(L"1.2.3", v));
            Assert::AreEqual(1, v.major);
            Assert::AreEqual(2, v.minor);
            Assert::AreEqual(3, v.patch);
        }

        TEST_METHOD(ParseSemVer_WithVPrefix_Correct)
        {
            SemVer v;
            Assert::IsTrue(ParseSemVer(L"v1.0.0", v));
            Assert::AreEqual(1, v.major);
            Assert::AreEqual(0, v.minor);
            Assert::AreEqual(0, v.patch);
        }

        TEST_METHOD(ParseSemVer_FourPart_FourthIgnored)
        {
            SemVer v;
            Assert::IsTrue(ParseSemVer(L"0.1.0.0", v));
            Assert::AreEqual(0, v.major);
            Assert::AreEqual(1, v.minor);
            Assert::AreEqual(0, v.patch);
        }

        TEST_METHOD(ParseSemVer_PreReleaseSuffix_Stripped)
        {
            SemVer v;
            Assert::IsTrue(ParseSemVer(L"1.2.0-beta.1", v));
            Assert::AreEqual(1, v.major);
            Assert::AreEqual(2, v.minor);
            Assert::AreEqual(0, v.patch);
        }

        TEST_METHOD(CompareSemVer_NewerMajor_Positive)
        {
            SemVer a{2, 0, 0}, b{1, 9, 9};
            Assert::IsTrue(CompareSemVer(a, b) > 0);
        }

        TEST_METHOD(CompareSemVer_NewerMinor_Positive)
        {
            SemVer a{1, 1, 0}, b{1, 0, 9};
            Assert::IsTrue(CompareSemVer(a, b) > 0);
        }

        TEST_METHOD(CompareSemVer_NewerPatch_Positive)
        {
            SemVer a{1, 0, 2}, b{1, 0, 1};
            Assert::IsTrue(CompareSemVer(a, b) > 0);
        }

        TEST_METHOD(CompareSemVer_Equal_Zero)
        {
            SemVer a{1, 0, 0}, b{1, 0, 0};
            Assert::AreEqual(0, CompareSemVer(a, b));
        }

        TEST_METHOD(CompareSemVer_OlderMinor_Negative)
        {
            SemVer a{1, 0, 0}, b{1, 1, 0};
            Assert::IsTrue(CompareSemVer(a, b) < 0);
        }
    };

    TEST_CLASS(UpdaterJsonTests)
    {
    public:
        TEST_METHOD(ExtractJsonString_SimpleValue_Extracted)
        {
            std::string json = R"({"tag_name":"v1.2.3","other":"x"})";
            std::string val;
            Assert::IsTrue(ExtractJsonString(json, "tag_name", val));
            Assert::AreEqual(std::string("v1.2.3"), val);
        }

        TEST_METHOD(ExtractJsonString_EscapedQuotes_Handled)
        {
            std::string json = R"({"key":"val\"ue"})";
            std::string val;
            Assert::IsTrue(ExtractJsonString(json, "key", val));
            Assert::AreEqual(std::string("val\\\"ue"), val);
        }

        TEST_METHOD(ExtractJsonString_MissingKey_ReturnsFalse)
        {
            std::string json = R"({"other":"x"})";
            std::string val;
            Assert::IsFalse(ExtractJsonString(json, "tag_name", val));
        }

        TEST_METHOD(ExtractJsonBool_True_Extracted)
        {
            std::string json = R"({"prerelease":true})";
            bool val = false;
            Assert::IsTrue(ExtractJsonBool(json, "prerelease", val));
            Assert::IsTrue(val);
        }

        TEST_METHOD(ExtractJsonBool_False_Extracted)
        {
            std::string json = R"({"prerelease":false})";
            bool val = true;
            Assert::IsTrue(ExtractJsonBool(json, "prerelease", val));
            Assert::IsFalse(val);
        }
    };

    TEST_CLASS(UpdaterAssetNameTests)
    {
    public:
        TEST_METHOD(IsExpectedMsiName_ValidAsset_True)
        {
            Assert::IsTrue(IsExpectedMsiName("XISF.ShellExtensions_1.0.0_x64.msi"));
        }

        TEST_METHOD(IsExpectedMsiName_WrongSuffix_False)
        {
            Assert::IsFalse(IsExpectedMsiName("XISF.ShellExtensions_1.0.0_x64.exe"));
        }

        TEST_METHOD(IsExpectedMsiName_WrongPrefix_False)
        {
            Assert::IsFalse(IsExpectedMsiName("SomeOtherApp_1.0.0_x64.msi"));
        }

        TEST_METHOD(IsExpectedMsiName_EmptyString_False)
        {
            Assert::IsFalse(IsExpectedMsiName(""));
        }

        TEST_METHOD(IsExpectedMsiName_PrefixSuffixOnly_False)
        {
            // No version component between prefix and suffix.
            Assert::IsFalse(IsExpectedMsiName("XISF.ShellExtensions__x64.msi"));
        }
    };

    TEST_CLASS(UpdaterChecksumParseTests)
    {
    public:
        TEST_METHOD(ParseChecksumFile_ValidEntry_ReturnsHash)
        {
            std::string content =
                "abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890  "
                "XISF.ShellExtensions_1.0.0_x64.msi\n";
            auto hash = ParseChecksumFile(content, L"XISF.ShellExtensions_1.0.0_x64.msi");
            Assert::AreEqual(std::wstring(L"abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890"),
                             hash);
        }

        TEST_METHOD(ParseChecksumFile_BsdFormat_ReturnsHash)
        {
            // BSD-style "*filename" after spaces
            std::string content =
                "abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890 *"
                "XISF.ShellExtensions_1.1.0_x64.msi\n";
            auto hash = ParseChecksumFile(content, L"XISF.ShellExtensions_1.1.0_x64.msi");
            Assert::IsFalse(hash.empty());
        }

        TEST_METHOD(ParseChecksumFile_CaseInsensitiveMatch_Succeeds)
        {
            std::string content =
                "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa  "
                "xisf.shellextensions_1.0.0_x64.msi\n";
            auto hash = ParseChecksumFile(content, L"XISF.ShellExtensions_1.0.0_x64.msi");
            Assert::IsFalse(hash.empty());
        }

        TEST_METHOD(ParseChecksumFile_FileNotInContent_ReturnsEmpty)
        {
            std::string content =
                "abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890  "
                "OtherFile.txt\n";
            auto hash = ParseChecksumFile(content, L"XISF.ShellExtensions_1.0.0_x64.msi");
            Assert::IsTrue(hash.empty());
        }

        TEST_METHOD(ParseChecksumFile_HashNormalisedToLower)
        {
            std::string content =
                "ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890  "
                "XISF.ShellExtensions_1.0.0_x64.msi\n";
            auto hash = ParseChecksumFile(content, L"XISF.ShellExtensions_1.0.0_x64.msi");
            Assert::AreEqual(std::wstring(L"abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890"),
                             hash);
        }
    };

    TEST_CLASS(UpdaterAllowListTests)
    {
    public:
        TEST_METHOD(IsAllowedUpdateHost_ApiHost_True)
        {
            Assert::IsTrue(IsAllowedUpdateHost(L"api.github.com"));
        }

        TEST_METHOD(IsAllowedUpdateHost_CdnHost_True)
        {
            Assert::IsTrue(IsAllowedUpdateHost(L"objects.githubusercontent.com"));
        }

        TEST_METHOD(IsAllowedUpdateHost_GithubCom_True)
        {
            Assert::IsTrue(IsAllowedUpdateHost(L"github.com"));
        }

        TEST_METHOD(IsAllowedUpdateHost_ArbitraryHost_False)
        {
            Assert::IsFalse(IsAllowedUpdateHost(L"evil.example.com"));
        }

        TEST_METHOD(IsAllowedUpdateHost_CaseInsensitive_True)
        {
            Assert::IsTrue(IsAllowedUpdateHost(L"API.GITHUB.COM"));
        }
    };
}
