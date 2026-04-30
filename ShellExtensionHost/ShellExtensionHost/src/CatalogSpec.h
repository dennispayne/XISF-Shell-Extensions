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
// To rotate the project-hosted catalogs (constellations.csv, sharpless.csv):
//   1. Edit the file in the data/ directory and commit
//   2. Recompute SHA-256 of the new file
//   3. Update kXISFDataCommit and the kExpectedSha256 entry below
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

// Project-hosted data commit (constellations.csv, sharpless.csv).
// Update after merging new data files and pinning the resulting commit SHA.
inline constexpr std::wstring_view kXISFDataCommit =
    L"PLACEHOLDER_COMMIT_SHA";
inline constexpr std::wstring_view kXISFDataCommitDate = L"2026-04-30";

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

// Sharpless HII-region catalog (OpenNGC-compatible semicolon-delimited CSV).
// Hosted in the project repository; update kXISFDataCommit after changing the file.
inline constexpr CatalogSource kSharpless {
    L"Sharpless sharpless.csv",
    L"sharpless.csv",
    L"https://raw.githubusercontent.com/dennispayne/XISF-Shell-Extensions/"
    L"PLACEHOLDER_COMMIT_SHA/data/sharpless.csv",
    L"3452cd838e2c9252a0b99ceb2c9c222ad4cbf38f3770cebd251e85dac725c081",
    4ull * 1024ull * 1024ull
};

// IAU constellation boundaries and names (Roman 1987 / Delporte 1930).
// Hosted in the project repository; update kXISFDataCommit after changing the file.
inline constexpr CatalogSource kConstellations {
    L"IAU Constellations constellations.csv",
    L"constellations.csv",
    L"https://raw.githubusercontent.com/dennispayne/XISF-Shell-Extensions/"
    L"PLACEHOLDER_COMMIT_SHA/data/constellations.csv",
    L"9e742f498fc6f355df37ff941c3d3adfcb3d759b05d7e0760a7b85822b5c074b",
    1ull * 1024ull * 1024ull
};

inline constexpr std::array<const CatalogSource*, 4> kAllCatalogs = {
    &kNGC, &kAddendum, &kSharpless, &kConstellations
};

// Host allow-list (full URL prefixes). Any URL not starting with one of these
// entries is rejected by InstallFromPinnedUrl. Extend only for new trusted hosts.
inline constexpr std::array<std::wstring_view, 2> kAllowedUrlPrefixes = {{
    L"https://raw.githubusercontent.com/mattiaverga/OpenNGC/",
    L"https://raw.githubusercontent.com/dennispayne/XISF-Shell-Extensions/",
}};

} // namespace xisf::catalogspec
