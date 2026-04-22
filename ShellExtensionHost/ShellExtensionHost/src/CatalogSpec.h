// CatalogSpec.h - Pinned, integrity-verified catalog sources.
//
// SECURITY MODEL
// --------------
// We download catalog files ONLY from fixed URLs pinned to an immutable git
// commit SHA at raw.githubusercontent.com. The SHA-256 of the bytes at that
// commit is compiled in below and verified after every download (and every
// offline file import that uses the verified path). Any mismatch is fatal:
// the candidate file is deleted and the install aborts.
//
// To rotate to a newer OpenNGC snapshot:
//   1. Pick a commit SHA on https://github.com/mattiaverga/OpenNGC
//   2. Download each file at that commit and recompute SHA-256
//   3. Update kOpenNGCCommit and the kExpectedSha256 entries below
//   4. Bump CHANGELOG.md and version.json
//
// DO NOT add URLs pointing to mutable refs (branches, tags). A pinned commit
// SHA is cryptographically stable; master / main is not.
#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <string_view>

namespace xisf::catalogspec {

// OpenNGC commit pinned for this release. See CHANGELOG for rotation history.
inline constexpr std::wstring_view kOpenNGCCommit =
    L"36cb178a0f69dba8bfc03a99c10512831edf1c6b";
inline constexpr std::wstring_view kOpenNGCCommitDate = L"2026-04-16";

struct CatalogSource {
    // Display name shown in the settings UI.
    std::wstring_view displayName;
    // Destination file name under %LOCALAPPDATA%\XISFShellExtension\catalogs\.
    std::wstring_view fileName;
    // Fully-qualified HTTPS URL. Host MUST be raw.githubusercontent.com.
    std::wstring_view url;
    // Expected SHA-256 of the file contents, lower-case hex, 64 chars.
    std::wstring_view expectedSha256;
    // Hard ceiling on download size (bytes). Reject anything larger before hashing.
    std::uint64_t maxBytes;
};

// NOTE: sizes below are generous upper bounds, not exact. Exact verification
// happens via SHA-256. The cap exists only to limit DoS from a compromised CDN.
inline constexpr CatalogSource kNGC {
    L"OpenNGC NGC.csv",
    L"NGC.csv",
    L"https://raw.githubusercontent.com/mattiaverga/OpenNGC/"
    L"36cb178a0f69dba8bfc03a99c10512831edf1c6b/database_files/NGC.csv",
    L"840fe0c9ee1332e551b2e722a0e92726cd7b157914a3d2177602832aadd3aa9e",
    8ull * 1024ull * 1024ull
};

inline constexpr CatalogSource kAddendum {
    L"OpenNGC addendum.csv",
    L"addendum.csv",
    L"https://raw.githubusercontent.com/mattiaverga/OpenNGC/"
    L"36cb178a0f69dba8bfc03a99c10512831edf1c6b/database_files/addendum.csv",
    L"1d8f0914e643ada325a5a94d88d8fefad6a4937a2f77cc34f21483af22b11983",
    1ull * 1024ull * 1024ull
};

inline constexpr std::array<const CatalogSource*, 2> kAllCatalogs = { &kNGC, &kAddendum };

// Host allow-list. Any URL not beginning with one of these is rejected.
inline constexpr std::wstring_view kAllowedUrlPrefix =
    L"https://raw.githubusercontent.com/mattiaverga/OpenNGC/";

} // namespace xisf::catalogspec
