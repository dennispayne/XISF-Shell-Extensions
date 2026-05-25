// UpdaterSpec.h - Compile-time policy constants for the self-update pipeline.
//
// SECURITY MODEL
// --------------
// Updates are fetched only from the pinned GitHub repo via releases/latest.
// The MSI URL must match the allow-listed host; the SHA-256 of the MSI is
// verified against SHA256SUMS.txt fetched alongside it. Authenticode signer
// pinning is opt-in (empty kExpectedSignerThumbprint = disabled). Downgrades
// are blocked by default.
#pragma once

#include <cstdint>
#include <string_view>

namespace xisf::updater {

// GitHub repo to check for releases.
inline constexpr std::wstring_view kOwner = L"dennispayne";
inline constexpr std::wstring_view kRepo  = L"XISF-Shell-Extensions";

// MSI asset filename: XISF.ShellExtensions_<version>_x64.msi
inline constexpr std::wstring_view kAssetPrefix = L"XISF.ShellExtensions_";
inline constexpr std::wstring_view kAssetSuffix = L"_x64.msi";
inline constexpr std::string_view  kAssetPrefixNarrow = "XISF.ShellExtensions_";
inline constexpr std::string_view  kAssetSuffixNarrow = "_x64.msi";

// Companion checksum file published alongside the MSI.
inline constexpr std::wstring_view kChecksumAssetName = L"SHA256SUMS.txt";
inline constexpr std::string_view  kChecksumAssetNameNarrow = "SHA256SUMS.txt";

// Hard ceiling on payload sizes (DoS / runaway-download guard).
inline constexpr std::uint64_t kMaxMsiBytes      = 100ull * 1024ull * 1024ull; // 100 MB
inline constexpr std::uint64_t kMaxChecksumBytes = 64ull * 1024ull;            //  64 KB
inline constexpr std::uint64_t kMaxApiBytes      = 256ull * 1024ull;           // 256 KB

// Authenticode leaf certificate SHA-1 thumbprint (lower-case hex, 40 chars).
// Empty = skip signer pinning (unsigned-OK mode).
inline constexpr std::wstring_view kExpectedSignerThumbprint = L"";

// Minimum minutes between GitHub API calls.
inline constexpr int kCheckIntervalMinutes = 60;

// HKCU registry key for persisting update state.
inline constexpr std::wstring_view kUpdateRegistryKey =
    L"Software\\DennisPayne\\XISF Shell Extension\\Updates";

// Registry value names under kUpdateRegistryKey.
inline constexpr std::wstring_view kRegETag         = L"LatestReleaseETag";
inline constexpr std::wstring_view kRegLastChecked  = L"LastCheckedEpoch";   // QWORD, Unix seconds
inline constexpr std::wstring_view kRegAvailableVer = L"AvailableVersion";
inline constexpr std::wstring_view kRegAssetUrl     = L"AvailableAssetUrl";
inline constexpr std::wstring_view kRegDownloadPath = L"PendingDownloadPath";

// Allowed HTTPS hosts. Any asset URL whose host is not in this set is rejected.
inline constexpr std::wstring_view kGitHubApiHost     = L"api.github.com";
inline constexpr std::wstring_view kGitHubDownloadHost = L"github.com";
inline constexpr std::wstring_view kGitHubCdnHost     = L"objects.githubusercontent.com";

} // namespace xisf::updater
